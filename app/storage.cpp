/*
 * PURPOSE: Save/load of runtime state (users, bookings, holds, trips, payments, notifications, fleet, meta)
 *          as versioned, checksummed text files, written atomically (temp file, then rename).
 * WHY THIS NAME: "storage" = persistence layer (replaces the original SQLite database).
 * CONNECTS TO: app.hpp, core/ds_hash (FNV-1a style checksum is hand-written here), ds_bitset (hex).
 * COMPLEXITY: O(total records).
 * FORMAT: first line "#SMARTROUTE v1 <section> <count> <fnv1a64 hex of body>"; body = one record per line,
 *         fields separated by '|' (escaped: \p = '|', \\ = '\', \n = newline).
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sr {

/* ---------- low-level helpers ---------- */

static uint64_t fnv1a(const std::string &s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '|') o += "\\p";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += ' ';
        else o += c;
    }
    return o;
}

static std::string unesc(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += n == 'p' ? '|' : (n == 'n' ? '\n' : n);
        } else o += s[i];
    }
    return o;
}

/* Builds one record line. */
class Line {
    std::string s_;
public:
    Line &add(const std::string &v) { if (!s_.empty() || started_) s_ += '|'; s_ += esc(v); started_ = true; return *this; }
    Line &num(long long v) { return add(std::to_string(v)); }
    Line &dbl(double v) { char b[40]; std::snprintf(b, sizeof b, "%.9g", v); return add(b); }
    const std::string &str() const { return s_; }
private:
    bool started_ = false;
};

/* Parses one record line. */
class Fields {
    std::vector<std::string> f_;
public:
    explicit Fields(const std::string &line) { for (const std::string &p : split_fields(line, '|')) f_.push_back(unesc(p)); }
    size_t size() const { return f_.size(); }
    std::string s(size_t i) const { return i < f_.size() ? f_[i] : ""; }
    long long n(size_t i) const { return i < f_.size() ? std::strtoll(f_[i].c_str(), nullptr, 10) : 0; }
    double d(size_t i) const { return i < f_.size() ? std::strtod(f_[i].c_str(), nullptr) : 0.0; }
};

static bool read_file(const std::string &path, std::string *out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

/* Verify header + checksum. Returns the record lines, or false when the file is damaged. */
static bool parse_section(const std::string &text, const std::string &name, std::vector<std::string> *lines, std::string *why) {
    size_t nl = text.find('\n');
    if (nl == std::string::npos) { *why = "empty file"; return false; }
    char section[32] = {0}, hex[32] = {0};
    unsigned long count = 0;
    if (std::sscanf(text.substr(0, nl).c_str(), "#SMARTROUTE v1 %31s %lu %31s", section, &count, hex) != 3) { *why = "bad header or unsupported version"; return false; }
    if (name != section) { *why = "wrong section"; return false; }
    std::string body = text.substr(nl + 1);
    char want[32];
    std::snprintf(want, sizeof want, "%016llx", (unsigned long long)fnv1a(body));
    if (std::strcmp(want, hex) != 0) { *why = "checksum mismatch (file damaged)"; return false; }
    std::istringstream in(body);
    std::string l;
    while (std::getline(in, l)) if (!l.empty()) lines->push_back(l);
    if (lines->size() != count) { *why = "record count mismatch"; return false; }
    return true;
}

static bool write_section(const std::string &dir, const std::string &name, const std::vector<std::string> &lines, std::string *err) {
    std::string body;
    for (const std::string &l : lines) body += l + "\n";
    char head[96];
    std::snprintf(head, sizeof head, "#SMARTROUTE v1 %s %zu %016llx\n", name.c_str(), lines.size(), (unsigned long long)fnv1a(body));
    std::string path = dir + "/" + name + ".sav", tmp = path + ".tmp", bak = path + ".bak";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { *err = "cannot write " + tmp; return false; }
        out << head << body;
        out.flush();
        if (!out) { *err = "write failed for " + tmp; return false; }
    }
    std::remove(bak.c_str());
    std::rename(path.c_str(), bak.c_str());                 /* keep the previous good copy */
    std::remove(path.c_str());                              /* Windows cannot rename over an existing file */
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { *err = "cannot replace " + path; return false; }
    return true;
}

