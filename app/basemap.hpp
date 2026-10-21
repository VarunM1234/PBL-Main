/*
 * PURPOSE: Builds the OFFLINE map background the tracker can draw when no tile server answers: a 2-degree
 *          graticule, every corridor, a labelled dot for every city, a scale bar - and, when the operator
 *          supplies land outlines, real coastlines. Output is one JSON file (web/basemap.json) that the
 *          server hands out at /basemap.json; the page only draws what this module decides.
 * WHY THIS NAME: "base map" = the picture underneath the live route.
 * CONNECTS TO: app/app.hpp (catalogue: cities, routes), core/geo.c (geo_simplify = Douglas-Peucker, row 17),
 *          app/http_server.cpp (GET /basemap.json), app/main.cpp (--make-basemap), web/index.html (drawing).
 * COMPLEXITY: generation O(cities + stops); land: parse O(bytes), Sutherland-Hodgman O(rings * 4 * pts),
 *          simplify O(n log n) average per ring (core/geo).
 * INVARIANTS: deterministic - the same catalogue and the same input file give byte-identical output;
 *          no locale dependency (numbers are formatted and parsed by hand, so a ","-locale machine writes
 *          the same file as a "."-one); every coordinate in the file is finite and inside `bounds`;
 *          NOTHING is written unless the text passes this module's own JSON reader (self-check), and the
 *          file is replaced atomically, so a rejected or crashed run leaves the old file intact;
 *          the parser is non-recursive past a fixed depth and gives up on absurd sizes, so corrupt input
 *          can neither overflow the stack nor eat all memory.
 * NOT A REAL MAP: cities, corridors and stops are this project's sample data. Coastlines come from
 *          Natural Earth (public domain) only if a GeoJSON is supplied; without one the background is
 *          neutral paper, never sea blue - an earlier version painted the whole canvas blue and put
 *          inland Delhi in an ocean.
 */
#ifndef SMARTROUTE_BASEMAP_HPP
#define SMARTROUTE_BASEMAP_HPP
#include <string>
#include <vector>
#include "app.hpp"
#include "geo.h"

namespace sr {

/* The rectangle the background covers, in degrees. */
struct BasemapRegion { double lat0 = 0, lat1 = 0, lng0 = 0, lng1 = 0; };

/* One drawn piece of land: a closed outline (a ring) clipped to the region and simplified. */
struct BasemapShape { std::vector<geo_point> pts; };

/* Smallest JSON reader in the project: values, objects, arrays, strings (with \u), numbers (hand-parsed,
   so no locale), true/false/null. Exposed because the self-check and the server both need it and because
   a test must be able to feed it garbage on purpose. Returns false with a message; never throws. */
struct Json {
    enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    bool boolean = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;                 /* insertion order kept */
    const Json *find(const char *key) const;                       /* object member or null */
};
bool json_parse(const std::string &text, Json *out, std::string *err);

/* Region that holds every catalogue city, padded and snapped outward onto the 2-degree grid. */
BasemapRegion basemap_region(const App &app);

/* Read a GeoJSON FeatureCollection (Polygon / MultiPolygon features, [lng, lat] order) and keep the rings
   that touch `region`: Sutherland-Hodgman clip, then core/geo Douglas-Peucker. Anything malformed, empty or
   not-a-FeatureCollection is refused with a message and an untouched `out`. */
bool basemap_land_from_geojson(const std::string &text, const BasemapRegion &region,
                               std::vector<BasemapShape> *out, std::string *err);

/* The whole file. `land` may be empty (neutral paper, no sea). Deterministic for the same inputs. */
std::string basemap_json(const App &app, const std::vector<BasemapShape> &land, const std::string &credit);

/* Re-read a basemap file with json_parse and check the promises the page relies on: the format tag, the
   bounds, the city and corridor counts, finite in-bounds coordinates everywhere. `want_cities` /
   `want_corridors` of -1 skip that check. */
bool basemap_self_check(const std::string &text, long want_cities, long want_corridors, std::string *msg);

/* Generate and write. `in_geojson` empty = no land data. Writes to `out_path` atomically only after the
   self-check passes; `msg` always gets a one-line summary (it is printed by --make-basemap). */
bool basemap_write(const App &app, const std::string &in_geojson, const std::string &out_path, std::string *msg);

/* The text the server answers with when there is no file on disk: generated from the catalogue, no land. */
std::string basemap_generated(const App &app);

}  // namespace sr
#endif
