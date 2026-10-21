/*
 * PURPOSE: The SmartRoute engine: one App object owns all data and offers every service
 *          (catalogue, auth, search, seats, booking, payment, tracking, vendor, storage).
 * WHY THIS NAME: "app" = the application core, shared by the CLI and the HTTP server (no duplicated logic).
 * CONNECTS TO: models.hpp, dswrap.hpp, core/ data structures. Methods are implemented in
 *              catalog.cpp, auth.cpp, search.cpp, seats.cpp, booking.cpp, payment.cpp, coupons.cpp,
 *              tracking.cpp, notifications.cpp, vendor.cpp, storage.cpp.
 * COMPLEXITY: see each method's comment.
 */
#ifndef SMARTROUTE_APP_HPP
#define SMARTROUTE_APP_HPP

#include <string>
#include <vector>
#include "csv.hpp"
#include "clock.hpp"
#include "models.hpp"
#include "money.hpp"
#include "result.hpp"

extern "C" {
#include "ds_fenwick.h"
#include "ds_graph.h"
#include "ds_lru.h"
#include "ds_trie.h"
#include "ds_unionfind.h"
}

namespace sr {

struct Quote {
    int64_t base = 0, discount = 0, tax = 0, insurance = 0, total = 0, deposit = 0, balance = 0;
    int n_seats = 0; int64_t seat_price[MAX_PAX] = {0};
    bool prebook_eligible = false; int64_t balance_due_ms = 0;
};

struct BookingRequest {
    int user_id = 0, trip_id = 0, n_pax = 0; Passenger pax[MAX_PAX];
    int boarding_stop = 0, dropping_stop = 0; char contact[48] = {0}; char coupon[16] = {0};
    bool insurance = false; int mode = MODE_FULL;
};

struct SearchFilters {
    unsigned type_mask = 0;            /* bit per bus type; 0 = any */
    int dep_window = -1, arr_window = -1;   /* 0 night, 1 morning, 2 afternoon, 3 evening; -1 any */
    int64_t max_fare = 0;              /* paise, 0 = no limit */
    int dep_after = -1, dep_before = -1;   /* departure time-of-day window, minutes after midnight; -1 = open */
    bool need_seats = false; char operator_substr[32] = {0}; char amenity[24] = {0};
};
enum SortKey { SORT_EARLIEST, SORT_LATEST, SORT_CHEAPEST, SORT_FASTEST, SORT_MOST_SEATS };

struct SearchRow {
    int trip_id, bus_idx, route_id; int64_t depart_ms, arrive_ms; int duration_min;
    int64_t fare_min, fare_max; int seats_left;
};

struct PlanLeg { int route_id, from_city, to_city; int64_t minutes, paise; };
enum PlanMode { PLAN_FASTEST, PLAN_CHEAPEST, PLAN_FEWEST };

struct RefundPreview { int pct = 0; int64_t refund = 0; int64_t hours_left = 0; int64_t refundable = 0; };

struct TrackInfo {
    double progress_pct = 0, km_left = 0, speed_kmh = 0; int64_t sched_arrive = 0, est_arrive = 0;
    int delay_min = 0; int64_t sim_now = 0; bool on_break = false; int break_min_left = 0;
};

struct AssistantAnswer { std::string text; int from = -1, to = -1; int date_key = 0; bool opened_search = false;
    /* the parse the page replays through /api/search so Copilot and the form show identical results */
    int sort_mode = -1;                /* SortKey, -1 = leave default */
    int dep_window = -1, dep_after = -1, dep_before = -1;
    unsigned type_mask = 0;            /* bit per bus type; 0 = any */
    std::string amenity, operator_substr;   /* amenity word, or coach-make operator substring */
    std::vector<std::string> ignored;       /* things asked for that the catalogue has no word for (honesty) */
};
struct CorridorRevenue { int route; int passes; int64_t paise; };

struct SeatCell { char label[6]; int index; int state; };   /* state: 0 free, 1 booked, 2 held by other, 3 my hold */

/* Spec 6.4: names, passenger names and contacts must not carry markup or control characters.
   Letters of any language, digits and . ' - @ + space are fine. Implemented in auth.cpp. */
bool display_text_ok(const std::string &s);

class App {
public:
    App();
    ~App();
    App(const App &) = delete;
    App &operator=(const App &) = delete;

    /* ---- catalogue (catalog.cpp) ---- */
    bool load_catalog(const std::string &data_dir, std::string *error);
    int find_city(const std::string &name) const;                       /* hash lookup, -1 if none */
    int find_route(int from_city, int to_city) const;                   /* hash lookup, -1 if none */
    size_t suggest_cities(const char *prefix, size_t max, char out[][64], int ids[]) const;   /* trie */
    size_t network_count() const;                                       /* union-find */
    bool same_network(int a, int b) const;
    int find_bus(const char *reg) const;
    int stop_index(const Route &r, int stop_type, int nth) const;
    int64_t planned_stop_time(const Trip &t, int stop_idx) const;
    int64_t eta_at_stop(const Trip &t, int stop_idx) const;   /* per-stop arrival estimate (spec 6.7) */

