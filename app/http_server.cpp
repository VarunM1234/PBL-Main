/*
 * PURPOSE: Tier 2 web layer: a small single-threaded HTTP server (raw sockets, no library) that serves
 *          web/index.html and a JSON API. It only calls the same App services as the CLI (no duplicated logic).
 * WHY THIS NAME: "http_server" = answers browser requests.
 * CONNECTS TO: app.hpp, asciimap.hpp, ticket.hpp. Windows uses Winsock (link with -lws2_32).
 * COMPLEXITY: O(request) + engine cost. One connection at a time (D10: single-threaded).
 * SECURITY NOTE: demo server for localhost. No TLS; tokens are random but sessions live in memory only.
 */
#include "http_server.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include "asciimap.hpp"
#include "basemap.hpp"
#include "qr.h"
#include "ticket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
static void close_sock(sock_t s) { closesocket(s); }
static const sock_t BAD_SOCK = INVALID_SOCKET;
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
static void close_sock(sock_t s) { close(s); }
static const sock_t BAD_SOCK = -1;
#endif

/* The page is compiled into the program (generated from web/index.html by the Makefile), so the server works from any folder. */
extern const unsigned char sr_page_data[];
extern const unsigned long sr_page_len;

namespace sr {

static std::string run_json(App &app, const Trip &t);        // defined below; used by the vendor, driver and admin handlers

static const char *role_name(int r) { return r == ROLE_VENDOR ? "vendor" : (r == ROLE_DRIVER ? "driver" : (r == ROLE_ADMIN ? "admin" : "passenger")); }

/* ---------- tiny JSON writer ---------- */

static std::string jq(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) o += ' ';
        else o += (char)c;
    }
    return o + "\"";
}

class JObj {
    std::string s_ = "{";
    bool first_ = true;
    void key(const char *k) { if (!first_) s_ += ","; first_ = false; s_ += jq(k) + ":"; }
public:
    JObj &str(const char *k, const std::string &v) { key(k); s_ += jq(v); return *this; }
    JObj &num(const char *k, long long v) { key(k); s_ += std::to_string(v); return *this; }
    JObj &dbl(const char *k, double v) { key(k); char b[40]; std::snprintf(b, sizeof b, "%.4f", v); s_ += b; return *this; }
    JObj &boolean(const char *k, bool v) { key(k); s_ += v ? "true" : "false"; return *this; }
    JObj &raw(const char *k, const std::string &v) { key(k); s_ += v; return *this; }
    std::string done() const { return s_ + "}"; }
};

class JArr {
    std::string s_ = "[";
    bool first_ = true;
public:
    JArr &add(const std::string &raw) { if (!first_) s_ += ","; first_ = false; s_ += raw; return *this; }
    std::string done() const { return s_ + "]"; }
};

/* ---------- request / response ---------- */

struct Req {
    std::string method, path, body, origin;
    std::vector<std::pair<std::string, std::string>> params;   /* query string + form body */
    std::string token;
    std::string get(const char *k, const std::string &def = "") const {
        for (const auto &p : params) if (p.first == k) return p.second;
        return def;
    }
    long long num(const char *k, long long def = 0) const {
        std::string v = get(k);
        return v.empty() ? def : std::strtoll(v.c_str(), nullptr, 10);
    }
};

struct Resp { int status = 200; std::string type = "application/json"; std::string body; };

static std::string url_decode(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}

static void parse_pairs(const std::string &text, std::vector<std::pair<std::string, std::string>> *out) {
    for (const std::string &part : split_fields(text, '&')) {
        size_t eq = part.find('=');
        if (part.empty()) continue;
        out->push_back({url_decode(part.substr(0, eq)), eq == std::string::npos ? "" : url_decode(part.substr(eq + 1))});
    }
}

static Resp ok(const std::string &json) { Resp r; r.body = json; return r; }
static Resp err(int status, const std::string &msg) { Resp r; r.status = status; r.body = JObj().boolean("ok", false).str("error", msg).done(); return r; }
static int http_status_for(Err e) {
    switch (e) {
        case Err::NOT_FOUND: return 404;
        case Err::UNAUTHENTICATED: return 401;
        case Err::FORBIDDEN_ROLE: return 401;
        case Err::CONFLICT: case Err::SEAT_TAKEN: case Err::BAD_STATE: case Err::HOLD_EXPIRED: return 409;
        case Err::PAYMENT_DECLINED: return 402;
        default: return 400;
    }
}
template <class T> static Resp fail_of(const Result<T> &r) { return err(http_status_for(r.code), r.message); }

/* ---------- server state ---------- */

struct Server {
    App &app;
    Map<int> sessions;            /* token -> user id */
    bool autosave;
    std::string web_dir;
    int64_t lookup_minute = -1;   /* public lookup is throttled: 20 per wall-clock minute (spec 7) */
    int lookup_count = 0;
    std::chrono::steady_clock::time_point anchor = std::chrono::steady_clock::now();   /* real-time start: see App::sync_wall_clock */
    int64_t virt_anchor_ms = 0;                                    /* the virtual time when the server started */
    std::string basemap_cache;                    /* /basemap.json body: read once from disk or generated (see h_basemap) */
    Server(App &a, bool save, const std::string &dir) : app(a), autosave(save), web_dir(dir) {}
};

static const size_t MAX_ACCOUNTS = 2000;      /* abuse limits found by probing (spec 7) */
static const int MAX_GUESTS = 300;
static const size_t MAX_SESSIONS = 5000;
static const int MAX_LOOKUPS_PER_MINUTE = 20;

static int user_of(Server &sv, const Req &rq) {
    int *u = sv.sessions.get(rq.token);
    return u ? *u : 0;
}

static std::string new_token(App &app) {
    char b[40];
    std::snprintf(b, sizeof b, "%016llx%016llx", (unsigned long long)app.next_random(), (unsigned long long)app.next_random());
    return b;
}

/* ---------- JSON builders for engine objects ---------- */

static std::string row_json(App &app, const SearchRow &r) {
    const Bus &b = app.buses[(size_t)r.bus_idx];
    const BusType &bt = app.types[(size_t)b.type_idx];
    const Route &rt = app.routes[(size_t)r.route_id];
    JArr st;
    for (int i = 0; i < rt.n_stops; i++) st.add(JObj().dbl("km", rt.stops[i].km).num("type", rt.stops[i].type).str("name", rt.stops[i].name).num("break_min", rt.stops[i].break_min).done());
    return JObj().str("from", app.cities[(size_t)rt.from_city].name).str("to", app.cities[(size_t)rt.to_city].name).dbl("total_km", rt.km_nominal).raw("stops", st.done()).num("trip", app.trip_by_id(r.trip_id)->id).str("operator", app.companies[(size_t)b.company_id - 1].name).str("reg", b.reg).str("type", bt.label)
        .str("amenities", bt.amenities).str("dep", fmt_hm(r.depart_ms)).str("arr", fmt_hm_plus(r.arrive_ms, r.depart_ms)).num("duration", r.duration_min)
        .str("duration_text", fmt_duration(r.duration_min)).num("fare_min", r.fare_min).num("fare_max", r.fare_max).num("seats", r.seats_left)
        .str("bar", journey_bar(app.routes[(size_t)r.route_id], -1)).done();
}

static std::string trip_json(App &app, const Trip &t, int user) {
    const Route &r = app.routes[(size_t)t.route_id];
    const BusType &bt = app.types[(size_t)app.buses[(size_t)t.bus_idx].type_idx];
    JArr stops, seats;
    for (int i = 0; i < r.n_stops; i++)
        stops.add(JObj().num("idx", i).str("name", r.stops[i].name).num("type", r.stops[i].type).dbl("km", r.stops[i].km).num("break_min", r.stops[i].break_min)
                      .str("time", fmt_hm_plus(app.planned_stop_time(t, i), t.depart_ms)).done());
    for (const SeatCell &c : app.seat_map(t, user))
        seats.add(JObj().str("label", c.label).num("state", c.state).num("row", app.seat_row(bt, c.index)).num("idx", c.index)
                      .num("price", seat_fare_paise(r.km_nominal, bt.rate_paise_per_km, bt.layout == LAYOUT_SLEEPER && c.label[0] == 'L')).done());
    int64_t exp = user ? app.hold_expiry(t.id, user) : 0;
    return JObj().num("id", t.id).str("route", app.route_name(r)).str("operator", app.bus_label(t.bus_idx)).str("type", bt.label).num("layout", bt.layout)
        .str("depart", fmt_datetime(t.depart_ms)).str("arrive", fmt_hm_plus(t.arrive_ms, t.depart_ms)).num("km", r.km_nominal).num("rate", bt.rate_paise_per_km)
        .boolean("sales_open", app.sales_open(t)).num("hold_seconds", exp ? (exp - app.clock.now()) / 1000 : 0).raw("stops", stops.done()).raw("seats", seats.done()).done();
}

