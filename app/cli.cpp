/*
 * PURPOSE: Console screens: search & book, tickets, live tracking, notifications, journey planner,
 *          vendor portal, help, and a scripted --demo session.
 * WHY THIS NAME: implements cli.hpp.
 * CONNECTS TO: app.hpp, ticket.hpp, asciimap.hpp. Input is read with fgets and validated (never scanf).
 * COMPLEXITY: n/a.
 */
#include "cli.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "asciimap.hpp"
#include "ticket.hpp"

namespace sr {

/* ---------- input helpers ---------- */

static bool g_eof = false;

static std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

/* Reads one trimmed line (max 200 chars). Returns false on EOF (Ctrl-D / Ctrl-Z). */
static bool ask(const std::string &prompt, std::string *out) {
    if (g_eof) return false;
    std::printf("%s", prompt.c_str());
    std::fflush(stdout);
    char buf[256];
    if (!std::fgets(buf, sizeof buf, stdin)) { g_eof = true; std::printf("\n"); return false; }
    bool too_long = std::strchr(buf, '\n') == nullptr && !std::feof(stdin);
    if (too_long) { int c; while ((c = std::getchar()) != '\n' && c != EOF) {} }       /* discard the rest of a long line */
    *out = trim(buf);
    return true;
}

static bool parse_int(const std::string &s, int lo, int hi, int *out) {
    if (s.empty() || s.size() > 9) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); k++) if (!std::isdigit((unsigned char)s[k])) return false;
    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < lo || v > hi) return false;
    *out = (int)v;
    return true;
}

static std::string lower_str(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::vector<std::string> words(const std::string &s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) { if (std::isspace((unsigned char)c)) { if (!cur.empty()) { w.push_back(cur); cur.clear(); } } else cur += c; }
    if (!cur.empty()) w.push_back(cur);
    return w;
}


/* ---------- context ---------- */

struct Ctx {
    App &app;
    CliOptions opt;
    int user = 0;                 /* logged-in passenger id (0 = guest) */
    int vendor = 0;               /* logged-in vendor user id (0 = none) */
    explicit Ctx(App &a, const CliOptions &o) : app(a), opt(o) {}
};

static void autosave(Ctx &c) {
    if (!c.opt.autosave) return;
    std::string err;
    if (!c.app.save_all(&err)) std::printf("(warning: could not save: %s)\n", err.c_str());
}

static void header(Ctx &c, const std::string &who_override = "") {
    int uid = c.vendor ? c.vendor : c.user;
    std::string who = !who_override.empty() ? who_override : (uid ? std::string(c.app.user_by_id(uid)->name) : "Guest");
    c.app.flush_outbox();
    std::printf("\n=== SmartRoute | %s | %s | Bell (%d) ===\n", who.c_str(), fmt_datetime(c.app.clock.now()).c_str(), uid ? c.app.unread_count(uid) : 0);
}

/* ---------- common prompts ---------- */

static int ask_city(Ctx &c, const std::string &label) {
    for (;;) {
        std::string s;
        if (!ask(label + " (type a few letters, blank to cancel): ", &s) || s.empty()) return -1;
        int exact = c.app.find_city(s);
        if (exact >= 0) return exact;
        char out[10][64]; int ids[10];
        size_t n = c.app.suggest_cities(s.c_str(), 10, out, ids);            /* trie autosuggest */
        if (n == 0) { std::printf("  No city starts with '%s'. Try Delhi, Jaipur, Mumbai...\n", s.c_str()); continue; }
        for (size_t i = 0; i < n; i++) std::printf("  %zu) %s\n", i + 1, c.app.cities[(size_t)ids[i]].name);
        if (n == 1) return ids[0];
        std::string pick;
        int k = 0;
        if (!ask("  Choose a number: ", &pick)) return -1;
        if (parse_int(pick, 1, (int)n, &k)) return ids[k - 1];
        std::printf("  Please enter a number from the list.\n");
    }
}

static bool ask_date(Ctx &c, int *date_key_out) {
    for (;;) {
        std::string s;
        if (!ask("Date (YYYY-MM-DD, today, tomorrow, +N; blank = cancel): ", &s) || s.empty()) return false;
        int64_t now = c.app.clock.now(), target = 0;
        int n = 0, y, m, d;
        if (lower_str(s) == "today") target = now;
        else if (lower_str(s) == "tomorrow") target = now + MS_DAY;
        else if (s[0] == '+' && parse_int(s, 0, 1000, &n)) target = now + n * MS_DAY;
        else if (parse_ist_date(s.c_str(), &y, &m, &d)) target = from_ist(y, m, d, 12, 0);
        else { std::printf("  Please use YYYY-MM-DD, today, tomorrow or +N.\n"); continue; }
        int64_t ahead = day_number(target) - day_number(now);
        if (ahead < 0 || ahead > 90) { std::printf("  Choose a date from today up to 90 days ahead.\n"); continue; }
        *date_key_out = date_key(target);
        return true;
    }
}

/* ---------- login / register ---------- */

static bool passenger_login(Ctx &c) {
    if (c.user) return true;
    std::printf("\n-- Passenger login --  (demo: user@smartroute.in / Demo@1234)\n  1) Log in   2) Register   b) Back\n");
    std::string ch;
    if (!ask("Choice: ", &ch)) return false;
    if (ch == "2") {
        std::string name, email, phone, pw;
        if (!ask("Full name: ", &name) || !ask("Email: ", &email) || !ask("Phone (optional): ", &phone) || !ask("Password (8+ chars, letter+digit): ", &pw)) return false;
        Result<int> r = c.app.register_passenger(name, email, phone, pw);
        if (!r.ok) { std::printf("  %s\n", r.message.c_str()); return false; }
        c.user = r.value;
        std::printf("  Registered. Welcome, %s!\n", name.c_str());
        autosave(c);
        return true;
    }
    if (ch != "1") return false;
    std::string email, pw;
    if (!ask("Email: ", &email) || !ask("Password: ", &pw)) return false;
    Result<int> r = c.app.login(email, pw, ROLE_PASSENGER);
    if (!r.ok) { std::printf("  %s\n", r.message.c_str()); return false; }
    c.user = r.value;
    std::printf("  %s\n", r.message.c_str());
    return true;
}

/* ---------- seat screen ---------- */