/* Load a section; fall back to .bak when the main file is damaged. Returns 0 missing, 1 loaded, -1 unusable. */
static int load_section(const std::string &dir, const std::string &name, std::vector<std::string> *lines, std::string *messages) {
    std::string path = dir + "/" + name + ".sav", text, why;
    bool have = read_file(path, &text);
    if (!have) return 0;
    lines->clear();
    if (parse_section(text, name, lines, &why)) return 1;
    *messages += name + ".sav: " + why + ". ";
    lines->clear();
    if (read_file(path + ".bak", &text) && parse_section(text, name, lines, &why)) { *messages += "Recovered from " + name + ".sav.bak. "; return 1; }
    *messages += "No usable backup; " + name + " starts empty. ";
    return -1;
}

static std::string clean_name(const char *s) {
    std::string o = s;
    for (char &c : o) if (c == '~' || c == ';' || c == '|' || c == '\\' || c == '\n') c = ' ';
    return o;
}

/* ---------- save ---------- */

bool App::save_all(std::string *error) {
    if (save_dir.empty()) return true;
    std::vector<std::string> L;
    L.push_back(Line().num(clock.now()).num((long long)rng_state).num(base_day).num((long long)session_user).str());
    for (int c = 1; c <= (int)companies.size(); c++)
        for (int d = 0; d < 120; d++) {
            int64_t v = ds_fenwick_range(revenue_co[c], (size_t)d, (size_t)d);
            if (v != 0) L.push_back(Line().add("rev").num(c).num(d).num(v).str());
        }
    if (!write_section(save_dir, "meta", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < users.size(); i++) {
        const User &u = users[i];
        L.push_back(Line().num(u.id).add(u.name).add(u.email).add(u.phone).num(u.role).num(u.company_id).add(u.salt).add(u.hash).num(u.prefs).num(u.failed_logins).num(u.staff_id).str());
    }
    if (!write_section(save_dir, "users", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < buses.size(); i++) L.push_back(Line().add(buses[i].reg).num(buses[i].status).num(buses[i].driver_id).num(buses[i].conductor_id).str());
    if (!write_section(save_dir, "fleet", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < trips.size(); i++) {
        Trip &t = *trips[i];
        std::string wl;
        for (size_t k = t.waitlist.size(); k > 0; k--) {         /* rotate the queue once to read it */
            WaitEntry w;
            t.waitlist.pop(&w);
            wl += std::to_string(w.user_id) + ":" + std::to_string(w.seats_wanted) + ",";
            t.waitlist.push(w);
        }
        L.push_back(Line().num(t.id).num(t.schedule_idx).num(t.date_key).num(t.status).num(t.delay_min).add(t.delay_reason).add(t.delay_note)
                        .dbl(t.km).dbl(t.jam_left_km).num(t.break_left_sec).num(t.breaks_done).dbl(t.sim_sec).num(t.sim_started)
                        .num(t.jam_alerted_level).num(t.auto_driver).num(t.auto_conductor).add(t.booked.hex()).add(t.held.hex()).add(wl).str());
    }
    if (!write_section(save_dir, "trips", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < holds.size(); i++) {
        const Hold &h = holds[i];
        L.push_back(Line().num(h.id).num(h.trip_id).num(h.seat).num(h.user_id).num(h.expires_ms).num(h.active).str());
    }
    if (!write_section(save_dir, "holds", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < bookings.size(); i++) {
        const Booking &b = bookings[i];
        std::string px;
        for (int k = 0; k < b.n_pax; k++)
            px += std::string(b.pax[k].seat) + "~" + clean_name(b.pax[k].name) + "~" + std::to_string(b.pax[k].age) + "~" + b.pax[k].gender + "~" + std::to_string(b.pax[k].price) + ";";
        L.push_back(Line().num(b.id).add(b.pnr).num(b.user_id).num(b.trip_id).num(b.status).num(b.pay_status).num(b.mode)
                        .num(b.base).num(b.discount).num(b.tax).num(b.insurance).num(b.total).num(b.paid).num(b.refunded)
                        .add(b.coupon).num(b.boarding_stop).num(b.dropping_stop).add(px).add(b.contact).num(b.balance_due_ms)
                        .num(b.created_ms).num(b.demo_payment).num(b.attempt_no).num(b.coupon_recorded).num(b.boarded_mask).num(b.boarded_ms).str());
    }
    if (!write_section(save_dir, "bookings", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < payments.size(); i++) {
        const Payment &p = payments[i];
        L.push_back(Line().num(p.id).num(p.booking_id).num(p.kind).num(p.amount).num(p.outcome).add(p.key).num(p.at_ms).str());
    }
    if (!write_section(save_dir, "payments", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < notes.size(); i++) {
        const Notification &n = notes[i];
        L.push_back(Line().num(n.id).num(n.user_id).num(n.kind).add(n.title).add(n.body).add(n.dedupe).num(n.created_ms).num(n.read).str());
    }
    if (!write_section(save_dir, "notes", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < coupons.size(); i++) {
        const Coupon &c = coupons[i];
        L.push_back(Line().add(c.code).num(c.is_percent).num(c.value).num(c.max_discount).num(c.min_base).num(c.per_user_limit).num(c.max_uses).add(c.allowed).num(c.valid_days).num(c.active).str());
    }
    if (!write_section(save_dir, "coupons", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < shares.size(); i++) L.push_back(Line().add(shares[i].token).num(shares[i].booking_id).num(shares[i].at_ms).num(shares[i].active).str());
    if (!write_section(save_dir, "shares", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < delay_log.size(); i++) { const DelayLog &d = delay_log[i]; L.push_back(Line().num(d.id).num(d.trip_id).num(d.at_ms).add(d.cause).num(d.minutes).add(d.source).str()); }
    if (!write_section(save_dir, "delays", L, error)) return false;
    L.clear();
    for (size_t i = gps_fixes.size() > 300 ? gps_fixes.size() - 300 : 0; i < gps_fixes.size(); i++) {   /* keep the latest 300 fixes */
        const GpsFix &g = gps_fixes[i];
        L.push_back(Line().num(g.id).num(g.trip_id).num(g.at_ms).dbl(g.lat).dbl(g.lng).dbl(g.speed).dbl(g.heading).dbl(g.accuracy).str());
    }
    if (!write_section(save_dir, "gps", L, error)) return false;
    L.clear();
    for (size_t i = 0; i < staff.size(); i++) if (staff[i].off_duty) L.push_back(Line().num(staff[i].id).num(staff[i].off_duty).str());
    return write_section(save_dir, "crew", L, error);
}

/* ---------- load ---------- */

static void copy_to(char *dst, size_t cap, const std::string &s) { std::snprintf(dst, cap, "%s", s.c_str()); }

bool App::load_saved(std::string *messages) {
    if (save_dir.empty()) return false;
    std::vector<std::string> L;
    bool any = false;
    if (load_section(save_dir, "meta", &L, messages) == 1 && !L.empty()) {
        any = true;
        Fields f(L[0]);
        clock.set(f.n(0)); rng_state = (uint64_t)f.n(1); base_day = f.n(2);   /* sessions are never restored */
        for (size_t i = 1; i < L.size(); i++) {
            Fields r(L[i]);
            if (r.s(0) == "rev" && r.n(1) >= 1 && r.n(1) < 32) ds_fenwick_add(revenue_co[r.n(1)], (size_t)r.n(2), r.n(3));
        }
    }
    if (load_section(save_dir, "users", &L, messages) == 1) {
        any = true;
        std::vector<User> seeds;                                   /* accounts loaded from users_seed.csv at startup */
        for (size_t i = 0; i < users.size(); i++) seeds.push_back(users[i]);
        users.clear(); idx_email.clear();
        for (const std::string &l : L) {
            Fields f(l);
            User u; std::memset(&u, 0, sizeof u);
            u.id = (int)f.n(0); copy_to(u.name, sizeof u.name, f.s(1)); copy_to(u.email, sizeof u.email, f.s(2)); copy_to(u.phone, sizeof u.phone, f.s(3));
            u.role = (int)f.n(4); u.company_id = (int)f.n(5); copy_to(u.salt, sizeof u.salt, f.s(6)); copy_to(u.hash, sizeof u.hash, f.s(7));
            u.prefs = (int)f.n(8); u.failed_logins = (int)f.n(9); u.staff_id = (int)f.n(10);
            users.push(u);
            idx_email.put(u.email, (int)users.size() - 1);
        }
        for (const User &seed : seeds) {                           /* an older save file lacks newer demo accounts (driver, admin, ...): add them */
            if (int *at = idx_email.get(seed.email)) {
                User &have = users[(size_t)*at];
                if (have.staff_id == 0 && seed.staff_id > 0) have.staff_id = seed.staff_id;
                continue;
            }
            User add = seed;
            add.id = (int)users.size() + 1;
            users.push(add);
            idx_email.put(add.email, (int)users.size() - 1);
        }
    }
    if (load_section(save_dir, "fleet", &L, messages) == 1) {
        any = true;
        for (size_t i = 0; i < staff.size(); i++) staff[i].bus_idx = -1;
        for (const std::string &l : L) {
            Fields f(l);
            int bi = find_bus(f.s(0).c_str());
            if (bi < 0) continue;
            Bus &b = buses[(size_t)bi];
            b.status = (int)f.n(1); b.driver_id = (int)f.n(2); b.conductor_id = (int)f.n(3);
        }
        for (size_t i = 0; i < buses.size(); i++) {
            if (buses[i].driver_id > 0) staff[(size_t)buses[i].driver_id - 1].bus_idx = (int)i;
            if (buses[i].conductor_id > 0) staff[(size_t)buses[i].conductor_id - 1].bus_idx = (int)i;
        }
    }
    int trip_state = load_section(save_dir, "trips", &L, messages);
    if (trip_state == 1) {
        any = true;
        free_all_trips(); idx_trip.clear();
        for (const std::string &l : L) {
            Fields f(l);
            Trip *t = get_or_create_trip((int)f.n(1), (int)f.n(2));
            if (!t || t->id != (int)f.n(0)) { *messages += "trips.sav: id sequence broken. "; break; }
            t->status = (int)f.n(3); t->delay_min = (int)f.n(4); copy_to(t->delay_reason, sizeof t->delay_reason, f.s(5)); copy_to(t->delay_note, sizeof t->delay_note, f.s(6));
            t->km = f.d(7); t->jam_left_km = f.d(8); t->break_left_sec = (int)f.n(9); t->breaks_done = (int)f.n(10); t->sim_sec = f.d(11);
            t->sim_started = (int)f.n(12); t->jam_alerted_level = (int)f.n(13); t->auto_driver = (int)f.n(14); t->auto_conductor = (int)f.n(15);
            t->booked.from_hex(f.s(16).c_str()); t->held.from_hex(f.s(17).c_str());
            for (const std::string &e : split_fields(f.s(18), ',')) {
                int u = 0, n = 0;
                if (std::sscanf(e.c_str(), "%d:%d", &u, &n) == 2) t->waitlist.push(WaitEntry{u, n});
            }
        }
    }
    if (load_section(save_dir, "holds", &L, messages) == 1 && trip_state != -1) {
        any = true;
        holds.clear();
        for (const std::string &l : L) {
            Fields f(l);
            holds.push(Hold{(int)f.n(0), (int)f.n(1), (int)f.n(2), (int)f.n(3), f.n(4), (int)f.n(5)});
        }
    }
    if (load_section(save_dir, "coupons", &L, messages) == 1 && !L.empty()) {
        any = true;
        coupons.clear(); idx_coupon.clear();
        for (const std::string &l : L) {
            Fields f(l);
            Coupon c; std::memset(&c, 0, sizeof c);
            copy_to(c.code, sizeof c.code, f.s(0)); c.is_percent = (int)f.n(1); c.value = f.n(2); c.max_discount = f.n(3); c.min_base = f.n(4);
            c.per_user_limit = (int)f.n(5); c.max_uses = (int)f.n(6); copy_to(c.allowed, sizeof c.allowed, f.s(7)); c.valid_days = (int)f.n(8); c.active = (int)f.n(9);
            coupons.push(c);
            idx_coupon.put(c.code, (int)coupons.size() - 1);
        }
    }
    if (load_section(save_dir, "bookings", &L, messages) == 1) {
        any = true;
        bookings.clear(); idx_pnr.clear(); coupon_uses.clear();
        for (const std::string &l : L) {
            Fields f(l);
            Booking b; std::memset(&b, 0, sizeof b);
            b.id = (int)f.n(0); copy_to(b.pnr, sizeof b.pnr, f.s(1)); b.user_id = (int)f.n(2); b.trip_id = (int)f.n(3);
            b.status = (int)f.n(4); b.pay_status = (int)f.n(5); b.mode = (int)f.n(6);
            b.base = f.n(7); b.discount = f.n(8); b.tax = f.n(9); b.insurance = f.n(10); b.total = f.n(11); b.paid = f.n(12); b.refunded = f.n(13);
            copy_to(b.coupon, sizeof b.coupon, f.s(14)); b.boarding_stop = (int)f.n(15); b.dropping_stop = (int)f.n(16);
            for (const std::string &p : split_fields(f.s(17), ';')) {
                auto q = split_fields(p, '~');
                if (q.size() != 5 || b.n_pax >= MAX_PAX) continue;
                Passenger &x = b.pax[b.n_pax++];
                copy_to(x.seat, sizeof x.seat, q[0]); copy_to(x.name, sizeof x.name, q[1]);
                x.age = std::atoi(q[2].c_str()); x.gender = q[3].empty() ? 'O' : q[3][0]; x.price = std::strtoll(q[4].c_str(), nullptr, 10);
            }
            copy_to(b.contact, sizeof b.contact, f.s(18)); b.balance_due_ms = f.n(19); b.created_ms = f.n(20);
            b.demo_payment = (int)f.n(21); b.attempt_no = (int)f.n(22); b.coupon_recorded = (int)f.n(23); b.boarded_mask = (int)f.n(24); b.boarded_ms = f.n(25);
            if (b.trip_id < 1 || (size_t)b.trip_id > trips.size()) { *messages += "Dropped booking " + std::string(b.pnr) + " (its trip is missing). "; continue; }
            bookings.push(b);
            idx_pnr.put(b.pnr, (int)bookings.size() - 1);
            if (b.coupon_recorded) if (int *ci = idx_coupon.get(b.coupon)) coupon_uses.push(CouponUse{b.user_id, *ci, b.id});
        }
    }
    if (load_section(save_dir, "payments", &L, messages) == 1) {
        any = true;
        payments.clear(); idx_pay.clear();
        for (const std::string &l : L) {
            Fields f(l);
            Payment p; std::memset(&p, 0, sizeof p);
            p.id = (int)f.n(0); p.booking_id = (int)f.n(1); p.kind = (int)f.n(2); p.amount = f.n(3); p.outcome = (int)f.n(4);
            copy_to(p.key, sizeof p.key, f.s(5)); p.at_ms = f.n(6);
            payments.push(p);
            idx_pay.put(p.key, p.id);
        }
    }
    if (load_section(save_dir, "notes", &L, messages) == 1) {
        any = true;
        notes.clear();
        for (const std::string &l : L) {
            Fields f(l);
            Notification n; std::memset(&n, 0, sizeof n);
            n.id = (int)f.n(0); n.user_id = (int)f.n(1); n.kind = (int)f.n(2);
            copy_to(n.title, sizeof n.title, f.s(3)); copy_to(n.body, sizeof n.body, f.s(4)); copy_to(n.dedupe, sizeof n.dedupe, f.s(5));
            n.created_ms = f.n(6); n.read = (int)f.n(7);
            notes.push(n);
            if (n.dedupe[0]) alert_seen.put(n.dedupe, 1);        /* alerts already sent stay suppressed after restart */
        }
    }
    if (load_section(save_dir, "shares", &L, messages) == 1) {
        any = true;
        shares.clear(); idx_share.clear();
        for (const std::string &l : L) {
            Fields f(l);
            ShareLink sl; std::memset(&sl, 0, sizeof sl);
            copy_to(sl.token, sizeof sl.token, f.s(0)); sl.booking_id = (int)f.n(1); sl.at_ms = f.n(2); sl.active = (int)f.n(3);
            shares.push(sl);
            idx_share.put(sl.token, (int)shares.size() - 1);
        }
    }
    if (load_section(save_dir, "delays", &L, messages) == 1) {
        any = true;
        delay_log.clear();
        for (const std::string &l : L) {
            Fields f(l);
            DelayLog d; std::memset(&d, 0, sizeof d);
            d.id = (int)f.n(0); d.trip_id = (int)f.n(1); d.at_ms = f.n(2); copy_to(d.cause, sizeof d.cause, f.s(3)); d.minutes = (int)f.n(4); copy_to(d.source, sizeof d.source, f.s(5));
            delay_log.push(d);
        }
    }
    if (load_section(save_dir, "gps", &L, messages) == 1) {
        any = true;
        gps_fixes.clear();
        for (const std::string &l : L) { Fields f(l); gps_fixes.push(GpsFix{(int)f.n(0), (int)f.n(1), f.n(2), f.d(3), f.d(4), f.d(5), f.d(6), f.d(7)}); }
    }
    if (load_section(save_dir, "crew", &L, messages) == 1) {
        any = true;
        for (const std::string &l : L) { Fields f(l); long long id = f.n(0); if (id >= 1 && (size_t)id <= staff.size()) staff[(size_t)id - 1].off_duty = (int)f.n(1); }
    }
    /* Rebuild the timer heap from saved state: active holds, pre-booking deadlines and reminders. */
    for (size_t i = 0; i < holds.size(); i++)
        if (holds[i].active) events.push(holds[i].expires_ms, ((uint64_t)1 << 56) | (uint64_t)holds[i].id);
    for (size_t i = 0; i < bookings.size(); i++)
        if (bookings[i].status == BK_PREBOOKED) schedule_booking_events(bookings[i]);
    return any;
}

}  // namespace sr
