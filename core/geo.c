/*
 * PURPOSE: Geometry implementation.
 * WHY THIS NAME: implements geo.h.
 * CONNECTS TO: ds_search (lower_bound on cumulative km), ds_stack (explicit stack).
 * COMPLEXITY: see header.
 */
#include "geo.h"
#include "ds_search.h"
#include "ds_stack.h"
#include <math.h>

#define EARTH_RADIUS_KM 6371.0088
#define DEG2RAD(d) ((d) * 3.14159265358979323846 / 180.0)

double geo_haversine_km(geo_point a, geo_point b) {
    double dlat = DEG2RAD(b.lat - a.lat), dlng = DEG2RAD(b.lng - a.lng);
    double h = sin(dlat / 2) * sin(dlat / 2) +
               cos(DEG2RAD(a.lat)) * cos(DEG2RAD(b.lat)) * sin(dlng / 2) * sin(dlng / 2);
    return 2.0 * EARTH_RADIUS_KM * asin(sqrt(h));
}

void geo_cumulative_km(const geo_point *pts, size_t n, double *cum) {
    if (n == 0) return;
    cum[0] = 0.0;
    for (size_t i = 1; i < n; i++) cum[i] = cum[i - 1] + geo_haversine_km(pts[i - 1], pts[i]);
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int geo_point_at_km(const geo_point *pts, const double *cum, size_t n, double km,
                    geo_point *out, size_t *segment) {
    if (!pts || !cum || !out || n == 0) return 0;
    if (n == 1 || km <= 0) { *out = pts[0]; if (segment) *segment = 0; return 1; }
    if (km >= cum[n - 1]) { *out = pts[n - 1]; if (segment) *segment = n - 2; return 1; }
    size_t hi = ds_upper_bound(cum, n, sizeof(double), &km, cmp_double); /* first cum > km */
    size_t seg = hi - 1;
    double span = cum[hi] - cum[seg];
    double t = span > 0 ? (km - cum[seg]) / span : 0.0;                  /* 0..1 inside segment */
    out->lat = pts[seg].lat + (pts[hi].lat - pts[seg].lat) * t;
    out->lng = pts[seg].lng + (pts[hi].lng - pts[seg].lng) * t;
    if (segment) *segment = seg;
    return 1;
}

/* Distance (km) from p to segment a-b using a local flat projection (fine at city scale). */
static double dist_to_segment_km(geo_point p, geo_point a, geo_point b) {
    double kx = 111.32 * cos(DEG2RAD((a.lat + b.lat) / 2)), ky = 110.574;
    double ax = a.lng * kx, ay = a.lat * ky, bx = b.lng * kx, by = b.lat * ky;
    double px = p.lng * kx, py = p.lat * ky;
    double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double cx = ax + t * dx - px, cy = ay + t * dy - py;
    return sqrt(cx * cx + cy * cy);
}

/* Same flat projection as above, but also reports where on the segment the closest point lies (0..1). */
static double project_on_segment(geo_point p, geo_point a, geo_point b, double *t_out) {
    double kx = 111.32 * cos(DEG2RAD((a.lat + b.lat) / 2)), ky = 110.574;
    double ax = a.lng * kx, ay = a.lat * ky, bx = b.lng * kx, by = b.lat * ky, px = p.lng * kx, py = p.lat * ky;
    double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double cx = ax + t * dx - px, cy = ay + t * dy - py;
    *t_out = t;
    return sqrt(cx * cx + cy * cy);
}

double geo_nearest_km(const geo_point *pts, const double *cum, size_t n, geo_point p, double *along_km) {
    if (!pts || !cum || n == 0) { if (along_km) *along_km = 0; return 0; }
    double best = 1e18, along = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        double t, d = project_on_segment(p, pts[i], pts[i + 1], &t);
        if (d < best) { best = d; along = cum[i] + t * (cum[i + 1] - cum[i]); }
    }
    if (n == 1) { best = geo_haversine_km(p, pts[0]); along = 0; }
    if (along_km) *along_km = along;
    return best;
}

typedef struct { size_t lo, hi; } range_t;

size_t geo_simplify(const geo_point *pts, size_t n, double tol_km, size_t *keep_idx) {
    if (!pts || !keep_idx || n == 0) return 0;
    if (n <= 2) { for (size_t i = 0; i < n; i++) keep_idx[i] = i; return n; }
    unsigned char *keep = ds_xcalloc(n, 1);
    ds_stack *stack = ds_stack_create(sizeof(range_t), NULL);
    if (!keep || !stack) { free(keep); ds_stack_destroy(stack); return 0; }
    keep[0] = keep[n - 1] = 1;
    range_t r = {0, n - 1};
    ds_stack_push(stack, &r);
    while (ds_stack_pop(stack, &r) == DS_OK) {          /* explicit stack replaces recursion */
        double worst = -1.0; size_t at = 0;
        for (size_t i = r.lo + 1; i < r.hi; i++) {
            double d = dist_to_segment_km(pts[i], pts[r.lo], pts[r.hi]);
            if (d > worst) { worst = d; at = i; }
        }
        if (worst > tol_km) {
            keep[at] = 1;
            range_t left = {r.lo, at}, right = {at, r.hi};
            ds_stack_push(stack, &left);
            ds_stack_push(stack, &right);
        }
    }
    size_t count = 0;
    for (size_t i = 0; i < n; i++) if (keep[i]) keep_idx[count++] = i;
    free(keep);
    ds_stack_destroy(stack);
    return count;
}
