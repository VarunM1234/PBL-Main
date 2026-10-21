/*
 * PURPOSE: Fare quote, booking creation, status machine, cancellation and refunds, pre-booking rules.
 * WHY THIS NAME: "booking" = a passenger's reservation lifecycle.
 * CONNECTS TO: money.hpp (pure formulas), seats.cpp (holds), coupons.cpp, payment.cpp, idx_pnr hash (row 2).
 * COMPLEXITY: quote O(seats); lookup by PNR O(1) average.
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>

namespace sr {

static constexpr int64_t PREBOOK_MIN_MS = 72 * MS_HOUR;
static constexpr int64_t PREBOOK_MAX_MS = 2160 * MS_HOUR;           /* 90 days */
static constexpr int64_t BALANCE_BEFORE_DEPARTURE_MS = 48 * MS_HOUR;

bool App::legal_transition(int from, int to) const {
    switch (from) {
        case BK_PENDING_PAYMENT: return to == BK_PREBOOKED || to == BK_CONFIRMED || to == BK_EXPIRED || to == BK_CANCELLED;
        case BK_PREBOOKED: return to == BK_CONFIRMED || to == BK_CANCELLED || to == BK_EXPIRED;
        case BK_CONFIRMED: return to == BK_CANCELLED || to == BK_COMPLETED;
        default: return false;                                     /* CANCELLED, EXPIRED, COMPLETED are final */
    }
}

Booking *App::booking_by_id(int id) const {
    return (id >= 1 && (size_t)id <= bookings.size()) ? bookings.at((size_t)id - 1) : nullptr;
}

Booking *App::booking_by_pnr(const std::string &pnr, int user_id) const {
    std::string key;
    for (char c : pnr) key += (char)std::toupper((unsigned char)c);
    int *i = idx_pnr.get(key);
    if (!i) return nullptr;
    Booking *b = bookings.at((size_t)*i);
    return (b && b->user_id == user_id) ? b : nullptr;             /* other users' PNRs look like "not found" */
}

void App::make_pnr(char out[12]) {
    static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* no 0/O/1/I confusion */
    for (;;) {
        out[0] = 'S'; out[1] = 'R';
        for (int i = 2; i < 10; i++) out[i] = ALPHABET[next_random() % 32];
        out[10] = '\0';
        if (!idx_pnr.has(out)) return;                             /* retry on the (rare) collision */
    }
}

/* ---------- quote ---------- */

Result<Quote> App::quote(int trip_id, const char *const *labels, int n, const char *coupon, bool insurance, int user_id) const {
    Trip *t = trip_by_id(trip_id);
    if (!t) return Result<Quote>::fail(Err::NOT_FOUND, "Trip not found");
    if (n < 1 || n > MAX_PAX) return Result<Quote>::fail(Err::VALIDATION, "Choose 1 to 6 seats");
    const BusType &bt = types[buses[t->bus_idx].type_idx];
    const Route &r = routes[(size_t)t->route_id];
    Quote q;
    q.n_seats = n;
    for (int i = 0; i < n; i++) {
        int idx = seat_index(bt, labels[i]);
        if (idx < 0) return Result<Quote>::fail(Err::VALIDATION, std::string("No such seat: ") + labels[i]);
        bool lower = bt.layout == LAYOUT_SLEEPER && labels[i][0] == 'L';
        q.seat_price[i] = seat_fare_paise(r.km_nominal, bt.rate_paise_per_km, lower);
        q.base += q.seat_price[i];
    }
    if (coupon && *coupon) {
        Result<int64_t> d = coupon_discount(coupon, q.base, bt, user_id);
        if (!d.ok) return Result<Quote>::fail(d.code, d.message);
        q.discount = d.value;
    }
    q.tax = tax_paise(q.base - q.discount);                        /* tax is applied after the discount, once */
    q.insurance = insurance ? n * INSURANCE_PER_PASSENGER : 0;
    q.total = q.base - q.discount + q.tax + q.insurance;
    int64_t left = t->depart_ms - clock.now();
    q.prebook_eligible = left >= PREBOOK_MIN_MS && left <= PREBOOK_MAX_MS;
    q.deposit = deposit_paise(q.total);
    q.balance = q.total - q.deposit;
    q.balance_due_ms = t->depart_ms - BALANCE_BEFORE_DEPARTURE_MS;
    return Result<Quote>::success(q);
}

/* ---------- create ---------- */

