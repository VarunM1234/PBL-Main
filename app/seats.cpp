/*
 * PURPOSE: Trips, seat layouts, seat bitsets, 10-minute holds (heap-driven expiry), waitlist queue.
 * WHY THIS NAME: "seats" = everything about which seat is free, held, or sold.
 * CONNECTS TO: core/ds_bitset (rows 3), ds_heap (row 4), ds_queue (row 5), ds_hash (trip index).
 * COMPLEXITY: seat test/set O(1); seats left O(n/64); hold expiry O(log n) per event.
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>

namespace sr {

static constexpr int64_t HOLD_MS = 10 * MS_MIN;
static constexpr int64_t SALES_CLOSE_MS = 15 * MS_MIN;
enum EventKind { EV_HOLD_EXPIRY = 1, EV_BALANCE_DEADLINE = 2, EV_REMINDER = 3 };

static uint64_t ev_id(int kind, uint64_t payload) { return ((uint64_t)kind << 56) | payload; }

/* ---------- layouts ---------- */

static int columns_of(const BusType &bt) { return bt.layout == LAYOUT_2P2 ? 4 : 3; }

void App::seat_label(const BusType &bt, int i, char out[6]) const {
    if (bt.layout == LAYOUT_SLEEPER) {
        std::snprintf(out, 6, "%c%d", i < 15 ? 'L' : 'U', (i % 15) + 1);
        return;
    }
    int cols = columns_of(bt);
    int row = (i / cols) % 100 + 1;                          /* rows are small; clamp keeps the label short */
    std::snprintf(out, 6, "%d%c", row, "ABCD"[i % cols]);
}

int App::seat_index(const BusType &bt, const char *label) const {
    char buf[6];
    for (int i = 0; i < bt.capacity; i++) {
        seat_label(bt, i, buf);
        if (std::strcmp(buf, label) == 0) return i;
    }
    return -1;
}

int App::seat_row(const BusType &bt, int i) const {
    if (bt.layout == LAYOUT_SLEEPER) return (i / 15) * 5 + (i % 15) / 3;   /* decks never share a row */
    return i / columns_of(bt);
}

/* ---------- trips ---------- */

Trip *App::trip_by_id(int id) const {
    return (id >= 1 && (size_t)id <= trips.size()) ? *trips.at((size_t)id - 1) : nullptr;
}

size_t App::seat_count(const Trip &t) const { return (size_t)types[buses[t.bus_idx].type_idx].capacity; }

Trip *App::get_or_create_trip(int schedule_idx, int date_key) {
    if (schedule_idx < 0 || (size_t)schedule_idx >= schedules.size()) return nullptr;
    std::string key = std::to_string(schedule_idx) + ":" + std::to_string(date_key);
    if (int *id = idx_trip.get(key)) return trip_by_id(*id);
    const Schedule &s = schedules[(size_t)schedule_idx];
    const Route &r = routes[(size_t)s.route_id];
    int cap = types[buses[s.bus_idx].type_idx].capacity;
    Trip *t = new Trip(cap);
    t->id = (int)trips.size() + 1;
    t->schedule_idx = schedule_idx; t->route_id = s.route_id; t->bus_idx = s.bus_idx; t->date_key = date_key;
    t->depart_ms = from_ist(date_key / 10000, (date_key / 100) % 100, date_key % 100, 0, 0) + (int64_t)s.depart_min_of_day * MS_MIN;
    t->arrive_ms = t->depart_ms + (int64_t)r.duration_min * MS_MIN;
    if (!trips.push(t)) { delete t; return nullptr; }
    idx_trip.put(key, t->id);
    return t;
}

bool App::sales_open(const Trip &t) const {
    return t.status == TRIP_SCHEDULED && clock.now() <= t.depart_ms - SALES_CLOSE_MS;
}

int64_t App::trip_hours_left(const Trip &t) const { return (t.depart_ms - clock.now()) / MS_HOUR; }

/* ---------- seat map ---------- */

std::vector<SeatCell> App::seat_map(const Trip &t, int viewer) const {
    std::vector<SeatCell> cells;
    const BusType &bt = types[buses[t.bus_idx].type_idx];
    for (int i = 0; i < bt.capacity; i++) {
        SeatCell c;
        std::memset(&c, 0, sizeof c);
        seat_label(bt, i, c.label);
        c.index = i;
        if (t.booked.test((size_t)i)) c.state = 1;
        else if (t.held.test((size_t)i)) {
            c.state = 2;
            for (size_t h = holds.size(); h-- > 0;)
                if (holds[h].active && holds[h].trip_id == t.id && holds[h].seat == i) { c.state = holds[h].user_id == viewer ? 3 : 2; break; }
        }
        cells.push_back(c);
    }
    return cells;
}