static void print_seat_grid(Ctx &c, const Trip &t, const std::vector<std::string> &selected) {
    const BusType &bt = c.app.types[(size_t)c.app.buses[(size_t)t.bus_idx].type_idx];
    std::vector<SeatCell> cells = c.app.seat_map(t, c.user);
    int cols = bt.layout == LAYOUT_2P2 ? 4 : 3;
    for (size_t i = 0; i < cells.size(); i++) {
        const SeatCell &s = cells[i];
        bool sel = std::find(selected.begin(), selected.end(), s.label) != selected.end();
        char mark = sel ? '*' : (s.state == 1 ? 'X' : (s.state == 2 ? 'H' : (s.state == 3 ? 'M' : ' ')));
        int col = bt.layout == LAYOUT_SLEEPER ? (int)(i % 15) % 3 : (int)i % cols;
        int per_row = bt.layout == LAYOUT_SLEEPER ? 3 : cols;
        if (bt.layout == LAYOUT_SLEEPER && i % 15 == 0) std::printf("  %s deck\n", i == 0 ? "Lower" : "Upper");
        if (col == 0) std::printf("   ");
        std::printf("%-3s[%c] ", s.label, mark);
        if ((bt.layout == LAYOUT_2P2 && col == 1) || (bt.layout == LAYOUT_2P1 && col == 1) || (bt.layout == LAYOUT_SLEEPER && col == 0)) std::printf("  ");  /* aisle */
        if (col == per_row - 1) std::printf("\n");
    }
    std::printf("  Legend: [ ] free  [X] booked  [H] held by others  [*] selected  [M] my hold\n");
}

static void print_quote(const Quote &q, int mode_is_deposit) {
    std::printf("  Fare (seats)     %12s\n", fmt_rs(q.base).c_str());
    std::printf("  Coupon discount  %12s\n", fmt_rs(-q.discount).c_str());
    std::printf("  Tax (5%%)         %12s\n", fmt_rs(q.tax).c_str());
    std::printf("  Insurance        %12s\n", fmt_rs(q.insurance).c_str());
    std::printf("  TOTAL            %12s\n", fmt_rs(q.total).c_str());
    if (mode_is_deposit) std::printf("  Pay now (20%%)    %12s   balance %s due %s\n", fmt_rs(q.deposit).c_str(), fmt_rs(q.balance).c_str(), fmt_datetime(q.balance_due_ms).c_str());
}

static int ask_stop(Ctx &c, const Trip &t, int type, const char *label) {
    const Route &r = c.app.routes[(size_t)t.route_id];
    std::vector<int> opts;
    for (int i = 0; i < r.n_stops; i++) if (r.stops[i].type == type) opts.push_back(i);
    for (;;) {
        for (size_t i = 0; i < opts.size(); i++) std::printf("  %zu) %s  (about %s)\n", i + 1, r.stops[opts[i]].name, fmt_hm_plus(c.app.planned_stop_time(t, opts[i]), t.depart_ms).c_str());
        std::string s;
        int k = 0;
        if (!ask(std::string(label) + " point number: ", &s)) return -1;
        if (parse_int(s, 1, (int)opts.size(), &k)) return opts[(size_t)k - 1];
        std::printf("  Enter 1 to %zu.\n", opts.size());
    }
}

/* Pay for a booking with the simulated gateway. Returns true when paid. */
static bool payment_screen(Ctx &c, int booking_id, int kind) {
    for (;;) {
        Booking *b = c.app.booking_by_id(booking_id);
        int64_t amount = kind == PAYK_FULL ? b->total : (kind == PAYK_DEPOSIT ? deposit_paise(b->total) : b->total - b->paid);
        std::printf("\n-- Payment (DEMO PAYMENT - no money is charged; no card details are asked) --\n  Amount: %s\n  Choose the simulated outcome: 1) success  2) declined  3) timeout  b) cancel\n", fmt_rs(amount).c_str());
        std::string s;
        int k = 0;
        if (!ask("Outcome: ", &s) || s == "b") return false;
        if (!parse_int(s, 1, 3, &k)) { std::printf("  Enter 1, 2, 3 or b.\n"); continue; }
        Result<Unit> r = c.app.pay_booking(booking_id, c.user, kind, k - 1);
        std::printf("  %s\n", r.message.c_str());
        autosave(c);
        if (r.ok) return true;
        if (r.code != Err::PAYMENT_DECLINED) return false;
    }
}

static void book_flow(Ctx &c, Trip &t, const std::vector<std::string> &seats) {
    std::vector<const char *> labels;
    for (const std::string &s : seats) labels.push_back(s.c_str());
    BookingRequest rq;
    rq.user_id = c.user; rq.trip_id = t.id; rq.n_pax = (int)seats.size();
    for (size_t i = 0; i < seats.size(); i++) {
        std::string name, age, gender;
        std::snprintf(rq.pax[i].seat, sizeof rq.pax[i].seat, "%s", seats[i].c_str());
        std::printf("Passenger for seat %s:\n", seats[i].c_str());
        for (;;) {
            if (!ask("  Name: ", &name) || !ask("  Age: ", &age) || !ask("  Gender (M/F/O): ", &gender)) return;
            int a = 0;
            std::string g = gender.empty() ? "" : std::string(1, (char)std::toupper((unsigned char)gender[0]));
            if (name.empty() || name.size() > 31) { std::printf("  Enter a name up to 31 characters.\n"); continue; }
            if (!parse_int(age, 1, 120, &a)) { std::printf("  Age must be 1 to 120.\n"); continue; }
            if (g != "M" && g != "F" && g != "O") { std::printf("  Gender must be M, F or O.\n"); continue; }
            for (char &ch : name) if (ch == '|' || ch == '~' || ch == ';' || ch == '\\') ch = ' ';
            std::snprintf(rq.pax[i].name, sizeof rq.pax[i].name, "%s", name.c_str());
            rq.pax[i].age = a; rq.pax[i].gender = g[0];
            break;
        }
    }
    std::printf("Where will you board?\n");
    if ((rq.boarding_stop = ask_stop(c, t, ST_BOARDING, "Boarding")) < 0) return;
    std::printf("Where will you get off?\n");
    if ((rq.dropping_stop = ask_stop(c, t, ST_DROPPING, "Dropping")) < 0) return;
    std::string contact, ins, coupon;
    if (!ask("Contact phone or email: ", &contact) || !ask("Add travel insurance Rs 15/passenger? (y/N): ", &ins) || !ask("Coupon code (blank = none; try SMART50): ", &coupon)) return;
    std::snprintf(rq.contact, sizeof rq.contact, "%s", contact.c_str());
    std::snprintf(rq.coupon, sizeof rq.coupon, "%s", coupon.c_str());
    rq.insurance = !ins.empty() && (ins[0] == 'y' || ins[0] == 'Y');
    Result<Quote> q = c.app.quote(t.id, labels.data(), (int)labels.size(), rq.coupon, rq.insurance, c.user);
    if (!q.ok) { std::printf("  %s\n", q.message.c_str()); return; }
    std::printf("\n-- Price --\n");
    print_quote(q.value, 0);
    if (q.value.prebook_eligible) {
        std::printf("  Pre-booking is available: pay 20%% now (%s), the balance %s by %s.\n", fmt_rs(q.value.deposit).c_str(), fmt_rs(q.value.balance).c_str(), fmt_datetime(q.value.balance_due_ms).c_str());
        std::string m;
        if (!ask("Pay 1) in full  2) 20% deposit now: ", &m)) return;
        rq.mode = m == "2" ? MODE_DEPOSIT : MODE_FULL;
    }
    std::printf("\n%s", cancellation_table().c_str());
    std::string ok;
    if (!ask("Confirm and go to payment? (y/N): ", &ok) || (ok != "y" && ok != "Y")) { c.app.release_user_holds(t.id, c.user); std::printf("  Cancelled; seats released.\n"); return; }
    Result<int> b = c.app.create_booking(rq);
    if (!b.ok) { std::printf("  %s\n", b.message.c_str()); return; }
    autosave(c);
    if (payment_screen(c, b.value, rq.mode == MODE_DEPOSIT ? PAYK_DEPOSIT : PAYK_FULL)) {
        std::printf("\n%s", render_ticket(c.app, *c.app.booking_by_id(b.value), c.opt.color).c_str());
    } else std::printf("  Booking %s is waiting for payment; your seats stay held until the timer ends.\n", c.app.booking_by_id(b.value)->pnr);
}