static std::string booking_json(App &app, const Booking &b) {
    const Trip *t = app.trip_by_id(b.trip_id);
    JArr pax;
    for (int i = 0; i < b.n_pax; i++) pax.add(JObj().str("seat", b.pax[i].seat).str("name", b.pax[i].name).num("age", b.pax[i].age).str("gender", std::string(1, b.pax[i].gender)).num("price", b.pax[i].price).boolean("boarded", (b.boarded_mask >> i) & 1).done());
    RefundPreview p = app.cancel_preview(b);
    return JObj().num("id", b.id).str("pnr", b.pnr).num("trip", b.trip_id).str("route", app.route_name(app.routes[(size_t)t->route_id])).str("depart", fmt_datetime(t->depart_ms))
        .str("status", booking_status_name(b.status)).str("pay_status", pay_status_name(b.pay_status)).num("mode", b.mode).num("total", b.total).num("paid", b.paid).num("refunded", b.refunded)
        .num("base", b.base).num("discount", b.discount).num("tax", b.tax).num("insurance", b.insurance)
        .num("boarding", b.boarding_stop).num("dropping", b.dropping_stop).str("balance_due", b.status == BK_PREBOOKED ? fmt_datetime(b.balance_due_ms) : "").num("refund_pct", p.pct).num("refund_now", p.refund)
        .boolean("demo_payment", b.demo_payment != 0).raw("passengers", pax.done()).done();
}

static std::string track_json(App &app, const Trip &t, int drop, bool owner, const Booking *bk = nullptr) {
    const Route &r = app.routes[(size_t)t.route_id];
    TrackInfo ti = app.track_info(t);
    JArr stops;
    for (int i = 0; i < r.n_stops; i++) {
        const Stop &s = r.stops[i];
        int64_t planned = app.planned_stop_time(t, i);
        int64_t eta = app.eta_at_stop(t, i);
        long long late = (long long)std::llround((double)(eta - planned) / 60000.0);
        stops.add(JObj().str("name", s.name).num("type", s.type).dbl("km", s.km).num("break_min", s.break_min).str("time", fmt_hm_plus(planned, t.depart_ms))
                      .num("planned_ms", planned).num("eta_ms", eta).num("late_min", late < 0 ? 0 : late)
                      .str("state", t.km >= s.km + 1e-9 ? "done" : (std::fabs(t.km - s.km) < 1e-6 ? "now" : "upcoming")).dbl("lat", s.pos.lat).dbl("lng", s.pos.lng).done());
    }
    geo_point p = app.bus_position(r, t.km);
    /* Tracker extras for the web panel: which bus and crew are on this run, the stop it is heading
       for, the intermediate halts ("via"), and whether a driver device has actually reported a
       position. "basis" is "gps" once the driver console has sent a fix and "schedule" before that,
       so the page can say which of the two the percentage came from (same rule as the TS app). */
    const Bus &bus = app.buses[(size_t)t.bus_idx];
    int drv = app.trip_driver(t), con = app.trip_conductor(t);
    const GpsFix *fix = nullptr;                                    /* newest fix for this trip, if any */
    long long n_fixes = 0; double fix_speed_sum = 0.0;              /* telemetry: how many fixes the driver reported and their mean speed */
    for (size_t i = app.gps_fixes.size(); i-- > 0;) if (app.gps_fixes[i].trip_id == t.id) {
        n_fixes++; fix_speed_sum += app.gps_fixes[i].speed;
        if (!fix) fix = &app.gps_fixes[i];
    }
    int next = -1;
    for (int i = 0; i < r.n_stops; i++) {
        const Stop &s = r.stops[i];                                 /* toll plazas are passed, never stopped at */
        if (s.type == ST_TOLL || s.km <= t.km + 1e-9) continue;
        if (next < 0 || s.km < r.stops[next].km) next = i;
    }
    std::string via;
    for (int i = 0; i < r.n_stops; i++) {
        const Stop &s = r.stops[i];
        if (s.type != ST_BOARDING && s.type != ST_BREAK) continue;
        if (!via.empty()) via += ", ";
        via += s.name;
    }
    const char *no_staff = "";
    const Stop *ds = (drop >= 0 && drop < r.n_stops) ? &r.stops[(size_t)drop] : nullptr;   /* drop-off details when ?drop=<idx> */
    return JObj().num("trip", t.id).str("route", app.route_name(r)).dbl("km", t.km).dbl("total_km", r.km_nominal).dbl("progress", ti.progress_pct).dbl("km_left", ti.km_left)
        .dbl("speed", ti.speed_kmh).str("scheduled", fmt_hm_plus(ti.sched_arrive, t.depart_ms)).str("estimated", fmt_hm_plus(ti.est_arrive, t.depart_ms)).num("delay", ti.delay_min)
        .num("sched_arrive_ms", ti.sched_arrive).num("est_arrive_ms", ti.est_arrive)   /* the tracker headline counts down against these */
        .str("delay_reason", t.delay_reason).boolean("on_break", ti.on_break).num("break_left", ti.break_min_left).boolean("jam", t.jam_left_km > 0).num("speed_x", t.sim_speed)
        .str("status", t.status == TRIP_SCHEDULED ? "SCHEDULED" : (t.status == TRIP_RUNNING ? "RUNNING" : "COMPLETED")).str("sim_time", fmt_datetime(ti.sim_now))
        .dbl("lat", p.lat).dbl("lng", p.lng).str("map", render_route_map(app, r, t.km, false)).str("bar", journey_bar(r, t.km, 60)).raw("stops", stops.done())
        .str("reg", bus.reg).str("operator", app.companies[(size_t)bus.company_id - 1].name).str("type", app.types[(size_t)bus.type_idx].label)
        .str("driver", drv > 0 ? app.staff[(size_t)drv - 1].name : no_staff).str("conductor", con > 0 ? app.staff[(size_t)con - 1].name : no_staff)
        /* "ON THIS COACH" call buttons on the tracker: same privacy rule as the ticket (spec 9.11) -
           the demo phone numbers only reach the page 24 h before departure for a paid booking. */
        .str("driver_phone", bk && drv > 0 && driver_phones_visible(app, *bk) ? app.staff[(size_t)drv - 1].phone : no_staff)
        .str("conductor_phone", bk && con > 0 && driver_phones_visible(app, *bk) ? app.staff[(size_t)con - 1].phone : no_staff)
        .str("via", via).num("next_type", next < 0 ? -1 : r.stops[next].type).str("next_stop", next < 0 ? no_staff : r.stops[next].name)
        .dbl("next_km", next < 0 ? 0.0 : r.stops[next].km).dbl("next_in", next < 0 ? 0.0 : r.stops[next].km - t.km)
        .str("next_time", next < 0 ? no_staff : fmt_hm_plus(app.planned_stop_time(t, next) + (int64_t)ti.delay_min * 60000, t.depart_ms))
        .str("basis", fix ? "gps" : "schedule").boolean("has_fix", fix != nullptr)
        .dbl("reported_speed", fix ? fix->speed : 0.0).dbl("heading", fix ? fix->heading : 0.0).dbl("accuracy", fix ? fix->accuracy : 0.0)
        .str("fix_at", fix ? fmt_datetime(fix->at_ms) : no_staff).dbl("fix_lat", fix ? fix->lat : 0.0).dbl("fix_lng", fix ? fix->lng : 0.0)
        .str("delay_note", t.delay_note).num("fixes", n_fixes).dbl("avg_kmh", n_fixes ? fix_speed_sum / (double)n_fixes : 0.0)
        .boolean("owner", owner)                                        /* the page only shows the simulator to the booking's owner (spec 6.9) */
        .str("drop_name", ds ? ds->name : no_staff)
        .dbl("km_to_drop", ds ? std::max(0.0, ds->km - t.km) : 0.0).num("drop_eta_ms", ds ? app.eta_at_stop(t, drop) : 0).done();
}

/* ---------- handlers ---------- */

static Resp h_state(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    sv.app.flush_outbox();
    JObj o;
    o.str("now", fmt_datetime(sv.app.clock.now())).num("now_ms", sv.app.clock.now());
    if (u) {
        const User *us = sv.app.user_by_id(u);
        o.raw("user", JObj().num("id", u).str("name", us->name).str("role", role_name(us->role))
                             .boolean("guest", us->hash[0] == '!')
                             .str("company", us->company_id ? sv.app.companies[(size_t)us->company_id - 1].name : "").done());
        o.num("bell", sv.app.unread_count(u));
    } else o.raw("user", "null").num("bell", 0);
    return ok(o.done());
}

