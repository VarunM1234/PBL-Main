/*
 * PURPOSE: Live-tracking simulator (DEMO), ETA, crossing-based alerts and bus position.
 * WHY THIS NAME: "tracking" = following a bus along its route.
 * CONNECTS TO: core/geo (position, row 13), ds_ring (speed average, row 14), ds_lru (memo, row 8),
 *              ds_list (stops), ds_sort (event order), notifications.cpp (dedupe + bell).
 * COMPLEXITY: advance O(steps + stops); position O(log s); alerts O(events x bookings).
 */
#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "ds_sort.h"
}

namespace sr {

static constexpr double JAM_SPEED_KMH = 15.0;
static constexpr double JAM_LENGTH_KM = 5.0;
static constexpr double EPS = 1e-9;

/* ---------- position (LRU-memoised) ---------- */

struct CachedPoint { double lat, lng, pad1, pad2; };

geo_point App::bus_position(const Route &r, double km) const {
    char key[64];
    std::snprintf(key, sizeof key, "p%d:%ld", r.id, std::lround(km * 10.0));
    if (void *hit = ds_lru_get(cache, key)) {
        const CachedPoint *c = static_cast<const CachedPoint *>(hit);
        return geo_point{c->lat, c->lng};
    }
    geo_point p{};
    size_t seg = 0;
    geo_point_at_km(r.pts, r.cum_km, (size_t)r.n_stops, km, &p, &seg);   /* binary search + interpolation */
    CachedPoint c{p.lat, p.lng, 0, 0};
    ds_lru_put(cache, key, &c);
    return p;
}

/* ---------- simulator core ---------- */

/* Next break stop not yet started, or nullptr. */
static const Stop *next_break(const Route &r, int breaks_done) {
    int seen = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK && seen++ == breaks_done) return &r.stops[i];
    return nullptr;
}

static void record_speed(Trip &t, double kmh) {
    int64_t at = t.depart_ms + (int64_t)(t.sim_sec * 1000.0);
    ds_ring_push(t.gps, at, kmh);
    ds_ring_evict_before(t.gps, at - 10 * MS_MIN);               /* keep only the last 10 virtual minutes */
}

/* Move the bus. Either a time budget (seconds) or a distance budget (km) is finite. */
static void sim_core(Trip &t, const Route &r, double seconds, double km_budget, bool stop_at_break) {
    const double total = r.km_nominal, cruise = r.avg_kmh;
    while (t.km < total - EPS && seconds > EPS && km_budget > EPS) {
        if (t.break_left_sec > 0) {
            if (stop_at_break) return;
            double use = std::min(seconds, (double)t.break_left_sec);
            t.break_left_sec -= (int)std::ceil(use - 1e-9);
            if (t.break_left_sec < 0) t.break_left_sec = 0;
            seconds -= use; t.sim_sec += use;
            record_speed(t, 0.0);
            continue;
        }
        bool jam = t.jam_left_km > EPS;
        double speed = jam ? JAM_SPEED_KMH : cruise;
        const Stop *brk = next_break(r, t.breaks_done);
        double step = std::min(speed * seconds / 3600.0, km_budget);
        step = std::min(step, total - t.km);
        if (brk) step = std::min(step, brk->km - t.km);
        if (jam) step = std::min(step, t.jam_left_km);
        if (step < 0) step = 0;
        t.km += step;
        if (jam) { t.jam_left_km -= step; if (t.jam_left_km < EPS) t.jam_left_km = 0; }
        double used = step / speed * 3600.0;
        seconds -= used; km_budget -= step; t.sim_sec += used;
        record_speed(t, speed);
        if (brk && std::fabs(t.km - brk->km) < 1e-6) {           /* arrived at a break stop: wait there */
            t.km = brk->km;
            t.break_left_sec = brk->break_min * 60;
            t.breaks_done++;
        }
        if (step <= EPS && t.break_left_sec == 0) break;          /* safety: no progress possible */
    }
}

static void bookings_of_trip_done(App &app, Trip &t) {
    for (size_t i = 0; i < app.bookings.size(); i++) {
        Booking &b = app.bookings[i];
        if (b.trip_id == t.id && app.legal_transition(b.status, BK_COMPLETED)) b.status = BK_COMPLETED;
    }
}