static void seat_screen(Ctx &c, Trip &t) {
    std::vector<std::string> selected;
    const BusType &bt = c.app.types[(size_t)c.app.buses[(size_t)t.bus_idx].type_idx];
    for (;;) {
        header(c);
        std::printf("Seats: %s  %s  %s -> departs %s\n", c.app.bus_label(t.bus_idx).c_str(), bt.label, c.app.route_name(c.app.routes[(size_t)t.route_id]).c_str(), fmt_datetime(t.depart_ms).c_str());
        print_seat_grid(c, t, selected);
        int64_t exp = c.app.hold_expiry(t.id, c.user);
        if (exp > 0) { int64_t left = (exp - c.app.clock.now()) / 1000; std::printf("  Your hold ends in %02lld:%02lld\n", (long long)(left / 60), (long long)(left % 60)); }
        if (t.booked.count() + t.held.count() >= c.app.seat_count(t)) std::printf("  SOLD OUT. Type 'w' to join the waitlist.\n");
        std::printf("Commands: pick 1A 2B ...  |  g N (find N adjacent)  |  c (continue)  |  w (waitlist)  |  b (back)\n");
        std::string line;
        if (!ask("> ", &line)) return;
        std::vector<std::string> w = words(line);
        if (w.empty()) continue;
        std::string cmd = lower_str(w[0]);
        if (cmd == "b") { c.app.release_user_holds(t.id, c.user); return; }
        if (cmd == "pick") {
            selected.assign(w.begin() + 1, w.end());
            for (std::string &s : selected) for (char &ch : s) ch = (char)std::toupper((unsigned char)ch);
        } else if (cmd == "g") {
            int n = 0;
            if (w.size() < 2 || !parse_int(w[1], 1, MAX_PAX, &n)) { std::printf("  Usage: g 3  (1 to 6)\n"); continue; }
            long at = c.app.find_adjacent(t, n);
            if (at < 0) { std::printf("  No %d adjacent free seats in one row.\n", n); continue; }
            selected.clear();
            for (int i = 0; i < n; i++) { char lb[6]; c.app.seat_label(bt, (int)at + i, lb); selected.push_back(lb); }
        } else if (cmd == "w") {
            Result<Unit> r = c.app.join_waitlist(c.user, t.id, 1);
            std::printf("  %s\n", r.message.c_str());
        } else if (cmd == "c") {
            if (selected.empty()) { std::printf("  Pick at least one seat first (pick 1A 1B).\n"); continue; }
            std::vector<const char *> labels;
            for (const std::string &s : selected) labels.push_back(s.c_str());
            Result<Unit> r = c.app.hold_seats(c.user, t.id, labels.data(), (int)labels.size());
            if (!r.ok) { std::printf("  %s\n", r.message.c_str()); continue; }
            std::printf("  %s\n", r.message.c_str());
            book_flow(c, t, selected);
            autosave(c);
            return;
        } else std::printf("  Unknown command.\n");
    }
}

/* ---------- search ---------- */

static void print_results(Ctx &c, Vec<SearchRow> &rows) {
    if (rows.size() == 0) { std::printf("No buses match. Try 'f' then 'x' to clear filters, or another date.\n"); return; }
    std::printf("\n %-2s %-24s %-22s %-5s %-9s %-7s %-9s %s\n", "#", "Operator (demo)", "Type", "Dep", "Arr", "Dur", "From Rs", "Seats");
    for (size_t i = 0; i < rows.size(); i++) {
        const SearchRow &r = rows[i];
        const Bus &b = c.app.buses[(size_t)r.bus_idx];
        std::printf(" %-2zu %-24s %-22s %-5s %-9s %-7s %-9lld %d\n", i + 1, c.app.companies[(size_t)b.company_id - 1].name, c.app.types[(size_t)b.type_idx].label,
                    fmt_hm(r.depart_ms).c_str(), fmt_hm_plus(r.arrive_ms, r.depart_ms).c_str(), fmt_duration(r.duration_min).c_str(), (long long)(r.fare_min / 100), r.seats_left);
        std::printf("    %s  %s | No ratings yet (demo data)\n", journey_bar(c.app.routes[(size_t)r.route_id], -1).c_str(), c.app.buses[(size_t)r.bus_idx].reg);
    }
}

static void edit_filters(Ctx &c, SearchFilters *f) {
    std::printf("Filters: 1 departure window  2 max fare  3 operator name  4 amenity  5 seats available only  x clear all  b back\n");
    std::string s;
    if (!ask("> ", &s)) return;
    int v = 0;
    if (s == "x") *f = SearchFilters();
    else if (s == "1") { std::string w; if (ask("Window 0 night, 1 morning, 2 afternoon, 3 evening, blank = any: ", &w)) f->dep_window = parse_int(w, 0, 3, &v) ? v : -1; }
    else if (s == "2") { std::string w; if (ask("Max fare in Rs (blank = none): ", &w)) f->max_fare = parse_int(w, 1, 1000000, &v) ? (int64_t)v * 100 : 0; }
    else if (s == "3") { std::string w; if (ask("Operator contains: ", &w)) std::snprintf(f->operator_substr, sizeof f->operator_substr, "%s", w.c_str()); }
    else if (s == "4") { std::string w; if (ask("Amenity contains (e.g. AC, Blanket): ", &w)) std::snprintf(f->amenity, sizeof f->amenity, "%s", w.c_str()); }
    else if (s == "5") f->need_seats = !f->need_seats;
    (void)c;
}

