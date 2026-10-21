/*
 * PURPOSE: Plain data records (cities, routes, buses, trips, bookings, ...) used across the app.
 * WHY THIS NAME: "models" = the data shapes of the domain.
 * CONNECTS TO: app.hpp (the engine), storage (save/load), cli, http_server.
 * COMPLEXITY: n/a (data only). Records are plain structs with fixed char arrays so they can be
 *             stored in my C containers with memcpy (no hidden heap pointers, no STL inside).
 */
#ifndef SMARTROUTE_MODELS_HPP
#define SMARTROUTE_MODELS_HPP
#include <cstdint>
#include "dswrap.hpp"
#include "geo.h"

extern "C" {
#include "ds_list.h"
#include "ds_ring.h"
}

namespace sr {

constexpr int MAX_STOPS = 16;
constexpr int MAX_PAX = 6;
constexpr int MAX_SEATS = 64;

enum StopType { ST_ORIGIN, ST_BOARDING, ST_TOLL, ST_BREAK, ST_DROPPING, ST_DESTINATION };
enum BusStatus { BUS_AVAILABLE, BUS_ON_TRIP, BUS_MAINTENANCE };
enum TripStatus { TRIP_SCHEDULED, TRIP_RUNNING, TRIP_COMPLETED, TRIP_CANCELLED };
enum BookingStatus { BK_PENDING_PAYMENT, BK_PREBOOKED, BK_CONFIRMED, BK_CANCELLED, BK_EXPIRED, BK_COMPLETED };
enum PayStatus { PAY_UNPAID, PAY_PARTIAL, PAY_PAID, PAY_PARTIALLY_REFUNDED, PAY_REFUNDED };
enum Layout { LAYOUT_2P2, LAYOUT_2P1, LAYOUT_SLEEPER };
enum Role { ROLE_PASSENGER, ROLE_VENDOR, ROLE_DRIVER, ROLE_ADMIN };
enum StaffRole { STAFF_DRIVER, STAFF_CONDUCTOR };
enum StaffStatus { SS_OFF_DUTY, SS_ON_TRIP, SS_ASSIGNED, SS_AVAILABLE };
enum BookingMode { MODE_FULL, MODE_DEPOSIT };
enum PayKind { PAYK_DEPOSIT, PAYK_FULL, PAYK_BALANCE };
enum PayOutcome { OUT_SUCCESS, OUT_DECLINED, OUT_TIMEOUT };
enum NoteKind { NK_BOOKING, NK_JOURNEY, NK_BREAKS, NK_TOLLS, NK_TRAFFIC, NK_DROPOFF, NK_REMINDERS, NK_COUNT };

struct City { int id; char name[32]; char state[32]; geo_point pos; char points[2][48]; };

struct Stop {
    int seq; char name[48]; int type; double km; geo_point pos; int break_min; int sample;
};

struct Route {
    int id; char slug[40]; int from_city, to_city; int km_nominal, avg_kmh, reverse_of;
    int n_stops; Stop stops[MAX_STOPS];
    double cum_km[MAX_STOPS]; geo_point pts[MAX_STOPS];
    ds_list *stop_list;                 /* doubly linked list of stop indexes (walk forward/backward) */
    int drive_min, break_min_total, duration_min;
};

struct BusType {
    char code[24]; char label[32]; int rate_paise_per_km, capacity, layout, rows; char amenities[112];
    int premium_for_coupon;
};

struct Company { int id; char name[40]; char slug[40]; char invite[48]; };

struct Bus { char reg[12]; int company_id; int type_idx; int base_route; int status; int driver_id, conductor_id; };

struct Staff { int id; int company_id; char name[32]; int role; char phone[20]; int off_duty; int bus_idx; };

struct Schedule { int id; int bus_idx; int route_id; int depart_min_of_day; };

struct WaitEntry { int user_id; int seats_wanted; };

struct Coupon {
    char code[16]; int is_percent; int64_t value, max_discount, min_base; int per_user_limit, max_uses;
    char allowed[160]; int valid_days; int active;
};

struct CouponUse { int user_id; int coupon_idx; int booking_id; };

struct User {
    int id; char name[40]; char email[64]; char phone[20]; int role; int company_id;
    char salt[33]; char hash[65]; int prefs; int failed_logins; int staff_id;   /* staff_id: driver accounts are linked to a crew member */
};

struct Passenger { char seat[6]; char name[32]; int age; char gender; int64_t price; };

struct Booking {
    int id; char pnr[12]; int user_id, trip_id, status, pay_status, mode;
    int64_t base, discount, tax, insurance, total, paid, refunded;
    char coupon[16]; int boarding_stop, dropping_stop; int n_pax; Passenger pax[MAX_PAX];
    char contact[48]; int64_t balance_due_ms, created_ms; int demo_payment, attempt_no, coupon_recorded;
    int boarded_mask; int64_t boarded_ms;       /* bit per passenger checked in by the driver's scanner */
};

struct Hold { int id; int trip_id; int seat; int user_id; int64_t expires_ms; int active; };

struct Payment { int id; int booking_id; int kind; int64_t amount; int outcome; char key[40]; int64_t at_ms; };

struct Notification {
    int id; int user_id; int kind; char title[64]; char body[220]; char dedupe[96]; int64_t created_ms; int read;
};

/* One GPS fix reported by a driver (DEMO: coordinates are typed or simulated). */
struct GpsFix { int id; int trip_id; int64_t at_ms; double lat, lng, speed, heading, accuracy; };

/* One delay report: who said so, how long, why. */
struct DelayLog { int id; int trip_id; int64_t at_ms; char cause[16]; int minutes; char source[24]; };

/* A public read-only tracking link for one booking. */
struct ShareLink { char token[16]; int booking_id; int64_t at_ms; int active; };

struct UndoRec { int kind; int bus_idx; int old_value; int company_id; };   /* 0 status, 1 driver, 2 conductor */

/* A concrete run of one schedule on one date. Not a plain struct (owns bitsets and a queue),
 * so trips are heap-allocated and referenced by id. */
struct Trip {
    int id, schedule_idx, route_id, bus_idx, date_key; int64_t depart_ms, arrive_ms;
    int status, delay_min; char delay_reason[16]; char delay_note[128];
    Bits booked, held; Queue<WaitEntry> waitlist;
    double km, jam_left_km; int break_left_sec, breaks_done; double sim_sec; int sim_speed, sim_started;
    int jam_alerted_level, auto_driver, auto_conductor;
    ds_ring *gps;                                 /* last 10 minutes of speed samples (row 14) */
    explicit Trip(int capacity)
        : id(0), schedule_idx(0), route_id(0), bus_idx(0), date_key(0), depart_ms(0), arrive_ms(0),
          status(TRIP_SCHEDULED), delay_min(0), booked(capacity), held(capacity), km(0), jam_left_km(0),
          break_left_sec(0), breaks_done(0), sim_sec(0.0), sim_speed(60), sim_started(0), jam_alerted_level(0),
          auto_driver(-1), auto_conductor(-1), gps(ds_ring_create(64)) {
        delay_reason[0] = delay_note[0] = '\0';
    }
    ~Trip() { ds_ring_destroy(gps); }
    Trip(const Trip &) = delete;
    Trip &operator=(const Trip &) = delete;
};

}  // namespace sr
#endif