static Resp h_cities(Server &sv, const Req &rq) {
    char out[10][64]; int ids[10];
    size_t n = sv.app.suggest_cities(rq.get("prefix").c_str(), 10, out, ids);
    JArr a;
    for (size_t i = 0; i < n; i++) a.add(JObj().num("id", ids[i]).str("name", sv.app.cities[(size_t)ids[i]].name).str("state", sv.app.cities[(size_t)ids[i]].state).done());
    return ok(a.done());
}

static Resp h_auth(Server &sv, const Req &rq, bool is_register) {
    if (is_register && sv.app.users.size() >= MAX_ACCOUNTS) return err(429, "The demo has reached its account limit. Try again later.");
    Result<int> r = is_register ? sv.app.register_passenger(rq.get("name"), rq.get("email"), rq.get("phone"), rq.get("password"))
                                : sv.app.login(rq.get("email"), rq.get("password"), rq.get("role") == "vendor" ? ROLE_VENDOR : (rq.get("role") == "driver" ? ROLE_DRIVER : (rq.get("role") == "admin" ? ROLE_ADMIN : ROLE_PASSENGER)));
    if (!r.ok) return fail_of(r);
    if (sv.sessions.size() >= MAX_SESSIONS) return err(429, "Too many open sessions. Try again later.");
    std::string tok = new_token(sv.app);
    sv.sessions.put(tok, r.value);
    if (is_register && sv.autosave) { std::string e; sv.app.save_all(&e); }
    return ok(JObj().boolean("ok", true).str("token", tok).str("name", sv.app.user_by_id(r.value)->name).done());
}

/* Booking without an account (spec 6.4): a guest passenger row whose hash is "!" can never be
   signed into; it is reachable only through the random token handed back here. */
static Resp h_guest(Server &sv, const Req &) {
    if (sv.sessions.size() >= MAX_SESSIONS) return err(429, "Too many open sessions. Try again later.");
    if (sv.app.users.size() >= MAX_ACCOUNTS || sv.app.guest_count() >= MAX_GUESTS)
        return err(429, "The demo has reached its guest limit. Try again later.");
    int id = sv.app.add_guest();
    std::string tok = new_token(sv.app);
    sv.sessions.put(tok, id);
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return ok(JObj().boolean("ok", true).str("token", tok).str("name", "Guest traveller").boolean("guest", true).done());
}

/* Public lookup (spec 6.4): find a trip by booking reference, or by the phone used at checkout
   comparing the LAST 10 digits (so "+91 98765 43210" equals "9876543210"). Route, time and status
   only - never names. Throttled to MAX_LOOKUPS_PER_MINUTE per minute of the engine clock. */
static std::string digits_of(const std::string &s) {
    std::string d;
    for (char c : s) if (std::isdigit((unsigned char)c)) d += c;
    return d;
}

static Resp h_lookup(Server &sv, const Req &rq) {
    int64_t minute = sv.app.clock.now() / 60000;
    if (sv.lookup_minute != minute) { sv.lookup_minute = minute; sv.lookup_count = 0; }
    if (++sv.lookup_count > MAX_LOOKUPS_PER_MINUTE) return err(429, "Too many lookups. Wait a minute and try again.");
    std::string q = rq.get("q");
    for (char &c : q) c = (char)std::toupper((unsigned char)c);
    const Booking *b = nullptr;
    if (q.size() >= 4) {                                        /* booking reference: hash map, O(1) */
        int *at = sv.app.idx_pnr.get(q);
        if (at && (size_t)*at < sv.app.bookings.size()) b = &sv.app.bookings[(size_t)*at];
    }
    if (!b && q.size() >= 6) {                                /* phone: newest booking first */
        std::string want = digits_of(q);
        if (want.size() >= 10) {
            want = want.substr(want.size() - 10);
            for (size_t i = sv.app.bookings.size(); i-- > 0 && !b;) {
                std::string have = digits_of(sv.app.bookings[i].contact);
                if (have.size() >= 10 && have.substr(have.size() - 10) == want) b = &sv.app.bookings[i];
            }
        }
    }
    if (!b) return err(404, "No booking found for that reference or phone number");
    const Trip *t = sv.app.trip_by_id(b->trip_id);
    if (!t) return err(404, "No booking found for that reference or phone number");
    /* route, time and status only - never names, and no reference either (the requester may have found
       it by phone, and a PNR must not leak that way) */
    return ok(JObj().boolean("ok", true).num("trip", t->id).str("route", sv.app.route_name(sv.app.routes[(size_t)t->route_id]))
                  .str("depart", fmt_datetime(t->depart_ms)).str("status", booking_status_name(b->status)).done());
}

static Resp h_search(Server &sv, const Req &rq) {
    App &a = sv.app;
    int from = a.find_city(rq.get("from")), to = a.find_city(rq.get("to")), y, m, d;
    if (from < 0 || to < 0) return err(400, "Choose two valid cities");
    if (from == to) return err(400, "Choose two different cities");
    if (a.find_route(from, to) < 0) return err(404, std::string("No direct bus route. ") + (a.same_network(from, to) ? "Try the journey planner." : "These cities are in different bus networks."));
    if (!parse_ist_date(rq.get("date").c_str(), &y, &m, &d)) return err(400, "Date must be YYYY-MM-DD");
    int64_t ahead = day_number(from_ist(y, m, d, 12, 0)) - day_number(a.clock.now());
    if (ahead < 0 || ahead > 90) return err(400, "Choose a date from today up to 90 days ahead");
    SearchFilters f;
    f.dep_window = rq.get("dep").empty() ? -1 : (int)rq.num("dep");
    f.dep_after = rq.get("after").empty() ? -1 : (int)rq.num("after");     /* minutes after midnight: "leaving after 7 am" */
    f.dep_before = rq.get("before").empty() ? -1 : (int)rq.num("before");
    f.max_fare = rq.num("maxfare") * 100;
    std::snprintf(f.operator_substr, sizeof f.operator_substr, "%s", rq.get("op").c_str());
    f.need_seats = rq.get("seats") == "1";
    if (!rq.get("type").empty() && rq.num("type") >= 0 && rq.num("type") < 31) f.type_mask = 1u << (unsigned)rq.num("type");
    for (const std::string &part : split_fields(rq.get("types"), ','))      /* coach-type list from the assistant ("seater") */
        if (!part.empty() && std::strtoll(part.c_str(), nullptr, 10) >= 0 && std::strtoll(part.c_str(), nullptr, 10) < 31)
            f.type_mask |= 1u << (unsigned)std::strtoll(part.c_str(), nullptr, 10);
    if (rq.get("amenity")[0]) std::snprintf(f.amenity, sizeof f.amenity, "%s", rq.get("amenity").c_str());
    Vec<SearchRow> rows;
    a.search(from, to, y * 10000 + m * 100 + d, f, (SortKey)rq.num("sort", 0), rows);
    JArr arr;
    for (size_t i = 0; i < rows.size(); i++) arr.add(row_json(a, rows[i]));
    return ok(JObj().raw("rows", arr.done()).str("route", a.route_name(a.routes[(size_t)a.find_route(from, to)]))
        .dbl("fare_cap", (double)a.route_highest_fare(a.find_route(from, to)) / 100.0).done());
}

static std::vector<std::string> seat_list(const std::string &s) {
    std::vector<std::string> v;
    for (const std::string &p : split_fields(s, ',')) if (!p.empty()) v.push_back(p);
    return v;
}

static Resp h_hold(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    std::vector<std::string> seats = seat_list(rq.get("seats"));
    std::vector<const char *> labels;
    for (const std::string &s : seats) labels.push_back(s.c_str());
    Result<Unit> r = sv.app.hold_seats(u, (int)rq.num("trip"), labels.data(), (int)labels.size());
    return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
}

static Resp h_quote(Server &sv, const Req &rq) {
    std::vector<std::string> seats = seat_list(rq.get("seats"));
    std::vector<const char *> labels;
    for (const std::string &s : seats) labels.push_back(s.c_str());
    Result<Quote> q = sv.app.quote((int)rq.num("trip"), labels.data(), (int)labels.size(), rq.get("coupon").c_str(), rq.get("insurance") == "1", user_of(sv, rq));
    if (!q.ok) return fail_of(q);
    const Quote &v = q.value;
    return ok(JObj().boolean("ok", true).num("base", v.base).num("discount", v.discount).num("tax", v.tax).num("insurance", v.insurance).num("total", v.total).num("deposit", v.deposit)
                  .num("balance", v.balance).boolean("prebook", v.prebook_eligible).str("balance_due", fmt_datetime(v.balance_due_ms)).done());
}