static void screen_search(Ctx &c) {
    header(c);
    std::printf("-- Search & book --\n");
    int from = ask_city(c, "From");
    if (from < 0) return;
    int to = ask_city(c, "To");
    if (to < 0) return;
    if (from == to) { std::printf("  Choose two different cities.\n"); return; }
    if (c.app.find_route(from, to) < 0) {
        std::printf("  There is no direct bus route %s -> %s.%s\n", c.app.cities[(size_t)from].name, c.app.cities[(size_t)to].name,
                    c.app.same_network(from, to) ? " Try the Journey planner (menu 5): you can connect through other cities." : " These cities are not in the same bus network.");
        return;
    }
    int dk = 0;
    if (!ask_date(c, &dk)) return;
    SearchFilters f;
    SortKey sort = SORT_EARLIEST;
    for (;;) {
        Vec<SearchRow> rows;
        c.app.search(from, to, dk, f, sort, rows);
        std::printf("\n%s -> %s on %d (DEMO data)", c.app.cities[(size_t)from].name, c.app.cities[(size_t)to].name, dk);
        print_results(c, rows);
        std::printf("\nCommands: number = open bus | f filters | s sort | b back\n");
        std::string s;
        if (!ask("> ", &s) || s == "b") return;
        int k = 0;
        if (s == "f") edit_filters(c, &f);
        else if (s == "s") {
            std::string w;
            if (ask("Sort: 1 earliest 2 latest 3 cheapest 4 fastest 5 most seats: ", &w) && parse_int(w, 1, 5, &k)) sort = (SortKey)(k - 1);
        } else if (parse_int(s, 1, (int)rows.size(), &k)) {
            if (!passenger_login(c)) continue;
            Trip *t = c.app.trip_by_id(rows[(size_t)k - 1].trip_id);
            if (t) seat_screen(c, *t);
        } else std::printf("  Unknown command.\n");
    }
}

/* ---------- tickets ---------- */

static std::vector<Booking *> my_bookings(Ctx &c) {
    std::vector<Booking *> v;
    for (size_t i = c.app.bookings.size(); i-- > 0;) if (c.app.bookings[i].user_id == c.user) v.push_back(c.app.bookings.at(i));
    return v;
}

static void track_screen(Ctx &c, Trip &t);

static void ticket_actions(Ctx &c, Booking &b) {
    for (;;) {
        std::printf("\n%s", render_ticket(c.app, b, c.opt.color).c_str());
        std::printf("Actions: t track | c cancel | p pay balance | s share | b back\n");
        std::string s;
        if (!ask("> ", &s) || s == "b") return;
        Trip *t = c.app.trip_by_id(b.trip_id);
        if (s == "t") track_screen(c, *t);
        else if (s == "s") std::printf("%s\n", share_summary(c.app, b).c_str());
        else if (s == "p") {
            if (b.status != BK_PREBOOKED) std::printf("  Nothing to pay on this booking.\n");
            else if (payment_screen(c, b.id, PAYK_BALANCE)) std::printf("  Balance paid. Booking confirmed.\n");
        } else if (s == "c") {
            RefundPreview p = c.app.cancel_preview(b);
            std::printf("  Departure in %lld h -> refund %d%% of the refundable fare: %s (paid %s).\n", (long long)p.hours_left, p.pct, fmt_rs(p.refund).c_str(), fmt_rs(b.paid).c_str());
            std::string ok;
            if (!ask("  Cancel this booking? (y/N): ", &ok) || (ok != "y" && ok != "Y")) continue;
            Result<int64_t> r = c.app.cancel_booking(b.id, c.user);
            std::printf("  %s\n", r.ok ? ("Cancelled. Refund " + fmt_rs(r.value) + " (DEMO - no real money moved).").c_str() : r.message.c_str());
            autosave(c);
        } else std::printf("  Unknown command.\n");
    }
}

static void screen_trips(Ctx &c) {
    if (!passenger_login(c)) return;
    for (;;) {
        header(c);
        std::vector<Booking *> list = my_bookings(c);
        std::printf("-- My trips & tickets --\n");
        if (list.empty()) { std::printf("  You have no bookings yet. Use menu 1 to search and book.\n"); return; }
        for (size_t i = 0; i < list.size(); i++) {
            const Trip *t = c.app.trip_by_id(list[i]->trip_id);
            std::printf("  %zu) %s  %-22s %s  %s  [%s]\n", i + 1, list[i]->pnr, c.app.route_name(c.app.routes[(size_t)t->route_id]).c_str(), fmt_datetime(t->depart_ms).c_str(), fmt_rs(list[i]->total).c_str(), booking_status_name(list[i]->status));
        }
        std::string s;
        int k = 0;
        if (!ask("Open number (or PNR), b = back: ", &s) || s == "b") return;
        Booking *b = parse_int(s, 1, (int)list.size(), &k) ? list[(size_t)k - 1] : c.app.booking_by_pnr(s, c.user);
        if (!b) { std::printf("  No such booking.\n"); continue; }
        ticket_actions(c, *b);
    }
}

/* ---------- tracking ---------- */

static void print_track(Ctx &c, Trip &t) {
    const Route &r = c.app.routes[(size_t)t.route_id];
    TrackInfo ti = c.app.track_info(t);
    std::printf("\n-- Driver GPS Stream Simulator - Demo only, simulated coordinates --\n");
    std::printf("Mode: DEMO SIMULATION | %s%s | speed x%d | %s\n", t.jam_left_km > 0 ? "DEMO TRAFFIC: jam ahead" : "DEMO TRAFFIC: clear", ti.on_break ? " | ON BREAK" : "", t.sim_speed, c.app.route_name(r).c_str());
    std::printf("Progress %.0f%% | %.0f km left | speed %.0f km/h | scheduled %s | estimated %s | delay %d min%s\n", ti.progress_pct, ti.km_left, ti.speed_kmh,
                fmt_hm_plus(ti.sched_arrive, t.depart_ms).c_str(), fmt_hm_plus(ti.est_arrive, t.depart_ms).c_str(), ti.delay_min,
                t.delay_reason[0] ? (std::string(" (reported: ") + t.delay_reason + ")").c_str() : "");
    if (ti.on_break) std::printf("Bus stopped at a break station - about %d min left.\n", ti.break_min_left);
    std::printf("%s", render_route_map(c.app, r, t.km, c.opt.color).c_str());
    std::printf("%s\n", journey_bar(r, t.km, 60).c_str());
    for (int i = 0; i < r.n_stops; i++) {
        const Stop &s = r.stops[i];
        if (s.type == ST_BOARDING || s.type == ST_DROPPING) continue;
        const char *st = t.km >= s.km + 1e-9 ? "Done    " : (std::fabs(t.km - s.km) < 1e-6 ? "Now     " : "Upcoming");
        std::printf("  %s %-26s km %-4.0f %s%s\n", st, s.name, s.km, fmt_hm_plus(c.app.planned_stop_time(t, i), t.depart_ms).c_str(), s.type == ST_TOLL ? "  (toll, no stop)" : (s.type == ST_BREAK ? ("  (break " + std::to_string(s.break_min) + " min)").c_str() : ""));
    }
    export_geojson(c.app, r, c.opt.autosave && !c.app.save_dir.empty() ? c.app.save_dir + "/route.geojson" : "route.geojson");
}