long App::find_adjacent(const Trip &t, int n) const {
    const BusType &bt = types[buses[t.bus_idx].type_idx];
    if (n < 1) return -1;
    Bits unavailable(MAX_SEATS);                               /* booked OR held */
    for (int i = 0; i < bt.capacity; i++) if (t.booked.test((size_t)i) || t.held.test((size_t)i)) unavailable.set((size_t)i);
    int row_start = 0;
    for (int i = 1; i <= bt.capacity; i++) {
        if (i == bt.capacity || seat_row(bt, i) != seat_row(bt, row_start)) {   /* row [row_start, i) ends */
            long at = unavailable.find_clear_run((size_t)row_start, (size_t)i, (size_t)n);
            if (at >= 0) return at;
            row_start = i;
        }
    }
    return -1;
}

/* ---------- holds ---------- */

int App::active_holds(int trip_id, int user_id) const {
    int c = 0;
    for (size_t i = 0; i < holds.size(); i++) if (holds[i].active && holds[i].trip_id == trip_id && holds[i].user_id == user_id) c++;
    return c;
}

int64_t App::hold_expiry(int trip_id, int user_id) const {
    int64_t best = 0;
    for (size_t i = 0; i < holds.size(); i++)
        if (holds[i].active && holds[i].trip_id == trip_id && holds[i].user_id == user_id && (best == 0 || holds[i].expires_ms < best)) best = holds[i].expires_ms;
    return best;
}

void App::release_user_holds(int trip_id, int user_id) {
    Trip *t = trip_by_id(trip_id);
    for (size_t i = 0; i < holds.size(); i++) {
        Hold &h = holds[i];
        if (!h.active || h.trip_id != trip_id || h.user_id != user_id) continue;
        h.active = 0;
        if (t) t->held.clear((size_t)h.seat);
        events.remove(ev_id(EV_HOLD_EXPIRY, (uint64_t)h.id));
    }
}

static Hold make_hold(int id, int trip, int seat, int user, int64_t expires) { return Hold{id, trip, seat, user, expires, 1}; }

Result<Unit> App::hold_seats(int user_id, int trip_id, const char *const *labels, int n) {
    Trip *t = trip_by_id(trip_id);
    if (!user_by_id(user_id)) return Result<Unit>::fail(Err::UNAUTHENTICATED, "Please log in first");
    if (!t) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    if (!sales_open(*t)) return Result<Unit>::fail(Err::BAD_STATE, "Sales for this trip are closed");
    if (n < 1 || n > MAX_PAX) return Result<Unit>::fail(Err::VALIDATION, "Choose 1 to 6 seats");
    const BusType &bt = types[buses[t->bus_idx].type_idx];
    int idx[MAX_PAX];
    for (int i = 0; i < n; i++) {
        idx[i] = seat_index(bt, labels[i]);
        if (idx[i] < 0) return Result<Unit>::fail(Err::VALIDATION, std::string("No such seat: ") + labels[i]);
        for (int j = 0; j < i; j++) if (idx[j] == idx[i]) return Result<Unit>::fail(Err::VALIDATION, "Duplicate seat in selection");
    }
    release_user_holds(trip_id, user_id);                      /* re-holding replaces earlier holds */
    for (int i = 0; i < n; i++)
        if (t->booked.test((size_t)idx[i]) || t->held.test((size_t)idx[i]))
            return Result<Unit>::fail(Err::SEAT_TAKEN, std::string("Seat ") + labels[i] + " was just taken. Pick another.");
    int64_t expires = clock.now() + HOLD_MS;
    for (int i = 0; i < n; i++) {
        int hid = (int)holds.size() + 1;
        holds.push(make_hold(hid, trip_id, idx[i], user_id, expires));
        t->held.set((size_t)idx[i]);
        events.push(expires, ev_id(EV_HOLD_EXPIRY, (uint64_t)hid));
    }
    return okay("Seats held for 10 minutes");
}

/* ---------- waitlist ---------- */

Result<Unit> App::join_waitlist(int user_id, int trip_id, int seats) {
    Trip *t = trip_by_id(trip_id);
    if (!t) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    size_t cap = seat_count(*t);
    if (t->booked.count() + t->held.count() < cap) return Result<Unit>::fail(Err::BAD_STATE, "Seats are still available; no waitlist needed");
    if (seats < 1 || seats > MAX_PAX) return Result<Unit>::fail(Err::VALIDATION, "Waitlist for 1 to 6 seats");
    t->waitlist.push(WaitEntry{user_id, seats});
    return okay("You are on the waitlist (position " + std::to_string(t->waitlist.size()) + ")");
}