void App::sim_advance(Trip &t, double seconds) {
    const Route &r = routes[(size_t)t.route_id];
    double prev = t.km;
    if (!t.sim_started && seconds > 0) {                          /* first movement starts the trip */
        t.sim_started = 1;
        if (t.status == TRIP_SCHEDULED) { t.status = TRIP_RUNNING; buses[(size_t)t.bus_idx].status = BUS_ON_TRIP; }
    }
    sim_core(t, r, seconds, 1e18, false);
    fire_alerts(t, prev, t.km);
    maybe_alert_traffic(t);
    if (t.km >= r.km_nominal - EPS) {
        t.km = r.km_nominal;
        if (t.status == TRIP_RUNNING) {
            t.status = TRIP_COMPLETED;
            buses[(size_t)t.bus_idx].status = BUS_AVAILABLE;
            bookings_of_trip_done(*this, t);
        }
    }
    flush_outbox();
}

void App::sim_advance_km(Trip &t, double km) {
    const Route &r = routes[(size_t)t.route_id];
    double prev = t.km;
    if (!t.sim_started) {
        t.sim_started = 1;
        if (t.status == TRIP_SCHEDULED) { t.status = TRIP_RUNNING; buses[(size_t)t.bus_idx].status = BUS_ON_TRIP; }
    }
    if (t.break_left_sec > 0) {                                   /* leaving a break: skip the remaining wait */
        t.sim_sec += t.break_left_sec;
        t.break_left_sec = 0;
    }
    sim_core(t, r, 1e18, km, true);                               /* stops at the next break if one is crossed */
    fire_alerts(t, prev, t.km);
    maybe_alert_traffic(t);
    if (t.km >= r.km_nominal - EPS && t.status == TRIP_RUNNING) {
        t.status = TRIP_COMPLETED;
        buses[(size_t)t.bus_idx].status = BUS_AVAILABLE;
        bookings_of_trip_done(*this, t);
    }
    flush_outbox();
}

void App::sim_jam(Trip &t) {
    t.jam_left_km = JAM_LENGTH_KM;
    maybe_alert_traffic(t);
    flush_outbox();
}

void App::sim_reset(Trip &t) {
    t.km = 0; t.jam_left_km = 0; t.break_left_sec = 0; t.breaks_done = 0; t.sim_sec = 0; t.sim_started = 0; t.jam_alerted_level = 0;
    if (t.status == TRIP_RUNNING) { t.status = TRIP_SCHEDULED; buses[(size_t)t.bus_idx].status = BUS_AVAILABLE; }
    ds_ring_evict_before(t.gps, INT64_MAX);
}

/* ---------- ETA ---------- */

TrackInfo App::track_info(const Trip &t) const {
    const Route &r = routes[(size_t)t.route_id];
    TrackInfo info;
    info.sim_now = t.depart_ms + (int64_t)(t.sim_sec * 1000.0);
    info.sched_arrive = t.arrive_ms;
    double left = std::max(0.0, r.km_nominal - t.km);
    info.km_left = left;
    info.progress_pct = 100.0 * t.km / r.km_nominal;
    double jam_km = std::min(t.jam_left_km, left);
    double minutes = jam_km / JAM_SPEED_KMH * 60.0 + (left - jam_km) / r.avg_kmh * 60.0;   /* drive time */
    int seen = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK && seen++ >= t.breaks_done) minutes += r.stops[i].break_min;   /* breaks still ahead */
    minutes += t.break_left_sec / 60.0;                                                     /* break in progress */
    if (!t.sim_started && t.jam_left_km <= EPS && t.breaks_done == 0 && t.break_left_sec == 0 && t.delay_min == 0)
        info.est_arrive = info.sched_arrive;   /* nothing has happened yet: the plan exactly, no rounding drift */
    else
        info.est_arrive = info.sim_now + (int64_t)std::llround(minutes * 60000.0);
    int observed = (int)std::llround((double)(info.est_arrive - info.sched_arrive) / 60000.0);
    info.delay_min = std::max(observed, t.delay_min);          /* reported delay never stacks on top of observed */
    info.est_arrive = std::max(info.est_arrive, info.sched_arrive + (int64_t)t.delay_min * MS_MIN);
    info.on_break = t.break_left_sec > 0;
    info.break_min_left = (t.break_left_sec + 59) / 60;
    info.speed_kmh = info.on_break ? 0.0 : (t.jam_left_km > EPS ? JAM_SPEED_KMH : (double)r.avg_kmh);
    if (ds_ring_count(t.gps) > 0 && !info.on_break) info.speed_kmh = ds_ring_mean(t.gps);
    if (t.km >= r.km_nominal - EPS) info.speed_kmh = 0.0;
    return info;
}