static Result<int> bad(const std::string &m) { return Result<int>::fail(Err::VALIDATION, m); }

Result<int> App::create_booking(const BookingRequest &req) {
    Trip *t = trip_by_id(req.trip_id);
    if (!user_by_id(req.user_id)) return Result<int>::fail(Err::UNAUTHENTICATED, "Please log in first");
    if (!t) return Result<int>::fail(Err::NOT_FOUND, "Trip not found");
    if (!sales_open(*t)) return Result<int>::fail(Err::BAD_STATE, "Sales for this trip are closed");
    if (req.n_pax < 1 || req.n_pax > MAX_PAX) return bad("Choose 1 to 6 seats");
    const BusType &bt = types[buses[t->bus_idx].type_idx];
    const Route &r = routes[(size_t)t->route_id];
    const char *labels[MAX_PAX];
    for (int i = 0; i < req.n_pax; i++) {
        const Passenger &p = req.pax[i];
        labels[i] = p.seat;
        int idx = seat_index(bt, p.seat);
        if (idx < 0) return bad(std::string("No such seat: ") + p.seat);
        bool mine = false;                                          /* the seat must be one of THIS user's live holds */
        for (size_t h = 0; h < holds.size(); h++)
            if (holds[h].active && holds[h].trip_id == t->id && holds[h].user_id == req.user_id && holds[h].seat == idx) mine = true;
        if (!mine) return Result<int>::fail(Err::HOLD_EXPIRED, std::string("Seat ") + p.seat + " is not held for you (hold expired?). Select seats again.");
        if (p.name[0] == '\0') return bad("Enter the passenger name for seat " + std::string(p.seat));
        if (!display_text_ok(p.name)) return bad("Passenger name for seat " + std::string(p.seat) + " cannot contain < > \" & \\ or a backtick");
        if (p.age < 1 || p.age > 120) return bad("Passenger age must be 1 to 120");
        if (p.gender != 'M' && p.gender != 'F' && p.gender != 'O') return bad("Gender must be M, F or O");
    }
    if (req.boarding_stop < 0 || req.boarding_stop >= r.n_stops || r.stops[req.boarding_stop].type != ST_BOARDING) return bad("Choose a valid boarding point");
    if (req.dropping_stop < 0 || req.dropping_stop >= r.n_stops || r.stops[req.dropping_stop].type != ST_DROPPING) return bad("Choose a valid dropping point");
    if (req.contact[0] == '\0') return bad("Enter a contact phone or email");
    if (!display_text_ok(req.contact)) return bad("Contact cannot contain < > \" & \\ or a backtick");
    Result<Quote> q = quote(t->id, labels, req.n_pax, req.coupon, req.insurance, req.user_id);
    if (!q.ok) return Result<int>::fail(q.code, q.message);
    if (req.mode == MODE_DEPOSIT && !q.value.prebook_eligible)
        return Result<int>::fail(Err::NOT_ELIGIBLE, "Pre-booking is offered only 3 to 90 days before departure");
    Booking b;
    std::memset(&b, 0, sizeof b);
    b.id = (int)bookings.size() + 1;
    make_pnr(b.pnr);
    b.user_id = req.user_id; b.trip_id = t->id; b.status = BK_PENDING_PAYMENT; b.pay_status = PAY_UNPAID; b.mode = req.mode;
    b.base = q.value.base; b.discount = q.value.discount; b.tax = q.value.tax; b.insurance = q.value.insurance; b.total = q.value.total;
    std::snprintf(b.coupon, sizeof b.coupon, "%s", req.coupon);
    b.boarding_stop = req.boarding_stop; b.dropping_stop = req.dropping_stop; b.n_pax = req.n_pax;
    for (int i = 0; i < req.n_pax; i++) { b.pax[i] = req.pax[i]; b.pax[i].price = q.value.seat_price[i]; }
    std::snprintf(b.contact, sizeof b.contact, "%s", req.contact);
    b.balance_due_ms = req.mode == MODE_DEPOSIT ? q.value.balance_due_ms : 0;
    b.created_ms = clock.now();
    b.demo_payment = 1;                                             /* "DEMO PAYMENT - no money was charged" */
    bookings.push(b);
    idx_pnr.put(b.pnr, b.id - 1);
    return Result<int>::success(b.id);
}

/* ---------- events for pre-bookings ---------- */

