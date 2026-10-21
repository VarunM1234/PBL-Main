/*
 * PURPOSE: Vendor portal rules: staff status, assignment, bus status with undo, trip start/complete,
 *          greedy staff auto-assignment, phone masking.
 * WHY THIS NAME: "vendor" = bus operator companies.
 * CONNECTS TO: core/ds_stack (undo, row 6), ds_heap + ds_sort (auto-assign, row 16), ds_fenwick (revenue, row 15).
 * COMPLEXITY: auto-assign O(n log n) for n trips; undo O(1) amortised per record.
 * RULE (D13): another company's bus/trip/staff always looks like "Not found".
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>

extern "C" {
#include "ds_sort.h"
}

namespace sr {

static constexpr int64_t REST_AFTER_TRIP_MS = 8 * MS_HOUR;

int App::staff_status(const Staff &s) const {
    if (s.off_duty) return SS_OFF_DUTY;                         /* status is derived, never stored */
    if (s.bus_idx < 0) return SS_AVAILABLE;
    return buses[(size_t)s.bus_idx].status == BUS_ON_TRIP ? SS_ON_TRIP : SS_ASSIGNED;
}

std::string App::mask_phone(const char *phone, bool reveal) const {
    if (reveal) return phone;
    std::string p = phone;
    std::string tail = p.size() >= 4 ? p.substr(p.size() - 4) : p;
    return "XXXXXX" + tail;
}

static bool owns_bus(const App &app, int company_id, int bus_idx) {
    return bus_idx >= 0 && (size_t)bus_idx < app.buses.size() && app.buses[(size_t)bus_idx].company_id == company_id;
}

Result<Unit> App::assign_staff(int company_id, int bus_idx, int staff_id) {
    if (!owns_bus(*this, company_id, bus_idx)) return Result<Unit>::fail(Err::NOT_FOUND, "Bus not found");
    if (staff_id < 1 || (size_t)staff_id > staff.size() || staff[(size_t)staff_id - 1].company_id != company_id)
        return Result<Unit>::fail(Err::NOT_FOUND, "Staff member not found");
    Staff &s = staff[(size_t)staff_id - 1];
    Bus &b = buses[(size_t)bus_idx];
    if (s.off_duty) return Result<Unit>::fail(Err::CONFLICT, std::string(s.name) + " is off duty and cannot be assigned");
    if (s.bus_idx >= 0 && s.bus_idx != bus_idx) return Result<Unit>::fail(Err::CONFLICT, std::string(s.name) + " is already assigned to " + buses[(size_t)s.bus_idx].reg);
    int &slot = s.role == STAFF_DRIVER ? b.driver_id : b.conductor_id;
    if (slot == staff_id) return okay("Already assigned");
    undo.push(UndoRec{s.role == STAFF_DRIVER ? 1 : 2, bus_idx, slot, company_id});   /* remember the previous holder */
    if (slot > 0) staff[(size_t)slot - 1].bus_idx = -1;
    slot = staff_id;
    s.bus_idx = bus_idx;
    return okay(std::string(s.name) + " assigned to " + b.reg);
}

Result<Unit> App::set_bus_status(int company_id, int bus_idx, int status, bool confirmed, int *affected) {
    if (!owns_bus(*this, company_id, bus_idx)) return Result<Unit>::fail(Err::NOT_FOUND, "Bus not found");
    if (status < BUS_AVAILABLE || status > BUS_MAINTENANCE) return Result<Unit>::fail(Err::VALIDATION, "Unknown bus status");
    Bus &b = buses[(size_t)bus_idx];
    int count = 0;
    if (status == BUS_MAINTENANCE) {                            /* who departs within the next 24 hours on this bus? */
        for (size_t i = 0; i < bookings.size(); i++) {
            const Booking &k = bookings[i];
            const Trip *t = trip_by_id(k.trip_id);
            if (t && t->bus_idx == bus_idx && (k.status == BK_PREBOOKED || k.status == BK_CONFIRMED) &&
                t->depart_ms >= clock.now() && t->depart_ms - clock.now() <= 24 * MS_HOUR) count++;
        }
    }
    if (affected) *affected = count;
    if (count > 0 && !confirmed) return Result<Unit>::fail(Err::BAD_STATE, std::to_string(count) + " booking(s) depart within 24 hours. Confirm to continue.");
    undo.push(UndoRec{0, bus_idx, b.status, company_id});
    b.status = status;
    if (count > 0) {
        for (size_t i = 0; i < bookings.size(); i++) {
            const Booking &k = bookings[i];
            const Trip *t = trip_by_id(k.trip_id);
            if (t && t->bus_idx == bus_idx && (k.status == BK_PREBOOKED || k.status == BK_CONFIRMED))
                notify(k.user_id, NK_BOOKING, "Bus unavailable", "Your bus " + std::string(b.reg) + " is in maintenance. The operator will contact you.", "maint:" + std::to_string(k.id));
        }
        flush_outbox();
    }
    return okay("Bus status updated");
}