static void track_screen(Ctx &c, Trip &t) {
    for (;;) {
        print_track(c, t);
        std::printf("Controls: 1/2/3 speed x60/x120/x300 | p play (30 ticks) | a advance +10 km | j inject jam | r reset | b back\n");
        std::string s;
        if (!ask("> ", &s) || s == "b") { autosave(c); return; }
        if (s == "1") t.sim_speed = 60; else if (s == "2") t.sim_speed = 120; else if (s == "3") t.sim_speed = 300;
        else if (s == "p") for (int i = 0; i < 30; i++) c.app.sim_advance(t, t.sim_speed);       /* 1 tick = 1 real second */
        else if (s == "a") c.app.sim_advance_km(t, 10);
        else if (s == "j") c.app.sim_jam(t);
        else if (s == "r") c.app.sim_reset(t);
        else std::printf("  Unknown control.\n");
    }
}

static void screen_track(Ctx &c) {
    if (!passenger_login(c)) return;
    std::vector<Booking *> list;
    for (Booking *b : my_bookings(c)) if (b->status == BK_CONFIRMED || b->status == BK_PREBOOKED || b->status == BK_COMPLETED) list.push_back(b);
    if (list.empty()) { std::printf("  No paid bookings to track yet.\n"); return; }
    for (size_t i = 0; i < list.size(); i++) std::printf("  %zu) %s %s\n", i + 1, list[i]->pnr, c.app.route_name(c.app.routes[(size_t)c.app.trip_by_id(list[i]->trip_id)->route_id]).c_str());
    std::string s;
    int k = 0;
    if (ask("Track number: ", &s) && parse_int(s, 1, (int)list.size(), &k)) track_screen(c, *c.app.trip_by_id(list[(size_t)k - 1]->trip_id));
}

/* ---------- notifications ---------- */

static void screen_notifications(Ctx &c) {
    if (!passenger_login(c)) return;
    static const char *CATS[NK_COUNT] = {"booking", "journey", "breaks", "tolls", "traffic", "dropoff", "reminders"};
    for (;;) {
        header(c);
        auto list = c.app.notifications_for(c.user);
        std::printf("-- Notifications (the bell) --\n");
        if (list.empty()) std::printf("  Nothing yet.\n");
        for (size_t i = 0; i < list.size() && i < 20; i++)
            std::printf(" %s %s  %s: %s\n", list[i]->read ? " " : "*", fmt_hm(list[i]->created_ms).c_str(), list[i]->title, list[i]->body);
        std::printf("m mark all read | p preferences | b back\n");
        std::string s;
        if (!ask("> ", &s) || s == "b") return;
        if (s == "m") c.app.mark_all_read(c.user);
        else if (s == "p") {
            int mask = c.app.user_by_id(c.user)->prefs;
            for (int i = 0; i < NK_COUNT; i++) std::printf("  %d) %-10s %s\n", i + 1, CATS[i], (mask >> i) & 1 ? "ON" : "off");
            std::string w;
            int k = 0;
            if (ask("Toggle number: ", &w) && parse_int(w, 1, NK_COUNT, &k)) c.app.set_prefs(c.user, mask ^ (1 << (k - 1)));
        }
        autosave(c);
    }
}

/* ---------- journey planner ---------- */

static void screen_planner(Ctx &c) {
    header(c);
    std::printf("-- Journey planner (multi-leg, 60 min layover between buses) --\n");
    int from = ask_city(c, "From");
    if (from < 0) return;
    int to = ask_city(c, "To");
    if (to < 0) return;
    std::string m;
    int k = 0;
    if (!ask("Optimise for 1) fastest 2) cheapest 3) fewest transfers: ", &m) || !parse_int(m, 1, 3, &k)) return;
    if (from != to && !c.app.same_network(from, to)) { std::printf("  No route: these cities are in different bus networks (%zu networks in total).\n", c.app.network_count()); return; }
    Vec<PlanLeg> legs;
    int64_t mins = 0, paise = 0;
    Result<Unit> r = c.app.plan_journey(from, to, (PlanMode)(k - 1), legs, &mins, &paise);
    if (!r.ok) { std::printf("  %s\n", r.message.c_str()); return; }
    for (size_t i = 0; i < legs.size(); i++) {
        std::printf("  Leg %zu: %s -> %s  %s, from %s per seat\n", i + 1, c.app.cities[(size_t)legs[i].from_city].name, c.app.cities[(size_t)legs[i].to_city].name, fmt_duration((int)legs[i].minutes).c_str(), fmt_rs(legs[i].paise).c_str());
        for (size_t s = 0; s < c.app.schedules.size(); s++)
            if (c.app.schedules[s].route_id == legs[i].route_id && c.app.buses[(size_t)c.app.schedules[s].bus_idx].status != BUS_MAINTENANCE)
                std::printf("          %s dep %02d:%02d\n", c.app.bus_label(c.app.schedules[s].bus_idx).c_str(), c.app.schedules[s].depart_min_of_day / 60, c.app.schedules[s].depart_min_of_day % 60);
    }
    std::printf("  Total: %s (including %zu layover(s)), lowest fares %s per seat\n", fmt_duration((int)mins).c_str(), legs.size() - 1, fmt_rs(paise).c_str());
}

/* ---------- vendor portal ---------- */

static int company_of(Ctx &c) { return c.app.user_by_id(c.vendor)->company_id; }

static void vendor_overview(Ctx &c) {
    int co = company_of(c), by_status[3] = {0, 0, 0}, ss[4] = {0, 0, 0, 0}, next24 = 0, active = 0;
    for (size_t i = 0; i < c.app.buses.size(); i++) if (c.app.buses[i].company_id == co) by_status[c.app.buses[i].status]++;
    for (size_t i = 0; i < c.app.staff.size(); i++) if (c.app.staff[i].company_id == co) ss[c.app.staff_status(c.app.staff[i])]++;
    for (size_t i = 0; i < c.app.trips.size(); i++) {
        const Trip &t = *c.app.trips[i];
        if (c.app.buses[(size_t)t.bus_idx].company_id == co && t.depart_ms >= c.app.clock.now() && t.depart_ms - c.app.clock.now() <= 24 * MS_HOUR) next24++;
    }
    for (size_t i = 0; i < c.app.bookings.size(); i++) {
        const Booking &b = c.app.bookings[i];
        if ((b.status == BK_PREBOOKED || b.status == BK_CONFIRMED) && c.app.buses[(size_t)c.app.trip_by_id(b.trip_id)->bus_idx].company_id == co) active++;
    }
    std::printf("Company: %s\n  Buses: %d available, %d on trip, %d in maintenance\n  Staff: %d available, %d assigned, %d on trip, %d off duty\n  Trips departing in the next 24 h: %d\n  Active bookings: %d\n  Demo money collected (paid minus refunded, Fenwick range query): %s\n",
                c.app.companies[(size_t)co - 1].name, by_status[0], by_status[1], by_status[2], ss[SS_AVAILABLE], ss[SS_ASSIGNED], ss[SS_ON_TRIP], ss[SS_OFF_DUTY], next24, active, fmt_rs(c.app.revenue_range(co, 0, 119)).c_str());
}