void App::schedule_booking_events(const Booking &b) {
    enum { EV_BALANCE_DEADLINE = 2, EV_REMINDER = 3 };
    events.push(b.balance_due_ms, ((uint64_t)EV_BALANCE_DEADLINE << 56) | (uint64_t)b.id);
    static const int64_t BEFORE[3] = {7 * MS_DAY, 24 * MS_HOUR, 6 * MS_HOUR};
    for (int k = 0; k < 3; k++) {
        int64_t at = b.balance_due_ms - BEFORE[k];
        if (at > clock.now()) events.push(at, ((uint64_t)EV_REMINDER << 56) | ((uint64_t)b.id << 4) | (uint64_t)k);
    }
}

void App::drop_booking_events(int booking_id) {
    events.remove(((uint64_t)2 << 56) | (uint64_t)booking_id);
    for (int k = 0; k < 3; k++) events.remove(((uint64_t)3 << 56) | ((uint64_t)booking_id << 4) | (uint64_t)k);
}

void App::record_revenue(int company_id, int64_t delta) {
    int64_t day = day_number(clock.now()) - base_day;
    if (day < 0) day = 0;
    if (day > 119) day = 119;
    if (company_id >= 0 && company_id < 32) ds_fenwick_add(revenue_co[company_id], (size_t)day, delta);
}

int64_t App::revenue_range(int company_id, int from, int to) const {
    if (company_id < 0 || company_id >= 32 || from < 0 || to < from) return 0;
    return ds_fenwick_range(revenue_co[company_id], (size_t)from, (size_t)to);
}

/* ---------- cancellation ---------- */

RefundPreview App::cancel_preview(const Booking &b) const {
    RefundPreview p;
    const Trip *t = trip_by_id(b.trip_id);
    int64_t left = t ? t->depart_ms - clock.now() : 0;
    p.hours_left = left / MS_HOUR;
    p.pct = left > 0 ? refund_percent(left) : 0;
    p.refundable = b.paid < b.total - b.insurance ? b.paid : b.total - b.insurance;
    p.refund = refund_paise(b.paid, b.total, b.insurance, b.refunded, p.pct);
    return p;
}

void App::release_holds_of_booking(const Booking &b) {
    release_user_holds(b.trip_id, b.user_id);
}

void App::free_seats_after_cancel(Booking &b) {
    Trip *t = trip_by_id(b.trip_id);
    if (!t) return;
    const BusType &bt = types[buses[t->bus_idx].type_idx];
    for (int i = 0; i < b.n_pax; i++) {
        int idx = seat_index(bt, b.pax[i].seat);
        if (idx >= 0) t->booked.clear((size_t)idx);
    }
}

/* Shared tail of cancel and expire: refund, free seats, drop timers, tell the waitlist. */
void App::finalize_end_state(Booking &b, int new_status) {
    RefundPreview p = cancel_preview(b);
    const Trip *t = trip_by_id(b.trip_id);
    b.refunded += p.refund;
    if (p.refund > 0 && t) record_revenue(buses[t->bus_idx].company_id, -p.refund);
    b.status = new_status;
    if (b.paid > 0) b.pay_status = (b.refunded >= b.paid) ? PAY_REFUNDED : PAY_PARTIALLY_REFUNDED;
    release_holds_of_booking(b);
    free_seats_after_cancel(b);
    drop_booking_events(b.id);
    on_seats_freed(b.trip_id);
}

Result<int64_t> App::cancel_booking(int booking_id, int user_id) {
    Booking *b = booking_by_id(booking_id);
    if (!b || b->user_id != user_id) return Result<int64_t>::fail(Err::NOT_FOUND, "Booking not found");
    if (!legal_transition(b->status, BK_CANCELLED)) return Result<int64_t>::fail(Err::BAD_STATE, "This booking can no longer be cancelled");
    const Trip *t = trip_by_id(b->trip_id);
    if (!t || clock.now() >= t->depart_ms) return Result<int64_t>::fail(Err::BAD_STATE, "The bus has already departed");
    int64_t before = b->refunded;
    finalize_end_state(*b, BK_CANCELLED);
    int64_t refund = b->refunded - before;
    notify(user_id, NK_BOOKING, "Booking cancelled", std::string(b->pnr) + " cancelled. Refund: " + fmt_rs(refund) + " (DEMO - no real money moved).", "");
    flush_outbox();
    return Result<int64_t>::success(refund);
}

}  // namespace sr
