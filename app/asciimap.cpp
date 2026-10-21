/*
 * PURPOSE: ASCII schematic map, journey bar and GeoJSON export.
 * WHY THIS NAME: implements asciimap.hpp.
 * CONNECTS TO: core/geo.h (geo_simplify), app.hpp.
 * COMPLEXITY: see header. Never a real road map: always labelled "schematic".
 */
#include "asciimap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace sr {

constexpr int MAP_W = 78;
constexpr int MAP_H = 18;

struct Projector {
    double min_x, max_x, min_lat, max_lat, scale;
    Projector(const Route &r) {
        double mid_lat = 0;
        for (int i = 0; i < r.n_stops; i++) mid_lat += r.pts[i].lat;
        mid_lat /= r.n_stops;
        double kx = std::cos(mid_lat * 3.14159265358979 / 180.0);
        min_x = max_x = r.pts[0].lng * kx; min_lat = max_lat = r.pts[0].lat;
        for (int i = 0; i < r.n_stops; i++) {
            double x = r.pts[i].lng * kx;
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_lat = std::min(min_lat, r.pts[i].lat); max_lat = std::max(max_lat, r.pts[i].lat);
        }
        cos_ = kx;
        double xr = std::max(max_x - min_x, 1e-6), yr = std::max(max_lat - min_lat, 1e-6);
        scale = std::min((MAP_W - 1) / xr, 2.0 * (MAP_H - 1) / yr);       /* terminal cells are about twice as tall as wide */
    }
    void cell(geo_point p, int *cx, int *cy) const {
        double x = (p.lng * cos_ - min_x) * scale, y = (max_lat - p.lat) * scale / 2.0;
        double xr = (max_x - min_x) * scale, yr = (max_lat - min_lat) * scale / 2.0;
        *cx = (int)std::lround(x + (MAP_W - 1 - xr) / 2.0);                 /* centre the drawing */
        *cy = (int)std::lround(y + (MAP_H - 1 - yr) / 2.0);
        *cx = std::max(0, std::min(MAP_W - 1, *cx)); *cy = std::max(0, std::min(MAP_H - 1, *cy));
    }
private:
    double cos_;
};

/* Bresenham line: draws '.' between two cells without overwriting markers. */
static void draw_line(std::vector<std::string> &g, int x0, int y0, int x1, int y1) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (g[(size_t)y0][(size_t)x0] == ' ') g[(size_t)y0][(size_t)x0] = '.';
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static int marker_rank(char c) { return c == '@' ? 5 : c == 'G' ? 4 : c == 'P' ? 3 : c == 'T' ? 2 : c == 'B' ? 1 : 0; }

std::string render_route_map(const App &app, const Route &r, double bus_km, bool color) {
    std::vector<std::string> grid((size_t)MAP_H, std::string((size_t)MAP_W, ' '));
    Projector pr(r);
    size_t keep[MAX_STOPS];
    size_t nk = geo_simplify(r.pts, (size_t)r.n_stops, 1.0, keep);        /* Douglas-Peucker, tolerance 1 km */
    for (size_t i = 1; i < nk; i++) {
        int x0, y0, x1, y1;
        pr.cell(r.pts[keep[i - 1]], &x0, &y0);
        pr.cell(r.pts[keep[i]], &x1, &y1);
        draw_line(grid, x0, y0, x1, y1);
    }
    for (int i = 0; i < r.n_stops; i++) {
        char m = 'B';
        switch (r.stops[i].type) {
            case ST_ORIGIN: case ST_DESTINATION: m = 'G'; break;
            case ST_TOLL: m = 'T'; break;
            case ST_BREAK: m = 'P'; break;
            default: m = 'B';
        }
        int cx, cy;
        pr.cell(r.pts[i], &cx, &cy);
        char &c = grid[(size_t)cy][(size_t)cx];
        if (marker_rank(m) > marker_rank(c)) c = m;
    }
    int bx = -1, by = -1;
    if (bus_km >= 0) {
        geo_point p = app.bus_position(r, bus_km);
        pr.cell(p, &bx, &by);
    }
    std::string out = "+" + std::string((size_t)MAP_W, '-') + "+\n";
    for (int y = 0; y < MAP_H; y++) {
        out += "|";
        for (int x = 0; x < MAP_W; x++) {
            if (x == bx && y == by) out += color ? "\x1b[31m@\x1b[0m" : "@";
            else out += grid[(size_t)y][(size_t)x];
        }
        out += "|\n";
    }
    out += "+" + std::string((size_t)MAP_W, '-') + "+\n";
    out += "G start/end  B boarding/dropping  T toll  P break  @ bus   (schematic map - not a road map)\n";
    return out;
}

std::string journey_bar(const Route &r, double bus_km, int width) {
    std::string bar((size_t)width, '=');
    for (int i = 0; i < r.n_stops; i++) {
        int pos = (int)std::lround(r.stops[i].km / r.km_nominal * (width - 1));
        if (r.stops[i].type == ST_BREAK) bar[(size_t)pos] = 'P';
        else if (r.stops[i].type == ST_TOLL && bar[(size_t)pos] == '=') bar[(size_t)pos] = 'T';
    }
    bar[0] = 'S'; bar[(size_t)width - 1] = 'E';
    if (bus_km >= 0) bar[(size_t)std::max(0, std::min(width - 1, (int)std::lround(bus_km / r.km_nominal * (width - 1))))] = '@';
    return "[" + bar + "]";
}

static std::string json_escape(const std::string &s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}

std::string route_geojson(const App &app, const Route &r) {
    size_t keep[MAX_STOPS];
    size_t nk = geo_simplify(r.pts, (size_t)r.n_stops, 1.0, keep);
    char buf[160];
    std::string s = "{\"type\":\"FeatureCollection\",\"features\":[\n{\"type\":\"Feature\",\"properties\":{\"name\":\"" +
                    json_escape(app.route_name(r)) + " (schematic, sample data)\"},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[";
    for (size_t i = 0; i < nk; i++) {
        std::snprintf(buf, sizeof buf, "%s[%.5f,%.5f]", i ? "," : "", r.pts[keep[i]].lng, r.pts[keep[i]].lat);
        s += buf;
    }
    s += "]}}";
    static const char *TYPE[6] = {"origin", "boarding", "toll", "break", "dropping", "destination"};
    for (int i = 0; i < r.n_stops; i++) {
        std::snprintf(buf, sizeof buf, "],\"km\":%.1f", r.stops[i].km);
        s += ",\n{\"type\":\"Feature\",\"properties\":{\"name\":\"" + json_escape(r.stops[i].name) + "\",\"stop_type\":\"" + TYPE[r.stops[i].type] + "\",\"km\":" +
             std::to_string((int)r.stops[i].km) + ",\"sample\":" + (r.stops[i].sample ? "true" : "false") + "},\"geometry\":{\"type\":\"Point\",\"coordinates\":";
        std::snprintf(buf, sizeof buf, "[%.5f,%.5f]}}", r.stops[i].pos.lng, r.stops[i].pos.lat);
        s += buf;
    }
    return s + "\n]}\n";
}

bool export_geojson(const App &app, const Route &r, const std::string &path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << route_geojson(app, r);
    return static_cast<bool>(out);
}

}  // namespace sr
