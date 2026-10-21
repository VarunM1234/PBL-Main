/*
 * PURPOSE: C++ tests for the SmartRoute engine (business rules, planner, alerts, persistence)
 *          and 10,000-operation random cross-checks of my C data structures against the STL.
 * WHY THIS NAME: tests for the app/ layer. STL containers are allowed here ONLY to check my own structures.
 * CONNECTS TO: app/app.hpp, the core headers, tests/test.h. Run from the project root (needs data/).
 * COMPLEXITY: n/a
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#ifdef _WIN32
#include <direct.h>
#define SR_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SR_MKDIR(p) mkdir(p, 0755)
#endif
#include "../app/app.hpp"
#include "../app/basemap.hpp"
#include "test.h"

extern "C" {
#include "ds_search.h"
#include "ds_sort.h"
}

using namespace sr;

#define CHECK_STR(expr, lit) do { std::string _tmp_s = (expr); CHECK_EQ_STR(_tmp_s.c_str(), lit); } while (0)

/* Portable "mkdir -p" for the two project paths the tests need (Windows cmd has no mkdir -p). */
static void make_dirs(const char *path) {
    std::string p = path;
    for (size_t i = p.find('/'); i != std::string::npos; i = p.find('/', i + 1)) {
        SR_MKDIR(p.substr(0, i).c_str());       /* ignore EEXIST */
    }
    SR_MKDIR(p.c_str());
}

/* Portable cleanup of a test save folder: removes only the known save files (and their .bak/.tmp). */
static void clear_save_dir(const char *dir) {
    static const char *sections[] = {"meta", "users", "fleet", "trips", "holds", "coupons",
                                     "bookings", "payments", "notes", "shares", "delays", "gps", "crew"};
    for (const char *s : sections) {
        for (const char *ext : {".sav", ".sav.bak", ".sav.tmp"}) {
            std::remove((std::string(dir) + "/" + s + ext).c_str());
        }
    }
}

/* ---------- helpers ---------- */

static std::unique_ptr<App> make_app(const char *now = "2026-10-19 10:00") {
    std::unique_ptr<App> app(new App());
    int64_t ms = 0;
    parse_ist_datetime(now, &ms);
    app->clock.set(ms);
    std::string err;
    if (!app->load_catalog("data", &err)) { std::fprintf(stderr, "catalog load failed: %s\n", err.c_str()); std::abort(); }
    return app;
}

static int sched_of(App &a, const char *reg, const char *slug) {
    for (size_t i = 0; i < a.schedules.size(); i++)
        if (a.routes[(size_t)a.schedules[i].route_id].slug == std::string(slug) && std::string(a.buses[(size_t)a.schedules[i].bus_idx].reg) == reg) return (int)i;
    return -1;
}

static Trip *trip_on(App &a, const char *reg, const char *slug, int days_ahead) {
    int key = date_key(a.clock.now() + days_ahead * MS_DAY);
    return a.get_or_create_trip(sched_of(a, reg, slug), key);
}

/* Hold + book + pay a booking; returns booking id. */
static int book_full(App &a, int user, Trip *t, std::vector<const char *> seats, const char *coupon, bool insurance, int mode = MODE_FULL) {
    Result<Unit> h = a.hold_seats(user, t->id, seats.data(), (int)seats.size());
    if (!h.ok) return 0;
    BookingRequest rq;
    rq.user_id = user; rq.trip_id = t->id; rq.n_pax = (int)seats.size(); rq.insurance = insurance; rq.mode = mode;
    for (size_t i = 0; i < seats.size(); i++) {
        std::snprintf(rq.pax[i].seat, sizeof rq.pax[i].seat, "%s", seats[i]);
        std::snprintf(rq.pax[i].name, sizeof rq.pax[i].name, "Test Pax %zu", i + 1);
        rq.pax[i].age = 30; rq.pax[i].gender = 'M';
    }
    rq.boarding_stop = a.stop_index(a.routes[(size_t)t->route_id], ST_BOARDING, 0);
    rq.dropping_stop = a.stop_index(a.routes[(size_t)t->route_id], ST_DROPPING, 0);
    std::snprintf(rq.contact, sizeof rq.contact, "test@example.com");
    std::snprintf(rq.coupon, sizeof rq.coupon, "%s", coupon);
    Result<int> b = a.create_booking(rq);
    if (!b.ok) { std::fprintf(stderr, "create_booking: %s\n", b.message.c_str()); return 0; }
    Result<Unit> p = a.pay_booking(b.value, user, mode == MODE_DEPOSIT ? PAYK_DEPOSIT : PAYK_FULL, OUT_SUCCESS);
    if (!p.ok) { std::fprintf(stderr, "pay: %s\n", p.message.c_str()); return 0; }
    return b.value;
}

/* ---------- catalogue ---------- */

static void test_catalog(void) {
    printf("Testing catalogue...\n");
    auto a = make_app();
    CHECK_EQ_INT(a->cities.size(), 20);
    CHECK_EQ_INT(a->routes.size(), 30);                              /* 15 forward + 15 reverse */
    CHECK_EQ_INT(a->companies.size(), 14);
    CHECK_EQ_INT(a->buses.size(), 34);
    CHECK_EQ_INT(a->staff.size(), 84);
    CHECK_EQ_INT(a->types.size(), 8);
    CHECK_EQ_INT(a->schedules.size(), 68);
    CHECK_EQ_INT(a->users.size(), 18);
    int per_route[64] = {0};
    for (size_t i = 0; i < a->schedules.size(); i++) per_route[a->schedules[i].route_id]++;
    bool two_each = true;
    for (size_t i = 0; i < a->routes.size(); i++) if (per_route[i] < 2) two_each = false;
    CHECK(two_each);                                                 /* every directed route has >= 2 buses */
    int on_trip = 0, maint = 0;
    for (size_t i = 0; i < a->buses.size(); i++) { on_trip += a->buses[i].status == BUS_ON_TRIP; maint += a->buses[i].status == BUS_MAINTENANCE; }
    CHECK_EQ_INT(on_trip, 1); CHECK_EQ_INT(maint, 1);
    bool used[8] = {false};
    for (size_t i = 0; i < a->buses.size(); i++) used[a->buses[i].type_idx] = true;
    bool all_types = true;
    for (bool u : used) all_types &= u;
    CHECK(all_types);
    const Route &dj = a->routes[(size_t)a->find_route(a->find_city("Delhi"), a->find_city("Jaipur"))];
    CHECK_EQ_INT(dj.n_stops, 10);                                     /* origin, 2 boarding, 2 breaks, 2 tolls, 2 dropping, destination */
    int br = 0, tl = 0;
    for (int i = 0; i < dj.n_stops; i++) { br += dj.stops[i].type == ST_BREAK; tl += dj.stops[i].type == ST_TOLL; }
    CHECK_EQ_INT(br, 2); CHECK_EQ_INT(tl, 2);
    CHECK_EQ_INT(a->network_count(), 5);                             /* exactly 5 separate bus networks */
    CHECK(a->same_network(a->find_city("Delhi"), a->find_city("Ahmedabad")));
    CHECK(!a->same_network(a->find_city("Delhi"), a->find_city("Goa")));
    CHECK(a->same_network(a->find_city("Kolkata"), a->find_city("Digha")));
    CHECK_EQ_INT(a->find_city("delhi"), 0);
    CHECK_EQ_INT(a->find_city("Atlantis"), -1);
    char out[10][64]; int ids[10];
    CHECK_EQ_INT(a->suggest_cities("Del", 10, out, ids), 1);
    CHECK_EQ_STR(out[0], "delhi");
    CHECK_EQ_INT(a->suggest_cities("M", 10, out, ids), 3);          /* Madurai, Mumbai, Mysuru */
    const Route &rev = a->routes[(size_t)dj.reverse_of];
    CHECK_EQ_INT(rev.stops[0].type, ST_ORIGIN);
    CHECK_EQ_INT(rev.stops[1].type, ST_BOARDING);
    CHECK_NEAR(rev.stops[rev.n_stops - 1].km, 280, 1e-9);
}

static void test_csv_errors(void) {
    printf("Testing CSV loader errors...\n");
    { std::ofstream f("build/bad.csv"); f << "# c\na,b,c\n1,2,3\n4,5\n"; }
    std::vector<CsvRow> rows; std::string err;
    CHECK(!csv_read("build/bad.csv", 3, &rows, &err));
    CHECK(err.find(":4:") != std::string::npos);                     /* names the line number */
    CHECK_EQ_INT(rows.size(), 1);
    std::vector<CsvRow> r2;
    CHECK(!csv_read("build/does_not_exist.csv", 3, &r2, &err));
    { std::ofstream f("build/q.csv"); f << "x,y\n\"a,b\",\"he said \"\"hi\"\"\"\n"; }
    std::vector<CsvRow> r3;
    CHECK(csv_read("build/q.csv", 2, &r3, &err));
    CHECK_EQ_STR(r3[0].fields[0].c_str(), "a,b");
    CHECK_EQ_STR(r3[0].fields[1].c_str(), "he said \"hi\"");
}

/* ---------- money ---------- */

static void test_money(void) {
    printf("Testing money / fares...\n");
    CHECK_EQ_INT(seat_fare_paise(280, 220, false), 62000);          /* Rs 620 */
    CHECK_EQ_INT(seat_fare_paise(280, 220, true), 68200);
    CHECK_EQ_INT(seat_fare_paise(230, 260, false), 60000);          /* Rs 600 */
    CHECK_EQ_INT(seat_fare_paise(150, 180, false), 27000);          /* Rs 270 */
    CHECK_STR(fmt_rs(134460), "Rs 1,344.60");
    CHECK_STR(fmt_rs(123456789), "Rs 12,34,567.89");
    CHECK_STR(fmt_rs(5), "Rs 0.05");
    CHECK_EQ_INT(planned_drive_minutes(280, 50) + 40, 376);
    CHECK_EQ_INT(planned_drive_minutes(250, 42) + 20, 377);
    CHECK_EQ_INT(planned_drive_minutes(230, 60) + 20, 250);
    CHECK_EQ_INT(planned_drive_minutes(150, 55) + 20, 184);
    CHECK_EQ_INT(planned_drive_minutes(185, 45) + 35, 282);
    CHECK_EQ_INT(planned_drive_minutes(145, 55) + 15, 173);
    CHECK_EQ_INT(planned_drive_minutes(345, 55) + 35, 411);
    CHECK_EQ_INT(planned_drive_minutes(460, 55) + 40, 542);
    CHECK_EQ_INT(deposit_paise(134460), 26892);
    CHECK_EQ_INT(refund_percent(100 * MS_HOUR), 100);
    CHECK_EQ_INT(refund_percent(72 * MS_HOUR), 100);
    CHECK_EQ_INT(refund_percent(72 * MS_HOUR - 1), 80);
    CHECK_EQ_INT(refund_percent(24 * MS_HOUR), 80);
    CHECK_EQ_INT(refund_percent(24 * MS_HOUR - 1), 50);
    CHECK_EQ_INT(refund_percent(6 * MS_HOUR), 50);
    CHECK_EQ_INT(refund_percent(6 * MS_HOUR - 1), 0);
    CHECK_EQ_INT(refund_paise(134460, 134460, 3000, 0, 100), 131460);   /* insurance excluded */
    CHECK_EQ_INT(refund_paise(134460, 134460, 3000, 0, 80), 105168);
    CHECK_EQ_INT(refund_paise(134460, 134460, 3000, 0, 50), 65730);
    CHECK_EQ_INT(refund_paise(134460, 134460, 3000, 0, 0), 0);
    CHECK_EQ_INT(refund_paise(26892, 134460, 3000, 0, 100), 26892);      /* deposit only: refund what was paid */
}

/* ---------- booking, coupons, status, refunds ---------- */

