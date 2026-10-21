/*
 * PURPOSE: Geometry helpers: Haversine distance, cumulative km, position at km, polyline simplify.
 * WHY THIS NAME: "geo" = geographic calculations on (lat, lng) points.
 * CONNECTS TO: app/tracking (bus position), app/asciimap (simplified drawing, GeoJSON).
 * COMPLEXITY: haversine O(1); cumulative O(n); point_at_km O(log n); simplify O(n log n) avg.
 * INVARIANTS: cum[0] == 0 and cum is non-decreasing.
 */
#ifndef SMARTROUTE_GEO_H
#define SMARTROUTE_GEO_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { double lat, lng; } geo_point;
double geo_haversine_km(geo_point a, geo_point b);
void geo_cumulative_km(const geo_point *pts, size_t n, double *cum);
/* Position after `km` along the polyline (clamped to the ends). Returns 1 on success. */
int geo_point_at_km(const geo_point *pts, const double *cum, size_t n, double km,
                    geo_point *out, size_t *segment);
/* Douglas-Peucker with an explicit stack. keep_idx must hold n entries.
 * Returns how many indexes are kept (always includes first and last). */
size_t geo_simplify(const geo_point *pts, size_t n, double tol_km, size_t *keep_idx);
/* Snaps p to the polyline: returns the distance (km) from p to the nearest point of the line and sets
 * *along_km to how far along the line (using cum[]) that nearest point is. O(n). */
double geo_nearest_km(const geo_point *pts, const double *cum, size_t n, geo_point p, double *along_km);
#ifdef __cplusplus
}
#endif
#endif