Result<std::string> App::undo_last(int company_id) {
    Stack<UndoRec> keep;                                        /* park other companies' records, then restore them */
    UndoRec rec;
    bool found = false;
    while (undo.pop(&rec)) {
        if (rec.company_id == company_id) { found = true; break; }
        keep.push(rec);
    }
    UndoRec back;
    Stack<UndoRec> reverse;
    while (keep.pop(&back)) reverse.push(back);
    while (reverse.pop(&back)) undo.push(back);                 /* original order restored */
    if (!found) return Result<std::string>::fail(Err::NOT_FOUND, "Nothing to undo");
    Bus &b = buses[(size_t)rec.bus_idx];
    if (rec.kind == 0) { b.status = rec.old_value; return Result<std::string>::success("Status restored for " + std::string(b.reg)); }
    int &slot = rec.kind == 1 ? b.driver_id : b.conductor_id;
    if (slot > 0) staff[(size_t)slot - 1].bus_idx = -1;
    slot = rec.old_value;
    if (slot > 0) staff[(size_t)slot - 1].bus_idx = rec.bus_idx;
    return Result<std::string>::success("Assignment restored for " + std::string(b.reg));
}

Result<Unit> App::start_trip(int company_id, int trip_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t || buses[(size_t)t->bus_idx].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    if (t->status != TRIP_SCHEDULED) return Result<Unit>::fail(Err::BAD_STATE, "Only a scheduled trip can be started");
    t->status = TRIP_RUNNING;
    buses[(size_t)t->bus_idx].status = BUS_ON_TRIP;
    return okay("Trip started");
}

Result<Unit> App::complete_trip(int company_id, int trip_id) {
    Trip *t = trip_by_id(trip_id);
    if (!t || buses[(size_t)t->bus_idx].company_id != company_id) return Result<Unit>::fail(Err::NOT_FOUND, "Trip not found");
    if (t->status != TRIP_RUNNING) return Result<Unit>::fail(Err::BAD_STATE, "Only a running trip can be completed");
    t->status = TRIP_COMPLETED;
    buses[(size_t)t->bus_idx].status = BUS_AVAILABLE;
    for (size_t i = 0; i < bookings.size(); i++)
        if (bookings[i].trip_id == t->id && legal_transition(bookings[i].status, BK_COMPLETED)) bookings[i].status = BK_COMPLETED;
    return okay("Trip completed");
}

/* ---------- greedy auto-assignment ---------- */

struct TripSlot { int64_t depart_ms; int trip_id; };
static int cmp_slot(const void *a, const void *b) {
    int64_t x = static_cast<const TripSlot *>(a)->depart_ms, y = static_cast<const TripSlot *>(b)->depart_ms;
    return (x > y) - (x < y);
}

/* Assign one role: min-heap of "free at" times; a staff member whose free time is after the trip start would overlap. */
static int assign_role(App &app, MinHeap &heap, const Vec<TripSlot> &slots, int role, std::string *report) {
    int assigned = 0;
    for (size_t i = 0; i < slots.size(); i++) {
        Trip *t = app.trip_by_id(slots[i].trip_id);
        int64_t free_at;
        uint64_t sid;
        if (!heap.peek(&free_at, &sid) || free_at > t->depart_ms) {           /* nobody free: would overlap */
            *report += "  trip " + std::to_string(t->id) + " (" + fmt_hm(t->depart_ms) + ") has no free " + (role == STAFF_DRIVER ? "driver" : "conductor") + "\n";
            continue;
        }
        heap.pop(&free_at, &sid);
        (role == STAFF_DRIVER ? t->auto_driver : t->auto_conductor) = (int)sid;
        heap.push(t->arrive_ms + REST_AFTER_TRIP_MS, sid);                      /* busy until the trip ends + 8 h rest */
        assigned++;
    }
    return assigned;
}

int App::auto_assign(int company_id, int date_key, std::string *report) {
    Vec<TripSlot> slots;
    for (size_t si = 0; si < schedules.size(); si++) {
        const Bus &b = buses[(size_t)schedules[si].bus_idx];
        if (b.company_id != company_id || b.status == BUS_MAINTENANCE) continue;
        Trip *t = get_or_create_trip((int)si, date_key);
        if (!t) continue;
        t->auto_driver = t->auto_conductor = -1;
        slots.push(TripSlot{t->depart_ms, t->id});
    }
    ds_merge_sort(slots.data(), slots.size(), sizeof(TripSlot), cmp_slot);      /* by start time */
    MinHeap drivers, conductors;
    for (size_t i = 0; i < staff.size(); i++) {
        const Staff &s = staff[i];
        if (s.company_id != company_id || s.off_duty) continue;                 /* off-duty staff are never used */
        (s.role == STAFF_DRIVER ? drivers : conductors).push(0, (uint64_t)s.id);
    }
    report->clear();
    int d = assign_role(*this, drivers, slots, STAFF_DRIVER, report);
    int c = assign_role(*this, conductors, slots, STAFF_CONDUCTOR, report);
    *report = "Trips on the day: " + std::to_string(slots.size()) + ", drivers assigned: " + std::to_string(d) +
              ", conductors assigned: " + std::to_string(c) + "\n" + *report;
    return d + c;
}

}  // namespace sr