static void test_booking(void) {
    printf("Testing booking rules...\n");
    auto a = make_app();
    int user = 1;
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 5);
    const char *seats[2] = {"L1", "U1"};
    Result<Quote> q = a->quote(t->id, seats, 2, "SMART50", true, user);
    CHECK(q.ok);
    CHECK_EQ_INT(q.value.seat_price[0], 68200); CHECK_EQ_INT(q.value.seat_price[1], 62000);
    CHECK_EQ_INT(q.value.base, 130200); CHECK_EQ_INT(q.value.discount, 5000); CHECK_EQ_INT(q.value.tax, 6260);
    CHECK_EQ_INT(q.value.insurance, 3000); CHECK_EQ_INT(q.value.total, 134460);
    CHECK_EQ_INT(q.value.deposit, 26892); CHECK_EQ_INT(q.value.balance, 107568);
    CHECK_EQ_INT(a->quote(t->id, seats, 2, "SMART50", false, user).value.total, 131460);   /* no insurance */
    /* Other quote vectors */
    Trip *agra = trip_on(*a, "DEMO-002", "delhi-agra", 5);
    const char *s1[1] = {"1A"};
    CHECK_EQ_INT(a->quote(agra->id, s1, 1, "", false, user).value.base, 60000);
    CHECK(!a->quote(agra->id, s1, 1, "NOPE", false, user).ok);
    const char *bad[1] = {"99Z"};
    CHECK(!a->quote(agra->id, bad, 1, "", false, user).ok);
    /* coupons: EXPRESS10 = 10% capped at Rs 150, premium types only, min base Rs 800 */
    CHECK(!a->quote(agra->id, s1, 1, "EXPRESS10", false, user).ok);       /* Rs 600 < Rs 800 minimum */
    const char *s2[2] = {"1A", "1B"};
    Result<Quote> ex = a->quote(agra->id, s2, 2, "express10", false, user);
    CHECK(ex.ok); CHECK_EQ_INT(ex.value.discount, 12000);                 /* 10% of 120000 */
    Trip *budget = trip_on(*a, "DEMO-019", "delhi-dehradun", 5);
    CHECK(!a->quote(budget->id, s2, 2, "EXPRESS10", false, user).ok);     /* Budget Seater is not premium */
    /* tax and discount applied exactly once: total = base - discount + tax + insurance */
    CHECK_EQ_INT(ex.value.total, ex.value.base - ex.value.discount + ex.value.tax + ex.value.insurance);

    /* hold -> book -> pay -> confirmed */
    int bid = book_full(*a, user, t, {"L1", "U1"}, "SMART50", true);
    CHECK(bid > 0);
    Booking *b = a->booking_by_id(bid);
    CHECK_EQ_INT(b->status, BK_CONFIRMED); CHECK_EQ_INT(b->pay_status, PAY_PAID); CHECK_EQ_INT(b->paid, 134460); CHECK(b->demo_payment == 1);
    CHECK(t->booked.test(0)); CHECK_EQ_INT(a->active_holds(t->id, user), 0);
    CHECK_EQ_INT(std::strlen(b->pnr), 10);
    CHECK(a->booking_by_pnr(b->pnr, user) != nullptr);
    CHECK(a->booking_by_pnr(b->pnr, 2) == nullptr);                        /* other users cannot read it */
    CHECK(!a->pay_booking(bid, user, PAYK_FULL, OUT_SUCCESS).ok);          /* double submit ignored */
    CHECK_EQ_INT(a->pay_booking(bid, user, PAYK_FULL, OUT_SUCCESS).code == Err::CONFLICT, 1);
    /* SMART50 is once per user */
    const char *again[1] = {"L2"};
    CHECK(!a->quote(t->id, again, 1, "SMART50", false, user).ok);
    /* seat taken */
    Result<Unit> taken = a->hold_seats(2, t->id, seats, 1);
    CHECK(!taken.ok && taken.code == Err::SEAT_TAKEN);

    /* refund table (preview) */
    int64_t dep = t->depart_ms;
    a->clock.set(dep - 100 * MS_HOUR); CHECK_EQ_INT(a->cancel_preview(*b).refund, 131460);
    a->clock.set(dep - 48 * MS_HOUR);  CHECK_EQ_INT(a->cancel_preview(*b).refund, 105168);
    a->clock.set(dep - 10 * MS_HOUR);  CHECK_EQ_INT(a->cancel_preview(*b).refund, 65730);
    a->clock.set(dep - 2 * MS_HOUR);   CHECK_EQ_INT(a->cancel_preview(*b).refund, 0);
    a->clock.set(dep - 100 * MS_HOUR);
    CHECK(!a->cancel_booking(bid, 2).ok);                                   /* not the owner */
    Result<int64_t> c = a->cancel_booking(bid, user);
    CHECK(c.ok); CHECK_EQ_INT(c.value, 131460);
    CHECK_EQ_INT(b->status, BK_CANCELLED); CHECK_EQ_INT(b->pay_status, PAY_PARTIALLY_REFUNDED);
    CHECK(!t->booked.test(0));                                              /* seats freed */
    CHECK(!a->cancel_booking(bid, user).ok);                                /* terminal state */
    CHECK_EQ_INT(a->revenue_range(1, 0, 119), 134460 - 131460);             /* Fenwick: paid minus refunded */

    /* status machine */
    CHECK(a->legal_transition(BK_PENDING_PAYMENT, BK_CONFIRMED)); CHECK(a->legal_transition(BK_PENDING_PAYMENT, BK_PREBOOKED));
    CHECK(a->legal_transition(BK_PREBOOKED, BK_EXPIRED)); CHECK(a->legal_transition(BK_CONFIRMED, BK_COMPLETED));
    CHECK(!a->legal_transition(BK_CONFIRMED, BK_PREBOOKED)); CHECK(!a->legal_transition(BK_CANCELLED, BK_CONFIRMED));
    CHECK(!a->legal_transition(BK_EXPIRED, BK_PREBOOKED)); CHECK(!a->legal_transition(BK_COMPLETED, BK_CANCELLED));
    CHECK(!a->legal_transition(BK_PREBOOKED, BK_COMPLETED)); CHECK(!a->legal_transition(BK_PENDING_PAYMENT, BK_COMPLETED));
}

static void test_prebook(void) {
    printf("Testing pre-booking edges and deposit flow...\n");
    auto a = make_app();
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 30);
    const char *seats[1] = {"U3"};
    int64_t dep = t->depart_ms;
    a->clock.set(dep - (72 * MS_HOUR - MS_MIN));  CHECK(!a->quote(t->id, seats, 1, "", false, 1).value.prebook_eligible);   /* 71h59m */
    a->clock.set(dep - 72 * MS_HOUR);             CHECK(a->quote(t->id, seats, 1, "", false, 1).value.prebook_eligible);
    a->clock.set(dep - 2160 * MS_HOUR);           CHECK(a->quote(t->id, seats, 1, "", false, 1).value.prebook_eligible);
    a->clock.set(dep - (2160 * MS_HOUR + MS_MIN)); CHECK(!a->quote(t->id, seats, 1, "", false, 1).value.prebook_eligible);  /* 2160h01m */
    a->clock.set(dep - 10 * MS_DAY);
    CHECK_EQ_INT(a->quote(t->id, seats, 1, "", false, 1).value.balance_due_ms, dep - 48 * MS_HOUR);
    int bid = book_full(*a, 1, t, {"U3"}, "", false, MODE_DEPOSIT);
    CHECK(bid > 0);
    Booking *b = a->booking_by_id(bid);
    CHECK_EQ_INT(b->status, BK_PREBOOKED); CHECK_EQ_INT(b->pay_status, PAY_PARTIAL);
    CHECK_EQ_INT(b->paid, deposit_paise(b->total));
    CHECK(a->pay_booking(bid, 1, PAYK_BALANCE, OUT_DECLINED).code == Err::PAYMENT_DECLINED);   /* retry allowed */
    CHECK(a->pay_booking(bid, 1, PAYK_BALANCE, OUT_SUCCESS).ok);
    CHECK_EQ_INT(b->status, BK_CONFIRMED); CHECK_EQ_INT(b->paid, b->total);
    /* a second deposit booking left unpaid expires at the deadline */
    int b2 = book_full(*a, 1, t, {"U4"}, "", false, MODE_DEPOSIT);
    Booking *bk2 = a->booking_by_id(b2);
    CHECK_EQ_INT(bk2->status, BK_PREBOOKED);
    a->advance_clock(dep - 48 * MS_HOUR - a->clock.now() + MS_MIN);        /* just past the balance deadline */
    CHECK_EQ_INT(bk2->status, BK_EXPIRED);
    CHECK(!t->booked.test(a->seat_index(a->types[(size_t)a->buses[(size_t)t->bus_idx].type_idx], "U4")));   /* seat freed */
    CHECK(bk2->refunded > 0);                                              /* refund per cancellation table */
    /* deposit not offered close to departure */
    Trip *soon = trip_on(*a, "DEMO-002", "delhi-agra", 12);
    a->clock.set(soon->depart_ms - 10 * MS_HOUR);
    CHECK(a->hold_seats(1, soon->id, seats, 0).code == Err::VALIDATION);
    const char *s1[1] = {"1A"};
    CHECK(a->hold_seats(1, soon->id, s1, 1).ok);
    BookingRequest rq; rq.user_id = 1; rq.trip_id = soon->id; rq.n_pax = 1; rq.mode = MODE_DEPOSIT;
    std::snprintf(rq.pax[0].seat, 6, "1A"); std::snprintf(rq.pax[0].name, 32, "X"); rq.pax[0].age = 20; rq.pax[0].gender = 'F';
    rq.boarding_stop = a->stop_index(a->routes[(size_t)soon->route_id], ST_BOARDING, 0);
    rq.dropping_stop = a->stop_index(a->routes[(size_t)soon->route_id], ST_DROPPING, 0);
    std::snprintf(rq.contact, 48, "x@y.z");
    CHECK(a->create_booking(rq).code == Err::NOT_ELIGIBLE);
}

/* ---------- holds, waitlist, seats ---------- */

static void test_holds_waitlist(void) {
    printf("Testing holds, waitlist, group seats...\n");
    auto a = make_app();
    a->register_passenger("Second User", "second@example.com", "", "Passw0rdX");
    a->register_passenger("Third User", "third@example.com", "", "Passw0rdY");
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 3);
    const char *s[1] = {"L1"};
    CHECK(a->hold_seats(1, t->id, s, 1).ok);
    CHECK_EQ_INT(a->active_holds(t->id, 1), 1);
    CHECK(t->held.test(0));
    CHECK(!a->hold_seats(2, t->id, s, 1).ok);                               /* held by user 1 */
    a->advance_clock(10 * MS_MIN - 1000);
    CHECK(t->held.test(0));                                                 /* not yet expired */
    a->advance_clock(2000);
    CHECK(!t->held.test(0)); CHECK_EQ_INT(a->active_holds(t->id, 1), 0);    /* freed by the heap event */
    CHECK_EQ_INT(a->unread_count(1), 1);                                    /* "Hold expired" alert */
    /* max 6 seats */
    const char *seven[7] = {"L1", "L2", "L3", "L4", "L5", "L6", "L7"};
    CHECK(a->hold_seats(1, t->id, seven, 7).code == Err::VALIDATION);
    /* re-hold replaces */
    const char *l2[1] = {"L2"};
    a->hold_seats(1, t->id, s, 1); a->hold_seats(1, t->id, l2, 1);
    CHECK(!t->held.test(0)); CHECK(t->held.test(1));
    a->release_user_holds(t->id, 1);
    /* group seats: sleeper rows are 3 wide */
    CHECK_EQ_INT(a->find_adjacent(*t, 3), 0);
    t->booked.set(1);
    CHECK_EQ_INT(a->find_adjacent(*t, 2), 3);                               /* row 0 has only seat 0 free; next row starts at 3 */
    CHECK_EQ_INT(a->find_adjacent(*t, 4), -1);                              /* no row is 4 wide */
    t->booked.clear(1);
    /* waitlist: sold out -> queue -> head gets a 10-minute claim window, then the next in line */
    Trip *w = trip_on(*a, "DEMO-005", "jaipur-udaipur", 3);
    for (int i = 1; i < 30; i++) w->booked.set((size_t)i);                  /* only seat 0 (L1) left */
    const char *l1[1] = {"L1"};
    CHECK(a->hold_seats(3, w->id, l1, 1).ok);                               /* now sold out (held) */
    CHECK(a->join_waitlist(1, w->id, 1).ok);
    CHECK(a->join_waitlist(2, w->id, 1).ok);
    CHECK(!a->join_waitlist(2, trip_on(*a, "DEMO-001", "delhi-jaipur", 3)->id, 1).ok);   /* seats available: no waitlist */
    a->advance_clock(11 * MS_MIN);                                          /* user 3's hold expires */
    CHECK_EQ_INT(a->active_holds(w->id, 1), 1);                             /* head of queue (user 1) holds the seat */
    CHECK_EQ_INT(a->active_holds(w->id, 2), 0);
    a->advance_clock(11 * MS_MIN);                                          /* user 1 never claims: next in line */
    CHECK_EQ_INT(a->active_holds(w->id, 1), 0);
    CHECK_EQ_INT(a->active_holds(w->id, 2), 1);
    CHECK(a->unread_count(2) >= 1);
}

/* ---------- planner ---------- */

static void test_planner(void) {
    printf("Testing journey planner...\n");
    auto a = make_app();
    Vec<PlanLeg> legs; int64_t mins = 0, paise = 0;
    Result<Unit> r = a->plan_journey(a->find_city("Delhi"), a->find_city("Ahmedabad"), PLAN_FASTEST, legs, &mins, &paise);
    CHECK(r.ok); CHECK_EQ_INT(legs.size(), 3);
    CHECK_EQ_INT(mins, 376 + 471 + 332 + 120);                              /* 1299 minutes with 2 layovers */
    CHECK_EQ_STR(a->cities[(size_t)legs[0].to_city].name, "Jaipur");
    CHECK_EQ_STR(a->cities[(size_t)legs[1].to_city].name, "Udaipur");
    r = a->plan_journey(a->find_city("Mysuru"), a->find_city("Madurai"), PLAN_FASTEST, legs, &mins, &paise);
    CHECK(r.ok); CHECK_EQ_INT(legs.size(), 3); CHECK_EQ_INT(mins, 173 + 411 + 542 + 120);   /* 1246 */
    CHECK_EQ_STR(a->cities[(size_t)legs[0].to_city].name, "Bengaluru");
    CHECK_EQ_STR(a->cities[(size_t)legs[1].to_city].name, "Chennai");
    r = a->plan_journey(a->find_city("Delhi"), a->find_city("Goa"), PLAN_FASTEST, legs, &mins, &paise);
    CHECK(!r.ok); CHECK_EQ_STR(r.message.c_str(), "No route");
    r = a->plan_journey(a->find_city("Delhi"), a->find_city("Delhi"), PLAN_FASTEST, legs, &mins, &paise);
    CHECK(!r.ok);
    r = a->plan_journey(a->find_city("Delhi"), a->find_city("Ahmedabad"), PLAN_CHEAPEST, legs, &mins, &paise);
    CHECK(r.ok); CHECK(paise > 0);
    r = a->plan_journey(a->find_city("Delhi"), a->find_city("Ahmedabad"), PLAN_FEWEST, legs, &mins, &paise);
    CHECK(r.ok); CHECK_EQ_INT(legs.size(), 3);
    r = a->plan_journey(a->find_city("Delhi"), a->find_city("Agra"), PLAN_FASTEST, legs, &mins, &paise);
    CHECK(r.ok); CHECK_EQ_INT(legs.size(), 1); CHECK_EQ_INT(mins, 250);     /* single leg: no layover */
}

