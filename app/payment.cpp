/*
 * PURPOSE: SIMULATED payment gateway: success / declined / timeout, with idempotency keys.
 * WHY THIS NAME: "payment" = taking money for a booking (here: pretend money, DEMO only).
 * CONNECTS TO: booking.cpp (status machine), seats.cpp (holds), idx_pay hash (row 2, idempotency).
 * COMPLEXITY: O(seats + holds).
 * NOTE: no card numbers are ever asked for or stored.
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>

namespace sr {

static constexpr int64_t PAY_EXTEND_MS = 5 * MS_MIN;

/* Holds are converted to sold seats on the first successful payment. */
static void convert_holds_to_sold(App &app, Booking &b) {
    Trip *t = app.trip_by_id(b.trip_id);
    const BusType &bt = app.types[app.buses[t->bus_idx].type_idx];
    app.release_user_holds(b.trip_id, b.user_id);
    for (int i = 0; i < b.n_pax; i++) {
        int idx = app.seat_index(bt, b.pax[i].seat);
        if (idx >= 0) t->booked.set((size_t)idx);
    }
}

/* Give the user at least 5 more minutes once payment starts (never shortens a hold). */
static void extend_holds(App &app, int trip_id, int user_id) {
    int64_t target = app.clock.now() + PAY_EXTEND_MS;
    for (size_t i = 0; i < app.holds.size(); i++) {
        Hold &h = app.holds[i];
        if (!h.active || h.trip_id != trip_id || h.user_id != user_id || h.expires_ms >= target) continue;
        h.expires_ms = target;
        app.events.update(((uint64_t)1 << 56) | (uint64_t)h.id, target);
    }
}

Result<Unit> App::pay_booking(int booking_id, int user_id, int kind, int outcome) {
    Booking *b = booking_by_id(booking_id);
    if (!b || b->user_id != user_id) return Result<Unit>::fail(Err::NOT_FOUND, "Booking not found");
    std::string key = "b" + std::to_string(b->id) + ":k" + std::to_string(kind) + ":a" + std::to_string(b->attempt_no);
    if (idx_pay.has(key)) return Result<Unit>::fail(Err::CONFLICT, "This payment was already processed (duplicate submit ignored)");
    Trip *t = trip_by_id(b->trip_id);
    if (!t) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    int64_t amount = 0;
    if (b->status == BK_PENDING_PAYMENT) {
        if ((b->mode == MODE_FULL && kind != PAYK_FULL) || (b->mode == MODE_DEPOSIT && kind != PAYK_DEPOSIT))
            return Result<Unit>::fail(Err::VALIDATION, "Wrong payment type for this booking");
        if (!sales_open(*t)) return Result<Unit>::fail(Err::BAD_STATE, "Sales for this trip are closed");
        if (active_holds(b->trip_id, b->user_id) < b->n_pax) return Result<Unit>::fail(Err::HOLD_EXPIRED, "Your seat hold expired. Please select seats again.");
        amount = kind == PAYK_FULL ? b->total : deposit_paise(b->total);
    } else if (b->status == BK_PREBOOKED && kind == PAYK_BALANCE) {
        if (clock.now() >= b->balance_due_ms) return Result<Unit>::fail(Err::BAD_STATE, "The balance deadline has passed");
        amount = b->total - b->paid;
    } else {
        return Result<Unit>::fail(Err::BAD_STATE, "This booking cannot accept a payment now");
    }
    if (b->status == BK_PENDING_PAYMENT) extend_holds(*this, b->trip_id, b->user_id);
    Payment p;
    std::memset(&p, 0, sizeof p);
    p.id = (int)payments.size() + 1; p.booking_id = b->id; p.kind = kind; p.amount = amount; p.outcome = outcome; p.at_ms = clock.now();
    std::snprintf(p.key, sizeof p.key, "%s", key.c_str());
    payments.push(p);
    idx_pay.put(key, p.id);
    if (outcome != OUT_SUCCESS) {
        b->attempt_no++;                                           /* a retry gets a fresh idempotency key */
        return Result<Unit>::fail(Err::PAYMENT_DECLINED, outcome == OUT_DECLINED ? "Payment declined by the demo gateway. You can retry." : "Payment timed out (demo). You can retry.");
    }
    bool first_payment = b->paid == 0;
    b->paid += amount;
    if (first_payment) {
        convert_holds_to_sold(*this, *b);
        if (b->coupon[0] && !b->coupon_recorded) {
            if (int *ci = idx_coupon.get(b->coupon)) coupon_uses.push(CouponUse{b->user_id, *ci, b->id});
            b->coupon_recorded = 1;
        }
    }
    record_revenue(buses[t->bus_idx].company_id, amount);
    int old_status = b->status;
    int next = (b->paid >= b->total) ? BK_CONFIRMED : BK_PREBOOKED;
    if (legal_transition(old_status, next)) b->status = next;
    b->pay_status = b->paid >= b->total ? PAY_PAID : PAY_PARTIAL;
    if (b->status == BK_PREBOOKED) schedule_booking_events(*b);
    if (b->status == BK_CONFIRMED) drop_booking_events(b->id);
    notify(b->user_id, NK_BOOKING, b->status == BK_CONFIRMED ? "Booking confirmed" : "Pre-booking confirmed",
           std::string(b->pnr) + " - " + route_name(routes[(size_t)t->route_id]) + ", " + fmt_datetime(t->depart_ms) + ". DEMO PAYMENT - no money was charged.",
           "bk:" + std::to_string(b->id) + ":" + std::to_string(kind));
    flush_outbox();
    return okay("Payment successful (DEMO PAYMENT - no money was charged)");
}

}  // namespace sr