static void vendor_fleet(Ctx &c) {
    int co = company_of(c);
    std::printf("  %-9s %-22s %-12s %-18s %-18s\n", "Bus", "Type", "Status", "Driver", "Conductor");
    for (size_t i = 0; i < c.app.buses.size(); i++) {
        const Bus &b = c.app.buses[i];
        if (b.company_id != co) continue;
        std::printf("  %-9s %-22s %-12s %-18s %-18s\n", b.reg, c.app.types[(size_t)b.type_idx].label, bus_status_name(b.status),
                    b.driver_id > 0 ? c.app.staff[(size_t)b.driver_id - 1].name : "-", b.conductor_id > 0 ? c.app.staff[(size_t)b.conductor_id - 1].name : "-");
    }
}

static void vendor_staff(Ctx &c) {
    int co = company_of(c);
    std::printf("  %-3s %-18s %-10s %-10s %s\n", "ID", "Name", "Role", "Status", "Phone (DEMO number)");
    for (size_t i = 0; i < c.app.staff.size(); i++) {
        const Staff &s = c.app.staff[i];
        if (s.company_id == co) std::printf("  %-3d %-18s %-10s %-10s %s\n", s.id, s.name, s.role == STAFF_DRIVER ? "Driver" : "Conductor", staff_status_name(c.app.staff_status(s)), s.phone);
    }
}

static void vendor_manifest(Ctx &c) {
    int co = company_of(c);
    std::printf("Upcoming trips for your buses (trips opened so far):\n");
    std::vector<Trip *> mine;
    for (size_t i = 0; i < c.app.trips.size(); i++) if (c.app.buses[(size_t)c.app.trips[i]->bus_idx].company_id == co) mine.push_back(c.app.trips[i]);
    for (size_t i = 0; i < mine.size() && i < 15; i++)
        std::printf("  %zu) trip %d %s %s %s [%s]\n", i + 1, mine[i]->id, c.app.buses[(size_t)mine[i]->bus_idx].reg, c.app.route_name(c.app.routes[(size_t)mine[i]->route_id]).c_str(), fmt_datetime(mine[i]->depart_ms).c_str(), mine[i]->status == TRIP_SCHEDULED ? "scheduled" : (mine[i]->status == TRIP_RUNNING ? "running" : "completed"));
    std::string s;
    int k = 0;
    if (mine.empty() || !ask("Manifest for number: ", &s) || !parse_int(s, 1, (int)std::min<size_t>(mine.size(), 15), &k)) return;
    Trip &t = *mine[(size_t)k - 1];
    bool reveal = t.depart_ms - c.app.clock.now() <= 24 * MS_HOUR;               /* masked unless departure is within 24 h */
    int n = 0;
    for (size_t i = 0; i < c.app.bookings.size(); i++) {
        const Booking &b = c.app.bookings[i];
        if (b.trip_id != t.id || (b.status != BK_PREBOOKED && b.status != BK_CONFIRMED)) continue;
        for (int p = 0; p < b.n_pax; p++, n++) std::printf("  %-4s %-20s %s %-10s contact %s\n", b.pax[p].seat, b.pax[p].name, b.pnr, booking_status_name(b.status), c.app.mask_phone(b.contact, reveal).c_str());
    }
    if (n == 0) std::printf("  No active bookings on this trip.\n");
    std::string a;
    if (!ask("Actions: d report delay | s start | f finish | b back: ", &a)) return;
    if (a == "s") { Result<Unit> r = c.app.start_trip(co, t.id); std::printf("  %s\n", r.message.c_str()); }
    else if (a == "f") { Result<Unit> r = c.app.complete_trip(co, t.id); std::printf("  %s\n", r.message.c_str()); }
    else if (a == "d") {
        std::string reason, mins, note;
        if (!ask("Reason (TRAFFIC/WEATHER/BREAKDOWN/ROADWORK/OTHER): ", &reason) || !ask("Minutes (1-600): ", &mins) || !ask("Note (optional): ", &note)) return;
        int m = 0;
        if (!parse_int(mins, 1, 600, &m)) { std::printf("  Minutes must be 1 to 600.\n"); return; }
        for (char &ch : reason) ch = (char)std::toupper((unsigned char)ch);
        Result<Unit> r = c.app.report_delay(co, t.id, reason, m, note);
        std::printf("  %s\n", r.message.c_str());
    }
}

static void vendor_assign(Ctx &c) {
    int co = company_of(c);
    vendor_fleet(c);
    vendor_staff(c);
    std::string reg, sid;
    if (!ask("Bus registration (e.g. DEMO-001): ", &reg) || !ask("Staff ID: ", &sid)) return;
    int id = 0;
    if (!parse_int(sid, 1, 100000, &id)) { std::printf("  Staff ID must be a number.\n"); return; }
    for (char &ch : reg) ch = (char)std::toupper((unsigned char)ch);
    Result<Unit> r = c.app.assign_staff(co, c.app.find_bus(reg.c_str()), id);
    std::printf("  %s\n", r.message.c_str());
}

static void vendor_status(Ctx &c) {
    int co = company_of(c);
    vendor_fleet(c);
    std::string reg, st;
    if (!ask("Bus registration: ", &reg) || !ask("New status 1 AVAILABLE, 2 ON_TRIP, 3 MAINTENANCE: ", &st)) return;
    int k = 0;
    if (!parse_int(st, 1, 3, &k)) { std::printf("  Enter 1, 2 or 3.\n"); return; }
    for (char &ch : reg) ch = (char)std::toupper((unsigned char)ch);
    int affected = 0, bi = c.app.find_bus(reg.c_str());
    Result<Unit> r = c.app.set_bus_status(co, bi, k - 1, false, &affected);
    if (!r.ok && r.code == Err::BAD_STATE) {
        std::printf("  %s\n", r.message.c_str());
        std::string ok;
        if (ask("  Confirm? Passengers will be notified (y/N): ", &ok) && (ok == "y" || ok == "Y")) r = c.app.set_bus_status(co, bi, k - 1, true, &affected);
    }
    std::printf("  %s\n", r.message.c_str());
}