/* ---------- search ---------- */

static void test_search(void) {
    printf("Testing search, filters, sorting...\n");
    auto a = make_app();
    int from = a->find_city("Delhi"), to = a->find_city("Jaipur");
    int dk = date_key(a->clock.now() + 2 * MS_DAY);
    Vec<SearchRow> rows;
    a->search(from, to, dk, SearchFilters(), SORT_EARLIEST, rows);
    CHECK_EQ_INT(rows.size(), 2);                                           /* DEMO-001 and DEMO-004 (ON_TRIP still bookable); DEMO-007 hidden (maintenance) */
    bool sorted = true;
    for (size_t i = 1; i < rows.size(); i++) if (rows[i - 1].depart_ms > rows[i].depart_ms) sorted = false;
    CHECK(sorted);
    for (size_t i = 0; i < rows.size(); i++) CHECK(std::string(a->buses[(size_t)rows[i].bus_idx].reg) != "DEMO-007");
    CHECK_EQ_INT(rows[0].duration_min, 376);
    SearchFilters f;
    f.max_fare = 62000;
    a->search(from, to, dk, f, SORT_CHEAPEST, rows);
    for (size_t i = 0; i < rows.size(); i++) CHECK(rows[i].fare_min <= 62000);
    SearchFilters ops; std::snprintf(ops.operator_substr, sizeof ops.operator_substr, "orbit");
    a->search(from, to, dk, ops, SORT_EARLIEST, rows);
    CHECK_EQ_INT(rows.size(), 1);
    SearchFilters win; win.dep_window = 3;                                  /* evening 18-24 */
    a->search(from, to, dk, win, SORT_EARLIEST, rows);
    for (size_t i = 0; i < rows.size(); i++) CHECK(date_key(rows[i].depart_ms) == dk);
    a->search(a->find_city("Delhi"), a->find_city("Goa"), dk, SearchFilters(), SORT_EARLIEST, rows);
    CHECK_EQ_INT(rows.size(), 0);
    /* maintenance hides the bus; after sales close the trip disappears */
    a->search(from, to, dk, SearchFilters(), SORT_EARLIEST, rows);
    int64_t first_dep = rows[0].depart_ms;
    a->clock.set(first_dep - 10 * MS_MIN);
    a->search(from, to, dk, SearchFilters(), SORT_EARLIEST, rows);
    for (size_t i = 0; i < rows.size(); i++) CHECK(rows[i].depart_ms != first_dep);
}

/* ---------- tracking ---------- */

static void test_tracking(void) {
    printf("Testing tracking, ETA, alerts...\n");
    auto a = make_app();
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 5);
    int bid = book_full(*a, 1, t, {"L5"}, "", false);
    CHECK(bid > 0);
    a->flush_outbox();
    a->mark_all_read(1);
    TrackInfo ti = a->track_info(*t);
    CHECK_EQ_INT(ti.delay_min, 0);
    CHECK_EQ_INT(ti.est_arrive, t->arrive_ms);                             /* ETA == schedule before departure */
    a->sim_jam(*t);
    ti = a->track_info(*t);
    CHECK_EQ_INT(ti.delay_min, 14);                                         /* 5 km at 15 km/h instead of 50: +14 min */
    CHECK_EQ_INT(ti.est_arrive, t->arrive_ms + 14 * MS_MIN);
    /* reported delay never stacks on observed delay */
    CHECK(a->report_delay(1, t->id, "TRAFFIC", 10, "").ok);
    CHECK_EQ_INT(a->track_info(*t).delay_min, 14);                         /* max(14, 10), not 24 */
    CHECK(a->report_delay(1, t->id, "TRAFFIC", 30, "").ok);
    CHECK_EQ_INT(a->track_info(*t).delay_min, 30);                         /* replaced, not added */
    CHECK(!a->report_delay(2, t->id, "TRAFFIC", 10, "").ok);               /* another company: not found */
    CHECK(a->report_delay(2, t->id, "TRAFFIC", 10, "").code == Err::NOT_FOUND);
    CHECK(!a->report_delay(1, t->id, "NOPE", 10, "").ok);
    CHECK(!a->report_delay(1, t->id, "OTHER", 601, "").ok);
    CHECK(!a->report_delay(1, t->id, "OTHER", 0, "").ok);
    a->sim_reset(*t);
    t->delay_min = 0;
    /* simulator: advance stops at the first break (Manesar km 45), waits 15 min, never stops at tolls */
    a->sim_advance_km(*t, 100);
    CHECK_NEAR(t->km, 45.0, 1e-6);                                          /* stopped at the break */
    CHECK_EQ_INT(t->break_left_sec, 15 * 60); CHECK_EQ_INT(t->breaks_done, 1);
    CHECK(a->track_info(*t).on_break);
    a->sim_advance(*t, 15 * 60);                                            /* wait out the break */
    CHECK_EQ_INT(t->break_left_sec, 0);
    a->sim_advance(*t, 3600);                                               /* 1 hour at 50 km/h */
    CHECK_NEAR(t->km, 95.0, 1e-3);
    CHECK_EQ_INT(t->status, TRIP_RUNNING);
    TrackInfo mid = a->track_info(*t);
    CHECK_EQ_INT(mid.delay_min, 0);                                         /* breaks are part of the plan: still on time */
    CHECK(ds_ring_count(t->gps) > 0);
    CHECK(mid.speed_kmh > 0);
    a->sim_advance(*t, 20 * 3600);                                          /* run to the end */
    CHECK_NEAR(t->km, 280.0, 1e-6);
    CHECK_EQ_INT(t->status, TRIP_COMPLETED);
    CHECK_EQ_INT(a->booking_by_id(bid)->status, BK_COMPLETED);
    CHECK_EQ_INT(a->buses[(size_t)t->bus_idx].status, BUS_AVAILABLE);
    CHECK_NEAR(a->track_info(*t).progress_pct, 100.0, 1e-6);
}

static void test_alert_crossing(void) {
    printf("Testing crossing-based alerts...\n");
    auto a = make_app();
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 5);
    int bid = book_full(*a, 1, t, {"L6"}, "", false);
    a->flush_outbox(); a->mark_all_read(1);
    size_t before = a->notes.size();
    a->fire_alerts(*t, 0.0, 30.0);                                          /* one 30 km jump */
    a->flush_outbox();
    std::vector<std::string> titles;
    for (size_t i = before; i < a->notes.size(); i++) titles.push_back(a->notes[i].title);
    const char *expect[] = {"[Demo] Boarding point ahead", "[Demo] Bus departed", "[Demo] Boarding point reached",
                            "[Demo] Toll ahead", "[Demo] Break ahead", "[Demo] Toll crossed"};
    CHECK_EQ_INT(titles.size(), 6);                                          /* every skipped event fires exactly once ... */
    for (size_t i = 0; i < titles.size() && i < 6; i++) CHECK_EQ_STR(titles[i].c_str(), expect[i]);   /* ... in km order */
    size_t after = a->notes.size();
    a->fire_alerts(*t, 0.0, 30.0);                                           /* replay: dedupe suppresses all */
    a->flush_outbox();
    CHECK_EQ_INT(a->notes.size(), after);
    a->fire_alerts(*t, 30.0, 50.0);                                          /* crosses break at 45: stopped + resumed */
    a->flush_outbox();
    bool stopped = false, resumed = false, drop = false;
    for (size_t i = after; i < a->notes.size(); i++) {
        stopped |= std::string(a->notes[i].title) == "[Demo] Bus stopped";
        resumed |= std::string(a->notes[i].title) == "[Demo] Bus resumed";
    }
    CHECK(stopped); CHECK(resumed);
    /* own drop-off only: the booking's dropping stop alone fires */
    size_t n2 = a->notes.size();
    a->fire_alerts(*t, 50.0, 280.0);
    a->flush_outbox();
    int drop_alerts = 0;
    for (size_t i = n2; i < a->notes.size(); i++) {
        std::string tt = a->notes[i].title;
        if (tt == "[Demo] Your drop-off is ahead" || tt == "[Demo] Drop-off reached") drop_alerts++;
        drop |= tt == "[Demo] Trip completed";
    }
    CHECK_EQ_INT(drop_alerts, 2);                                            /* ahead + reached for ONE stop, not both dropping points */
    CHECK(drop);
    (void)bid;
    /* mute a category */
    a->set_prefs(1, ((1 << NK_COUNT) - 1) & ~(1 << NK_TOLLS));
    Trip *t2 = trip_on(*a, "DEMO-002", "delhi-agra", 6);
    book_full(*a, 1, t2, {"1A"}, "", false);
    size_t n3 = a->notes.size();
    a->fire_alerts(*t2, 0.0, 60.0);
    a->flush_outbox();
    bool toll = false;
    for (size_t i = n3; i < a->notes.size(); i++) toll |= std::string(a->notes[i].title).find("Toll") != std::string::npos;
    CHECK(!toll);
}

/* ---------- auth + vendor ---------- */