static Resp h_book(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    std::vector<std::string> seats = seat_list(rq.get("seats"));
    BookingRequest b;
    b.user_id = u; b.trip_id = (int)rq.num("trip"); b.n_pax = (int)std::min<size_t>(seats.size(), (size_t)MAX_PAX + 1);
    if (b.n_pax > MAX_PAX) return err(400, "Choose 1 to 6 seats");
    for (int i = 0; i < b.n_pax; i++) {
        std::string idx = std::to_string(i + 1), nm = rq.get(("name" + idx).c_str());
        for (char &c : nm) if (c == '|' || c == '~' || c == ';' || c == '\\') c = ' ';
        std::snprintf(b.pax[i].seat, sizeof b.pax[i].seat, "%s", seats[(size_t)i].c_str());
        std::snprintf(b.pax[i].name, sizeof b.pax[i].name, "%s", nm.c_str());
        b.pax[i].age = (int)rq.num(("age" + idx).c_str());
        std::string g = rq.get(("gender" + idx).c_str());
        b.pax[i].gender = g.empty() ? 'X' : (char)std::toupper((unsigned char)g[0]);
    }
    b.boarding_stop = (int)rq.num("boarding", -1); b.dropping_stop = (int)rq.num("dropping", -1);
    std::snprintf(b.contact, sizeof b.contact, "%s", rq.get("contact").c_str());
    std::snprintf(b.coupon, sizeof b.coupon, "%s", rq.get("coupon").c_str());
    b.insurance = rq.get("insurance") == "1";
    b.mode = rq.get("mode") == "deposit" ? MODE_DEPOSIT : MODE_FULL;
    Result<int> r = sv.app.create_booking(b);
    if (!r.ok) return fail_of(r);
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return ok(JObj().boolean("ok", true).num("booking", r.value).str("pnr", sv.app.booking_by_id(r.value)->pnr).done());
}

static Resp h_pay(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    std::string k = rq.get("kind"), o = rq.get("outcome");
    Result<Unit> r = sv.app.pay_booking((int)rq.num("booking"), u, k == "deposit" ? PAYK_DEPOSIT : (k == "balance" ? PAYK_BALANCE : PAYK_FULL), o == "declined" ? OUT_DECLINED : (o == "timeout" ? OUT_TIMEOUT : OUT_SUCCESS));
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
}

static Resp h_bookings(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    JArr a;
    for (size_t i = sv.app.bookings.size(); i-- > 0;) if (sv.app.bookings[i].user_id == u) a.add(booking_json(sv.app, sv.app.bookings[i]));
    return ok(a.done());
}

static Resp h_ticket(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    Booking *b = sv.app.booking_by_pnr(rq.get("pnr"), u);
    if (!u || !b) return err(404, "Booking not found");
    const Trip *tr = sv.app.trip_by_id(b->trip_id);
    JArr crew;                                                       // privacy rule: only 24 h before departure, paid bookings
    if (driver_phones_visible(sv.app, *b)) {
        const Bus &bus = sv.app.buses[(size_t)tr->bus_idx];
        if (bus.driver_id > 0) crew.add(JObj().str("role", "Driver").str("name", sv.app.staff[(size_t)bus.driver_id - 1].name).str("phone", sv.app.staff[(size_t)bus.driver_id - 1].phone).done());
        if (bus.conductor_id > 0) crew.add(JObj().str("role", "Conductor").str("name", sv.app.staff[(size_t)bus.conductor_id - 1].name).str("phone", sv.app.staff[(size_t)bus.conductor_id - 1].phone).done());
    }
    unsigned char modules[QR_MAX_SIZE * QR_MAX_SIZE];
    int qsize = 0;
    JArr qrows;
    if (qr_encode(sv.app.qr_payload(*b).c_str(), modules, &qsize) == 0)          // the boarding-pass QR (drawn by the page as SVG)
        for (int y = 0; y < qsize; y++) { std::string row; for (int x = 0; x < qsize; x++) row += modules[y * qsize + x] ? '1' : '0'; qrows.add(jq(row)); }
    std::string state = sv.app.boarding_state(*b);
    const char *caption = state == "VALID" ? "Valid. Show this code at the door." : (state == "BOARDED" ? "All seats boarded." : (state == "PART-BOARDED" ? "Some seats are boarded." : "This pass is cancelled."));
    return ok(JObj().raw("booking", booking_json(sv.app, *b)).raw("trip", trip_json(sv.app, *tr, u)).raw("crew", crew.done())
              .raw("qr", qrows.done()).str("qr_text", sv.app.qr_payload(*b)).str("boarding_state", state).str("boarding_caption", caption).str("share_token", sv.app.share_token_of(b->id)).str("ticket", render_ticket(sv.app, *b, false)).str("share", share_summary(sv.app, *b)).done());
}

static Resp h_cancel(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    Booking *b = sv.app.booking_by_id((int)rq.num("booking"));
    if (!b || b->user_id != u) return err(404, "Booking not found");
    if (rq.get("preview") == "1") {
        RefundPreview p = sv.app.cancel_preview(*b);
        return ok(JObj().boolean("ok", true).num("pct", p.pct).num("refund", p.refund).num("hours_left", p.hours_left).done());
    }
    Result<int64_t> r = sv.app.cancel_booking(b->id, u);
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return r.ok ? ok(JObj().boolean("ok", true).num("refund", r.value).done()) : fail_of(r);
}

static Resp h_track(Server &sv, const Req &rq) {
    Trip *t = sv.app.trip_by_id((int)rq.num("trip"));
    if (!t) return err(404, "Trip not found");
    int u = user_of(sv, rq);                                          /* is the caller one of this trip's passengers? */
    bool owner = false;
    const Booking *mine = nullptr;                                    /* their booking, for the crew-phone privacy rule */
    if (u) for (size_t i = 0; i < sv.app.bookings.size(); i++)
        if (sv.app.bookings[i].trip_id == t->id && sv.app.bookings[i].user_id == u) { owner = true; if (!mine) mine = &sv.app.bookings[i]; }
    return ok(track_json(sv.app, *t, (int)rq.num("drop", -1), owner, mine));
}

static Resp h_sim(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    Trip *t = sv.app.trip_by_id((int)rq.num("trip"));
    if (!u || !t) return err(u ? 404 : 401, u ? "Trip not found" : "Please log in first");
    bool owns = false;                                              /* passengers may only simulate trips they booked */
    for (size_t i = 0; i < sv.app.bookings.size(); i++) if (sv.app.bookings[i].trip_id == t->id && sv.app.bookings[i].user_id == u) owns = true;
    if (!owns && sv.app.user_by_id(u)->role == ROLE_PASSENGER) return err(404, "Trip not found");
    std::string act = rq.get("action");
    long long speed = rq.num("speed", t->sim_speed);
    if (speed == 60 || speed == 120 || speed == 300) t->sim_speed = (int)speed;
    if (act == "play") for (int i = 0; i < 30; i++) sv.app.sim_advance(*t, t->sim_speed);
    else if (act == "tick") sv.app.sim_advance(*t, t->sim_speed);
    else if (act == "km") sv.app.sim_advance_km(*t, 10);
    else if (act == "jam") sv.app.sim_jam(*t);
    else if (act == "reset") sv.app.sim_reset(*t);
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return ok(track_json(sv.app, *t, -1, owns));
}

static Resp h_meta(Server &sv, const Req &) {                      // coach types and the 15 forward routes (for filters and route cards)
    App &a = sv.app;
    JArr types, routes;
    for (size_t i = 0; i < a.types.size(); i++) types.add(JObj().num("idx", (long long)i).str("label", a.types[i].label).done());
    for (size_t i = 0; i < a.routes.size(); i += 2)
        routes.add(JObj().str("from", a.cities[(size_t)a.routes[i].from_city].name).str("to", a.cities[(size_t)a.routes[i].to_city].name).num("km", a.routes[i].km_nominal).str("duration", fmt_duration(a.routes[i].duration_min)).done());
    return ok(JObj().raw("types", types.done()).raw("routes", routes.done()).num("networks", (long long)a.network_count()).done());
}

static Resp h_adjacent(Server &sv, const Req &rq) {              /* group seats: the C engine (bitset run search) decides */
    Trip *t = sv.app.trip_by_id((int)rq.num("trip"));
    int n = (int)rq.num("n");
    if (!t || n < 1 || n > MAX_PAX) return err(400, "Choose a trip and 1 to 6 seats");
    long at = sv.app.find_adjacent(*t, n);
    if (at < 0) return err(404, "No " + std::to_string(n) + " adjacent free seats in one row");
    const BusType &bt = sv.app.types[(size_t)sv.app.buses[(size_t)t->bus_idx].type_idx];
    JArr a;
    for (int i = 0; i < n; i++) { char lb[6]; sv.app.seat_label(bt, (int)at + i, lb); a.add(jq(lb)); }
    return ok(JObj().raw("seats", a.done()).done());
}