/* Per-stop arrival estimate (spec 6.7). For a stop at distance s while the bus is at k:
   jam part of the remaining distance at JAM_SPEED_KMH + the rest at the route average + the
   minutes of meal halts the bus has not waited at yet (that sit before s) + the rest of a break
   in progress. A reported delay is a floor on top of the planned time. Before departure nothing
   has happened, so the answer is the plan exactly (history fix 11: no seconds-level drift).
   At the destination this reproduces track_info's headline ETA. */
int64_t App::eta_at_stop(const Trip &t, int stop_idx) const {
    const Route &r = routes[(size_t)t.route_id];
    if (stop_idx < 0 || stop_idx >= r.n_stops) return 0;
    int64_t planned = planned_stop_time(t, stop_idx);
    if (!t.sim_started && t.jam_left_km <= EPS && t.breaks_done == 0 && t.break_left_sec == 0 && t.delay_min == 0)
        return planned;                       /* nothing has happened yet: the plan exactly */
    /* a pre-departure jam or reported delay still flows through the normal estimate below */
    double s_km = r.stops[(size_t)stop_idx].km;
    double left = std::max(0.0, s_km - t.km);
    if (left <= EPS) return std::max(planned + (int64_t)t.delay_min * MS_MIN, t.depart_ms + (int64_t)(t.sim_sec * 1000.0));
    double jam = std::min(t.jam_left_km, left);
    double minutes = jam / JAM_SPEED_KMH * 60.0 + (left - jam) / r.avg_kmh * 60.0;
    int seen = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK && r.stops[i].km < s_km - 1e-9 && seen++ >= t.breaks_done) minutes += r.stops[i].break_min;
    minutes += t.break_left_sec / 60.0;                          /* the break in progress lies before every future stop */
    int64_t eta = t.depart_ms + (int64_t)(t.sim_sec * 1000.0) + (int64_t)std::llround(minutes * 60000.0);
    return std::max(eta, planned + (int64_t)t.delay_min * MS_MIN);
}

/* ---------- crossing-based alerts ---------- */

/* Planned clock time at which the bus is at `km` (drive time + breaks already completed before it). */
static int64_t time_at_km(const Route &r, const Trip &t, double km) {
    int64_t drive_ms = (int64_t)std::llround(r.drive_min * (km / r.km_nominal) * 60000.0);
    int break_before = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK && r.stops[i].km < km - 1e-9) break_before += r.stops[i].break_min;
    return t.depart_ms + drive_ms + (int64_t)break_before * MS_MIN;
}

enum AlertKind { A_DEPARTED, A_BOARD_AHEAD, A_BOARD_REACHED, A_BREAK_AHEAD, A_BREAK_STOPPED, A_BREAK_RESUMED,
                 A_TOLL_AHEAD, A_TOLL_CROSSED, A_DROP_AHEAD, A_DROP_REACHED, A_COMPLETED };

struct AlertEvent { double km; int seq; int kind; int stop; };

static int cmp_alert(const void *a, const void *b) {
    const AlertEvent *x = static_cast<const AlertEvent *>(a), *y = static_cast<const AlertEvent *>(b);
    if (x->km < y->km) return -1;
    if (x->km > y->km) return 1;
    return x->seq - y->seq;
}

static bool own_stop_event(int kind) {
    return kind == A_BOARD_AHEAD || kind == A_BOARD_REACHED || kind == A_DROP_AHEAD || kind == A_DROP_REACHED;
}

struct AlertTime {                                      /* stamps alerts with the simulated trip time while in scope */
    App &app;
    AlertTime(App &a, const Trip &t) : app(a) { a.alert_time_ms = t.sim_started ? t.depart_ms + (int64_t)(t.sim_sec * 1000.0) : 0; }
    ~AlertTime() { app.alert_time_ms = 0; }
};