static void test_auth_vendor(void) {
    printf("Testing auth and vendor rules...\n");
    auto a = make_app();
    CHECK(a->login("user@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);
    CHECK(a->login("USER@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);   /* case-insensitive email */
    Result<int> wrong_role = a->login("user@smartroute.in", "Demo@1234", ROLE_VENDOR);
    CHECK(!wrong_role.ok); CHECK_EQ_STR(wrong_role.message.c_str(), "Invalid credentials");
    Result<int> wrong_role2 = a->login("vendor@smartroute.in", "Demo@1234", ROLE_PASSENGER);
    CHECK_EQ_STR(wrong_role2.message.c_str(), "Invalid credentials");
    Result<int> nobody = a->login("nobody@x.in", "whatever1", ROLE_PASSENGER);
    CHECK_EQ_STR(nobody.message.c_str(), "Invalid credentials");
    CHECK(a->login("vendor@smartroute.in", "Demo@1234", ROLE_VENDOR).ok);
    CHECK(a->login("vendor.kaveri-express@smartroute.in", "Demo@1234", ROLE_VENDOR).ok);
    for (int i = 0; i < 5; i++) a->login("user@smartroute.in", "bad", ROLE_PASSENGER);
    CHECK(!a->login("user@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);   /* locked after 5 failures */
    CHECK(!a->register_passenger("A", "bad-email", "", "Passw0rd1").ok);
    CHECK(!a->register_passenger("A", "a@b.co", "", "short").ok);
    CHECK(!a->register_passenger("A", "a@b.co", "", "allletters").ok);
    CHECK(a->register_passenger("Neha", "neha@b.co", "", "Passw0rd1").ok);
    CHECK(a->register_passenger("Neha", "neha@b.co", "", "Passw0rd1").code == Err::CONFLICT);
    CHECK(a->login("neha@b.co", "Passw0rd1", ROLE_PASSENGER).ok);

    int orbit = 1;
    int bus3 = a->find_bus("DEMO-003"), bus4 = a->find_bus("DEMO-004");
    CHECK(a->assign_staff(orbit, bus3, 3).code == Err::CONFLICT);             /* driver #3 is off duty */
    CHECK_EQ_INT(a->staff_status(a->staff[2]), SS_OFF_DUTY);
    CHECK_EQ_INT(a->staff_status(a->staff[0]), SS_ASSIGNED);
    CHECK_EQ_INT(a->staff_status(a->staff[5]), SS_AVAILABLE);
    CHECK(a->assign_staff(orbit, bus3, 1).code == Err::CONFLICT);             /* already on DEMO-001 */
    CHECK(a->assign_staff(orbit, bus4, 6).code == Err::NOT_FOUND);            /* another company's bus */
    CHECK(a->assign_staff(2, a->find_bus("DEMO-005"), 6).code == Err::NOT_FOUND);   /* another company's staff */
    CHECK(a->assign_staff(orbit, bus3, 6).ok);
    CHECK_EQ_INT(a->buses[(size_t)bus3].conductor_id, 6);
    CHECK_EQ_INT(a->staff_status(a->staff[5]), SS_ASSIGNED);
    CHECK(a->undo_last(orbit).ok);
    CHECK_EQ_INT(a->buses[(size_t)bus3].conductor_id, -1);
    CHECK_EQ_INT(a->staff_status(a->staff[5]), SS_AVAILABLE);
    CHECK(!a->undo_last(orbit).ok);                                           /* nothing left */
    CHECK(a->set_bus_status(orbit, bus3, BUS_MAINTENANCE, false, nullptr).ok);
    CHECK_EQ_INT(a->buses[(size_t)bus3].status, BUS_MAINTENANCE);
    CHECK(a->set_bus_status(orbit, bus4, BUS_AVAILABLE, false, nullptr).code == Err::NOT_FOUND);
    CHECK(a->undo_last(orbit).ok);
    CHECK_EQ_INT(a->buses[(size_t)bus3].status, BUS_AVAILABLE);
    /* maintenance with bookings in the next 24 h needs confirmation */
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 0);
    a->clock.set(t->depart_ms - 5 * MS_HOUR);
    int bid = book_full(*a, 1, t, {"U9"}, "", false);
    CHECK(bid > 0);
    int affected = 0;
    Result<Unit> need = a->set_bus_status(orbit, a->find_bus("DEMO-001"), BUS_MAINTENANCE, false, &affected);
    CHECK(!need.ok); CHECK_EQ_INT(affected, 1);
    CHECK(a->set_bus_status(orbit, a->find_bus("DEMO-001"), BUS_MAINTENANCE, true, &affected).ok);
    /* trip lifecycle */
    Trip *t2 = trip_on(*a, "DEMO-002", "delhi-agra", 2);
    CHECK(!a->complete_trip(orbit, t2->id).ok);
    CHECK(a->start_trip(orbit, t2->id).ok);
    CHECK_EQ_INT(a->buses[(size_t)t2->bus_idx].status, BUS_ON_TRIP);
    CHECK(!a->start_trip(orbit, t2->id).ok);
    CHECK(a->complete_trip(2, t2->id).code == Err::NOT_FOUND);
    CHECK(a->complete_trip(orbit, t2->id).ok);
    CHECK_EQ_INT(a->buses[(size_t)t2->bus_idx].status, BUS_AVAILABLE);
    /* auto-assign never uses off-duty staff and never overlaps */
    std::string report;
    int n = a->auto_assign(orbit, date_key(a->clock.now() + MS_DAY), &report);
    CHECK(n > 0);
    for (size_t i = 0; i < a->trips.size(); i++) CHECK(a->trips[i]->auto_driver != 3);
    CHECK_STR(a->mask_phone("+91 00000 01101", false), "XXXXXX1101");
    CHECK_STR(a->mask_phone("+91 00000 01101", true), "+91 00000 01101");
}

/* ---------- persistence ---------- */

static void test_persistence(void) {
    printf("Testing save/load round trip...\n");
    make_dirs("build/test_save");
    clear_save_dir("build/test_save");
    auto a = make_app();
    a->save_dir = "build/test_save";
    a->register_passenger("Round Trip", "rt@example.com", "9999", "Passw0rdZ");
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 20);
    int bid = book_full(*a, 1, t, {"L1", "U1"}, "SMART50", true, MODE_DEPOSIT);
    const char *held[1] = {"L9"};
    a->hold_seats(2, t->id, held, 1);
    a->clock.advance(MS_MIN);
    a->flush_outbox();
    std::string err;
    CHECK(a->save_all(&err));
    auto b = make_app();
    b->save_dir = "build/test_save";
    std::string msgs;
    CHECK(b->load_saved(&msgs));
    CHECK_EQ_INT(b->users.size(), a->users.size());
    CHECK_EQ_INT(b->bookings.size(), a->bookings.size());
    CHECK_EQ_INT(b->trips.size(), a->trips.size());
    CHECK_EQ_INT(b->holds.size(), a->holds.size());
    CHECK_EQ_INT(b->notes.size(), a->notes.size());
    CHECK_EQ_INT(b->clock.now(), a->clock.now());
    Booking *x = a->booking_by_id(bid), *y = b->booking_by_id(bid);
    CHECK(y != nullptr);
    CHECK_EQ_STR(y->pnr, x->pnr); CHECK_EQ_INT(y->total, x->total); CHECK_EQ_INT(y->paid, x->paid); CHECK_EQ_INT(y->status, x->status);
    CHECK_EQ_INT(y->n_pax, 2); CHECK_EQ_STR(y->pax[1].seat, "U1"); CHECK_EQ_INT(y->pax[0].price, 68200); CHECK_EQ_INT(y->balance_due_ms, x->balance_due_ms);
    CHECK(b->trip_by_id(t->id)->booked.hex() == t->booked.hex());
    CHECK(b->trip_by_id(t->id)->held.hex() == t->held.hex());
    CHECK_EQ_INT(b->events.size(), a->events.size());                          /* timers rebuilt */
    CHECK(b->login("rt@example.com", "Passw0rdZ", ROLE_PASSENGER).ok);          /* passwords survive restart */
    CHECK(b->booking_by_pnr(y->pnr, 1) != nullptr);
    CHECK_EQ_INT(b->revenue_range(1, 0, 119), a->revenue_range(1, 0, 119));
    CHECK(!b->pay_booking(bid, 1, PAYK_DEPOSIT, OUT_SUCCESS).ok);                 /* idempotency keys restored */
    /* corruption: flip a byte -> checksum mismatch -> falls back to .bak, or starts empty with a message */
    CHECK(a->save_all(&err));                                                    /* second save creates .bak */
    { std::fstream f("build/test_save/bookings.sav", std::ios::in | std::ios::out | std::ios::binary); f.seekp(-5, std::ios::end); f.put('#'); }
    auto c = make_app();
    c->save_dir = "build/test_save";
    std::string m2;
    c->load_saved(&m2);
    CHECK(m2.find("checksum") != std::string::npos);
    CHECK(m2.find("Recovered") != std::string::npos);
    CHECK_EQ_INT(c->bookings.size(), a->bookings.size());                        /* recovered from .bak */
}

/* ---------- roles, driver console, check-in, crew, coupons, share links, assistant ---------- */

static User make_user(int role, int company, int staff_id) { User u; std::memset(&u, 0, sizeof u); u.role = role; u.company_id = company; u.staff_id = staff_id; return u; }

static void test_roles_driver_ops(void) {
    printf("Testing roles, driver console, GPS, check-in...\n");
    auto a = make_app();
    CHECK(a->login("driver@smartroute.in", "Demo@1234", ROLE_DRIVER).ok);
    CHECK(a->login("admin@smartroute.in", "Demo@1234", ROLE_ADMIN).ok);
    CHECK(a->login("pilot@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);
    CHECK(!a->login("driver@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);        /* wrong portal = generic failure */
    CHECK(!a->login("admin@smartroute.in", "Demo@1234", ROLE_VENDOR).ok);
    CHECK(!a->login("vendor@smartroute.in", "Demo@1234", ROLE_ADMIN).ok);
    User driver = *a->user_by_id(a->user_id_by_email("driver@smartroute.in"));
    CHECK_EQ_INT(driver.staff_id, 1);
    std::vector<Trip *> duty = a->driver_trips(driver);
    CHECK(duty.size() >= 2);
    bool only_mine = true;
    for (Trip *t : duty) only_mine &= std::string(a->buses[(size_t)t->bus_idx].reg) == "DEMO-001";
    CHECK(only_mine);                                                                 /* driver #1 drives DEMO-001 only */
    bool sorted = true;
    for (size_t i = 1; i < duty.size(); i++) sorted &= duty[i - 1]->depart_ms <= duty[i]->depart_ms;
    CHECK(sorted);
    Trip *t = duty[0];
    const Route &r = a->routes[(size_t)t->route_id];
    CHECK(!a->driver_position(driver, t->id, 28.6, 77.2, 40, 90, 10).ok);              /* trip not started yet */
    User other = make_user(ROLE_DRIVER, 1, 2);                                        /* another driver of the same company */
    CHECK(a->driver_start(other, t->id).code == Err::NOT_FOUND);                      /* not on their duty list */
    CHECK(a->driver_start(driver, t->id).ok);
    CHECK_EQ_INT(t->status, TRIP_RUNNING);
    geo_point p100 = a->bus_position(r, 100.0);
    CHECK(a->driver_position(driver, t->id, 95.0, p100.lng, 40, 90, 10).code == Err::VALIDATION);   /* latitude out of range */
    CHECK(a->driver_position(driver, t->id, p100.lat, p100.lng, 200, 90, 10).code == Err::VALIDATION);   /* speed */
    CHECK(a->driver_position(driver, t->id, p100.lat, p100.lng, 40, 360, 10).code == Err::VALIDATION);   /* heading */
    CHECK(a->driver_position(driver, t->id, p100.lat, p100.lng, 40, 90, 6000).code == Err::VALIDATION);  /* accuracy */
    CHECK(a->driver_position(driver, t->id, 12.97, 77.59, 40, 90, 10).code == Err::VALIDATION);          /* Bengaluru is nowhere near */
    CHECK(a->gps_fixes.size() == 0);                                                  /* rejected fixes are never stored */
    CHECK(a->driver_position(driver, t->id, p100.lat, p100.lng, 52, 90, 10).ok);
    CHECK_NEAR(t->km, 100.0, 1.0);                                                    /* snapped to the nearest point on the route */
    CHECK_EQ_INT(a->gps_fixes.size(), 1);
    geo_point p50 = a->bus_position(r, 50.0);
    CHECK(a->driver_position(driver, t->id, p50.lat, p50.lng, 40, 90, 10).ok);
    CHECK_NEAR(t->km, 100.0, 1.0);                                                    /* the tracker never moves backwards */
    CHECK(ds_ring_count(t->gps) >= 2);
    CHECK(a->driver_delay(driver, t->id, "TRAFFIC", 20, "Flyover bottleneck").ok);
    CHECK_EQ_INT(a->delay_log.size(), 1);
    CHECK_EQ_STR(a->delay_log[0].source, "driver");
    CHECK(!a->driver_delay(driver, t->id, "NOPE", 20, "").ok);
    CHECK(!a->driver_delay(driver, t->id, "OTHER", 700, "").ok);
    CHECK(a->driver_end(driver, t->id).ok);
    CHECK_EQ_INT(t->status, TRIP_COMPLETED);

    /* boarding check-in */
    auto b = make_app();
    User drv = *b->user_by_id(b->user_id_by_email("driver@smartroute.in"));
    Trip *mine = b->driver_trips(drv)[0];
    int bid = 0;
    {   /* book 2 seats on the driver's own trip, paid in full */
        const char *seats[2] = {"U5", "U6"};
        b->clock.set(mine->depart_ms - 30 * MS_HOUR);
        bid = book_full(*b, 1, mine, {seats[0], seats[1]}, "", false);
    }
    CHECK(bid > 0);
    Booking *bk = b->booking_by_id(bid);
    std::string before = b->qr_payload(*bk);
    CHECK_STR(b->boarding_state(*bk), "VALID");
    bool already = true;
    Result<int> sc = b->driver_scan(drv, before, &already);
    CHECK(sc.ok); CHECK(!already);
    CHECK_STR(b->boarding_state(*bk), "BOARDED");
    CHECK(b->qr_payload(*bk) != before);                                              /* the code changes once boarded */
    Result<int> again = b->driver_scan(drv, std::string(bk->pnr), &already);          /* typing the bare reference works too */
    CHECK(again.ok); CHECK(already);
    CHECK(b->unread_count(1) >= 1);                                                   /* passenger is told once */
    CHECK_EQ_INT(b->driver_scan(drv, "SR|ZZZZZZZZ|1|0", &already).code == Err::NOT_FOUND, 1);
    CHECK_EQ_INT(b->driver_scan(drv, "abc", &already).code == Err::VALIDATION, 1);
    Trip *far = trip_on(*b, "DEMO-005", "jaipur-udaipur", 3);                         /* a different company's trip */
    int other_bid = book_full(*b, 1, far, {"1A"}, "", false);
    if (other_bid == 0) other_bid = book_full(*b, 1, far, {"L3"}, "", false);
    CHECK(other_bid > 0);
    CHECK_EQ_INT(b->driver_scan(drv, b->booking_by_id(other_bid)->pnr, &already).code == Err::NOT_FOUND, 1);   /* not your trip */
    User admin = *b->user_by_id(b->user_id_by_email("admin@smartroute.in"));
    CHECK(b->driver_scan(admin, b->booking_by_id(other_bid)->pnr, &already).ok);                               /* admin may scan any */
    Trip *pre = trip_on(*b, "DEMO-001", "delhi-jaipur", 40);
    b->clock.set(pre->depart_ms - 20 * MS_DAY);
    int pre_id = book_full(*b, 1, pre, {"U9"}, "", false, MODE_DEPOSIT);
    CHECK(pre_id > 0);
    Result<int> scp = b->driver_scan(admin, b->booking_by_id(pre_id)->pnr, &already);
    CHECK(!scp.ok && scp.code == Err::BAD_STATE);                                     /* prebooked = balance unpaid */
    b->cancel_booking(pre_id, 1);
    CHECK_STR(b->boarding_state(*b->booking_by_id(pre_id)), "CANCELLED");
}

static void test_crew_coupons_share(void) {
    printf("Testing trip crew, off-duty rules, coupons, share links, traffic scan, corridor revenue...\n");
    auto a = make_app();
    int orbit = 1;
    /* overlap detection: DEMO-001 runs jaipur->delhi 20:00 and delhi->jaipur 23:00 on the same date */
    int dk = date_key(a->clock.now() + 2 * MS_DAY);
    Trip *rev = a->get_or_create_trip(sched_of(*a, "DEMO-001", "jaipur-delhi"), dk);
    Trip *fwd = a->get_or_create_trip(sched_of(*a, "DEMO-001", "delhi-jaipur"), dk);
    CHECK(rev->depart_ms < fwd->depart_ms && fwd->depart_ms < rev->arrive_ms);        /* the two runs overlap in time */
    CHECK(a->assign_trip_crew(orbit, rev->id, 2, -1).ok);                             /* driver #2 takes the first run */
    CHECK_EQ_INT(a->trip_driver(*rev), 2);
    Result<Unit> clash = a->assign_trip_crew(orbit, fwd->id, 2, -1);
    CHECK(!clash.ok && clash.code == Err::CONFLICT);                                  /* ... but cannot be on both at once */
    CHECK(clash.message.find("overlaps") != std::string::npos);
    CHECK(a->assign_trip_crew(orbit, fwd->id, 3, -1).code == Err::CONFLICT);          /* #3 is off duty */
    CHECK(a->assign_trip_crew(orbit, fwd->id, 4, -1).code == Err::VALIDATION);        /* #4 is a conductor */
    CHECK(a->assign_trip_crew(orbit, fwd->id, 20, -1).code == Err::NOT_FOUND);        /* another company's staff */
    CHECK(a->assign_trip_crew(2, fwd->id, 1, -1).code == Err::NOT_FOUND);             /* another company's trip */
    CHECK(a->assign_trip_crew(orbit, fwd->id, 0, 6).ok);                              /* 0 = back to the bus default; conductor #6 */
    CHECK_EQ_INT(a->trip_conductor(*fwd), 6);
    CHECK_EQ_INT(a->trip_driver(*fwd), 1);
    /* off duty */
    CHECK(a->set_staff_off_duty(orbit, 6, false).ok);
    std::vector<Trip *> today = a->trips_on_date(date_key(a->clock.now()), orbit);
    CHECK(today.size() > 0);
    CHECK(a->set_staff_off_duty(orbit, 5, true).code == Err::CONFLICT);               /* conductor #5 is booked on a run today */
    CHECK(a->set_staff_off_duty(2, 5, true).code == Err::NOT_FOUND);                  /* not their staff */
    CHECK(a->set_staff_off_duty(orbit, 99, true).code == Err::NOT_FOUND);
    CHECK(a->assign_trip_crew(orbit, fwd->id, -1, 5).ok);                             /* so #5 can still be assigned, but ... */
    CHECK(a->set_staff_off_duty(orbit, 3, false).ok);
    CHECK_EQ_INT(a->staff[2].off_duty, 0);                                            /* back on duty */
    CHECK(a->assign_trip_crew(orbit, fwd->id, 3, -1).ok);

    /* coupons */
    CHECK(a->add_coupon("diwali100", false, 10000, 0, 30000).ok);
    CHECK(a->add_coupon("DIWALI100", false, 5000, 0, 0).code == Err::CONFLICT);
    CHECK(a->add_coupon("ab", false, 5000, 0, 0).code == Err::VALIDATION);
    CHECK(a->add_coupon("BAD CODE!", false, 5000, 0, 0).code == Err::VALIDATION);
    CHECK(a->add_coupon("OVER", true, 150, 0, 0).code == Err::VALIDATION);
    CHECK(a->add_coupon("ZERO", false, 0, 0, 0).code == Err::VALIDATION);
    CHECK(a->add_coupon("PCT20", true, 20, 20000, 0).ok);
    Trip *agra = trip_on(*a, "DEMO-002", "delhi-agra", 6);
    const char *two[2] = {"1A", "1B"};
    Result<Quote> q = a->quote(agra->id, two, 2, "diwali100", false, 1);
    CHECK(q.ok); CHECK_EQ_INT(q.value.discount, 10000);                               /* Rs 100 off Rs 1200 */
    CHECK(a->set_coupon_active("DIWALI100", false).ok);
    CHECK(!a->quote(agra->id, two, 2, "DIWALI100", false, 1).ok);                     /* disabled coupons are refused */
    CHECK(a->set_coupon_active("DIWALI100", true).ok);
    CHECK(a->quote(agra->id, two, 2, "DIWALI100", false, 1).ok);
    CHECK(a->set_coupon_active("NOPE", true).code == Err::NOT_FOUND);
    int pct = a->quote(agra->id, two, 2, "PCT20", false, 1).value.discount;
    CHECK_EQ_INT(pct, 20000 < 24000 ? 20000 : 24000);                                 /* 20% of 1200 = 240 but capped at 200 */
    int bid = book_full(*a, 1, agra, {"1A", "1B"}, "DIWALI100", false);
    CHECK(bid > 0);
    CHECK_EQ_INT(a->coupon_use_count(*a->idx_coupon.get("DIWALI100")), 1);
    CHECK_EQ_INT(a->coupon_use_count(*a->idx_coupon.get("PCT20")), 0);

    /* revenue by corridor */
    std::vector<CorridorRevenue> rv = a->revenue_by_corridor();
    CHECK_EQ_INT(rv.size(), 15);
    int agra_corridor = a->routes[(size_t)agra->route_id].id / 2;
    CHECK_EQ_INT(rv[(size_t)agra_corridor].passes, 2);
    CHECK_EQ_INT(rv[(size_t)agra_corridor].paise, a->booking_by_id(bid)->paid);
    CHECK_EQ_INT(rv[0].passes, 0);

    /* share links */
    Result<std::string> s1 = a->share_create(1, bid, false);
    CHECK(s1.ok); CHECK_EQ_INT(s1.value.size(), 12);
    CHECK(a->share_create(1, bid, false).value == s1.value);                          /* same link unless rotated */
    CHECK(a->share_lookup(s1.value) == a->booking_by_id(bid));
    CHECK(a->share_create(2, bid, false).code == Err::NOT_FOUND);                     /* another user's booking */
    Result<std::string> s2 = a->share_create(1, bid, true);
    CHECK(s2.value != s1.value);
    CHECK(a->share_lookup(s1.value) == nullptr);                                      /* rotating kills the old link */
    CHECK(a->share_lookup(s2.value) != nullptr);
    CHECK(a->share_lookup("nope") == nullptr);
    CHECK(a->share_revoke(2, bid).code == Err::NOT_FOUND);
    CHECK(a->share_revoke(1, bid).ok);
    CHECK(a->share_lookup(s2.value) == nullptr);
    CHECK(a->share_token_of(bid).empty());
    Result<std::string> s3 = a->share_create(1, bid, false);
    a->cancel_booking(bid, 1);
    CHECK(a->share_lookup(s3.value) == nullptr);                                      /* cancelled bookings cannot be tracked via a link */
    CHECK(!a->share_create(1, bid, false).ok);

    /* DEMO traffic scan: only at peak hours, only trips on the road or leaving within 3 h */
    User drv = *a->user_by_id(a->user_id_by_email("driver@smartroute.in"));
    Trip *run = a->driver_trips(drv)[0];
    a->driver_start(drv, run->id);
    int64_t base = ist_midnight(a->clock.now());
    std::string rep;
    a->clock.set(base + 14 * MS_HOUR);
    CHECK_EQ_INT(a->scan_traffic_delays(&rep), 0);                                    /* 14:00 is off-peak */
    CHECK(rep.find("off-peak") != std::string::npos);
    a->clock.set(base + 9 * MS_HOUR);
    int changed = a->scan_traffic_delays(&rep);
    CHECK(changed >= 1);
    CHECK(run->delay_min >= 8 && run->delay_min <= 20);
    CHECK_EQ_STR(a->delay_log[a->delay_log.size() - 1].source, "traffic-scan (DEMO)");
    CHECK_EQ_INT(a->scan_traffic_delays(&rep), 0);                                    /* a second scan adds nothing new */
}

static void test_assistant(void) {
    printf("Testing the rule-based assistant...\n");
    auto a = make_app();
    AssistantAnswer x = a->assistant("cheapest bus from Delhi to Jaipur tomorrow");
    CHECK_EQ_INT(x.from, a->find_city("Delhi")); CHECK_EQ_INT(x.to, a->find_city("Jaipur"));
    CHECK(x.text.find("cheapest first") != std::string::npos);
    CHECK(x.text.find("1) Orbit Express") != std::string::npos);                      /* Rs 620 AC Sleeper beats the Rs 730 Volvo */
    CHECK(x.opened_search);
    CHECK_EQ_INT(x.date_key, date_key(a->clock.now() + MS_DAY));
    AssistantAnswer f = a->assistant("fastest bus delhi to agra today");
    CHECK(f.text.find("fastest first") != std::string::npos);
    CHECK_EQ_INT(f.date_key, date_key(a->clock.now()));
    AssistantAnswer p = a->assistant("del to jai");                                    /* partial names via the trie */
    CHECK_EQ_INT(p.from, a->find_city("Delhi")); CHECK_EQ_INT(p.to, a->find_city("Jaipur"));
    AssistantAnswer rv = a->assistant("I want to go to Delhi from Jaipur");            /* "from X" / "to Y" decide direction */
    CHECK_EQ_INT(rv.from, a->find_city("Jaipur")); CHECK_EQ_INT(rv.to, a->find_city("Delhi"));
    AssistantAnswer c = a->assistant("mysuru to madurai");
    CHECK(c.text.find("Bengaluru") != std::string::npos && c.text.find("Chennai") != std::string::npos);
    CHECK(!c.opened_search);
    CHECK(a->assistant("delhi to goa").text.find("different bus networks") != std::string::npos);
    CHECK(a->assistant("delhi").text.find("Jaipur") != std::string::npos);
    CHECK(a->assistant("hello there").text.find("rule-based") != std::string::npos);
    CHECK(a->assistant("delhi to delhi").text.find("different cities") != std::string::npos);
    CHECK(a->assistant("delhi to jaipur in 200 days").text.find("90 days") != std::string::npos);
    AssistantAnswer iso = a->assistant("buses delhi to jaipur on 2026-10-24");
    CHECK_EQ_INT(iso.date_key, 20261024);
    AssistantAnswer dat = a->assistant("delhi to jaipur day after tomorrow");
    CHECK_EQ_INT(dat.date_key, date_key(a->clock.now() + 2 * MS_DAY));
    AssistantAnswer sl = a->assistant("sleeper bus delhi to jaipur +3");
    CHECK_EQ_INT(sl.date_key, date_key(a->clock.now() + 3 * MS_DAY));
    CHECK(sl.text.find("Sleeper") != std::string::npos && sl.text.find("Volvo") == std::string::npos);
}

/* ---------- spec extras: guests, lookup rules, name rules, time window, fare cap, per-stop ETA, driver note ---------- */

static void test_spec_extras(void) {
    printf("Testing guests, name rules, time-of-day search, fare cap, eta_at_stop, driver note...\n");
    auto a = make_app();

    /* guests (spec 6.4): a passenger row whose hash can never match a password */
    int g1 = a->add_guest(), g2 = a->add_guest();
    CHECK(g1 > 0 && g2 > g1);
    CHECK(a->user_by_id(g1)->hash[0] == '!');
    CHECK(std::string(a->user_by_id(g1)->name) == "Guest traveller");
    CHECK_EQ_INT(a->guest_count(), 2);
    Result<int> bad_login = a->login(a->user_by_id(g1)->email, "Demo@1234", ROLE_PASSENGER);   /* no guest can be signed into */
    CHECK(!bad_login.ok);

    /* a guest can book exactly like a passenger */
    Trip *t = trip_on(*a, "DEMO-001", "delhi-jaipur", 5);
    int gbid = book_full(*a, g1, t, {"L7"}, "", false);
    CHECK(gbid > 0);

    /* name / contact rules (spec 6.4): markup rejected, ordinary punctuation kept */
    CHECK(!a->register_passenger("Hacker<img onerror=x>", "h@e.co", "999", "Passw0rd1").ok);
    CHECK(!a->register_passenger("Bad & Backtick`", "h2@e.co", "999", "Passw0rd1").ok);
    CHECK(a->register_passenger("O'Brien-Nut", "ob@e.co", "999", "Passw0rd1").ok);          /* ' - . are fine */
    CHECK(!display_text_ok("back\\slash"));
    CHECK(!display_text_ok(std::string("tab\tchar")));                                      /* control characters are rejected */
    CHECK(!display_text_ok("caret\x01""x"));

    /* the passenger name and contact are checked at booking time too */
    Trip *t2 = trip_on(*a, "DEMO-001", "delhi-jaipur", 6);
    const char *one[] = {"L8"};
    CHECK(a->hold_seats(g2, t2->id, one, 1).ok);
    BookingRequest rq;
    rq.user_id = g2; rq.trip_id = t2->id; rq.n_pax = 1;
    std::snprintf(rq.pax[0].seat, sizeof rq.pax[0].seat, "L8");
    std::snprintf(rq.pax[0].name, sizeof rq.pax[0].name, "Malicious<b>");
    rq.pax[0].age = 30; rq.pax[0].gender = 'M';
    rq.boarding_stop = a->stop_index(a->routes[(size_t)t2->route_id], ST_BOARDING, 0);
    rq.dropping_stop = a->stop_index(a->routes[(size_t)t2->route_id], ST_DROPPING, 0);
    std::snprintf(rq.contact, sizeof rq.contact, "9876543210");
    Result<int> hostile = a->create_booking(rq);
    CHECK(!hostile.ok);
    CHECK(hostile.message.find("cannot contain") != std::string::npos);

    /* departure time-of-day window (spec 6.8) */
    int from = a->find_city("Delhi"), to = a->find_city("Jaipur");
    int dk = date_key(a->clock.now() + 5 * MS_DAY);
    Vec<SearchRow> all, late;
    a->search(from, to, dk, SearchFilters(), SORT_EARLIEST, all);
    CHECK(all.size() >= 2);
    SearchFilters f6;
    f6.dep_after = 6 * 60;                                        /* leaving at or after 06:00 */
    Vec<SearchRow> kept;
    a->search(from, to, dk, f6, SORT_EARLIEST, kept);
    for (size_t i = 0; i < kept.size(); i++) {
        int64_t mins = (kept[i].depart_ms - ist_midnight(kept[i].depart_ms)) / MS_MIN;
        CHECK(mins >= 6 * 60);
    }
    CHECK(kept.size() <= all.size());
    SearchFilters before4;
    before4.dep_before = 4 * 60;                                  /* "before 04:00": nothing later than 04:00 may pass */
    a->search(from, to, dk, before4, SORT_EARLIEST, late);
    for (size_t i = 0; i < late.size(); i++) {
        int64_t mins = (late[i].depart_ms - ist_midnight(late[i].depart_ms)) / MS_MIN;
        CHECK(mins <= 4 * 60);
    }

    /* fare cap: the dearest seat on the corridor is at least the cheapest, and the sleeper upper berth costs more */
    int ri = a->find_route(from, to);
    int64_t lo = a->route_lowest_fare(ri), hi = a->route_highest_fare(ri);
    CHECK(hi > lo);

    /* eta_at_stop (spec 6.7) */
    const Route &r = a->routes[(size_t)ri];
    for (int i = 0; i < r.n_stops; i++)                            /* pre-departure, nothing injected: the plan exactly */
        CHECK_EQ_INT(a->eta_at_stop(*t, i), a->planned_stop_time(*t, i));
    a->sim_jam(*t);                                                /* jam before departure still shows up */
    CHECK(a->eta_at_stop(*t, r.n_stops - 1) >= t->arrive_ms + 10 * MS_MIN);
    a->sim_reset(*t);
    t->delay_min = 0;
    a->sim_advance(*t, 3600);                                      /* one simulated hour */
    CHECK(a->eta_at_stop(*t, r.n_stops - 1) == a->track_info(*t).est_arrive);   /* at the destination: the headline ETA */
    CHECK(a->eta_at_stop(*t, 0) <= a->eta_at_stop(*t, r.n_stops - 1));          /* stops in order */
    CHECK(a->report_delay(1, t->id, "WEATHER", 45, "Heavy rain near Manesar").ok);
    CHECK(a->eta_at_stop(*t, r.n_stops - 1) >= a->planned_stop_time(*t, r.n_stops - 1) + 45 * MS_MIN);   /* delay is a floor */

    /* the driver note reaches the passenger (spec 6.6 + audit fix 14) */
    int notes_before = 0;
    for (const Notification *n : a->notifications_for(g1)) if (std::string(n->body).find("Driver note:") != std::string::npos) notes_before++;
    CHECK_EQ_INT(notes_before, 1);                                  /* g1 is booked on t and was told once */
    a->report_delay(1, t->id, "WEATHER", 45, "Heavy rain near Manesar");   /* identical: the dedupe key swallows it */
    int notes_same = 0;
    for (const Notification *n : a->notifications_for(g1)) if (std::string(n->body).find("Driver note:") != std::string::npos) notes_same++;
    CHECK_EQ_INT(notes_same, notes_before);
    a->report_delay(1, t->id, "WEATHER", 45, "Rain cleared, slow traffic"); /* a changed note is a new message */
    int notes_after = 0;
    for (const Notification *n : a->notifications_for(g1)) if (std::string(n->body).find("Driver note:") != std::string::npos) notes_after++;
    CHECK_EQ_INT(notes_after, notes_before + 1);

    /* the assistant understands the new phrases (spec 6.8) */
    AssistantAnswer v = a->assistant("Volvo coach from Delhi to Jaipur leaving after 7 am");
    CHECK_EQ_INT(v.dep_after, 7 * 60);
    bool volvo_bit = false;
    for (size_t i = 0; i < a->types.size(); i++)
        if (std::string(a->types[i].label).find("Volvo") != std::string::npos) volvo_bit = (v.type_mask & (1u << i)) != 0;
    CHECK(volvo_bit);
    CHECK_EQ_INT(v.sort_mode, SORT_EARLIEST);
    AssistantAnswer w = a->assistant("cheapest seater delhi to jaipur with wifi");
    CHECK_EQ_INT(v.dep_before, -1);
    CHECK_EQ_INT(w.sort_mode, SORT_CHEAPEST);                        /* affordable/cheapest */
    CHECK(w.ignored.size() == 1 && w.ignored[0] == "Wi-Fi");        /* honest about what the catalogue lacks */
    CHECK(w.text.find("Wi-Fi") != std::string::npos);
    bool any_sleeper = false;
    for (size_t i = 0; i < a->types.size(); i++) if (a->types[i].layout == LAYOUT_SLEEPER && (w.type_mask & (1u << i))) any_sleeper = true;
    CHECK(!any_sleeper);                                             /* seater excludes sleepers */
    AssistantAnswer night = a->assistant("delhi to jaipur after 19:30");
    CHECK_EQ_INT(night.dep_after, 19 * 60 + 30);
    AssistantAnswer mid = a->assistant("delhi to jaipur after 12 am");
    CHECK_EQ_INT(mid.dep_after, 0);
    AssistantAnswer pm = a->assistant("delhi to jaipur before 9pm");
    CHECK_EQ_INT(pm.dep_before, 21 * 60);
}

static void test_ops_persistence(void) {
    printf("Testing persistence of the new data (and older save files)...\n");
    make_dirs("build/test_save2");
    clear_save_dir("build/test_save2");
    auto a = make_app();
    a->save_dir = "build/test_save2";
    User drv = *a->user_by_id(a->user_id_by_email("driver@smartroute.in"));
    Trip *t = a->driver_trips(drv)[0];
    a->clock.set(t->depart_ms - 30 * MS_HOUR);
    int bid = book_full(*a, 1, t, {"U2"}, "", false);
    bool already = false;
    a->driver_scan(drv, a->booking_by_id(bid)->pnr, &already);
    a->add_coupon("SAVEME", true, 15, 5000, 1000);
    a->set_coupon_active("SMART50", false);
    std::string token = a->share_create(1, bid, false).value;
    a->set_staff_off_duty(1, 6, true);
    a->driver_start(drv, t->id);
    geo_point p = a->bus_position(a->routes[(size_t)t->route_id], 80.0);
    CHECK(a->driver_position(drv, t->id, p.lat, p.lng, 45, 180, 12).ok);
    a->driver_delay(*a->user_by_id(a->user_id_by_email("admin@smartroute.in")), t->id, "WEATHER", 25, "Rain");
    CHECK(a->assign_trip_crew(1, t->id, 2, -1).ok);               /* the manager moves driver #2 onto this run afterwards */
    std::string err;
    CHECK(a->save_all(&err));
    auto b = make_app();
    b->save_dir = "build/test_save2";
    std::string msgs;
    CHECK(b->load_saved(&msgs));
    CHECK_EQ_INT(b->booking_by_id(bid)->boarded_mask, 1);
    CHECK_STR(b->boarding_state(*b->booking_by_id(bid)), "BOARDED");
    CHECK(b->idx_coupon.has("SAVEME"));
    CHECK_EQ_INT(b->coupons[(size_t)*b->idx_coupon.get("SMART50")].active, 0);
    CHECK_EQ_INT(b->coupons[(size_t)*b->idx_coupon.get("SAVEME")].is_percent, 1);
    CHECK(b->share_lookup(token) == b->booking_by_id(bid));
    CHECK_EQ_INT(b->staff[5].off_duty, 1);
    CHECK_EQ_INT(b->trip_by_id(t->id)->auto_driver, 2);
    CHECK_EQ_INT(b->gps_fixes.size(), 1);
    CHECK_NEAR(b->gps_fixes[0].lat, p.lat, 1e-4);
    CHECK_EQ_INT(b->delay_log.size(), 1);
    CHECK_EQ_STR(b->delay_log[0].source, "admin");              /* logged with who reported it */
    CHECK(b->login("driver@smartroute.in", "Demo@1234", ROLE_DRIVER).ok);
    CHECK_EQ_INT(b->user_by_id(b->user_id_by_email("driver@smartroute.in"))->staff_id, 1);
    /* an OLD save file knows only the first 15 accounts: the newer demo accounts must still be able to sign in */
    auto old = make_app();
    old->save_dir = "build/test_save3";
    make_dirs("build/test_save3");
    clear_save_dir("build/test_save3");
    for (int i = 0; i < 3; i++) old->users.remove(old->users.size() - 1);              /* drop pilot, driver, admin */
    CHECK_EQ_INT(old->users.size(), 15);
    CHECK(old->save_all(&err));
    auto upgraded = make_app();
    upgraded->save_dir = "build/test_save3";
    CHECK(upgraded->load_saved(&msgs));
    CHECK_EQ_INT(upgraded->users.size(), 18);
    CHECK(upgraded->login("driver@smartroute.in", "Demo@1234", ROLE_DRIVER).ok);
    CHECK(upgraded->login("admin@smartroute.in", "Demo@1234", ROLE_ADMIN).ok);
    CHECK(upgraded->login("user@smartroute.in", "Demo@1234", ROLE_PASSENGER).ok);
}

/* ---------- offline map background (app/basemap.cpp, spec 8) ----------
   The generator is the only part of this project that writes a file the browser then draws, so it is
   tested three ways: the promises the page relies on (counts, coordinates, determinism), the geometry
   (Sutherland-Hodgman clipping and core/geo simplification against a deliberately awkward stand-in
   GeoJSON), and 600 randomly corrupted inputs under the sanitizers (`make debug`) to prove a broken
   file can neither crash the program nor reach the disk half-written. */

static std::string slurp_file(const std::string &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
static void spit_file(const std::string &p, const std::string &text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}
static long count_of(const std::string &hay, const std::string &needle) {
    long n = 0;
    for (size_t i = hay.find(needle); i != std::string::npos; i = hay.find(needle, i + needle.size())) n++;
    return n;
}
/* A second, dumber reader for the same promises: count the literal shapes in the text and require the
   number the page will see. It catches a document that parses but is missing a section entirely. */
static bool doc_shape_counts_match(const std::string &doc, const std::string &needle, long want) {
    return count_of(doc, needle) == want;
}
/* One corridor per pair, worked out here independently of the generator: catalog.cpp writes the partner's
   id on BOTH routes of a pair, so the lower id of the two is the line that gets drawn. */
static long hand_corridors(const App &a) {
    long n = 0;
    for (size_t i = 0; i < a.routes.size(); i++) {
        const Route &r = a.routes[i];
        if (r.n_stops >= 2 && (r.reverse_of < 0 || r.id < r.reverse_of)) n++;
    }
    return n;
}

/* Seven outlines, six of them awkward on purpose: wholly inside the region, across its west edge, larger
   than the world, a MultiPolygon whose second part belongs to another continent, one whose bounding box
   overlaps the region while not one of its vertices enters it (the real Natural Earth Eurasia behaves
   exactly like this at 1:110m), one that is far away, and a Point that is not an outline at all.
   GeoJSON nesting: Polygon coordinates = [ring] = [[lng,lat]...], MultiPolygon = [polygon] = [[[...]]]. */
static const char *STANDIN_LAND =
"{\"type\":\"FeatureCollection\",\"features\":["
"{\"type\":\"Feature\",\"properties\":{\"name\":\"inside blob\"},"
 "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[70,10],[80,8],[88,14],[85,22],[75,20],[70,10]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"across the west edge\"},"
 "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[-10,10],[80,10],[80,30],[-10,30],[-10,10]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"the whole world\"},"
 "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[-180,-90],[180,-90],[180,90],[-180,90],[-180,-90]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"two parts, one of them ours\"},"
 "\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":["
 "[[[70,25],[78,24],[76,32],[72,30],[70,25]]],[[[100,40],[120,40],[120,50],[100,50],[100,40]]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"box overlaps, no vertex inside\"},"
 "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[60,5],[66,30],[95,35],[90,2],[60,5]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"far away\"},"
 "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[0,0],[5,0],[5,5],[0,5],[0,0]]]}},"
"{\"type\":\"Feature\",\"properties\":{\"name\":\"not an outline\"},"
 "\"geometry\":{\"type\":\"Point\",\"coordinates\":[77.2,28.6]}}]}";

static bool all_inside(const std::vector<BasemapShape> &land, const BasemapRegion &r) {
    for (size_t i = 0; i < land.size(); i++)
        for (size_t k = 0; k < land[i].pts.size(); k++) {
            const geo_point &p = land[i].pts[k];
            if (!(p.lng >= r.lng0 && p.lng <= r.lng1 && p.lat >= r.lat0 && p.lat <= r.lat1)) return false;
        }
    return true;
}
static size_t total_points(const std::vector<BasemapShape> &land) {
    size_t n = 0;
    for (size_t i = 0; i < land.size(); i++) n += land[i].pts.size();
    return n;
}

static void test_basemap(void) {
    printf("Testing offline map background: document, clipping, self-check, 600 fuzzed files...\n");
    int before = g_tests_passed + g_tests_failed;                                  /* so the summary can state this section's own total */
    auto a = make_app();
    std::vector<BasemapShape> none;
    std::string doc = basemap_json(*a, none, "");

    /* ---- 1. the promises the page makes out of ---- */
    CHECK(doc == basemap_json(*a, none, ""));                                    /* deterministic: same inputs, same bytes */
    Json root;
    std::string err;
    CHECK(json_parse(doc, &root, &err));                                         /* the file the page fetches is valid JSON */
    const Json *fmt = root.find("format");
    CHECK(fmt && fmt->kind == Json::STR && fmt->str == "smartroute-basemap/1");
    const Json *bnd = root.find("bounds");
    bool box_ok = bnd && bnd->kind == Json::ARR && bnd->arr.size() == 4 &&
                  bnd->arr[0].num < bnd->arr[2].num && bnd->arr[1].num < bnd->arr[3].num &&
                  std::fabs(bnd->arr[0].num - std::round(bnd->arr[0].num)) < 1e-9 &&
                  std::fabs(bnd->arr[1].num - std::round(bnd->arr[1].num)) < 1e-9 &&
                  std::fabs(bnd->arr[2].num - std::round(bnd->arr[2].num)) < 1e-9 &&
                  std::fabs(bnd->arr[3].num - std::round(bnd->arr[3].num)) < 1e-9;
    CHECK(box_ok);                                                               /* an even-degree box, west < east, south < north */
    BasemapRegion reg = basemap_region(*a);
    const Json *step = root.find("grid_step_deg");
    CHECK(step && step->num == 2.0);                                             /* the 2-degree grid the spec asks for */
    long want_grid = (long)std::floor((reg.lng1 - reg.lng0) / 2.0 + 1e-9) + 1
                   + (long)std::floor((reg.lat1 - reg.lat0) / 2.0 + 1e-9) + 1;
    const Json *grid = root.find("graticule");
    CHECK(grid && (long)grid->arr.size() == want_grid);                          /* meridian count + parallel count */
    const Json *cor = root.find("corridors"), *cities = root.find("cities");
    CHECK(cor && (long)cor->arr.size() == hand_corridors(*a) && hand_corridors(*a) == 15);
    CHECK(cities && (long)cities->arr.size() == (long)a->cities.size() && a->cities.size() == 20 &&
          doc_shape_counts_match(doc, "\"at\":[", (long)a->cities.size()));   /* parsed and counted two ways */
    bool dots_exact = cities != nullptr && (long)cities->arr.size() == (long)a->cities.size();
    for (size_t i = 0; dots_exact && i < cities->arr.size(); i++) {
        const Json *at = cities->arr[i].find("at"), *nm = cities->arr[i].find("name");
        dots_exact = at && at->kind == Json::ARR && at->arr.size() == 2 &&
                     nm && nm->str == std::string(a->cities[i].name) &&
                     std::fabs(at->arr[0].num - a->cities[i].pos.lng) <= 5e-7 &&
                     std::fabs(at->arr[1].num - a->cities[i].pos.lat) <= 5e-7;
    }
    CHECK(dots_exact);                                                           /* every dot on its own catalogue city, [lng,lat] */
    bool grid_straight = grid != nullptr;
    for (size_t i = 0; grid_straight && i < grid->arr.size(); i++) {
        const Json &ln = grid->arr[i];
        grid_straight = ln.kind == Json::ARR && ln.arr.size() == 2 &&
                        ln.arr[0].kind == Json::ARR && ln.arr[1].kind == Json::ARR &&
                        ln.arr[0].arr.size() == 2 && ln.arr[1].arr.size() == 2;
        if (!grid_straight) break;
        double lng_a = ln.arr[0].arr[0].num, lat_a = ln.arr[0].arr[1].num;
        double lng_b = ln.arr[1].arr[0].num, lat_b = ln.arr[1].arr[1].num;
        grid_straight = (std::fabs(lng_a - lng_b) < 1e-9 || std::fabs(lat_a - lat_b) < 1e-9) &&     /* a meridian or a parallel */
                        lng_a >= reg.lng0 - 1e-9 && lng_a <= reg.lng1 + 1e-9 &&
                        lat_a >= reg.lat0 - 1e-9 && lat_a <= reg.lat1 + 1e-9 &&
                        lng_b >= reg.lng0 - 1e-9 && lng_b <= reg.lng1 + 1e-9 &&
                        lat_b >= reg.lat0 - 1e-9 && lat_b <= reg.lat1 + 1e-9;                       /* and nothing outside the box */
    }
    CHECK(grid_straight);
    bool labels = cities != nullptr;
    for (size_t i = 0; labels && i < cities->arr.size(); i++) {                   /* the page prints a name and a state per dot */
        const Json *nm = cities->arr[i].find("name"), *st = cities->arr[i].find("state");
        labels = nm && nm->kind == Json::STR && !nm->str.empty() && st && st->kind == Json::STR;
    }
    CHECK(labels);
    /* Every corridor line starts at the route's first stop and ends at its last one, so the picture cannot
      drift away from the timetable it claims to show. */
    std::vector<geo_point> firsts, lasts;
    for (size_t i = 0; i < a->routes.size(); i++) {
        const Route &r = a->routes[i];
        if (r.n_stops < 2 || !(r.reverse_of < 0 || r.id < r.reverse_of)) continue;
        firsts.push_back(r.pts[0]);
        lasts.push_back(r.pts[r.n_stops - 1]);
    }
    bool ends_ok = cor != nullptr && (long)cor->arr.size() == (long)firsts.size();
    for (size_t i = 0; ends_ok && i < cor->arr.size(); i++) {
        const Json *pts = cor->arr[i].find("pts");
        ends_ok = pts && pts->arr.size() >= 2 &&
                  std::fabs(pts->arr.front().arr[0].num - firsts[i].lng) < 1e-6 &&
                  std::fabs(pts->arr.front().arr[1].num - firsts[i].lat) < 1e-6 &&
                  std::fabs(pts->arr.back().arr[0].num - lasts[i].lng) < 1e-6 &&
                  std::fabs(pts->arr.back().arr[1].num - lasts[i].lat) < 1e-6;
    }
    CHECK(ends_ok);
    const Json *sc = root.find("scale");
    const Json *bar = sc ? sc->find("bar_km") : nullptr, *kpd = sc ? sc->find("km_per_deg_lng") : nullptr;
    const Json *mid = sc ? sc->find("at_lat") : nullptr;
    CHECK(bar && bar->num == 100 && mid && std::fabs(mid->num - (reg.lat0 + reg.lat1) / 2.0) < 1e-9 &&
          kpd && std::fabs(kpd->num - 111.320 * std::cos((reg.lat0 + reg.lat1) / 2.0 * 3.14159265358979323846 / 180.0)) < 1e-6);
    const Json *ld = root.find("land"), *credit = root.find("land_credit");
    CHECK(ld && ld->arr.empty() && credit && credit->str.empty());               /* no land data = neutral paper, no sea */
    std::string msg;
    CHECK(basemap_self_check(doc, (long)a->cities.size(), hand_corridors(*a), &msg));
    CHECK(!basemap_self_check(doc, (long)a->cities.size() + 1, hand_corridors(*a), &msg));   /* a wrong count is caught */
    CHECK(!basemap_self_check(doc.substr(0, doc.size() - 40), -1, -1, &msg));     /* a truncated file is caught */
    CHECK(basemap_generated(*a) == doc);                                          /* what the server answers with no file on disk */

    /* ---- 2. land: clip to the region, then simplify ---- */
    std::vector<BasemapShape> land;
    std::string why;
    bool got_land = basemap_land_from_geojson(STANDIN_LAND, reg, &land, &why);
    CHECK(got_land);
    CHECK_EQ_STR(why.c_str(), "5 of 7 outlines kept");              /* the far-away square and the foreign MultiPolygon part are cut; a Point is not an outline */
    CHECK_EQ_INT(land.size(), 5);
    CHECK(all_inside(land, reg));                                                 /* nothing outside the region survives */
    bool world_cut = false;
    for (size_t i = 0; i < land.size() && !world_cut; i++) {
        double ln0 = land[i].pts[0].lng, ln1 = ln0, la0 = land[i].pts[0].lat, la1 = la0;
        for (size_t k = 1; k < land[i].pts.size(); k++) {
            ln0 = std::min(ln0, land[i].pts[k].lng); ln1 = std::max(ln1, land[i].pts[k].lng);
            la0 = std::min(la0, land[i].pts[k].lat); la1 = std::max(la1, land[i].pts[k].lat);
        }
        world_cut = land[i].pts.size() >= 4 && std::fabs(ln0 - reg.lng0) < 1e-9 && std::fabs(ln1 - reg.lng1) < 1e-9 &&
                    std::fabs(la0 - reg.lat0) < 1e-9 && std::fabs(la1 - reg.lat1) < 1e-9;   /* the -180..180 box, cut at the edge */
    }
    CHECK(world_cut);
    CHECK(total_points(land) <= 400);                                             /* the point budget Douglas-Peucker is there for */
    std::string utf8_credit = "Coastlines: K\xc3\xb6ln plus \xe0\xa4\x95\xe0\xa4\xbf\xe0\xa4\xa8 (public domain)";
    std::string land_doc = basemap_json(*a, land, utf8_credit);
    CHECK(land_doc == basemap_json(*a, land, utf8_credit));                       /* deterministic with land too */
    CHECK(basemap_self_check(land_doc, (long)a->cities.size(), hand_corridors(*a), &msg));
    Json lroot;
    CHECK(json_parse(land_doc, &lroot, &err) && lroot.find("land") && lroot.find("land_credit"));
    if (lroot.find("land")) CHECK_EQ_INT((long)lroot.find("land")->arr.size(), 5);
    if (lroot.find("land_credit"))                              /* the credit survives as UTF-8 */
        CHECK_EQ_STR(lroot.find("land_credit")->str.c_str(), utf8_credit.c_str());
    Json esc;
    std::string esc_err;
    bool esc_parsed = json_parse("{\"a\":\"K\\u00f6ln \\u0915\\u093f\\u0938\"}", &esc, &esc_err);
    CHECK(esc_parsed);
    const Json *esc_a = esc.find("a");                            /* a \u escape reads as the same bytes as the UTF-8 spelling */
    CHECK(esc_a != nullptr);
    if (esc_a) CHECK_EQ_STR(esc_a->str.c_str(), "K\xc3\xb6ln \xe0\xa4\x95\xe0\xa4\xbf\xe0\xa4\xb8");

    /* ---- 3. a broken input says why and never leaves half a file behind ---- */
    const char *bad_texts[] = {
        "",
        "this is a bus timetable, not a map",
        "{\"type\":\"FeatureCollection\",\"features\":[{\"name\":\"trunc",
        "[1,2,3]",
        "{\"type\":\"Point\",\"coordinates\":[77,28]}",
        "{\"type\":\"FeatureCollection\"}",
        "{\"type\":\"FeatureCollection\",\"features\":7}",
        "{\"type\":\"FeatureCollection\",\"features\":[{\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[0,0],[5,0],[5,5],[0,5],[0,0]]]}}]}",
    };
    std::string deep;
    for (int i = 0; i < 60; i++) deep += "[";
    for (int i = 0; i < 60; i++) deep += "]";
    std::vector<BasemapShape> unused;
    for (size_t i = 0; i < sizeof bad_texts / sizeof bad_texts[0]; i++) {
        std::string why2;
        bool refused = !basemap_land_from_geojson(bad_texts[i], reg, &unused, &why2);
        CHECK(refused && !why2.empty() && unused.empty());                        /* a message, and nothing half-built */
    }
    {
        std::string dw;
        CHECK(!basemap_land_from_geojson(deep, reg, &unused, &dw) && !dw.empty());   /* nesting past the cap is refused, not followed */
    }
    {
        const std::string out_path = "build/bm-test.json";
        spit_file(out_path, "PREVIOUS GOOD FILE");
        spit_file("build/bm-bad.geojson", "not json at all");
        std::string m;
        bool done = basemap_write(*a, "build/bm-bad.geojson", out_path, &m);
        CHECK(!done && !m.empty() && slurp_file(out_path) == "PREVIOUS GOOD FILE" &&
              !std::ifstream((out_path + ".tmp").c_str()).good());               /* rejected: the old file is intact */
        std::string m2;
        bool missing = basemap_write(*a, "build/bm-no-such-file.geojson", out_path, &m2);
        CHECK(!missing && m2.find("cannot open") != std::string::npos &&
              slurp_file(out_path) == "PREVIOUS GOOD FILE");                      /* a missing path names itself */
        std::remove("build/bm-bad.geojson");
        std::remove(out_path.c_str());
    }
    {                                                                    /* the positive path, which the two checks above never reach */
        const std::string land_out = "build/bm-land.json", plain_out = "build/bm-plain.json";
        spit_file("build/bm-standin.geojson", STANDIN_LAND);
        std::string wm;
        bool wrote = basemap_write(*a, "build/bm-standin.geojson", land_out, &wm);
        Json wroot;
        std::string werr;
        const std::string written = slurp_file(land_out);
        bool land_written = wrote && wm.find("5 of 7 outlines kept") != std::string::npos &&
                            json_parse(written, &wroot, &werr) && wroot.find("land") &&
                            (long)wroot.find("land")->arr.size() == 5 &&
                            wroot.find("land_credit") && !wroot.find("land_credit")->str.empty() &&
                            basemap_self_check(written, (long)a->cities.size(), hand_corridors(*a), &werr);
        CHECK(land_written);                                             /* a land file really does produce a sea-blue background */
        std::string pm;
        bool plain_written = basemap_write(*a, "", plain_out, &pm) &&
                             basemap_self_check(slurp_file(plain_out), (long)a->cities.size(), hand_corridors(*a), &pm);
        CHECK(plain_written);                                            /* no input file still writes a file that passes its own check */
        std::remove("build/bm-standin.geojson");
        std::remove(land_out.c_str());
        std::remove(plain_out.c_str());
    }

    /* ---- 4. 600 randomly corrupted inputs: no crash, and anything accepted is a valid file ----
       Truncated, byte-flipped, byte-deleted, junk-inserted, numbers turned odd, chunks duplicated. This
       runs again under ASan + UBSan with `make debug`, which is where a memory error would surface. */
    uint64_t fseed = 0xB0A5CAFE20261020ULL;
    for (int round = 0; round < 600; round++) {
        fseed ^= fseed >> 12; fseed ^= fseed << 25; fseed ^= fseed >> 27; fseed *= 0x2545F4914F6CDD1DULL;
        std::string text = STANDIN_LAND;
        size_t len = text.size();
        unsigned r = (unsigned)(fseed >> 32);
        size_t at = len ? (size_t)(r % len) : 0;
        switch (r % 6) {
            case 0: text.resize(at); break;                                       /* truncated */
            case 1: text[at] = (char)(0x20 + (r >> 6) % 0x5f); break;             /* flipped to another printable byte */
            case 2: text.erase(at, 1); break;                                     /* a byte deleted */
            case 3: { static const char *junk[] = {")))", "[[[", "\"\\", "e+9", ",,", "{}", "\n\t", "//"};
                      text.insert(at, junk[(r >> 9) % 8]); break; }               /* junk inserted */
            case 4: {                                                            /* a number turned odd */
                const char *odd[] = {"1e999", "-0", "0x10", "9999999999999999999999", ".", "NaN"};
                text.insert(at, odd[(r >> 12) % 6]);
                break;
            }
            default: text.insert(at, text.substr(at, std::min<size_t>(64, len - at))); break;   /* chunk duplicated */
        }
        std::vector<BasemapShape> fuzz_land;
        std::string fw;
        bool accepted = basemap_land_from_geojson(text, reg, &fuzz_land, &fw);
        bool sound = false;
        if (accepted) {
            std::string fm;
            sound = all_inside(fuzz_land, reg) && !fuzz_land.empty() &&
                    basemap_self_check(basemap_json(*a, fuzz_land, "fuzz"), (long)a->cities.size(), hand_corridors(*a), &fm);
        } else {
            sound = !fw.empty() && fuzz_land.empty();                             /* refused, with a reason and no leftovers */
        }
        CHECK(sound);                                                             /* 600 checks: nothing accepted is invalid */
    }
    printf("  offline map background: %d checks, 600 of them randomly corrupted inputs\n",
           g_tests_passed + g_tests_failed - before);
}

/* ---------- STL cross-checks (STL allowed here ONLY to verify my structures) ---------- */

static uint64_t g_seed = 12345;
static uint64_t rnd() { g_seed ^= g_seed >> 12; g_seed ^= g_seed << 25; g_seed ^= g_seed >> 27; return g_seed * 0x2545F4914F6CDD1DULL; }

static void xcheck_hash() {
    Map<int> mine; std::unordered_map<std::string, int> ref;
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        std::string k = "k" + std::to_string(rnd() % 700);
        int op = (int)(rnd() % 3), v = (int)(rnd() % 1000);
        if (op == 0) { mine.put(k, v); ref[k] = v; }
        else if (op == 1) { same &= mine.remove(k) == (ref.erase(k) == 1); }
        else { int *p = mine.get(k); auto it = ref.find(k); same &= (p != nullptr) == (it != ref.end()); if (p && it != ref.end()) same &= *p == it->second; }
    }
    CHECK(same); CHECK_EQ_INT(mine.size(), ref.size());
}

static void xcheck_heap() {
    MinHeap mine; std::set<std::pair<int64_t, uint64_t>> ref; std::map<uint64_t, int64_t> where;
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        int op = (int)(rnd() % 4); uint64_t id = rnd() % 400; int64_t pr = (int64_t)(rnd() % 100000);
        if (op == 0 && !where.count(id)) { same &= mine.push(pr, id); ref.insert({pr, id}); where[id] = pr; }
        else if (op == 1 && where.count(id)) { same &= mine.update(id, pr); ref.erase({where[id], id}); ref.insert({pr, id}); where[id] = pr; }
        else if (op == 2 && where.count(id)) { same &= mine.remove(id); ref.erase({where[id], id}); where.erase(id); }
        else if (op == 3 && !ref.empty()) {
            int64_t p; uint64_t d; same &= mine.pop(&p, &d);
            auto first = *ref.begin();
            same &= p == first.first && ref.count({p, d}) == 1;    /* ties may pop either id */
            ref.erase({p, d}); where.erase(d);
        }
        same &= mine.size() == ref.size();
    }
    CHECK(same);
}

static void xcheck_queue_stack_vec() {
    Queue<int> q; std::deque<int> dq; Stack<int> s; std::vector<int> sv; Vec<int> v; std::vector<int> rv;
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        int op = (int)(rnd() % 6), x = (int)(rnd() % 1000);
        if (op == 0) { q.push(x); dq.push_back(x); }
        else if (op == 1 && !dq.empty()) { int o; q.pop(&o); same &= o == dq.front(); dq.pop_front(); }
        else if (op == 2) { s.push(x); sv.push_back(x); }
        else if (op == 3 && !sv.empty()) { int o; s.pop(&o); same &= o == sv.back(); sv.pop_back(); }
        else if (op == 4) { size_t pos = rv.empty() ? 0 : rnd() % (rv.size() + 1); ds_vector *raw = nullptr; (void)raw; v.push(x); rv.push_back(x); (void)pos; }
        else if (op == 5 && !rv.empty()) { size_t pos = rnd() % rv.size(); v.remove(pos); rv.erase(rv.begin() + (long)pos); }
        same &= q.size() == dq.size() && s.size() == sv.size() && v.size() == rv.size();
    }
    for (size_t i = 0; i < rv.size(); i++) same &= v[i] == rv[i];
    CHECK(same);
}

static void xcheck_bitset_fenwick() {
    Bits b(130); std::vector<bool> ref(130, false);
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        size_t k = rnd() % 130; int op = (int)(rnd() % 3);
        if (op == 0) { b.set(k); ref[k] = true; } else if (op == 1) { b.clear(k); ref[k] = false; } else same &= b.test(k) == ref[k];
    }
    size_t cnt = 0; for (bool x : ref) cnt += x;
    same &= b.count() == cnt;
    for (size_t k = 1; k <= 5; k++) {                                         /* k-run search vs brute force */
        long want = -1;
        for (size_t i = 0; i + k <= 130 && want < 0; i++) { bool ok = true; for (size_t j = 0; j < k; j++) ok &= !ref[i + j]; if (ok) want = (long)i; }
        same &= b.find_clear_run(0, 130, k) == want;
    }
    CHECK(same);
    ds_fenwick *f = ds_fenwick_create(200); std::vector<int64_t> r(200, 0);
    bool fsame = true;
    for (int i = 0; i < 10000; i++) {
        size_t k = rnd() % 200; int64_t d = (int64_t)(rnd() % 2001) - 1000;
        if (rnd() % 2) { ds_fenwick_add(f, k, d); r[k] += d; }
        else { size_t hi = rnd() % 200, lo = rnd() % (hi + 1); int64_t sum = 0; for (size_t j = lo; j <= hi; j++) sum += r[j]; fsame &= ds_fenwick_range(f, lo, hi) == sum; }
    }
    ds_fenwick_destroy(f);
    CHECK(fsame);
}

static void xcheck_lru_uf_trie() {
    ds_lru *c = ds_lru_create(16, sizeof(int), nullptr);
    std::list<std::pair<std::string, int>> order; std::unordered_map<std::string, std::list<std::pair<std::string, int>>::iterator> idx;
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        std::string k = "k" + std::to_string(rnd() % 40); int v = (int)(rnd() % 1000);
        if (rnd() % 2) {
            ds_lru_put(c, k.c_str(), &v);
            if (idx.count(k)) order.erase(idx[k]);
            order.push_front({k, v}); idx[k] = order.begin();
            if (order.size() > 16) { idx.erase(order.back().first); order.pop_back(); }
        } else {
            int *p = static_cast<int *>(ds_lru_get(c, k.c_str()));
            bool has = idx.count(k) > 0;
            same &= (p != nullptr) == has;
            if (has) { same &= p && *p == idx[k]->second; auto e = *idx[k]; order.erase(idx[k]); order.push_front(e); idx[k] = order.begin(); }
        }
        same &= ds_lru_size(c) == order.size();
    }
    ds_lru_destroy(c);
    CHECK(same);
    ds_unionfind *u = ds_uf_create(300); std::vector<int> label(300);
    for (int i = 0; i < 300; i++) label[(size_t)i] = i;
    bool usame = true;
    for (int i = 0; i < 10000; i++) {
        int a = (int)(rnd() % 300), b = (int)(rnd() % 300);
        if (rnd() % 2) { ds_uf_unite(u, a, b); int la = label[(size_t)a], lb = label[(size_t)b]; for (int &x : label) if (x == lb) x = la; }
        else usame &= (ds_uf_connected(u, a, b) != 0) == (label[(size_t)a] == label[(size_t)b]);
    }
    std::set<int> sets(label.begin(), label.end());
    usame &= ds_uf_count(u) == sets.size();
    ds_uf_destroy(u);
    CHECK(usame);
    ds_trie *t = ds_trie_create(); std::set<std::string> words; bool tsame = true;
    for (int i = 0; i < 3000; i++) {
        std::string w; size_t len = 1 + rnd() % 6; for (size_t j = 0; j < len; j++) w += (char)('a' + rnd() % 4);
        ds_trie_insert(t, w.c_str(), i); words.insert(w);
    }
    for (int i = 0; i < 2000; i++) {
        std::string p; size_t len = rnd() % 4; for (size_t j = 0; j < len; j++) p += (char)('a' + rnd() % 4);
        std::vector<std::string> want;
        for (const std::string &w : words) if (w.compare(0, p.size(), p) == 0 && want.size() < 10) want.push_back(w);
        char out[10][64]; int vals[10];
        size_t n = ds_trie_suggest(t, p.c_str(), 10, out, vals);
        tsame &= n == want.size();
        for (size_t j = 0; j < n && j < want.size(); j++) tsame &= want[j] == out[j];
    }
    ds_trie_destroy(t);
    CHECK(tsame);
}

struct Rec { int key, order; };
static int cmp_rec(const void *a, const void *b) { return static_cast<const Rec *>(a)->key - static_cast<const Rec *>(b)->key; }

static void xcheck_sort_search() {
    bool same = true;
    for (int round = 0; round < 20; round++) {
        size_t n = (size_t)(rnd() % 600);
        std::vector<Rec> a(n);
        for (size_t i = 0; i < n; i++) a[i] = Rec{(int)(rnd() % 100), (int)i};
        std::vector<Rec> m = a, q = a, h = a, ref = a;
        ds_merge_sort(m.data(), n, sizeof(Rec), cmp_rec);
        ds_quick_sort(q.data(), n, sizeof(Rec), cmp_rec);
        ds_heap_sort(h.data(), n, sizeof(Rec), cmp_rec);
        std::stable_sort(ref.begin(), ref.end(), [](const Rec &x, const Rec &y) { return x.key < y.key; });
        for (size_t i = 0; i < n; i++) { same &= m[i].key == ref[i].key && m[i].order == ref[i].order; same &= q[i].key == ref[i].key && h[i].key == ref[i].key; }
        for (int k = 0; k < 50; k++) {
            Rec key{(int)(rnd() % 110) - 5, 0};
            size_t lb = ds_lower_bound(ref.data(), n, sizeof(Rec), &key, cmp_rec), ub = ds_upper_bound(ref.data(), n, sizeof(Rec), &key, cmp_rec);
            size_t wlb = (size_t)(std::lower_bound(ref.begin(), ref.end(), key, [](const Rec &x, const Rec &y) { return x.key < y.key; }) - ref.begin());
            size_t wub = (size_t)(std::upper_bound(ref.begin(), ref.end(), key, [](const Rec &x, const Rec &y) { return x.key < y.key; }) - ref.begin());
            same &= lb == wlb && ub == wub;
        }
    }
    for (int i = 0; i < 2000; i++) {                                           /* KMP vs std::string::find */
        std::string text, pat;
        size_t tl = rnd() % 40, pl = 1 + rnd() % 4;
        for (size_t j = 0; j < tl; j++) text += (char)('a' + rnd() % 3);
        for (size_t j = 0; j < pl; j++) pat += (char)('a' + rnd() % 3);
        size_t pos = text.find(pat);
        long got = ds_kmp_find(text.c_str(), pat.c_str());
        same &= (pos == std::string::npos) ? got == -1 : got == (long)pos;
    }
    CHECK(same);
}

int main() {
    printf("SMARTROUTE APP TESTS\n====================\n");
    make_dirs("build");
    test_catalog(); test_csv_errors(); test_money(); test_booking(); test_prebook(); test_holds_waitlist();
    test_planner(); test_search(); test_tracking(); test_alert_crossing(); test_auth_vendor(); test_persistence();
    test_roles_driver_ops(); test_crew_coupons_share(); test_assistant(); test_spec_extras(); test_ops_persistence();
    test_basemap();
    printf("Testing 10,000-operation STL cross-checks...\n");
    xcheck_hash(); xcheck_heap(); xcheck_queue_stack_vec(); xcheck_bitset_fenwick(); xcheck_lru_uf_trie(); xcheck_sort_search();
    return test_report();
}