static Resp h_notes(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    sv.app.flush_outbox();
    JArr a;
    for (const Notification *n : sv.app.notifications_for(u)) a.add(JObj().str("title", n->title).str("body", n->body).str("time", fmt_hm(n->created_ms)).num("kind", n->kind).boolean("read", n->read != 0).done());
    return ok(JObj().raw("items", a.done()).num("prefs", sv.app.user_by_id(u)->prefs).done());
}

static Resp h_plan(Server &sv, const Req &rq) {
    App &a = sv.app;
    int from = a.find_city(rq.get("from")), to = a.find_city(rq.get("to"));
    if (from < 0 || to < 0) return err(400, "Choose two valid cities");
    Vec<PlanLeg> legs;
    int64_t mins = 0, paise = 0;
    Result<Unit> r = a.plan_journey(from, to, (PlanMode)rq.num("mode", 0), legs, &mins, &paise);
    if (!r.ok) return fail_of(r);
    JArr arr;
    for (size_t i = 0; i < legs.size(); i++) arr.add(JObj().str("from", a.cities[(size_t)legs[i].from_city].name).str("to", a.cities[(size_t)legs[i].to_city].name).str("duration", fmt_duration((int)legs[i].minutes)).num("fare", legs[i].paise).done());
    return ok(JObj().raw("legs", arr.done()).str("total", fmt_duration((int)mins)).num("fare", paise).done());
}

static Resp h_waitlist(Server &sv, const Req &rq) {
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    Result<Unit> r = sv.app.join_waitlist(u, (int)rq.num("trip"), (int)rq.num("seats", 1));
    return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
}

static Resp h_vendor(Server &sv, const Req &rq, const std::string &what) {
    int u = user_of(sv, rq);
    const User *us = u ? sv.app.user_by_id(u) : nullptr;
    if (!us || us->role != ROLE_VENDOR) return err(401, "Vendor login required");
    App &a = sv.app;
    int co = us->company_id;
    if (what == "fleet") {
        JArr arr;
        for (size_t i = 0; i < a.buses.size(); i++) {
            const Bus &b = a.buses[i];
            if (b.company_id != co) continue;
            arr.add(JObj().num("idx", (long long)i).str("reg", b.reg).str("type", a.types[(size_t)b.type_idx].label).str("status", bus_status_name(b.status)).num("status_code", b.status)
                        .str("driver", b.driver_id > 0 ? a.staff[(size_t)b.driver_id - 1].name : "").str("conductor", b.conductor_id > 0 ? a.staff[(size_t)b.conductor_id - 1].name : "").done());
        }
        return ok(arr.done());
    }
    if (what == "staff") {
        JArr arr;
        for (size_t i = 0; i < a.staff.size(); i++) {
            const Staff &s = a.staff[i];
            if (s.company_id == co) arr.add(JObj().num("id", s.id).str("name", s.name).str("role", s.role == STAFF_DRIVER ? "Driver" : "Conductor").str("status", staff_status_name(a.staff_status(s))).str("phone", std::string(s.phone) + " (DEMO number)").done());
        }
        return ok(arr.done());
    }
    if (what == "overview") {
        int bs[3] = {0, 0, 0};
        for (size_t i = 0; i < a.buses.size(); i++) if (a.buses[i].company_id == co) bs[a.buses[i].status]++;
        return ok(JObj().str("company", a.companies[(size_t)co - 1].name).num("available", bs[0]).num("on_trip", bs[1]).num("maintenance", bs[2]).num("revenue", a.revenue_range(co, 0, 119)).done());
    }
    if (what == "assign") { Result<Unit> r = a.assign_staff(co, a.find_bus(rq.get("reg").c_str()), (int)rq.num("staff")); return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r); }
    if (what == "status") {
        int affected = 0;
        Result<Unit> r = a.set_bus_status(co, a.find_bus(rq.get("reg").c_str()), (int)rq.num("status"), rq.get("confirm") == "1", &affected);
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "undo") { Result<std::string> r = a.undo_last(co); return r.ok ? ok(JObj().boolean("ok", true).str("message", r.value).done()) : fail_of(r); }
    if (what == "autoassign") {
        std::string rep;
        int n = a.auto_assign(co, date_key(a.clock.now() + MS_DAY), &rep);
        return ok(JObj().boolean("ok", true).num("assigned", n).str("report", rep).done());
    }
    if (what == "delay") { Result<Unit> r = a.report_delay(co, (int)rq.num("trip"), rq.get("reason"), (int)rq.num("minutes"), rq.get("note")); return r.ok ? ok(JObj().boolean("ok", true).done()) : fail_of(r); }
    if (what == "runs") {                                               /* today's runs with crew and last report */
        JArr arr;
        for (Trip *t : a.trips_on_date(date_key(a.clock.now()), co)) arr.add(run_json(a, *t));
        return ok(arr.done());
    }
    if (what == "crew") {
        Result<Unit> r = a.assign_trip_crew(co, (int)rq.num("trip"), (int)rq.num("driver", -1), (int)rq.num("conductor", -1));
        if (r.ok && sv.autosave) { std::string e; a.save_all(&e); }
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "staff-status") {
        Result<Unit> r = a.set_staff_off_duty(co, (int)rq.num("staff"), rq.get("off") == "1");
        if (r.ok && sv.autosave) { std::string e; a.save_all(&e); }
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "trip") {
        std::string act = rq.get("action");
        Result<Unit> r = act == "start" ? a.start_trip(co, (int)rq.num("trip")) : (act == "complete" ? a.complete_trip(co, (int)rq.num("trip")) : Result<Unit>::fail(Err::VALIDATION, "Action must be start or complete"));
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "manifest") {                                           /* phone numbers masked unless departure is within 24 h */
        Trip *t = a.trip_by_id((int)rq.num("trip"));
        if (!t || a.buses[(size_t)t->bus_idx].company_id != co) return err(404, "Trip not found");
        bool reveal = t->depart_ms - a.clock.now() <= 24 * MS_HOUR;
        JArr rows;
        for (size_t i = 0; i < a.bookings.size(); i++) {
            const Booking &b = a.bookings[i];
            if (b.trip_id != t->id || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) continue;
            for (int k = 0; k < b.n_pax; k++) rows.add(JObj().str("seat", b.pax[k].seat).str("name", b.pax[k].name).str("pnr", b.pnr).str("status", booking_status_name(b.status)).str("contact", a.mask_phone(b.contact, reveal)).done());
        }
        return ok(rows.done());
    }
    return err(404, "Unknown vendor action");
}

static Resp h_clock(Server &sv, const Req &rq) {
    long long mins = rq.num("minutes");
    if (mins < 1 || mins > 60 * 24 * 90) return err(400, "Minutes must be 1 to 129600");
    sv.app.advance_clock(mins * MS_MIN);
    if (sv.autosave) { std::string e; sv.app.save_all(&e); }
    return ok(JObj().boolean("ok", true).str("now", fmt_datetime(sv.app.clock.now())).done());
}

/* ---------- driver, admin, share links, assistant ---------- */

static const User *auth_user(Server &sv, const Req &rq, std::initializer_list<int> roles) {
    int u = user_of(sv, rq);
    const User *us = u ? sv.app.user_by_id(u) : nullptr;
    if (!us) return nullptr;
    for (int r : roles) if (us->role == r) return us;
    return nullptr;
}
#define NEED(var, ...) const User *var = auth_user(sv, rq, {__VA_ARGS__}); if (!var) return err(401, "Sign in with an account that can use this screen")

/* One row describing a trip for the driver, vendor and admin consoles. */
static std::string run_json(App &app, const Trip &t) {
    const Route &r = app.routes[(size_t)t.route_id];
    const Bus &bus = app.buses[(size_t)t.bus_idx];
    TrackInfo ti = app.track_info(t);
    int d = app.trip_driver(t), c = app.trip_conductor(t);
    JObj o;
    o.num("id", t.id).str("reg", bus.reg).str("operator", app.companies[(size_t)bus.company_id - 1].name).str("type", app.types[(size_t)bus.type_idx].label)
        .str("route", app.route_name(r)).str("depart", fmt_datetime(t.depart_ms)).str("dep", fmt_hm(t.depart_ms)).str("arr", fmt_hm_plus(t.arrive_ms, t.depart_ms))
        .str("est", fmt_hm_plus(ti.est_arrive, t.depart_ms)).str("status", t.status == TRIP_SCHEDULED ? "SCHEDULED" : (t.status == TRIP_RUNNING ? "RUNNING" : "COMPLETED"))
        .dbl("km", t.km).dbl("progress", ti.progress_pct).dbl("speed", ti.speed_kmh).num("delay", ti.delay_min).str("delay_reason", t.delay_reason)
        .num("booked", (long long)t.booked.count()).num("seats", (long long)app.seat_count(t)).num("driver_id", d).num("conductor_id", c)
        .str("driver", d ? app.staff[(size_t)d - 1].name : "").str("conductor", c ? app.staff[(size_t)c - 1].name : "")
        .str("driver_status", d ? staff_status_name(app.staff_status(app.staff[(size_t)d - 1])) : "").str("conductor_status", c ? staff_status_name(app.staff_status(app.staff[(size_t)c - 1])) : "");
    return o.done();
}