void App::fire_alerts(Trip &t, double prev, double cur) {
    if (cur <= prev) return;
    const Route &r = routes[(size_t)t.route_id];
    Vec<AlertEvent> ev;
    int seq = 0;
    ev.push(AlertEvent{0.5, seq++, A_DEPARTED, 0});
    for (int i = 0; i < r.n_stops; i++) {
        const Stop &s = r.stops[i];
        if (s.type == ST_BOARDING) {
            ev.push(AlertEvent{std::max(0.01, s.km - 5.0), seq++, A_BOARD_AHEAD, i});
            ev.push(AlertEvent{s.km, seq++, A_BOARD_REACHED, i});
        } else if (s.type == ST_BREAK) {
            ev.push(AlertEvent{s.km - 20.0, seq++, A_BREAK_AHEAD, i});
            ev.push(AlertEvent{s.km, seq++, A_BREAK_STOPPED, i});
            ev.push(AlertEvent{s.km + 0.001, seq++, A_BREAK_RESUMED, i});
        } else if (s.type == ST_TOLL) {
            ev.push(AlertEvent{s.km - r.avg_kmh * 25.0 / 60.0, seq++, A_TOLL_AHEAD, i});   /* about 25 minutes before */
            ev.push(AlertEvent{s.km, seq++, A_TOLL_CROSSED, i});
        } else if (s.type == ST_DROPPING) {
            ev.push(AlertEvent{s.km - 20.0, seq++, A_DROP_AHEAD, i});
            ev.push(AlertEvent{s.km - 0.5, seq++, A_DROP_REACHED, i});
        }
    }
    ev.push(AlertEvent{(double)r.km_nominal, seq++, A_COMPLETED, 0});
    ds_merge_sort(ev.data(), ev.size(), sizeof(AlertEvent), cmp_alert);          /* events fire in km order */
    for (size_t e = 0; e < ev.size(); e++) {
        const AlertEvent &a = ev[e];
        if (!(prev < a.km && a.km <= cur + EPS)) continue;                         /* crossing test */
        alert_time_ms = t.sim_started ? time_at_km(r, t, std::max(0.0, std::min(a.km, (double)r.km_nominal))) : 0;   /* stamp with the time the bus reached this km */
        const Stop &s = r.stops[a.stop];
        std::string place = s.name, title, body;
        int kind = NK_JOURNEY;
        switch (a.kind) {
            case A_DEPARTED: title = "Bus departed"; body = "Your bus left " + std::string(cities[(size_t)r.from_city].name) + "."; break;
            case A_BOARD_AHEAD: title = "Boarding point ahead"; body = "Approaching " + place + "."; break;
            case A_BOARD_REACHED: title = "Boarding point reached"; body = "The bus is at " + place + "."; break;
            case A_BREAK_AHEAD: kind = NK_BREAKS; title = "Break ahead"; body = "Break stop " + place + " in about 20 km (" + std::to_string(s.break_min) + " min)."; break;
            case A_BREAK_STOPPED: kind = NK_BREAKS; title = "Bus stopped"; body = "Bus stopped at " + place + " - about " + std::to_string(s.break_min) + " min, expected departure " + fmt_hm(planned_stop_time(t, a.stop) + (int64_t)s.break_min * MS_MIN) + "."; break;
            case A_BREAK_RESUMED: kind = NK_BREAKS; title = "Bus resumed"; body = "The bus left " + place + "."; break;
            case A_TOLL_AHEAD: kind = NK_TOLLS; title = "Toll ahead"; body = place + " in about 25 minutes (the bus does not stop)."; break;
            case A_TOLL_CROSSED: kind = NK_TOLLS; title = "Toll crossed"; body = "Crossed " + place + "."; break;
            case A_DROP_AHEAD: kind = NK_DROPOFF; title = "Your drop-off is ahead"; body = place + " in about 20 km."; break;
            case A_DROP_REACHED: kind = NK_DROPOFF; title = "Drop-off reached"; body = "Arriving at " + place + "."; break;
            default: title = "Trip completed"; body = "The trip has reached " + std::string(cities[(size_t)r.to_city].name) + "."; break;
        }
        for (size_t bi = 0; bi < bookings.size(); bi++) {
            const Booking &b = bookings[bi];
            if (b.trip_id != t.id || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) continue;
            if (own_stop_event(a.kind)) {
                int own = (a.kind == A_BOARD_AHEAD || a.kind == A_BOARD_REACHED) ? b.boarding_stop : b.dropping_stop;
                if (own != a.stop) continue;                                       /* only the passenger's own stop */
            }
            std::string key = "u" + std::to_string(b.user_id) + ":t" + std::to_string(t.id) + ":s" + std::to_string(a.stop) + ":e" + std::to_string(a.kind);
            notify(b.user_id, kind, "[Demo] " + title, body, key);
        }
    }
    alert_time_ms = 0;
}