    /* ---- auth (auth.cpp) ---- */
    Result<int> register_passenger(const std::string &name, const std::string &email, const std::string &phone, const std::string &password);
    int add_guest();                                                   /* account-less booking: a passenger whose stored hash can never match a password */
    int guest_count() const;
    Result<int> login(const std::string &email, const std::string &password, int expected_role);
    int add_user(const char *name, const char *email, const char *phone, int role, int company_id, const char *password, int staff_id = 0);
    const User *user_by_id(int id) const;
    int user_id_by_email(const std::string &email) const;

    /* ---- trips and seats (seats.cpp) ---- */
    Trip *get_or_create_trip(int schedule_idx, int date_key);
    Trip *trip_by_id(int id) const;
    size_t seat_count(const Trip &t) const;
    void seat_label(const BusType &bt, int index, char out[6]) const;
    int seat_index(const BusType &bt, const char *label) const;          /* -1 if invalid */
    int seat_row(const BusType &bt, int index) const;                    /* row id (deck-aware) */
    std::vector<SeatCell> seat_map(const Trip &t, int viewer_user) const;
    Result<Unit> hold_seats(int user_id, int trip_id, const char *const *labels, int n);
    void release_user_holds(int trip_id, int user_id);
    long find_adjacent(const Trip &t, int n_seats) const;               /* first seat index of n free adjacent seats or -1 */
    Result<Unit> join_waitlist(int user_id, int trip_id, int seats);
    int active_holds(int trip_id, int user_id) const;
    int64_t hold_expiry(int trip_id, int user_id) const;
    bool sales_open(const Trip &t) const;
    void process_events();                                              /* heap-driven: hold expiry, deadlines, reminders */
    void advance_clock(int64_t ms);
    void sync_wall_clock(int64_t wall_ms);      /* web server: the virtual clock follows real time (never backwards), so 10-minute seat holds expire even when nobody clicks "+1 hour" */

    /* ---- search + planner (search.cpp) ---- */
    void search(int from_city, int to_city, int date_key, const SearchFilters &f, SortKey sort, Vec<SearchRow> &out);
    Result<Unit> plan_journey(int from_city, int to_city, PlanMode mode, Vec<PlanLeg> &legs, int64_t *total_min, int64_t *total_paise);
    int64_t route_lowest_fare(int route_id) const;                      /* paise, 0 if no bus */
    int64_t route_highest_fare(int route_id) const;                     /* dearest seat on the route: the fare slider cap */

    /* ---- booking (booking.cpp, coupons.cpp, payment.cpp) ---- */
    Result<Quote> quote(int trip_id, const char *const *labels, int n, const char *coupon, bool insurance, int user_id) const;
    Result<int> create_booking(const BookingRequest &req);
    Result<Unit> pay_booking(int booking_id, int user_id, int kind, int outcome);
    RefundPreview cancel_preview(const Booking &b) const;
    Result<int64_t> cancel_booking(int booking_id, int user_id);
    Booking *booking_by_id(int id) const;
    Booking *booking_by_pnr(const std::string &pnr, int user_id) const;   /* only the owner can read it */
    bool legal_transition(int from, int to) const;
    Result<int64_t> coupon_discount(const char *code, int64_t base, const BusType &bt, int user_id) const;

    /* ---- tracking + notifications (tracking.cpp, notifications.cpp) ---- */
    void sim_advance(Trip &t, double seconds);
    void sim_advance_km(Trip &t, double km);
    void sim_jam(Trip &t);
    void sim_reset(Trip &t);
    TrackInfo track_info(const Trip &t) const;
    geo_point bus_position(const Route &r, double km) const;
    void fire_alerts(Trip &t, double prev_km, double cur_km);
    Result<Unit> report_delay(int company_id, int trip_id, const std::string &reason, int minutes, const std::string &note);
    void notify(int user_id, int kind, const std::string &title, const std::string &body, const std::string &dedupe_key);
    void flush_outbox();
    int unread_count(int user_id) const;
    void mark_all_read(int user_id);
    std::vector<const Notification *> notifications_for(int user_id) const;
    void set_prefs(int user_id, int mask);

    /* ---- vendor (vendor.cpp) ---- */
    int staff_status(const Staff &s) const;
    Result<Unit> assign_staff(int company_id, int bus_idx, int staff_id);
    Result<Unit> set_bus_status(int company_id, int bus_idx, int status, bool confirmed, int *affected);
    Result<std::string> undo_last(int company_id);
    Result<Unit> start_trip(int company_id, int trip_id);
    Result<Unit> complete_trip(int company_id, int trip_id);
    int auto_assign(int company_id, int date_key, std::string *report);
    int64_t revenue_range(int company_id, int day_from, int day_to) const;
    void record_revenue(int company_id, int64_t paise_delta);
    std::string mask_phone(const char *phone, bool reveal) const;