static const char *VENDOR_GUIDE =
    "Vendor guide (numbers match the menu):\n"
    "  1 Overview      - counts of buses/staff, departures in 24 h, active bookings, demo money.\n"
    "  2 Fleet         - your buses, status, assigned driver and conductor.\n"
    "  3 Staff         - drivers and conductors with derived status (off duty / assigned / on trip / available).\n"
    "  4 Bookings      - manifests per trip; phone numbers are masked until 24 h before departure.\n"
    "  5 Assign staff  - one driver and one conductor per bus; off-duty or busy staff are refused.\n"
    "  6 Bus status    - maintenance hides the bus from search; bookings within 24 h need confirmation.\n"
    "  7 Auto-assign   - greedy, no overlaps: sort trips by start, min-heap of free-at times, 8 h rest.\n"
    "  8 Undo          - reverts your last status change or assignment.\n"
    "  The 'Bookings' screen also starts/finishes trips and reports delays (1-600 min; a new report replaces the old).\n";

static void screen_vendor(Ctx &c) {
    if (!c.vendor) {
        std::printf("\n-- Vendor portal login --  (demo: vendor@smartroute.in / Demo@1234)\n");
        std::string email, pw;
        if (!ask("Vendor email: ", &email) || !ask("Password: ", &pw)) return;
        Result<int> r = c.app.login(email, pw, ROLE_VENDOR);
        if (!r.ok) { std::printf("  %s\n", r.message.c_str()); return; }
        c.vendor = r.value;
    }
    for (;;) {
        header(c, std::string("VENDOR: ") + c.app.companies[(size_t)company_of(c) - 1].name);
        std::printf("1 Overview  2 Fleet  3 Staff  4 Bookings & manifests  5 Assign staff  6 Bus status  7 Auto-assign  8 Undo  9 Guide  0 Log out\n");
        std::string s;
        if (!ask("> ", &s)) return;
        int co = company_of(c);
        if (s == "1") vendor_overview(c);
        else if (s == "2") vendor_fleet(c);
        else if (s == "3") vendor_staff(c);
        else if (s == "4") vendor_manifest(c);
        else if (s == "5") vendor_assign(c);
        else if (s == "6") vendor_status(c);
        else if (s == "7") {
            std::string rep;
            int n = c.app.auto_assign(co, date_key(c.app.clock.now() + MS_DAY), &rep);
            std::printf("  Auto-assigned %d slot(s) for tomorrow.\n%s", n, rep.c_str());
        } else if (s == "8") { Result<std::string> r = c.app.undo_last(co); std::printf("  %s\n", r.ok ? r.value.c_str() : r.message.c_str()); }
        else if (s == "9") std::printf("%s", VENDOR_GUIDE);
        else if (s == "0") { c.vendor = 0; return; }
        else std::printf("  Unknown choice.\n");
        autosave(c);
    }
}

/* ---------- help + time machine ---------- */

static void screen_help() {
    std::printf("\nSmartRoute help (DEMO: sample operators, buses, staff, phones, fares, stops, tolls; simulated payments, traffic and GPS)\n"
                "  1 Search & book   pick cities (autosuggest), date, open a bus, pick seats, pay (simulated), get a ticket.\n"
                "  2 My trips        tickets, cancel with exact refund preview, pay the balance of a pre-booking.\n"
                "  3 Live track      simulated bus with break stops, tolls, traffic jam and ETA.\n"
                "  4 Notifications   the one bell: unread count, mark read, preferences.\n"
                "  5 Journey planner multi-leg trips (Dijkstra / BFS) between cities in the same network.\n"
                "  6 Vendor portal   separate login for bus operators.\n"
                "  T Time machine    advance the virtual clock (30m, 2h, 3d) to see holds expire and alerts fire.\n"
                "  Pre-booking: 3 to 90 days ahead pay 20%% now; balance due 48 h before departure.\n"
                "  Passenger demo login: user@smartroute.in / Demo@1234\n");
}

static void time_machine(Ctx &c) {
    std::string s;
    if (!ask("Advance the virtual clock by (e.g. 30m, 2h, 3d): ", &s) || s.size() < 2) return;
    int n = 0;
    char unit = (char)std::tolower((unsigned char)s.back());
    if (!parse_int(s.substr(0, s.size() - 1), 1, 100000, &n) || (unit != 'm' && unit != 'h' && unit != 'd')) { std::printf("  Use a number followed by m, h or d.\n"); return; }
    c.app.advance_clock(n * (unit == 'm' ? MS_MIN : (unit == 'h' ? MS_HOUR : MS_DAY)));
    std::printf("  Clock is now %s. Timers (hold expiry, deadlines, reminders) were processed.\n", fmt_datetime(c.app.clock.now()).c_str());
    autosave(c);
}

int run_cli(App &app, const CliOptions &opt) {
    Ctx c(app, opt);
    std::printf("SmartRoute - intercity bus booking demo (C/C++ with hand-written data structures)\n");
    for (;;) {
        header(c);
        std::printf("1 Search & book  2 My trips & tickets  3 Live track  4 Notifications  5 Journey planner  6 Vendor portal  7 Help  8 Exit   (T = time machine)\n");
        std::string s;
        if (!ask("> ", &s)) break;
        if (s == "1") screen_search(c);
        else if (s == "2") screen_trips(c);
        else if (s == "3") screen_track(c);
        else if (s == "4") screen_notifications(c);
        else if (s == "5") screen_planner(c);
        else if (s == "6") screen_vendor(c);
        else if (s == "7") screen_help();
        else if (s == "t" || s == "T") time_machine(c);
        else if (s == "8") break;
        else if (!s.empty()) std::printf("  Please choose 1 to 8.\n");
    }
    autosave(c);
    std::printf("Goodbye.\n");
    return 0;
}

/* ---------- scripted demo ---------- */

static void section(const char *title) { std::printf("\n########## %s ##########\n", title); }

