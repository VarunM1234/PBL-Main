/*
 * PURPOSE: Bus search (filters, binary-search time windows, stable sorts, KMP name search) and the
 *          multi-leg journey planner (graph + Dijkstra + BFS).
 * WHY THIS NAME: "search" = finding buses and journeys.
 * CONNECTS TO: core/ds_sort (row 11), ds_search (rows 12, 18), ds_graph (row 9), seats.cpp (trips).
 * COMPLEXITY: search O(n log n) for n schedules on the route; planner O((V+E) log V).
 */
#include "app.hpp"
#include <climits>
#include <cstring>

extern "C" {
#include "ds_search.h"
#include "ds_sort.h"
}

namespace sr {

static int cmp_depart(const void *a, const void *b) {
    int64_t x = static_cast<const SearchRow *>(a)->depart_ms, y = static_cast<const SearchRow *>(b)->depart_ms;
    return (x > y) - (x < y);
}
static int cmp_depart_desc(const void *a, const void *b) { return cmp_depart(b, a); }
static int cmp_fare(const void *a, const void *b) {
    int64_t x = static_cast<const SearchRow *>(a)->fare_min, y = static_cast<const SearchRow *>(b)->fare_min;
    return (x > y) - (x < y);
}
static int cmp_duration(const void *a, const void *b) {
    return static_cast<const SearchRow *>(a)->duration_min - static_cast<const SearchRow *>(b)->duration_min;
}
static int cmp_seats_desc(const void *a, const void *b) {
    return static_cast<const SearchRow *>(b)->seats_left - static_cast<const SearchRow *>(a)->seats_left;
}

int64_t App::route_lowest_fare(int route_id) const {
    int64_t best = 0;
    const Route &r = routes[(size_t)route_id];
    for (size_t i = 0; i < schedules.size(); i++) {
        if (schedules[i].route_id != route_id || buses[(size_t)schedules[i].bus_idx].status == BUS_MAINTENANCE) continue;
        int64_t f = seat_fare_paise(r.km_nominal, types[(size_t)buses[(size_t)schedules[i].bus_idx].type_idx].rate_paise_per_km, false);
        if (best == 0 || f < best) best = f;
    }
    return best;
}

/* The dearest seat any bus on this route sells - the web page uses it as the max-fare slider ceiling. */
int64_t App::route_highest_fare(int route_id) const {
    int64_t best = 0;
    const Route &r = routes[(size_t)route_id];
    for (size_t i = 0; i < schedules.size(); i++) {
        if (schedules[i].route_id != route_id || buses[(size_t)schedules[i].bus_idx].status == BUS_MAINTENANCE) continue;
        const BusType &bt = types[(size_t)buses[(size_t)schedules[i].bus_idx].type_idx];
        int64_t f = seat_fare_paise(r.km_nominal, bt.rate_paise_per_km, bt.layout == LAYOUT_SLEEPER);   /* upper berth is the top price */
        if (f > best) best = f;
    }
    return best;
}

/* Keep rows i..j-1 of a depart-sorted array: removes everything outside via a compacting copy. */
static void keep_slice(Vec<SearchRow> &rows, size_t from, size_t to) {
    Vec<SearchRow> tmp;
    for (size_t i = from; i < to; i++) tmp.push(rows[i]);
    rows.clear();
    for (size_t i = 0; i < tmp.size(); i++) rows.push(tmp[i]);
}

void App::search(int from, int to, int date_key, const SearchFilters &f, SortKey sort, Vec<SearchRow> &out) {
    out.clear();
    int ri = find_route(from, to);
    if (ri < 0) return;
    const Route &r = routes[(size_t)ri];
    for (size_t si = 0; si < schedules.size(); si++) {
        const Schedule &s = schedules[si];
        if (s.route_id != ri || buses[(size_t)s.bus_idx].status == BUS_MAINTENANCE) continue;   /* maintenance hides the bus */
        Trip *t = get_or_create_trip((int)si, date_key);
        if (!t || t->status == TRIP_CANCELLED || clock.now() > t->depart_ms - 15 * MS_MIN) continue;
        const BusType &bt = types[(size_t)buses[(size_t)s.bus_idx].type_idx];
        SearchRow row;
        row.trip_id = t->id; row.bus_idx = s.bus_idx; row.route_id = ri; row.depart_ms = t->depart_ms; row.arrive_ms = t->arrive_ms;
        row.duration_min = r.duration_min;
        row.fare_min = seat_fare_paise(r.km_nominal, bt.rate_paise_per_km, false);
        row.fare_max = bt.layout == LAYOUT_SLEEPER ? seat_fare_paise(r.km_nominal, bt.rate_paise_per_km, true) : row.fare_min;
        row.seats_left = bt.capacity - (int)(t->booked.count() + t->held.count());
        out.push(row);
    }
    ds_merge_sort(out.data(), out.size(), sizeof(SearchRow), cmp_depart);          /* pre-sort by departure */
    if (f.dep_window >= 0 && out.size() > 0) {                                     /* binary search the window */
        int64_t midnight = ist_midnight(out[0].depart_ms);
        SearchRow lo_key, hi_key;
        std::memset(&lo_key, 0, sizeof lo_key); std::memset(&hi_key, 0, sizeof hi_key);
        lo_key.depart_ms = midnight + f.dep_window * 6 * MS_HOUR;
        hi_key.depart_ms = lo_key.depart_ms + 6 * MS_HOUR;
        size_t i = ds_lower_bound(out.data(), out.size(), sizeof(SearchRow), &lo_key, cmp_depart);
        size_t j = ds_lower_bound(out.data(), out.size(), sizeof(SearchRow), &hi_key, cmp_depart);
        keep_slice(out, i, j);
    }
    if (f.max_fare > 0 && out.size() > 0) {                                        /* fare range: sort by fare + upper_bound */
        ds_merge_sort(out.data(), out.size(), sizeof(SearchRow), cmp_fare);
        SearchRow key;
        std::memset(&key, 0, sizeof key);
        key.fare_min = f.max_fare;
        keep_slice(out, 0, ds_upper_bound(out.data(), out.size(), sizeof(SearchRow), &key, cmp_fare));
        ds_merge_sort(out.data(), out.size(), sizeof(SearchRow), cmp_depart);
    }
    Vec<SearchRow> kept;                                                           /* remaining linear filters */
    for (size_t i = 0; i < out.size(); i++) {
        const SearchRow &row = out[i];
        const Bus &bus = buses[(size_t)row.bus_idx];
        if (f.type_mask && !(f.type_mask & (1u << bus.type_idx))) continue;
        if (f.arr_window >= 0) {
            int64_t m = (row.arrive_ms - ist_midnight(row.arrive_ms)) / MS_HOUR;
            if (m / 6 != f.arr_window) continue;
        }
        if (f.need_seats && row.seats_left < 1) continue;
        if (f.dep_after >= 0 || f.dep_before >= 0) {                    /* time-of-day window: "after 7 am", "before 9pm" */
            int64_t mins = (row.depart_ms - ist_midnight(row.depart_ms)) / MS_MIN;
            if (f.dep_after >= 0 && mins < f.dep_after) continue;
            if (f.dep_before >= 0 && mins > f.dep_before) continue;
        }
        if (f.operator_substr[0] && !ds_kmp_contains_nocase(companies[(size_t)bus.company_id - 1].name, f.operator_substr)) continue;
        if (f.amenity[0] && !ds_kmp_contains_nocase(types[(size_t)bus.type_idx].amenities, f.amenity)) continue;
        kept.push(row);
    }
    out.clear();
    for (size_t i = 0; i < kept.size(); i++) out.push(kept[i]);
    int (*cmp)(const void *, const void *) = cmp_depart;
    if (sort == SORT_LATEST) cmp = cmp_depart_desc;
    else if (sort == SORT_CHEAPEST) cmp = cmp_fare;
    else if (sort == SORT_FASTEST) cmp = cmp_duration;
    else if (sort == SORT_MOST_SEATS) cmp = cmp_seats_desc;
    ds_merge_sort(out.data(), out.size(), sizeof(SearchRow), cmp);                 /* stable: ties keep departure order */
}

/* ---------- journey planner ---------- */

Result<Unit> App::plan_journey(int from, int to, PlanMode mode, Vec<PlanLeg> &legs, int64_t *total_min, int64_t *total_paise) {
    legs.clear();
    *total_min = *total_paise = 0;
    if (from < 0 || to < 0 || (size_t)from >= cities.size() || (size_t)to >= cities.size()) return Result<Unit>::fail(Err::VALIDATION, "Unknown city");
    if (from == to) return Result<Unit>::fail(Err::VALIDATION, "Choose two different cities");
    ds_graph_destroy(graph);                                   /* rebuild: bus status may have changed */
    graph = ds_graph_create(cities.size());
    for (size_t i = 0; i < routes.size(); i++) {
        int64_t fare = route_lowest_fare((int)i);
        if (fare == 0) continue;                                /* no bus on offer: not a usable edge */
        ds_graph_add_edge(graph, routes[i].from_city, routes[i].to_city, routes[i].duration_min, fare, (int)i);
    }
    size_t n = cities.size();
    int64_t dist[64]; int pv[64], pe[64], legs_cnt[64], path[64];
    if (n > 64) return Result<Unit>::fail(Err::VALIDATION, "Too many cities for planner buffers");
    if (mode == PLAN_FEWEST) ds_graph_bfs_fewest_legs(graph, from, legs_cnt, pv, pe);
    else ds_graph_dijkstra(graph, from, mode == PLAN_CHEAPEST, mode == PLAN_CHEAPEST ? 0 : 60, dist, pv, pe);
    size_t len = ds_graph_path(pv, from, to, path, 64);
    if (len < 2) return Result<Unit>::fail(Err::NOT_FOUND, "No route");
    for (size_t i = 1; i < len; i++) {
        int rid = pe[path[i]];
        PlanLeg leg{rid, path[i - 1], path[i], routes[(size_t)rid].duration_min, route_lowest_fare(rid)};
        legs.push(leg);
        *total_min += leg.minutes;
        *total_paise += leg.paise;
    }
    *total_min += 60 * (int64_t)(legs.size() - 1);              /* 60-minute layover between legs */
    return okay();
}

}  // namespace sr