    /* ---- driver, admin, vendor runs, share links (ops.cpp) ---- */
    int trip_driver(const Trip &t) const;                               /* staff id on this trip (trip override, else the bus's driver), 0 = none */
    int trip_conductor(const Trip &t) const;
    bool driver_can_access(const User &u, const Trip &t) const;
    std::vector<Trip *> trips_on_date(int date_key, int company_id);    /* creates trips lazily; company 0 = every company */
    std::vector<Trip *> driver_trips(const User &u);
    Result<Unit> driver_start(const User &u, int trip_id);
    Result<Unit> driver_end(const User &u, int trip_id);
    Result<Unit> driver_position(const User &u, int trip_id, double lat, double lng, double speed, double heading, double accuracy);
    Result<Unit> driver_delay(const User &u, int trip_id, const std::string &reason, int minutes, const std::string &note);
    Result<int> driver_scan(const User &u, const std::string &code, bool *already_checked_in);
    std::string boarding_state(const Booking &b) const;                 /* VALID / PART-BOARDED / BOARDED / CANCELLED */
    Booking *booking_by_pnr_any(const std::string &pnr) const;          /* staff lookup (not owner-only) */
    std::string qr_payload(const Booking &b) const;
    Result<Unit> assign_trip_crew(int company_id, int trip_id, int driver_id, int conductor_id);   /* -1 = leave unchanged */
    Result<Unit> set_staff_off_duty(int company_id, int staff_id, bool off);
    std::vector<CorridorRevenue> revenue_by_corridor() const;
    Result<Unit> add_coupon(const std::string &code, bool percent, int64_t value, int64_t cap, int64_t min_base);
    Result<Unit> set_coupon_active(const std::string &code, bool active);
    int coupon_use_count(int coupon_idx) const;
    int scan_traffic_delays(std::string *report);                       /* DEMO TRAFFIC model */
    void apply_delay(Trip &t, const std::string &reason, int minutes, const std::string &note, const char *source);
    Result<std::string> share_create(int user_id, int booking_id, bool rotate);
    Result<Unit> share_revoke(int user_id, int booking_id);
    Booking *share_lookup(const std::string &token) const;
    std::string share_token_of(int booking_id) const;
    AssistantAnswer assistant(const std::string &text);                 /* rule-based (no AI service) */

    /* ---- storage (storage.cpp) ---- */
    bool save_all(std::string *error);
    bool load_saved(std::string *messages);

    /* ---- shared state (public: CLI/HTTP read it directly) ---- */
    Clock clock;
    std::string data_dir, save_dir;
    Vec<City> cities; Vec<Route> routes; Vec<BusType> types; Vec<Company> companies; Vec<Bus> buses;
    Vec<Staff> staff; Vec<Schedule> schedules; Vec<Coupon> coupons; Vec<User> users;
    Vec<Trip *> trips; Vec<Booking> bookings; Vec<Hold> holds; Vec<Payment> payments;
    Vec<Notification> notes; Vec<CouponUse> coupon_uses;
    Vec<GpsFix> gps_fixes; Vec<DelayLog> delay_log; Vec<ShareLink> shares;
    Map<int> idx_city, idx_route, idx_email, idx_pnr, idx_coupon, idx_trip, idx_bus, idx_pay, idx_share;
    Map<char> alert_seen;                    /* hash SET for alert dedupe (row 19) */
    ds_trie *city_trie; ds_graph *graph; ds_unionfind *networks; ds_fenwick *revenue_co[32]; ds_lru *cache;
    MinHeap events; Queue<Notification> outbox; Stack<UndoRec> undo;
    int64_t base_day; uint64_t rng_state; int session_user;
    int plain_seed_pending;
    int64_t alert_time_ms = 0;               /* when non-zero, new alerts are stamped with this (simulated trip) time */

    uint64_t next_random();
    void make_pnr(char out[12]);
    std::string route_name(const Route &r) const;
    std::string bus_label(int bus_idx) const;
    void free_all_trips();
    int64_t trip_hours_left(const Trip &t) const;
private:
    void build_routes_from_csv(const std::vector<CsvRow> &rows);
    void release_holds_of_booking(const Booking &b);
    void free_seats_after_cancel(Booking &b);
    void on_seats_freed(int trip_id);
    void expire_hold(int hold_id);
    void expire_booking_if_unheld(int trip_id, int user_id);
    void schedule_booking_events(const Booking &b);
    void drop_booking_events(int booking_id);
    void finalize_end_state(Booking &b, int new_status);
    void maybe_alert_traffic(Trip &t);
};

}  // namespace sr
#endif
