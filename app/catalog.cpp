/*
 * PURPOSE: Loads every CSV in data/, builds routes + stops (doubly linked list), the city trie,
 *          the hash indexes, the route graph and the union-find of bus networks.
 * WHY THIS NAME: "catalog" = the fixed reference data (cities, routes, buses, staff, coupons).
 * CONNECTS TO: csv.hpp, app.hpp, core/ds_trie, ds_hash, ds_graph, ds_unionfind, ds_list, ds_sort.
 * COMPLEXITY: load O(total rows); city lookup O(1); autosuggest O(L + k).
 */
#include "app.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

extern "C" {
#include "ds_sort.h"
}

namespace sr {

static std::string lower(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static void copy_str(char *dst, size_t cap, const std::string &src) {
    std::snprintf(dst, cap, "%s", src.c_str());
}

static bool to_int(const std::string &s, long *out) {
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0') return false;
    *out = v;
    return true;
}

static bool to_double(const std::string &s, double *out) {
    char *end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0') return false;
    *out = v;
    return true;
}

App::App()
    : clock(0), city_trie(ds_trie_create()), graph(nullptr), networks(nullptr),
      cache(ds_lru_create(64, 32, nullptr)),
      base_day(0), rng_state(0x9E3779B97F4A7C15ULL), session_user(0), plain_seed_pending(0) {
    for (int i = 0; i < 32; i++) { revenue_co[i] = ds_fenwick_create(120); if (!revenue_co[i]) std::abort(); }
    if (!city_trie || !cache) std::abort();
}

void App::free_all_trips() {
    for (size_t i = 0; i < trips.size(); i++) delete trips[i];
    trips.clear();
}

App::~App() {
    free_all_trips();
    for (size_t i = 0; i < routes.size(); i++) ds_list_destroy(routes[i].stop_list);
    ds_trie_destroy(city_trie);
    ds_graph_destroy(graph);
    ds_uf_destroy(networks);
    for (int i = 0; i < 32; i++) ds_fenwick_destroy(revenue_co[i]);
    ds_lru_destroy(cache);
}

uint64_t App::next_random() {                       /* xorshift64*: tiny deterministic PRNG */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

int App::find_city(const std::string &name) const {
    int *v = idx_city.get(lower(name));
    return v ? *v : -1;
}

int App::find_route(int from, int to) const {
    int *v = idx_route.get(std::to_string(from) + ">" + std::to_string(to));
    return v ? *v : -1;
}

int App::find_bus(const char *reg) const {
    int *v = idx_bus.get(reg);
    return v ? *v : -1;
}

size_t App::suggest_cities(const char *prefix, size_t max, char out[][64], int ids[]) const {
    return ds_trie_suggest(city_trie, prefix, max, out, ids);
}

size_t App::network_count() const { return ds_uf_count(networks); }
bool App::same_network(int a, int b) const { return ds_uf_connected(networks, a, b); }

std::string App::route_name(const Route &r) const {
    return std::string(cities[r.from_city].name) + " -> " + cities[r.to_city].name;
}

std::string App::bus_label(int bus_idx) const {
    const Bus &b = buses[bus_idx];
    return std::string(companies[b.company_id - 1].name) + " " + b.reg;
}

int App::stop_index(const Route &r, int type, int nth) const {
    int seen = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == type && seen++ == nth) return i;
    return -1;
}

int64_t App::planned_stop_time(const Trip &t, int stop_idx) const {
    const Route &r = routes[t.route_id];
    const Stop &s = r.stops[stop_idx];
    int64_t drive_ms = (int64_t)std::llround(r.drive_min * (s.km / r.km_nominal)) * MS_MIN;
    int break_before = 0;
    for (int i = 0; i < r.n_stops; i++)
        if (r.stops[i].type == ST_BREAK && r.stops[i].km < s.km) break_before += r.stops[i].break_min;
    return t.depart_ms + drive_ms + (int64_t)break_before * MS_MIN;
}

/* ---------- route construction ---------- */

static int cmp_stop_km(const void *a, const void *b) {
    double x = static_cast<const Stop *>(a)->km, y = static_cast<const Stop *>(b)->km;
    return (x > y) - (x < y);
}

static geo_point lerp_point(geo_point a, geo_point b, double f) {
    return geo_point{a.lat + (b.lat - a.lat) * f, a.lng + (b.lng - a.lng) * f};
}

static void add_stop(Route &r, const char *name, int type, double km, int break_min, geo_point from, geo_point to) {
    if (r.n_stops >= MAX_STOPS) return;
    Stop &s = r.stops[r.n_stops];
    std::memset(&s, 0, sizeof s);
    std::snprintf(s.name, sizeof s.name, "%s", name);
    s.type = type;
    s.km = km;
    s.break_min = break_min;
    s.sample = (type == ST_TOLL || type == ST_BREAK) ? 1 : 0;
    s.pos = lerp_point(from, to, km / r.km_nominal);   /* straight-line interpolation (schematic) */
    r.n_stops++;
}

/* Fill derived fields: sorted stops, seq numbers, cumulative km, geometry, linked list, durations. */
static bool finish_route(Route &r) {
    ds_merge_sort(r.stops, (size_t)r.n_stops, sizeof(Stop), cmp_stop_km);
    r.break_min_total = 0;
    int toll_no = 0;
    r.stop_list = ds_list_create(sizeof(int), nullptr);
    if (!r.stop_list) return false;
    for (int i = 0; i < r.n_stops; i++) {
        Stop &s = r.stops[i];
        s.seq = i;
        if (s.type == ST_TOLL) std::snprintf(s.name, sizeof s.name, "Toll Plaza %d (sample)", ++toll_no);
        if (s.type == ST_BREAK) r.break_min_total += s.break_min;
        r.cum_km[i] = s.km;
        r.pts[i] = s.pos;
        if (!ds_list_push_back(r.stop_list, &i)) return false;
    }
    r.drive_min = planned_drive_minutes(r.km_nominal, r.avg_kmh);
    r.duration_min = r.drive_min + r.break_min_total;
    return true;
}

/* Reverse route: stops in opposite order, km' = total - km, origin<->destination, boarding<->dropping. */
static bool make_reverse(const Route &fwd, Route &rev) {
    rev = fwd;
    rev.stop_list = nullptr;
    rev.reverse_of = fwd.id;
    for (int i = 0; i < fwd.n_stops; i++) {
        Stop s = fwd.stops[fwd.n_stops - 1 - i];
        s.km = fwd.km_nominal - s.km;
        if (s.type == ST_ORIGIN) s.type = ST_DESTINATION;
        else if (s.type == ST_DESTINATION) s.type = ST_ORIGIN;
        else if (s.type == ST_BOARDING) s.type = ST_DROPPING;
        else if (s.type == ST_DROPPING) s.type = ST_BOARDING;
        rev.stops[i] = s;
    }
    return finish_route(rev);
}

void App::build_routes_from_csv(const std::vector<CsvRow> &rows) {
    for (const CsvRow &row : rows) {
        const auto &f = row.fields;
        int from = find_city(f[1]), to = find_city(f[2]);
        long km = 0, avg = 0;
        to_int(f[3], &km);
        to_int(f[4], &avg);
        Route r;
        std::memset(&r, 0, sizeof r);
        r.id = (int)routes.size();
        copy_str(r.slug, sizeof r.slug, f[0]);
        r.from_city = from; r.to_city = to; r.km_nominal = (int)km; r.avg_kmh = (int)avg; r.reverse_of = -1;
        geo_point a = cities[from].pos, b = cities[to].pos;
        add_stop(r, cities[from].name, ST_ORIGIN, 0, 0, a, b);
        add_stop(r, cities[from].points[0], ST_BOARDING, 1, 0, a, b);
        add_stop(r, cities[from].points[1], ST_BOARDING, 3, 0, a, b);
        for (const std::string &brk : split_fields(f[5], ';')) {           /* name:km:minutes */
            auto p = split_fields(brk, ':');
            long bk = 0, bm = 0;
            if (p.size() == 3 && to_int(p[1], &bk) && to_int(p[2], &bm)) add_stop(r, p[0].c_str(), ST_BREAK, (double)bk, (int)bm, a, b);
        }
        for (const std::string &tk : split_fields(f[6], ';')) {
            long kmv = 0;
            if (to_int(tk, &kmv)) add_stop(r, "Toll Plaza", ST_TOLL, (double)kmv, 0, a, b);
        }
        add_stop(r, cities[to].points[0], ST_DROPPING, (double)(km - 3), 0, a, b);
        add_stop(r, cities[to].points[1], ST_DROPPING, (double)(km - 1), 0, a, b);
        add_stop(r, cities[to].name, ST_DESTINATION, (double)km, 0, a, b);
        if (!finish_route(r)) std::abort();
        routes.push(r);
        Route rev;
        if (!make_reverse(routes[r.id], rev)) std::abort();
        rev.id = (int)routes.size();
        std::string revslug = std::string(cities[to].name) + "-" + cities[from].name;
        copy_str(rev.slug, sizeof rev.slug, lower(revslug));
        rev.from_city = to; rev.to_city = from;
        routes.push(rev);
        routes[r.id].reverse_of = rev.id;
    }
}

/* ---------- file loaders ---------- */

#define LOAD(path, cols, rows)                                              \
    std::vector<CsvRow> rows;                                               \
    if (!csv_read(data_dir + "/" + (path), (cols), &rows, error)) return false

#define FAIL(row, msg) do { *error = data_dir + ": line " + std::to_string((row).line) + ": " + (msg); return false; } while (0)

bool App::load_catalog(const std::string &dir, std::string *error) {
    data_dir = dir;
    { LOAD("cities.csv", 5, rows);
      for (const CsvRow &r : rows) {
        City c; std::memset(&c, 0, sizeof c);
        long id = 0;
        if (!to_int(r.fields[0], &id) || id != (long)cities.size() + 1) FAIL(r, "city ids must run 1,2,3,...");
        c.id = (int)id;
        copy_str(c.name, sizeof c.name, r.fields[1]);
        copy_str(c.state, sizeof c.state, r.fields[2]);
        if (!to_double(r.fields[3], &c.pos.lat) || !to_double(r.fields[4], &c.pos.lng)) FAIL(r, "bad latitude/longitude");
        cities.push(c);
        idx_city.put(lower(r.fields[1]), (int)cities.size() - 1);
        ds_trie_insert(city_trie, c.name, (int)cities.size() - 1);
      } }
    { LOAD("points.csv", 3, rows);
      for (const CsvRow &r : rows) {
        int ci = find_city(r.fields[0]);
        if (ci < 0) FAIL(r, "unknown city '" + r.fields[0] + "'");
        copy_str(cities[ci].points[0], 48, r.fields[1]);
        copy_str(cities[ci].points[1], 48, r.fields[2]);
      } }
    { LOAD("routes.csv", 7, rows);
      for (const CsvRow &r : rows) {
        long km = 0, avg = 0;
        if (find_city(r.fields[1]) < 0 || find_city(r.fields[2]) < 0) FAIL(r, "unknown city in route");
        if (!to_int(r.fields[3], &km) || !to_int(r.fields[4], &avg) || km <= 0 || avg <= 0) FAIL(r, "bad km or speed");
      }
      build_routes_from_csv(rows); }
    for (size_t i = 0; i < routes.size(); i++)
        idx_route.put(std::to_string(routes[i].from_city) + ">" + std::to_string(routes[i].to_city), (int)i);
    { LOAD("bus_types.csv", 7, rows);
      for (const CsvRow &r : rows) {
        BusType t; std::memset(&t, 0, sizeof t);
        long rate = 0, cap = 0, nrows = 0;
        copy_str(t.code, sizeof t.code, r.fields[0]); copy_str(t.label, sizeof t.label, r.fields[1]);
        if (!to_int(r.fields[2], &rate) || !to_int(r.fields[3], &cap) || !to_int(r.fields[5], &nrows) || cap > MAX_SEATS) FAIL(r, "bad bus type numbers");
        t.rate_paise_per_km = (int)rate; t.capacity = (int)cap; t.rows = (int)nrows;
        t.layout = r.fields[4] == "SLEEPER" ? LAYOUT_SLEEPER : (r.fields[4] == "SEATER_2P1" ? LAYOUT_2P1 : LAYOUT_2P2);
        copy_str(t.amenities, sizeof t.amenities, r.fields[6]);
        types.push(t);
      } }
    { LOAD("companies.csv", 4, rows);
      for (const CsvRow &r : rows) {
        Company c; std::memset(&c, 0, sizeof c);
        long id = 0;
        if (!to_int(r.fields[0], &id) || id != (long)companies.size() + 1) FAIL(r, "company ids must run 1,2,3,...");
        c.id = (int)id;
        copy_str(c.name, sizeof c.name, r.fields[1]); copy_str(c.slug, sizeof c.slug, r.fields[2]); copy_str(c.invite, sizeof c.invite, r.fields[3]);
        companies.push(c);
      } }
    { LOAD("buses.csv", 5, rows);
      for (const CsvRow &r : rows) {
        Bus b; std::memset(&b, 0, sizeof b);
        long cid = 0; int ti = -1;
        for (size_t i = 0; i < types.size(); i++) if (r.fields[2] == types[i].code) ti = (int)i;
        int ri = -1;
        for (size_t i = 0; i < routes.size(); i++) if (r.fields[3] == routes[i].slug) ri = (int)i;
        if (!to_int(r.fields[1], &cid) || cid < 1 || cid > (long)companies.size()) FAIL(r, "unknown company id");
        if (ti < 0) FAIL(r, "unknown bus type '" + r.fields[2] + "'");
        if (ri < 0) FAIL(r, "unknown route '" + r.fields[3] + "'");
        copy_str(b.reg, sizeof b.reg, r.fields[0]);
        b.company_id = (int)cid; b.type_idx = ti; b.base_route = ri; b.driver_id = b.conductor_id = -1;
        b.status = r.fields[4] == "ON_TRIP" ? BUS_ON_TRIP : (r.fields[4] == "MAINTENANCE" ? BUS_MAINTENANCE : BUS_AVAILABLE);
        buses.push(b);
        idx_bus.put(b.reg, (int)buses.size() - 1);
      } }
    { LOAD("staff.csv", 7, rows);
      for (const CsvRow &r : rows) {
        Staff s; std::memset(&s, 0, sizeof s);
        long id = 0, cid = 0, off = 0;
        if (!to_int(r.fields[0], &id) || id != (long)staff.size() + 1) FAIL(r, "staff ids must run 1,2,3,...");
        if (!to_int(r.fields[1], &cid) || !to_int(r.fields[5], &off)) FAIL(r, "bad number");
        s.id = (int)id; s.company_id = (int)cid; s.off_duty = (int)off; s.bus_idx = -1;
        s.role = r.fields[3] == "CONDUCTOR" ? STAFF_CONDUCTOR : STAFF_DRIVER;
        copy_str(s.name, sizeof s.name, r.fields[2]); copy_str(s.phone, sizeof s.phone, r.fields[4]);
        if (!r.fields[6].empty()) {
            int bi = find_bus(r.fields[6].c_str());
            if (bi < 0) FAIL(r, "unknown bus '" + r.fields[6] + "'");
            s.bus_idx = bi;
            (s.role == STAFF_DRIVER ? buses[bi].driver_id : buses[bi].conductor_id) = s.id;
        }
        staff.push(s);
      } }
    { LOAD("schedules.csv", 3, rows);
      for (const CsvRow &r : rows) {
        int bi = find_bus(r.fields[0].c_str()), ri = -1;
        for (size_t i = 0; i < routes.size(); i++) if (r.fields[1] == routes[i].slug) ri = (int)i;
        int hh = 0, mm = 0;
        if (bi < 0 || ri < 0 || std::sscanf(r.fields[2].c_str(), "%d:%d", &hh, &mm) != 2 || hh > 23 || mm > 59) FAIL(r, "bad schedule row");
        schedules.push(Schedule{(int)schedules.size(), bi, ri, hh * 60 + mm});
      } }
    { LOAD("coupons.csv", 9, rows);
      for (const CsvRow &r : rows) {
        Coupon c; std::memset(&c, 0, sizeof c);
        long v = 0, mx = 0, mb = 0, pu = 0, mu = 0, vd = 0;
        if (!to_int(r.fields[2], &v) || !to_int(r.fields[3], &mx) || !to_int(r.fields[4], &mb) ||
            !to_int(r.fields[5], &pu) || !to_int(r.fields[6], &mu) || !to_int(r.fields[8], &vd)) FAIL(r, "bad coupon number");
        copy_str(c.code, sizeof c.code, r.fields[0]);
        c.is_percent = r.fields[1] == "PERCENT"; c.value = v; c.max_discount = mx; c.min_base = mb;
        c.per_user_limit = (int)pu; c.max_uses = (int)mu; c.valid_days = (int)vd; c.active = 1;
        copy_str(c.allowed, sizeof c.allowed, r.fields[7]);
        coupons.push(c);
        idx_coupon.put(c.code, (int)coupons.size() - 1);
      } }
    { LOAD("users_seed.csv", 6, rows);
      for (const CsvRow &r : rows) {
        int company = 0;
        for (size_t i = 0; i < companies.size(); i++) if (r.fields[3] == companies[i].slug) company = companies[i].id;
        long staff_id = 0;
        if (!r.fields[4].empty() && (!to_int(r.fields[4], &staff_id) || staff_id < 1 || staff_id > (long)staff.size())) FAIL(r, "bad staff_id");
        int role = r.fields[2] == "vendor" ? ROLE_VENDOR : (r.fields[2] == "driver" ? ROLE_DRIVER : (r.fields[2] == "admin" ? ROLE_ADMIN : ROLE_PASSENGER));
        if ((role == ROLE_VENDOR || role == ROLE_DRIVER) && company == 0) FAIL(r, "vendor/driver needs a valid company_slug");
        if (role == ROLE_DRIVER && staff_id == 0) FAIL(r, "driver needs a staff_id");
        add_user(r.fields[1].c_str(), r.fields[0].c_str(), "", role, company, r.fields[5].c_str(), (int)staff_id);
      } }
    graph = ds_graph_create(cities.size());
    networks = ds_uf_create(cities.size());
    if (!graph || !networks) { *error = "out of memory"; return false; }
    for (size_t i = 0; i < routes.size(); i++)                     /* union-find over every route */
        ds_uf_unite(networks, routes[i].from_city, routes[i].to_city);
    base_day = day_number(clock.now());
    return true;
}

}  // namespace sr
