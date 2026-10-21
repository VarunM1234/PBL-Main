/*
 * PURPOSE: Ticket rendering.
 * WHY THIS NAME: implements ticket.hpp.
 * CONNECTS TO: asciimap.hpp
 * COMPLEXITY: see header.
 */
#include "ticket.hpp"
#include "asciimap.hpp"
#include <cstdio>
#include <cstring>

namespace sr {

const char *booking_status_name(int s) {
    static const char *N[6] = {"PENDING_PAYMENT", "PREBOOKED", "CONFIRMED", "CANCELLED", "EXPIRED", "COMPLETED"};
    return (s >= 0 && s < 6) ? N[s] : "?";
}
const char *pay_status_name(int s) {
    static const char *N[5] = {"UNPAID", "PARTIAL", "PAID", "PARTIALLY_REFUNDED", "REFUNDED"};
    return (s >= 0 && s < 5) ? N[s] : "?";
}
const char *bus_status_name(int s) {
    static const char *N[3] = {"AVAILABLE", "ON_TRIP", "MAINTENANCE"};
    return (s >= 0 && s < 3) ? N[s] : "?";
}
const char *staff_status_name(int s) {
    static const char *N[4] = {"OFF_DUTY", "ON_TRIP", "ASSIGNED", "AVAILABLE"};
    return (s >= 0 && s < 4) ? N[s] : "?";
}

std::string cancellation_table() {
    return "Cancellation (demo policy, refund of the fare; insurance is non-refundable):\n"
           "  72 h or more before departure : 100%\n"
           "  24 h to 72 h                  :  80%\n"
           "  6 h to 24 h                   :  50%\n"
           "  under 6 h                     :   0%\n";
}

bool driver_phones_visible(const App &app, const Booking &b) {
    const Trip *t = app.trip_by_id(b.trip_id);
    if (!t || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) return false;
    int64_t now = app.clock.now();
    return now >= t->depart_ms - 24 * MS_HOUR && now <= t->arrive_ms + 2 * MS_HOUR;
}

static void box_line(std::string &out, const std::string &text) {
    std::string t = text.size() > 74 ? text.substr(0, 74) : text;
    out += "| " + t + std::string(74 - t.size(), ' ') + " |\n";
}

std::string render_ticket(const App &app, const Booking &b, bool color) {
    const Trip *t = app.trip_by_id(b.trip_id);
    const Route &r = app.routes[(size_t)t->route_id];
    const Bus &bus = app.buses[(size_t)t->bus_idx];
    const BusType &bt = app.types[(size_t)bus.type_idx];
    std::string border = "+" + std::string(76, '=') + "+\n", out = border;
    box_line(out, "SMARTROUTE  |  E-TICKET (DEMO - sample data, no real travel)");
    out += border;
    box_line(out, std::string("PNR: ") + b.pnr + "    Status: " + booking_status_name(b.status) + "    Payment: " + pay_status_name(b.pay_status));
    box_line(out, "Route: " + app.route_name(r) + "    Date: " + fmt_date(t->depart_ms));
    box_line(out, "Departs: " + fmt_hm(t->depart_ms) + " IST    Arrives: " + fmt_hm_plus(t->arrive_ms, t->depart_ms) + " IST    Duration: " + fmt_duration(r.duration_min));
    box_line(out, "Boarding: " + std::string(r.stops[b.boarding_stop].name) + " @ " + fmt_hm(app.planned_stop_time(*t, b.boarding_stop)));
    box_line(out, "Dropping: " + std::string(r.stops[b.dropping_stop].name) + " @ " + fmt_hm_plus(app.planned_stop_time(*t, b.dropping_stop), t->depart_ms));
    box_line(out, "Operator: " + app.bus_label(t->bus_idx) + "  (" + bt.label + ")");
    for (int i = 0; i < b.n_pax; i++)
        box_line(out, "  Seat " + std::string(b.pax[i].seat) + "  " + b.pax[i].name + ", " + std::to_string(b.pax[i].age) + " " + b.pax[i].gender + "  " + fmt_rs(b.pax[i].price));
    box_line(out, "Total " + fmt_rs(b.total) + "   Paid " + fmt_rs(b.paid) + "   Balance " + fmt_rs(b.status == BK_PREBOOKED ? b.total - b.paid : 0) +
                      (b.refunded ? "   Refunded " + fmt_rs(b.refunded) : ""));
    if (b.status == BK_PREBOOKED) box_line(out, "Balance due by: " + fmt_datetime(b.balance_due_ms) + "  (unpaid => booking expires)");
    if (b.demo_payment) box_line(out, "DEMO PAYMENT - no money was charged");
    std::string breaks;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK) breaks += std::string(r.stops[i].name) + " (" + std::to_string(r.stops[i].break_min) + " min), ";
    box_line(out, "Break stations: " + (breaks.empty() ? std::string("none") : breaks.substr(0, breaks.size() - 2)));
    if (driver_phones_visible(app, b)) {
        const Staff *d = bus.driver_id > 0 ? app.staff.at((size_t)bus.driver_id - 1) : nullptr;
        const Staff *c = bus.conductor_id > 0 ? app.staff.at((size_t)bus.conductor_id - 1) : nullptr;
        if (d) box_line(out, std::string("Driver: ") + d->name + "  " + d->phone + "  (DEMO number)");
        if (c) box_line(out, std::string("Conductor: ") + c->name + "  " + c->phone + "  (DEMO number)");
    } else box_line(out, "Driver/conductor numbers appear 24 h before departure for paid bookings.");
    box_line(out, "Verified by PNR lookup - a code alone is not proof of payment");
    out += border;
    out += cancellation_table();
    out += render_route_map(app, r, -1, color);
    return out;
}

std::string share_summary(const App &app, const Booking &b) {
    const Trip *t = app.trip_by_id(b.trip_id);
    const Route &r = app.routes[(size_t)t->route_id];
    return "SmartRoute trip (demo): " + app.route_name(r) + ", " + fmt_datetime(t->depart_ms) + ", arrives " + fmt_hm_plus(t->arrive_ms, t->depart_ms) +
           " IST, " + app.bus_label(t->bus_idx) + ". Seats: " + std::to_string(b.n_pax) + ". (No personal data included.)";
}

}  // namespace sr