static Resp h_driver(Server &sv, const Req &rq, const std::string &what) {
    NEED(us, ROLE_DRIVER, ROLE_ADMIN);
    App &a = sv.app;
    if (what == "trips") {
        JArr arr;
        for (Trip *t : a.driver_trips(*us)) arr.add(run_json(a, *t));
        return ok(arr.done());
    }
    if (what == "scan") {
        bool already = false;
        Result<int> r = a.driver_scan(*us, rq.get("code"), &already);
        if (!r.ok) return fail_of(r);
        const Booking &b = *a.booking_by_id(r.value);
        JArr seats;
        for (int i = 0; i < b.n_pax; i++) seats.add(JObj().str("seat", b.pax[i].seat).str("name", b.pax[i].name).num("age", b.pax[i].age).boolean("checked_in", (b.boarded_mask >> i) & 1).done());
        return ok(JObj().boolean("found", true).boolean("already", already).raw("manifest", JObj().str("booking_reference", b.pnr).str("status", booking_status_name(b.status)).num("trip_id", b.trip_id)
            .str("boarding_state", a.boarding_state(b)).raw("seats", seats.done()).done()).done());
    }
    Trip *t = a.trip_by_id((int)rq.num("trip"));
    if (!t || !a.driver_can_access(*us, *t)) return err(404, "Trip not found");
    if (what == "start") { Result<Unit> r = a.driver_start(*us, t->id); return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r); }
    if (what == "end") { Result<Unit> r = a.driver_end(*us, t->id); return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r); }
    if (what == "delay") { Result<Unit> r = a.driver_delay(*us, t->id, rq.get("reason"), (int)rq.num("minutes"), rq.get("note")); return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r); }
    if (what == "position" || what == "simulate-fix") {
        double lat = std::atof(rq.get("lat").c_str()), lng = std::atof(rq.get("lng").c_str()), speed = std::atof(rq.get("speed", "45").c_str()), heading = std::atof(rq.get("heading", "0").c_str()), acc = std::atof(rq.get("accuracy", "10").c_str());
        if (what == "simulate-fix") {                                   /* DEMO: the "device" reports a point 5 km further along the route */
            const Route &r = a.routes[(size_t)t->route_id];
            geo_point p = a.bus_position(r, std::min((double)r.km_nominal, t->km + 5.0));
            lat = p.lat; lng = p.lng; speed = r.avg_kmh; heading = 90; acc = 12;
        } else if (rq.get("lat").empty() || rq.get("lng").empty()) return err(400, "Enter a latitude and a longitude");
        Result<Unit> r = a.driver_position(*us, t->id, lat, lng, speed, heading, acc);
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).dbl("lat", lat).dbl("lng", lng).done()) : fail_of(r);
    }
    if (what == "manifest") {
        JArr rows;
        const Route &r = a.routes[(size_t)t->route_id];
        for (size_t i = 0; i < a.bookings.size(); i++) {
            const Booking &b = a.bookings[i];
            if (b.trip_id != t->id || (b.status != BK_CONFIRMED && b.status != BK_PREBOOKED && b.status != BK_COMPLETED)) continue;
            for (int k = 0; k < b.n_pax; k++)
                rows.add(JObj().str("seat", b.pax[k].seat).str("name", b.pax[k].name).num("age", b.pax[k].age).str("boarding", std::string(r.stops[b.boarding_stop].name) + " " + fmt_hm(a.planned_stop_time(*t, b.boarding_stop)))
                    .str("pnr", b.pnr).str("status", booking_status_name(b.status)).boolean("checked_in", (b.boarded_mask >> k) & 1).str("checked_at", (b.boarded_mask >> k) & 1 ? fmt_hm(b.boarded_ms) : "").done());
        }
        return ok(rows.done());
    }
    if (what == "trail") {
        JArr rows;
        int shown = 0;
        for (size_t i = a.gps_fixes.size(); i-- > 0 && shown < 20;) {
            const GpsFix &g = a.gps_fixes[i];
            if (g.trip_id != t->id) continue;
            rows.add(JObj().str("at", fmt_datetime(g.at_ms)).dbl("lat", g.lat).dbl("lng", g.lng).dbl("heading", g.heading).dbl("speed", g.speed).dbl("accuracy", g.accuracy).done());
            shown++;
        }
        return ok(rows.done());
    }
    return err(404, "Unknown driver action");
}

static Resp h_admin(Server &sv, const Req &rq, const std::string &what) {
    NEED(us, ROLE_ADMIN);
    (void)us;
    App &a = sv.app;
    int today = date_key(a.clock.now());
    if (what == "summary") {
        int bs[3] = {0, 0, 0}, active = 0, pending = 0;
        for (size_t i = 0; i < a.buses.size(); i++) bs[a.buses[i].status]++;
        int64_t money = 0;
        for (size_t i = 0; i < a.bookings.size(); i++) { const Booking &b = a.bookings[i]; active += b.status == BK_CONFIRMED || b.status == BK_PREBOOKED; pending += b.status == BK_PENDING_PAYMENT; money += b.paid - b.refunded; }
        int run = 0, total = 0;
        for (Trip *t : a.trips_on_date(today, 0)) { total++; run += t->status == TRIP_RUNNING; }
        return ok(JObj().num("buses", (long long)a.buses.size()).num("available", bs[0]).num("on_trip", bs[1]).num("maintenance", bs[2]).num("companies", (long long)a.companies.size())
            .num("trips_today", total).num("running", run).num("active_bookings", active).num("pending", pending).num("revenue", money).num("networks", (long long)a.network_count()).done());
    }
    if (what == "fleet") {                                              /* today's fleet telemetry */
        JArr arr;
        for (Trip *t : a.trips_on_date(today, 0)) arr.add(run_json(a, *t));
        return ok(arr.done());
    }
    if (what == "revenue") {
        JArr arr;
        for (const CorridorRevenue &c : a.revenue_by_corridor()) arr.add(JObj().str("route", a.route_name(a.routes[(size_t)c.route])).num("passes", c.passes).num("revenue", c.paise).done());
        return ok(arr.done());
    }
    if (what == "coupons") {
        JArr arr;
        for (size_t i = 0; i < a.coupons.size(); i++) {
            const Coupon &c = a.coupons[i];
            arr.add(JObj().str("code", c.code).str("type", c.is_percent ? "PERCENT" : "FLAT_PAISE").num("value", c.value).num("cap", c.max_discount).num("min", c.min_base).num("uses", a.coupon_use_count((int)i)).num("max_uses", c.max_uses).boolean("active", c.active != 0).done());
        }
        return ok(arr.done());
    }
    if (what == "coupon-add") {
        bool pct = rq.get("type") == "PERCENT";
        double v = std::atof(rq.get("value").c_str()), cap = std::atof(rq.get("cap").c_str()), mn = std::atof(rq.get("min").c_str());
        Result<Unit> r = a.add_coupon(rq.get("code"), pct, pct ? (int64_t)std::llround(v) : (int64_t)std::llround(v * 100), (int64_t)std::llround(cap * 100), (int64_t)std::llround(mn * 100));
        if (r.ok && sv.autosave) { std::string e; a.save_all(&e); }
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "coupon-state") {
        Result<Unit> r = a.set_coupon_active(rq.get("code"), rq.get("active") == "1");
        if (r.ok && sv.autosave) { std::string e; a.save_all(&e); }
        return r.ok ? ok(JObj().boolean("ok", true).str("message", r.message).done()) : fail_of(r);
    }
    if (what == "delays") {
        JArr arr;
        for (size_t i = a.delay_log.size(); i-- > 0 && arr.done().size() < 60000;) {
            const DelayLog &d = a.delay_log[i];
            const Trip *t = a.trip_by_id(d.trip_id);
            arr.add(JObj().str("at", fmt_datetime(d.at_ms)).str("route", t ? a.route_name(a.routes[(size_t)t->route_id]) : "").num("trip", d.trip_id).str("cause", d.cause).num("minutes", d.minutes).str("source", d.source).done());
        }
        return ok(arr.done());
    }
    if (what == "scan-traffic") {
        std::string rep;
        int n = a.scan_traffic_delays(&rep);
        if (sv.autosave) { std::string e; a.save_all(&e); }
        return ok(JObj().boolean("ok", true).num("changed", n).str("report", rep).done());
    }
    return err(404, "Unknown admin action");
}

