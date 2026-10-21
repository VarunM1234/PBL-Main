/*
 * PURPOSE: The operations side of SmartRoute: driver console (duty list, start/end, GPS fixes, delay reports,
 *          boarding check-in), per-trip crew with overlap checks, staff off-duty rules, platform-admin figures
 *          (revenue by corridor, coupon controls, DEMO traffic scan) and shareable tracking links.
 * WHY THIS NAME: "ops" = operations; the original project kept these in opsRoutes.ts.
 * CONNECTS TO: app.hpp, core/geo (nearest point on the route), core/ds_sort (trips by departure), core/ds_hash (indexes).
 * COMPLEXITY: crew overlap check O(trips); revenue by corridor O(bookings); scans O(trips).
 * RULES: D13 (another company's data looks absent); a driver sees only trips whose crew includes them.
 */
#include "app.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "ds_sort.h"
}

namespace sr {

static int cmp_trip_depart(const void *a, const void *b) {
    int64_t x = (*static_cast<Trip *const *>(a))->depart_ms, y = (*static_cast<Trip *const *>(b))->depart_ms;
    return (x > y) - (x < y);
}

static void sort_trips(std::vector<Trip *> &v) {
    if (v.size() > 1) ds_merge_sort(v.data(), v.size(), sizeof(Trip *), cmp_trip_depart);    /* stable merge sort by departure */
}

static std::string upper_trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    std::string o = s.substr(a, b - a);
    for (char &c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

/* ---------- who is on a trip ---------- */

int App::trip_driver(const Trip &t) const {
    if (t.auto_driver > 0) return t.auto_driver;                  /* a per-trip assignment beats the bus default */
    return buses[(size_t)t.bus_idx].driver_id > 0 ? buses[(size_t)t.bus_idx].driver_id : 0;
}

int App::trip_conductor(const Trip &t) const {
    if (t.auto_conductor > 0) return t.auto_conductor;
    return buses[(size_t)t.bus_idx].conductor_id > 0 ? buses[(size_t)t.bus_idx].conductor_id : 0;
}

bool App::driver_can_access(const User &u, const Trip &t) const {
    if (u.role == ROLE_ADMIN) return true;
    return u.role == ROLE_DRIVER && u.staff_id > 0 && trip_driver(t) == u.staff_id;
}

std::vector<Trip *> App::trips_on_date(int dk, int company_id) {
    std::vector<Trip *> out;
    for (size_t si = 0; si < schedules.size(); si++) {
        if (company_id && buses[(size_t)schedules[si].bus_idx].company_id != company_id) continue;
        if (Trip *t = get_or_create_trip((int)si, dk)) out.push_back(t);
    }
    sort_trips(out);
    return out;
}

std::vector<Trip *> App::driver_trips(const User &u) {
    std::vector<Trip *> out;
    int company = u.role == ROLE_ADMIN ? 0 : u.company_id;
    for (int d = 0; d < 2; d++)                                     /* today and tomorrow */
        for (Trip *t : trips_on_date(date_key(clock.now() + d * MS_DAY), company))
            if (driver_can_access(u, *t)) out.push_back(t);
    for (size_t i = 0; i < trips.size(); i++) {                     /* a trip still on the road from an earlier day */
        Trip *t = *trips.at(i);
        if (t->status == TRIP_RUNNING && driver_can_access(u, *t) && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    sort_trips(out);
    return out;
}

Result<Unit> App::driver_start(const User &u, int trip_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t || !driver_can_access(u, *t)) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    return start_trip(buses[(size_t)t->bus_idx].company_id, trip_id);
}

Result<Unit> App::driver_end(const User &u, int trip_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t || !driver_can_access(u, *t)) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    return complete_trip(buses[(size_t)t->bus_idx].company_id, trip_id);
}

/* A GPS fix is range-checked, snapped to the nearest point of the route, stored, and fed to the same alert engine the simulator uses. */
Result<Unit> App::driver_position(const User &u, int trip_id, double lat, double lng, double speed, double heading, double accuracy) {
    Trip *t = trip_by_id(trip_id);
    if (!t || !driver_can_access(u, *t)) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    if (!(lat >= -90 && lat <= 90) || !(lng >= -180 && lng <= 180)) return Result<Unit>::fail(Err::VALIDATION, "Latitude must be -90 to 90 and longitude -180 to 180");
    if (!(speed >= 0 && speed <= 160)) return Result<Unit>::fail(Err::VALIDATION, "Speed must be 0 to 160 km/h");
    if (!(accuracy >= 0 && accuracy <= 5000)) return Result<Unit>::fail(Err::VALIDATION, "Accuracy must be 0 to 5000 metres");
    if (!(heading >= 0 && heading < 360)) return Result<Unit>::fail(Err::VALIDATION, "Heading must be 0 to under 360 degrees");
    if (t->status != TRIP_RUNNING) return Result<Unit>::fail(Err::BAD_STATE, "Start the trip before sending positions");
    const Route &r = routes[(size_t)t->route_id];
    double along = 0;
    double off = geo_nearest_km(r.pts, r.cum_km, (size_t)r.n_stops, geo_point{lat, lng}, &along);
    if (off > 25.0) return Result<Unit>::fail(Err::VALIDATION, "That position is " + std::to_string((int)off) + " km from the route");
    double prev = t->km;
    t->km = std::max(prev, std::min(along, (double)r.km_nominal));  /* the bus never moves backwards on the tracker */
    t->sim_started = 1;
    t->sim_sec = std::max(0.0, (double)(clock.now() - t->depart_ms) / 1000.0);
    ds_ring_push(t->gps, clock.now(), speed);
    ds_ring_evict_before(t->gps, clock.now() - 10 * MS_MIN);
    GpsFix f{(int)gps_fixes.size() + 1, t->id, clock.now(), lat, lng, speed, heading, accuracy};
    gps_fixes.push(f);
    fire_alerts(*t, prev, t->km);
    flush_outbox();
    return okay("Position recorded");
}

Result<Unit> App::driver_delay(const User &u, int trip_id, const std::string &reason, int minutes, const std::string &note) {
    Trip *t = trip_by_id(trip_id);
    if (!t || !driver_can_access(u, *t)) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    static const char *REASONS[5] = {"TRAFFIC", "WEATHER", "BREAKDOWN", "ROADWORK", "OTHER"};
    bool known = false;
    for (const char *s : REASONS) known |= reason == s;
    if (!known) return Result<Unit>::fail(Err::VALIDATION, "Reason must be TRAFFIC, WEATHER, BREAKDOWN, ROADWORK or OTHER");
    if (minutes < 1 || minutes > 600) return Result<Unit>::fail(Err::VALIDATION, "Delay must be 1 to 600 minutes");
    if (note.size() > 240) return Result<Unit>::fail(Err::VALIDATION, "Note must be at most 240 characters");
    apply_delay(*t, reason, minutes, note.substr(0, 120), u.role == ROLE_ADMIN ? "admin" : "driver");
    return okay("Delay reported");
}

/* ---------- boarding check-in ---------- */

Booking *App::booking_by_pnr_any(const std::string &pnr) const {
    int *i = idx_pnr.get(upper_trim(pnr));
    return i ? bookings.at((size_t)*i) : nullptr;
}

std::string App::boarding_state(const Booking &b) const {
    if (b.status == BK_CANCELLED || b.status == BK_EXPIRED) return "CANCELLED";
    int full = (1 << b.n_pax) - 1;
    if ((b.boarded_mask & full) == 0) return "VALID";
    return (b.boarded_mask & full) == full ? "BOARDED" : "PART-BOARDED";
}

std::string App::qr_payload(const Booking &b) const {
    /* The text inside the QR changes once seats are boarded, so a used pass no longer shows its old code. */
    return "SR|" + std::string(b.pnr) + "|" + std::to_string(b.trip_id) + "|" + std::to_string(b.boarded_mask);
}

Result<int> App::driver_scan(const User &u, const std::string &code, bool *already) {
    *already = false;
    if (code.size() < 6 || code.size() > 80) return Result<int>::fail(Err::VALIDATION, "Enter the ticket code or the booking reference");
    std::string ref = code;
    size_t bar = code.find('|');
    if (bar != std::string::npos) {                                 /* "SR|PNR|trip|mask": the reference is field 2 */
        size_t bar2 = code.find('|', bar + 1);
        ref = code.substr(bar + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar - 1);
    }
    Booking *b = booking_by_pnr_any(ref);
    if (!b) return Result<int>::fail(Err::NOT_FOUND, "Ticket not found on this network.");
    Trip *t = trip_by_id(b->trip_id);
    if (!t || !driver_can_access(u, *t)) return Result<int>::fail(Err::NOT_FOUND, "This ticket is not for one of your trips.");
    if (b->status != BK_CONFIRMED) {
        std::string why = b->status == BK_PREBOOKED ? "Ticket is PREBOOKED: the balance is not paid yet." : std::string("Ticket is ") + (b->status == BK_COMPLETED ? "COMPLETED" : b->status == BK_CANCELLED ? "CANCELLED" : b->status == BK_EXPIRED ? "EXPIRED" : "not paid") + ".";
        return Result<int>::fail(Err::BAD_STATE, why);
    }
    int full = (1 << b->n_pax) - 1;
    *already = (b->boarded_mask & full) == full;
    if (!*already) {
        b->boarded_mask = full;
        b->boarded_ms = clock.now();
        std::string seats;
        for (int i = 0; i < b->n_pax; i++) seats += (i ? ", " : "") + std::string(b->pax[i].seat);
        notify(b->user_id, NK_JOURNEY, "Boarding confirmed", "Seat " + seats + " checked in at " + fmt_hm(clock.now()) + ". Your pass now shows as boarded.", "boarded:" + std::to_string(b->id));
        flush_outbox();
    }
    return Result<int>::success(b->id);
}

/* ---------- per-trip crew (vendor) ---------- */

Result<Unit> App::assign_trip_crew(int company_id, int trip_id, int driver_id, int conductor_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t || buses[(size_t)t->bus_idx].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    if (t->status == TRIP_COMPLETED || t->status == TRIP_CANCELLED) return Result<Unit>::fail(Err::BAD_STATE, "That trip is already over");
    const int ids[2] = {driver_id, conductor_id};
    for (int k = 0; k < 2; k++) {
        int sid = ids[k];
        if (sid <= 0) continue;                                      /* -1 leave alone, 0 handled below */
        if ((size_t)sid > staff.size() || staff[(size_t)sid - 1].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Staff member not found");
        const Staff &s = staff[(size_t)sid - 1];
        if (s.role != (k == 0 ? STAFF_DRIVER : STAFF_CONDUCTOR)) return Result<Unit>::fail(Err::VALIDATION, std::string(s.name) + (k == 0 ? " is a conductor, not a driver" : " is a driver, not a conductor"));
        if (s.off_duty) return Result<Unit>::fail(Err::CONFLICT, std::string(s.name) + " is off duty and cannot be assigned");
        for (size_t i = 0; i < trips.size(); i++) {                  /* interval overlap against every other live trip */
            const Trip &o = *trips[i];
            if (o.id == t->id || o.status == TRIP_COMPLETED || o.status == TRIP_CANCELLED) continue;
            int other = k == 0 ? trip_driver(o) : trip_conductor(o);
            if (other == sid && o.depart_ms < t->arrive_ms && t->depart_ms < o.arrive_ms)
                return Result<Unit>::fail(Err::CONFLICT, std::string(s.name) + " is already on trip " + std::to_string(o.id) + " (" + fmt_hm(o.depart_ms) + " to " + fmt_hm(o.arrive_ms) + ") which overlaps this one");
        }
    }
    if (driver_id != -1) t->auto_driver = driver_id > 0 ? driver_id : -1;
    if (conductor_id != -1) t->auto_conductor = conductor_id > 0 ? conductor_id : -1;
    return okay("Crew updated");
}

Result<Unit> App::set_staff_off_duty(int company_id, int staff_id, bool off) {
    if (staff_id < 1 || (size_t)staff_id > staff.size() || staff[(size_t)staff_id - 1].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Staff member not found");
    Staff &s = staff[(size_t)staff_id - 1];
    if (off) {
        int today = date_key(clock.now());
        for (size_t i = 0; i < trips.size(); i++) {
            const Trip &t = *trips[i];
            if (t.date_key == today && t.status != TRIP_COMPLETED && t.status != TRIP_CANCELLED && (trip_driver(t) == staff_id || trip_conductor(t) == staff_id))
                return Result<Unit>::fail(Err::CONFLICT, std::string(s.name) + " is booked on a run today (trip " + std::to_string(t.id) + ") and cannot go off duty");
        }
    }
    s.off_duty = off ? 1 : 0;
    return okay(std::string(s.name) + (off ? " is now off duty" : " is back on duty"));
}

/* ---------- platform admin ---------- */

std::vector<CorridorRevenue> App::revenue_by_corridor() const {
    std::vector<CorridorRevenue> out;
    for (size_t i = 0; i < routes.size(); i += 2) out.push_back(CorridorRevenue{(int)i, 0, 0});   /* forward routes are the even ids */
    for (size_t i = 0; i < bookings.size(); i++) {
        const Booking &b = bookings[i];
        if (b.status != BK_CONFIRMED && b.status != BK_COMPLETED) continue;
        const Trip *t = trip_by_id(b.trip_id);
        if (!t) continue;
        CorridorRevenue &c = out[(size_t)t->route_id / 2];
        c.passes += b.n_pax;
        c.paise += b.paid - b.refunded;
    }
    return out;
}

Result<Unit> App::add_coupon(const std::string &raw, bool percent, int64_t value, int64_t cap, int64_t min_base) {
    std::string code = upper_trim(raw);
    if (code.size() < 3 || code.size() > 20) return Result<Unit>::fail(Err::VALIDATION, "Code must be 3 to 20 letters or digits");
    for (char c : code) if (!std::isalnum((unsigned char)c)) return Result<Unit>::fail(Err::VALIDATION, "Code may contain only letters and digits");
    if (value <= 0) return Result<Unit>::fail(Err::VALIDATION, "Discount must be more than zero");
    if (percent && value > 100) return Result<Unit>::fail(Err::VALIDATION, "A percentage cannot be more than 100");
    if (cap < 0 || min_base < 0) return Result<Unit>::fail(Err::VALIDATION, "Cap and minimum cannot be negative");
    if (idx_coupon.has(code)) return Result<Unit>::fail(Err::CONFLICT, "That coupon code already exists");
    Coupon c;
    std::memset(&c, 0, sizeof c);
    std::snprintf(c.code, sizeof c.code, "%s", code.c_str());
    c.is_percent = percent ? 1 : 0; c.value = value; c.max_discount = cap; c.min_base = min_base;
    c.per_user_limit = 1; c.max_uses = 1000; c.valid_days = 365; c.active = 1;
    std::snprintf(c.allowed, sizeof c.allowed, "ALL");
    coupons.push(c);
    idx_coupon.put(c.code, (int)coupons.size() - 1);
    return okay("Coupon " + code + " issued");
}

Result<Unit> App::set_coupon_active(const std::string &raw, bool active) {
    int *ci = idx_coupon.get(upper_trim(raw));
    if (!ci) return Result<Unit>::fail(Err::NOT_FOUND, "Coupon not found");
    coupons[(size_t)*ci].active = active ? 1 : 0;
    return okay(active ? "Coupon enabled" : "Coupon disabled");
}

int App::coupon_use_count(int coupon_idx) const {
    int n = 0;
    for (size_t i = 0; i < coupon_uses.size(); i++) n += coupon_uses[i].coupon_idx == coupon_idx;
    return n;
}

/* DEMO TRAFFIC: a fixed time-of-day pattern (not real traffic). Peak hours add a delay to trips on the road or leaving soon. */
int App::scan_traffic_delays(std::string *report) {
    Civil c = to_ist(clock.now());
    bool peak = (c.hour >= 8 && c.hour < 11) || (c.hour >= 17 && c.hour < 21);
    int changed = 0;
    report->clear();
    for (size_t i = 0; i < trips.size(); i++) {
        Trip &t = *trips[i];
        bool on_road = t.status == TRIP_RUNNING, soon = t.status == TRIP_SCHEDULED && t.depart_ms >= clock.now() && t.depart_ms - clock.now() <= 3 * MS_HOUR;
        if (!on_road && !soon) continue;
        int minutes = peak ? 8 + (t.id * 7) % 13 : 0;
        if (minutes <= t.delay_min) continue;
        apply_delay(t, "TRAFFIC", minutes, "DEMO TRAFFIC scan", "traffic-scan (DEMO)");
        *report += "  trip " + std::to_string(t.id) + " " + route_name(routes[(size_t)t.route_id]) + ": +" + std::to_string(minutes) + " min\n";
        changed++;
    }
    *report = std::string("DEMO TRAFFIC scan at ") + fmt_hm(clock.now()) + (peak ? " (peak hour)" : " (off-peak: no delays)") + "; trips updated: " + std::to_string(changed) + "\n" + *report;
    return changed;
}

/* ---------- share links ---------- */

static std::string make_token(App &app) {
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    std::string t;
    for (int i = 0; i < 12; i++) t += ALPHABET[app.next_random() % 64];
    return t;
}

std::string App::share_token_of(int booking_id) const {
    for (size_t i = shares.size(); i-- > 0;) if (shares[i].active && shares[i].booking_id == booking_id) return shares[i].token;
    return "";
}

Result<std::string> App::share_create(int user_id, int booking_id, bool rotate) {
    Booking *b = booking_by_id(booking_id);
    if (!b || b->user_id != user_id) return Result<std::string>::fail(Err::NOT_FOUND, "Booking not found");
    if (b->status == BK_CANCELLED || b->status == BK_EXPIRED) return Result<std::string>::fail(Err::BAD_STATE, "A cancelled or expired booking cannot be shared");
    std::string existing = share_token_of(booking_id);
    if (!existing.empty() && !rotate) return Result<std::string>::success(existing);
    for (size_t i = 0; i < shares.size(); i++) if (shares[i].active && shares[i].booking_id == booking_id) shares[i].active = 0;   /* rotating kills the old link */
    ShareLink s;
    std::memset(&s, 0, sizeof s);
    std::snprintf(s.token, sizeof s.token, "%s", make_token(*this).c_str());
    s.booking_id = booking_id; s.at_ms = clock.now(); s.active = 1;
    shares.push(s);
    idx_share.put(s.token, (int)shares.size() - 1);
    return Result<std::string>::success(s.token);
}

Result<Unit> App::share_revoke(int user_id, int booking_id) {
    Booking *b = booking_by_id(booking_id);
    if (!b || b->user_id != user_id) return Result<Unit>::fail(Err::NOT_FOUND, "Booking not found");
    for (size_t i = 0; i < shares.size(); i++) if (shares[i].active && shares[i].booking_id == booking_id) shares[i].active = 0;
    return okay("Sharing link revoked");
}

Booking *App::share_lookup(const std::string &token) const {
    int *i = idx_share.get(token);
    if (!i || !shares[(size_t)*i].active) return nullptr;
    Booking *b = booking_by_id(shares[(size_t)*i].booking_id);
    if (!b || b->status == BK_CANCELLED || b->status == BK_EXPIRED) return nullptr;
    return b;
}

}  // namespace sr