/* When seats become free, offer them to the head of the waitlist with a 10-minute claim window. */
void App::on_seats_freed(int trip_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t) return;
    const BusType &bt = types[buses[t->bus_idx].type_idx];
    WaitEntry w;
    for (;;) {
        int free_seat[MAX_SEATS], nfree = 0;
        for (int i = 0; i < bt.capacity; i++) if (!t->booked.test((size_t)i) && !t->held.test((size_t)i)) free_seat[nfree++] = i;
        if (nfree == 0 || !t->waitlist.pop(&w)) return;
        int take = w.seats_wanted < nfree ? w.seats_wanted : nfree;
        int64_t expires = clock.now() + HOLD_MS;
        for (int i = 0; i < take; i++) {
            int hid = (int)holds.size() + 1;
            holds.push(make_hold(hid, trip_id, free_seat[i], w.user_id, expires));
            t->held.set((size_t)free_seat[i]);
            events.push(expires, ev_id(EV_HOLD_EXPIRY, (uint64_t)hid));
        }
        notify(w.user_id, NK_BOOKING, "Waitlist: seats available",
               std::to_string(take) + " seat(s) held for you for 10 minutes on " + route_name(routes[(size_t)t->route_id]) +
               " (" + fmt_datetime(t->depart_ms) + ")", "");
    }
}

/* ---------- time-driven events ---------- */

void App::expire_booking_if_unheld(int trip_id, int user_id) {
    if (active_holds(trip_id, user_id) > 0) return;
    for (size_t i = 0; i < bookings.size(); i++) {
        Booking &b = bookings[i];
        if (b.trip_id == trip_id && b.user_id == user_id && b.status == BK_PENDING_PAYMENT) {
            b.status = BK_EXPIRED;
            notify(user_id, NK_BOOKING, "Seat hold expired", "Booking " + std::string(b.pnr) + " expired because payment was not completed in time.", "");
        }
    }
}

void App::expire_hold(int hold_id) {
    Hold &h = holds[(size_t)hold_id - 1];
    if (!h.active) return;
    h.active = 0;
    Trip *t = trip_by_id(h.trip_id);
    if (t) t->held.clear((size_t)h.seat);
    notify(h.user_id, NK_REMINDERS, "Hold expired", "Your 10-minute seat hold ended.", "hold:" + std::to_string(hold_id));
    expire_booking_if_unheld(h.trip_id, h.user_id);
    on_seats_freed(h.trip_id);
}

void App::process_events() {
    int64_t prio;
    uint64_t id;
    while (events.peek(&prio, &id) && prio <= clock.now()) {
        events.pop(&prio, &id);
        int kind = (int)(id >> 56);
        uint64_t payload = id & 0x00FFFFFFFFFFFFFFULL;
        if (kind == EV_HOLD_EXPIRY) expire_hold((int)payload);
        else if (kind == EV_BALANCE_DEADLINE) {
            Booking *b = booking_by_id((int)payload);
            if (b && b->status == BK_PREBOOKED) {
                finalize_end_state(*b, BK_EXPIRED);
                notify(b->user_id, NK_BOOKING, "Pre-booking expired", "Balance for " + std::string(b->pnr) + " was not paid by the deadline. Seats released; refund per policy applied.", "");
            }
        } else if (kind == EV_REMINDER) {
            Booking *b = booking_by_id((int)(payload >> 4));
            static const char *WHEN[3] = {"7 days", "24 hours", "6 hours"};
            int k = (int)(payload & 15);
            if (b && b->status == BK_PREBOOKED && k < 3)
                notify(b->user_id, NK_REMINDERS, "Balance reminder",
                       "Pay the balance of " + fmt_rs(b->total - b->paid) + " for " + b->pnr + " (due " + fmt_datetime(b->balance_due_ms) + ", " + WHEN[k] + " reminder).",
                       "rem:" + std::to_string(b->id) + ":" + std::to_string(k));
        }
    }
    flush_outbox();
}

void App::advance_clock(int64_t ms) {
    clock.advance(ms);
    process_events();
}

/* The web server calls this once per loop tick with the virtual time that corresponds to real time
   right now (the virtual time at server start + steady-clock seconds since - a laptop waking from
   sleep does not jump the clock forward). Without it the server's virtual time froze at startup,
   hold-expiry events never fired, and a seat held by a visitor who closed the tab stayed blocked
   forever. Like advance_clock it only moves forward, so the "+1 hour" demo buttons still stick. */
void App::sync_wall_clock(int64_t wall_ms) {
    if (wall_ms > clock.now()) advance_clock(wall_ms - clock.now());
}

}  // namespace sr