int run_demo(App &app, const CliOptions &opt) {
    Ctx c(app, opt);
    std::printf("SmartRoute --demo (virtual clock, no prompts; everything below is DEMO data)\n");
    section("1. Search Delhi -> Jaipur, 5 days ahead");
    int from = app.find_city("Delhi"), to = app.find_city("Jaipur"), dk = date_key(app.clock.now() + 5 * MS_DAY);
    Vec<SearchRow> rows;
    app.search(from, to, dk, SearchFilters(), SORT_EARLIEST, rows);
    header(c);
    print_results(c, rows);
    section("2. Log in, hold seats L1 + U1 (10-minute hold)");
    Result<int> login = app.login("user@smartroute.in", "Demo@1234", ROLE_PASSENGER);
    c.user = login.value;
    std::printf("%s\n", login.message.c_str());
    int sched = -1;
    for (size_t i = 0; i < app.schedules.size(); i++)
        if (std::string(app.buses[(size_t)app.schedules[i].bus_idx].reg) == "DEMO-001" && app.routes[(size_t)app.schedules[i].route_id].from_city == from) sched = (int)i;
    Trip &t = *app.get_or_create_trip(sched, dk);
    const char *seats[2] = {"L1", "U1"};
    Result<Unit> h = app.hold_seats(c.user, t.id, seats, 2);
    std::printf("%s\n", h.message.c_str());
    print_seat_grid(c, t, {"L1", "U1"});
    section("3. Price quote with coupon SMART50 + insurance, 20% pre-booking deposit");
    Result<Quote> q = app.quote(t.id, seats, 2, "SMART50", true, c.user);
    print_quote(q.value, 1);
    BookingRequest rq;
    rq.user_id = c.user; rq.trip_id = t.id; rq.n_pax = 2; rq.insurance = true; rq.mode = MODE_DEPOSIT;
    std::snprintf(rq.pax[0].seat, 6, "L1"); std::snprintf(rq.pax[0].name, 32, "Asha Demo"); rq.pax[0].age = 29; rq.pax[0].gender = 'F';
    std::snprintf(rq.pax[1].seat, 6, "U1"); std::snprintf(rq.pax[1].name, 32, "Ravi Demo"); rq.pax[1].age = 31; rq.pax[1].gender = 'M';
    rq.boarding_stop = app.stop_index(app.routes[(size_t)t.route_id], ST_BOARDING, 0);
    rq.dropping_stop = app.stop_index(app.routes[(size_t)t.route_id], ST_DROPPING, 0);
    std::snprintf(rq.contact, sizeof rq.contact, "demo@example.com");
    std::snprintf(rq.coupon, sizeof rq.coupon, "SMART50");
    Result<int> b = app.create_booking(rq);
    std::printf("Booking created: %s (status PENDING_PAYMENT)\n", app.booking_by_id(b.value)->pnr);
    section("4. Pay the deposit (simulated gateway: DEMO PAYMENT)");
    Result<Unit> p = app.pay_booking(b.value, c.user, PAYK_DEPOSIT, OUT_SUCCESS);
    std::printf("%s\n", p.message.c_str());
    std::printf("Double-submit of the same payment: %s\n", app.pay_booking(b.value, c.user, PAYK_DEPOSIT, OUT_SUCCESS).message.c_str());
    std::printf("\n%s", render_ticket(app, *app.booking_by_id(b.value), opt.color).c_str());
    section("5. Time passes: 3 days later the balance is paid, then the booking is cancelled");
    app.advance_clock(3 * MS_DAY);
    std::printf("Clock: %s\n", fmt_datetime(app.clock.now()).c_str());
    std::printf("Balance payment: %s\n", app.pay_booking(b.value, c.user, PAYK_BALANCE, OUT_SUCCESS).message.c_str());
    Booking *bk = app.booking_by_id(b.value);
    RefundPreview rp = app.cancel_preview(*bk);
    std::printf("Cancel preview: departure in %lld h -> %d%% of the refundable amount = %s (paid %s)\n", (long long)rp.hours_left, rp.pct, fmt_rs(rp.refund).c_str(), fmt_rs(bk->paid).c_str());
    Result<int64_t> cx = app.cancel_booking(b.value, c.user);
    std::printf("Cancelled: refund %s. Booking status %s, payment %s.\n", fmt_rs(cx.value).c_str(), booking_status_name(bk->status), pay_status_name(bk->pay_status));
    section("6. Book Delhi -> Agra, then track it with a break and a traffic jam");
    int agra = app.find_city("Agra");
    int sched2 = -1;
    for (size_t i = 0; i < app.schedules.size(); i++)
        if (std::string(app.buses[(size_t)app.schedules[i].bus_idx].reg) == "DEMO-002" && app.routes[(size_t)app.schedules[i].route_id].to_city == agra) sched2 = (int)i;
    Trip &t2 = *app.get_or_create_trip(sched2, date_key(app.clock.now() + 2 * MS_DAY));
    const char *s2[1] = {"1A"};
    app.hold_seats(c.user, t2.id, s2, 1);
    BookingRequest r2;
    r2.user_id = c.user; r2.trip_id = t2.id; r2.n_pax = 1; r2.mode = MODE_FULL;
    std::snprintf(r2.pax[0].seat, 6, "1A"); std::snprintf(r2.pax[0].name, 32, "Asha Demo"); r2.pax[0].age = 29; r2.pax[0].gender = 'F';
    r2.boarding_stop = app.stop_index(app.routes[(size_t)t2.route_id], ST_BOARDING, 0);
    r2.dropping_stop = app.stop_index(app.routes[(size_t)t2.route_id], ST_DROPPING, 1);
    std::snprintf(r2.contact, sizeof r2.contact, "demo@example.com");
    Result<int> b2 = app.create_booking(r2);
    std::printf("%s\n", app.pay_booking(b2.value, c.user, PAYK_FULL, OUT_SUCCESS).message.c_str());
    app.sim_advance_km(t2, 130);
    std::printf("After +130 km the bus stops at the Expressway Food Court break (km 120):\n");
    print_track(c, t2);
    app.sim_advance(t2, 20 * 60);
    app.sim_jam(t2);
    std::printf("\nAfter waiting out the break, then an injected traffic jam (DEMO TRAFFIC):\n");
    print_track(c, t2);
    app.sim_advance(t2, 3600 * 5);
    TrackInfo ti = app.track_info(t2);
    std::printf("\nTrip finished: progress %.0f%%, status %s, final delay %d min.\n", ti.progress_pct, t2.status == TRIP_COMPLETED ? "COMPLETED" : "RUNNING", ti.delay_min);
    section("7. Notifications (one bell)");
    app.flush_outbox();
    auto list = app.notifications_for(c.user);
    std::printf("Bell (%d unread)\n", app.unread_count(c.user));
    for (size_t i = list.size(); i-- > 0;) std::printf("  %s  %s: %s\n", fmt_hm(list[i]->created_ms).c_str(), list[i]->title, list[i]->body);
    section("8. Vendor portal: Orbit Express assigns staff");
    Result<int> v = app.login("vendor@smartroute.in", "Demo@1234", ROLE_VENDOR);
    c.vendor = v.value;
    std::printf("%s\n", v.message.c_str());
    vendor_overview(c);
    int bus3 = app.find_bus("DEMO-003");
    std::printf("Assign off-duty driver #3 to DEMO-003: %s\n", app.assign_staff(1, bus3, 3).message.c_str());
    std::printf("Assign conductor #6 to DEMO-003: %s\n", app.assign_staff(1, bus3, 6).message.c_str());
    std::string rep;
    int n = app.auto_assign(1, date_key(app.clock.now() + 3 * MS_DAY), &rep);
    std::printf("Auto-assign (greedy + min-heap): %d slots\n%s", n, rep.c_str());
    std::printf("Undo: %s\n", app.undo_last(1).value.c_str());
    vendor_fleet(c);
    std::printf("\nDemo finished.\n");
    return 0;
}

}  // namespace sr