/* Traffic delay alert: first time the remaining jam delay reaches 10 min, then every further 10. */
void App::maybe_alert_traffic(Trip &t) {
    AlertTime stamp(*this, t);
    const Route &r = routes[(size_t)t.route_id];
    if (t.jam_left_km <= EPS) { t.jam_alerted_level = 0; return; }
    double extra = t.jam_left_km * (60.0 / JAM_SPEED_KMH - 60.0 / r.avg_kmh);
    int level = (int)(extra / 10.0);
    if (level <= t.jam_alerted_level) return;
    t.jam_alerted_level = level;
    for (size_t bi = 0; bi < bookings.size(); bi++) {
        const Booking &b = bookings[bi];
        if (b.trip_id != t.id || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) continue;
        notify(b.user_id, NK_TRAFFIC, "[Demo] Traffic delay (DEMO TRAFFIC)", "Heavy traffic ahead: about " + std::to_string((int)extra) + " extra minutes.",
               "u" + std::to_string(b.user_id) + ":t" + std::to_string(t.id) + ":jam:" + std::to_string(level));
    }
}

Result<Unit> App::report_delay(int company_id, int trip_id, const std::string &reason, int minutes, const std::string &note) {
    Trip *t = trip_by_id(trip_id);
    if (!t || buses[(size_t)t->bus_idx].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");   /* other vendors' trips look absent */
    static const char *REASONS[5] = {"TRAFFIC", "WEATHER", "BREAKDOWN", "ROADWORK", "OTHER"};
    bool known = false;
    for (const char *s : REASONS) known |= reason == s;
    if (!known) return Result<Unit>::fail(Err::VALIDATION, "Reason must be TRAFFIC, WEATHER, BREAKDOWN, ROADWORK or OTHER");
    if (minutes < 1 || minutes > 600) return Result<Unit>::fail(Err::VALIDATION, "Delay must be 1 to 600 minutes");
    if (note.size() > 120) return Result<Unit>::fail(Err::VALIDATION, "Note must be at most 120 characters");
    apply_delay(*t, reason, minutes, note, "vendor");
    return okay("Delay reported");
}

/* Records a delay (replacing any earlier one), tells the booked passengers, and writes the delay log. */
void App::apply_delay(Trip &t, const std::string &reason, int minutes, const std::string &note, const char *source) {
    t.delay_min = minutes;                                      /* a new report replaces the old value; it never stacks */
    std::snprintf(t.delay_reason, sizeof t.delay_reason, "%s", reason.c_str());
    std::snprintf(t.delay_note, sizeof t.delay_note, "%s", note.c_str());
    DelayLog d;
    std::memset(&d, 0, sizeof d);
    d.id = (int)delay_log.size() + 1; d.trip_id = t.id; d.at_ms = clock.now(); d.minutes = minutes;
    std::snprintf(d.cause, sizeof d.cause, "%s", reason.c_str());
    std::snprintf(d.source, sizeof d.source, "%s", source);
    delay_log.push(d);
    TrackInfo ti = track_info(t);
    std::string body = reason + ": about " + std::to_string(minutes) + " min. New ETA " + fmt_hm_plus(ti.est_arrive, t.depart_ms) + ".";
    if (!note.empty()) body += " Driver note: " + note + ".";      /* spec 6.6: the note must reach the passenger */
    for (size_t bi = 0; bi < bookings.size(); bi++) {
        const Booking &b = bookings[bi];
        if (b.trip_id != t.id || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) continue;
        /* a changed note (or minutes) is a new message: the dedupe key carries both */
        notify(b.user_id, NK_TRAFFIC, "Delay reported", body,
               "u" + std::to_string(b.user_id) + ":t" + std::to_string(t.id) + ":delay:" + std::to_string(minutes) + ":" + note);
    }
    flush_outbox();
}

}  // namespace sr