static Resp h_share(Server &sv, const Req &rq, const std::string &what) {
    App &a = sv.app;
    if (what == "view") {                                               /* public, read-only: no names, no PNR */
        std::string tok = rq.get("token");
        if (tok.size() < 8 || tok.size() > 24 || tok.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) return err(400, "That sharing link is malformed.");
        Booking *b = a.share_lookup(tok);
        if (!b) return err(404, "That sharing link is no longer active.");
        const Trip *t = a.trip_by_id(b->trip_id);
        return ok(JObj().str("route", a.route_name(a.routes[(size_t)t->route_id])).str("operator", a.bus_label(t->bus_idx)).str("type", a.types[(size_t)a.buses[(size_t)t->bus_idx].type_idx].label)
            .str("depart", fmt_datetime(t->depart_ms)).num("trip", t->id).raw("track", track_json(a, *t, -1, true)).done());
    }
    int u = user_of(sv, rq);
    if (!u) return err(401, "Please log in first");
    int bid = (int)rq.num("booking");
    if (what == "create") { Result<std::string> r = a.share_create(u, bid, rq.get("rotate") == "1"); if (sv.autosave) { std::string e; a.save_all(&e); } return r.ok ? ok(JObj().boolean("ok", true).str("token", r.value).done()) : fail_of(r); }
    if (what == "revoke") { Result<Unit> r = a.share_revoke(u, bid); if (sv.autosave) { std::string e; a.save_all(&e); } return r.ok ? ok(JObj().boolean("ok", true).done()) : fail_of(r); }
    return err(404, "Unknown share action");
}

static Resp h_assistant(Server &sv, const Req &rq) {
    AssistantAnswer r = sv.app.assistant(rq.get("text"));
    char iso[16] = "";
    if (r.date_key) std::snprintf(iso, sizeof iso, "%04d-%02d-%02d", r.date_key / 10000, (r.date_key / 100) % 100, r.date_key % 100);
    JArr ignored, types;                                                           /* the parse, so the page replays the same search */
    for (const std::string &s : r.ignored) ignored.add(jq(s));
    for (size_t i = 0; i < sv.app.types.size(); i++) if (r.type_mask & (1u << i)) types.add(JObj().num("idx", (long long)i).str("label", sv.app.types[i].label).done());
    return ok(JObj().str("text", r.text).str("from", r.from >= 0 ? sv.app.cities[(size_t)r.from].name : "").str("to", r.to >= 0 ? sv.app.cities[(size_t)r.to].name : "").str("date", iso).boolean("open_search", r.opened_search)
        .num("sort", r.sort_mode).num("dep_window", r.dep_window).num("dep_after", r.dep_after).num("dep_before", r.dep_before)
        .str("amenity", r.amenity).str("operator", r.operator_substr).raw("types", types.done()).raw("ignored", ignored.done()).done());
}

/* ---------- routing ---------- */

static Resp serve_file(Server &sv, const std::string &name) {
    if (name != "index.html") return err(404, "Page not found");   /* the page is the only file on disk we serve */
    /* Deliver the ASSEMBLED page only: tools/page_gen.cpp inlines Leaflet (CSS + JS) into web/index.html and
       writes build/web_page.html. The authoring copy on disk keeps two <script src="#"> placeholders, so it is
       never served - that page has no map library. The usual start-up folders are tried (repo root, bin/); each
       candidate gets its own ifstream, because reopening a failed stream on the same object leaves libstdc++
       reading zero bytes. If nothing on disk can be read, the copy compiled into the binary is served, which is
       the same assembled page ("make all" builds web_page.cpp from build/web_page.html), so the server also
       works when it is started from a folder that has no web/ next to it. */
    const std::string cand[3] = { sv.web_dir + "/build/web_page.html", "build/web_page.html", "../build/web_page.html" };
    for (int i = 0; i < 3; i++) {
        std::ifstream in(cand[i].c_str(), std::ios::binary);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        if (ss.str().empty()) continue;
        Resp r;
        r.type = "text/html; charset=utf-8";
        r.body = ss.str();
        return r;
    }
    Resp r;
    r.type = "text/html; charset=utf-8";
    r.body.assign(reinterpret_cast<const char *>(sr_page_data), sr_page_len);
    return r;
}

/* The offline map background (spec 8, "built into the program, served at /basemap.json"). Prefer the file the
   operator generated (web/basemap.json) so coastlines show when they were supplied; a missing, empty or
   foreign file is ignored and the same document is generated from the catalogue instead, which means a machine
   that never ran --make-basemap still gets a background when every tile server is blocked. Answered once and
   cached: the catalogue cannot change under a running server. */
static Resp h_basemap(Server &sv) {
    if (sv.basemap_cache.empty()) {
        const std::string cand[3] = { sv.web_dir + "/basemap.json", "web/basemap.json", "../web/basemap.json" };
        for (int i = 0; i < 3; i++) {
            std::ifstream in(cand[i].c_str(), std::ios::binary);      /* a fresh stream per candidate: see serve_file */
            if (!in) continue;
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string text = ss.str();
            if (text.size() < 200 || text.find("smartroute-basemap") == std::string::npos) continue;
            sv.basemap_cache = text;
            break;
        }
        if (sv.basemap_cache.empty()) sv.basemap_cache = sr::basemap_generated(sv.app);
    }
    Resp r;
    r.body = sv.basemap_cache;
    return r;
}

static Resp route(Server &sv, const Req &rq) {
    const std::string &p = rq.path;
    bool post = rq.method == "POST";
    if (rq.method == "GET" && (p == "/" || p == "/index.html")) return serve_file(sv, "index.html");
    if (rq.method == "GET" && p == "/basemap.json") return h_basemap(sv);
    if (p == "/api/state") return h_state(sv, rq);
    if (p == "/api/cities") return h_cities(sv, rq);
    if (p == "/api/register" && post) return h_auth(sv, rq, true);
    if (p == "/api/login" && post) return h_auth(sv, rq, false);
    if (p == "/api/guest" && post) return h_guest(sv, rq);
    if (p == "/api/lookup") return h_lookup(sv, rq);
    if (p == "/api/logout" && post) { sv.sessions.remove(rq.token); return ok(JObj().boolean("ok", true).done()); }
    if (p == "/api/search") return h_search(sv, rq);
    if (p == "/api/trip") { Trip *t = sv.app.trip_by_id((int)rq.num("id")); return t ? ok(trip_json(sv.app, *t, user_of(sv, rq))) : err(404, "Trip not found"); }
    if (p == "/api/hold" && post) return h_hold(sv, rq);
    if (p == "/api/quote" && post) return h_quote(sv, rq);
    if (p == "/api/bookings" && post) return h_book(sv, rq);
    if (p == "/api/bookings") return h_bookings(sv, rq);
    if (p == "/api/pay" && post) return h_pay(sv, rq);
    if (p == "/api/ticket") return h_ticket(sv, rq);
    if (p == "/api/cancel" && post) return h_cancel(sv, rq);
    if (p == "/api/track") return h_track(sv, rq);
    if (p == "/api/sim" && post) return h_sim(sv, rq);
    if (p == "/api/meta") return h_meta(sv, rq);
    if (p == "/api/adjacent") return h_adjacent(sv, rq);
    if (p == "/api/notifications") return h_notes(sv, rq);
    if (p == "/api/notifications/read" && post) { int u = user_of(sv, rq); if (u) sv.app.mark_all_read(u); return ok(JObj().boolean("ok", true).done()); }
    if (p == "/api/prefs" && post) { int u = user_of(sv, rq); if (u) sv.app.set_prefs(u, (int)rq.num("mask")); return ok(JObj().boolean("ok", true).done()); }
    if (p == "/api/plan") return h_plan(sv, rq);
    if (p == "/api/waitlist" && post) return h_waitlist(sv, rq);
    if (p == "/api/clock" && post) return h_clock(sv, rq);
    if (p == "/api/assistant" && post) return h_assistant(sv, rq);
    if (p.rfind("/api/share/", 0) == 0) return h_share(sv, rq, p.substr(11));
    if (p.rfind("/api/driver/", 0) == 0) return h_driver(sv, rq, p.substr(12));
    if (p.rfind("/api/admin/", 0) == 0) return h_admin(sv, rq, p.substr(11));
    if (p.rfind("/api/vendor/", 0) == 0) return h_vendor(sv, rq, p.substr(12));
    return err(404, "Unknown endpoint");
}

/* ---------- socket plumbing: one select() loop, so no client can stall the server ---------- */

enum ParseState { NEED_MORE, COMPLETE, BAD };

/* Parses a request that may still be arriving. Browsers open spare connections and send nothing,
 * or send a request in pieces, so "not complete yet" is a normal answer. */
static ParseState parse_request(const std::string &data, Req *rq) {
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) return data.size() > 16384 ? BAD : NEED_MORE;
    std::istringstream hs(data.substr(0, header_end));
    std::string line, target, version;
    std::getline(hs, line);
    std::istringstream rl(line);
    rl >> rq->method >> target >> version;
    if (rq->method.empty() || target.empty() || target[0] != '/') return BAD;
    size_t content_len = 0;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = line.substr(0, colon), v = line.substr(colon + 1);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        for (char &ch : k) ch = (char)std::tolower((unsigned char)ch);
        if (k == "content-length") content_len = (size_t)std::strtoul(v.c_str(), nullptr, 10);
        else if (k == "authorization" && v.rfind("Bearer ", 0) == 0) rq->token = v.substr(7);
        else if (k == "origin") rq->origin = v;
    }
    if (content_len > 65536) return BAD;
    if (data.size() < header_end + 4 + content_len) return NEED_MORE;      /* body still arriving */
    rq->body = data.substr(header_end + 4, content_len);
    size_t q = target.find('?');
    rq->path = target.substr(0, q);
    if (q != std::string::npos) parse_pairs(target.substr(q + 1), &rq->params);
    if (!rq->body.empty()) parse_pairs(rq->body, &rq->params);
    return COMPLETE;
}

/* CORS: allow the page itself, a page opened from disk (Origin "null") and localhost dev servers; nothing else. */
static std::string cors_origin(const std::string &o) {
    if (o == "null" || o.rfind("http://localhost:", 0) == 0 || o.rfind("http://127.0.0.1:", 0) == 0) return o;
    return "http://localhost:5173";
}

static void send_response(sock_t c, const Resp &r, const std::string &origin = "") {
    const char *reason = r.status == 200 ? "OK" : (r.status == 404 ? "Not Found" : (r.status == 401 ? "Unauthorized" : (r.status == 409 ? "Conflict" : (r.status == 402 ? "Payment Required" : (r.status == 429 ? "Too Many Requests" : "Bad Request")))));
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason + "\r\nContent-Type: " + r.type + "\r\nContent-Length: " + std::to_string(r.body.size()) +
        "\r\nAccess-Control-Allow-Origin: " + cors_origin(origin) + "\r\nVary: Origin\r\nAccess-Control-Allow-Headers: Content-Type, Authorization\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n" + r.body;
    size_t sent = 0;
    while (sent < out.size()) {
        int n = (int)send(c, out.data() + sent, (int)(out.size() - sent), 0);
        if (n <= 0) break;                                    /* client left or send timed out */
        sent += (size_t)n;
    }
}

/* Listens on the loopback address of one family only (this is a localhost demo server). */
static sock_t open_listener(int family, int port) {
    sock_t s = socket(family, SOCK_STREAM, 0);
    if (s == BAD_SOCK) return BAD_SOCK;
    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, sizeof yes);   /* Windows: REUSEADDR would let another program share the port */
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
#endif
    int ok = 0;
    if (family == AF_INET) {
        sockaddr_in a;
        std::memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((unsigned short)port);
        ok = bind(s, (sockaddr *)&a, sizeof a) == 0;
    } else {
        sockaddr_in6 a;
        std::memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6; a.sin6_addr = in6addr_loopback; a.sin6_port = htons((unsigned short)port);
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&yes, sizeof yes);
        ok = bind(s, (sockaddr *)&a, sizeof a) == 0;
    }
    if (!ok || listen(s, 32) != 0) { close_sock(s); return BAD_SOCK; }
    return s;
}

struct Client { sock_t s; std::string buf; std::chrono::steady_clock::time_point last; };

static constexpr size_t MAX_CLIENTS = 40;                    /* stays below the 64-socket select() limit on Windows */
static constexpr int IDLE_SECONDS = 15;

static void launch_browser(int port) {
    std::string url = "http://localhost:" + std::to_string(port);
#ifdef _WIN32
    std::string cmd = "start \"\" " + url;
#elif defined(__APPLE__)
    std::string cmd = "open " + url + " >/dev/null 2>&1 &";
#else
    std::string cmd = "xdg-open " + url + " >/dev/null 2>&1 &";
#endif
    if (std::system(cmd.c_str()) != 0) std::printf("(Could not open a browser automatically; open %s yourself.)\n", url.c_str());
}

int run_server(App &app, int port, const std::string &web_dir, bool autosave, bool open_browser) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("Winsock could not start.\n"); return 1; }
#endif
    std::vector<sock_t> listeners;
    sock_t v4 = open_listener(AF_INET, port);
    if (v4 == BAD_SOCK) { std::printf("Could not listen on port %d (is it already in use?). Try: bin/smartroute --serve %d\n", port, port + 1); return 1; }
    listeners.push_back(v4);
    sock_t v6 = open_listener(AF_INET6, port);               /* so "localhost" works whichever address the browser picks */
    if (v6 != BAD_SOCK) listeners.push_back(v6);
    Server sv(app, autosave, web_dir);
    sv.virt_anchor_ms = app.clock.now();                            /* real seconds from here on map onto the virtual timeline */
    std::printf("SmartRoute web UI (DEMO data) is running.\n  Open:  http://localhost:%d   (or http://127.0.0.1:%d)\n  Stop:  Ctrl+C\n", port, port);
    std::fflush(stdout);
    if (open_browser) launch_browser(port);
    std::vector<Client> clients;
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        int maxfd = 0;
        for (sock_t l : listeners) { FD_SET(l, &rd); maxfd = std::max(maxfd, (int)l); }
        for (const Client &c : clients) { FD_SET(c.s, &rd); maxfd = std::max(maxfd, (int)c.s); }
        timeval tv;
        tv.tv_sec = 1; tv.tv_usec = 0;
        int ready = select(maxfd + 1, &rd, nullptr, nullptr, &tv);
        auto now = std::chrono::steady_clock::now();
        /* The virtual clock follows real time (App::sync_wall_clock): without this tick a 10-minute seat
           hold could never expire while the server ran, leaving seats blocked by visitors who left. */
        app.sync_wall_clock(sv.virt_anchor_ms + std::chrono::duration_cast<std::chrono::milliseconds>(now - sv.anchor).count());
        if (ready > 0) {
            for (sock_t l : listeners) {
                if (!FD_ISSET(l, &rd)) continue;
                sock_t c = accept(l, nullptr, nullptr);
                if (c == BAD_SOCK) continue;
                if (clients.size() >= MAX_CLIENTS) { close_sock(clients.front().s); clients.erase(clients.begin()); }   /* drop the oldest */
#ifdef _WIN32
                DWORD to = 3000;
#else
                timeval to; to.tv_sec = 3; to.tv_usec = 0;
#endif
                setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof to);
                clients.push_back(Client{c, "", now});
            }
        }
        std::vector<Client> keep;
        for (Client &c : clients) {
            bool close_it = false;
            if (ready > 0 && FD_ISSET(c.s, &rd)) {
                char buf[4096];
                int n = (int)recv(c.s, buf, sizeof buf, 0);
                if (n <= 0) close_it = true;                       /* client closed (e.g. an unused pre-connect) */
                else {
                    c.buf.append(buf, (size_t)n);
                    c.last = now;
                    Req rq;
                    ParseState st = parse_request(c.buf, &rq);
                    if (st == COMPLETE) {
                        Resp r;
                        if (rq.method == "OPTIONS") r.body = "{}";
                        else r = route(sv, rq);
                        std::printf("%s %s -> %d\n", rq.method.c_str(), rq.path.c_str(), r.status);   /* one line per request (spec 7) */
                        std::fflush(stdout);
                        send_response(c.s, r, rq.origin);
                        close_it = true;
                    } else if (st == BAD) {
                        std::printf("bad request -> 400\n");
                        std::fflush(stdout);
                        send_response(c.s, err(400, "Bad request"));
                        close_it = true;
                    }
                }
            } else if (std::chrono::duration_cast<std::chrono::seconds>(now - c.last).count() > IDLE_SECONDS) close_it = true;   /* idle too long */
            if (close_it) close_sock(c.s); else keep.push_back(std::move(c));
        }
        clients.swap(keep);
    }
}

}  // namespace sr
