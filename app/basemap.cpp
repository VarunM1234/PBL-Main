/*
 * PURPOSE: See app/basemap.hpp - it builds web/basemap.json, the offline map background for the tracker.
 * WHY THIS NAME: matches the header.
 * CONNECTS TO: app/app.hpp (catalogue), core/geo.c (Douglas-Peucker), app/main.cpp (--make-basemap),
 *          app/http_server.cpp (GET /basemap.json), tests/test_app.cpp (generator + fuzz).
 * COMPLEXITY: see the header. Everything here is standard C++17 plus core/geo - no third-party library,
 *          no JSON library, no geometry library.
 */
#include "basemap.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

namespace sr {

namespace {

const char *kFormat = "smartroute-basemap/1";
const size_t kMaxText = 64u << 20;          /* 64 MB of GeoJSON is far past this project's needs */
const size_t kMaxNodes = 3000000;           /* one JVal per JSON value; stops a "[[[[[" file eating RAM */
const size_t kMaxDepth = 32;                /* recursion cap: nested files are refused, not followed */
const double kLandTolKm = 8.0;              /* coastline slack: invisible at this zoom, huge saving in points */
const double kCorridorTolKm = 1.0;          /* sample corridors are straight lines: simplify collapses them */

/* ---------------------------------------------------------------- numbers, both ways, locale-free ---- */
/* std::snprintf("%f") and std::strtod both answer to the C locale, which in some locales means a comma.
   The file must be byte-identical on every machine, so both directions are written out by hand. */

bool fmt_coord(double v, std::string *out) {
    if (!std::isfinite(v)) return false;
    if (v > 1e9 || v < -1e9) return false;                       /* not a coordinate */
    bool neg = v < 0;
    double av = neg ? -v : v;
    unsigned long long scaled = static_cast<unsigned long long>(av * 1000000.0 + 0.5);   /* 6 decimals */
    unsigned long long ip = scaled / 1000000ULL, fp = scaled % 1000000ULL;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%llu.%06llu", ip, fp);
    std::string s(buf);
    while (s.size() > 1 && s[s.size() - 1] == '0') s.erase(s.size() - 1);
    if (!s.empty() && s[s.size() - 1] == '.') s.erase(s.size() - 1);
    if (s == "0.000000" || s == "0") neg = false;
    *out = (neg ? "-" : "") + s;
    if (*out == "-0") *out = "0";
    return true;
}

bool parse_json_number(const std::string &t, size_t *i, double *out) {
    size_t p = *i;
    bool neg = false;
    if (p < t.size() && (t[p] == '-' )) { neg = true; p++; }
    if (p >= t.size()) return false;
    if (t[p] == '0') { p++; }
    else if (t[p] >= '1' && t[p] <= '9') { while (p < t.size() && t[p] >= '0' && t[p] <= '9') p++; }
    else return false;
    if (p - *i > 24) return false;                               /* absurd precision */
    double ip = 0;
    for (size_t q = neg ? *i + 1 : *i; q < p; q++) ip = ip * 10.0 + (t[q] - '0');
    double frac = 0, scale = 1;
    if (p < t.size() && t[p] == '.') {
        p++;
        size_t start = p;
        while (p < t.size() && t[p] >= '0' && t[p] <= '9') { frac = frac * 10.0 + (t[p] - '0'); scale *= 10.0; p++; }
        if (p == start) return false;                            /* "1." is not JSON */
    }
    double val = ip + frac / scale;
    if (p < t.size() && (t[p] == 'e' || t[p] == 'E')) {
        p++;
        bool eneg = false;
        if (p < t.size() && (t[p] == '+' || t[p] == '-')) { eneg = t[p] == '-'; p++; }
        size_t estart = p;
        long e = 0;
        while (p < t.size() && t[p] >= '0' && t[p] <= '9') { if (e < 1000) e = e * 10 + (t[p] - '0'); p++; }
        if (p == estart || e > 400) return false;                /* overflow territory: refuse it */
        for (long k = 0; k < e; k++) { if (eneg) val /= 10.0; else val *= 10.0; }
    }
    if (neg) val = -val;
    if (!std::isfinite(val)) return false;
    *i = p;
    *out = val;
    return true;
}

void append_escaped(std::string *o, const std::string &s) {
    o->push_back('"');
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') { o->push_back('\\'); o->push_back(static_cast<char>(c)); }
        else if (c == '\n') o->append("\\n");
        else if (c == '\r') o->append("\\r");
        else if (c == '\t') o->append("\\t");
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o->append(b); }
        else o->push_back(static_cast<char>(c));                 /* UTF-8 bytes pass through untouched */
    }
    o->push_back('"');
}

/* ------------------------------------------------------------------ the tiny JSON reader ---- */

struct Reader {
    const std::string &t;
    size_t p = 0, nodes = 0;
    std::string err;

    explicit Reader(const std::string &text) : t(text) {}

    void ws() { while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) p++; }

    bool fail(const std::string &m, size_t depth) { if (err.empty() && depth == 0) err = m; return false; }

    bool str(std::string *out) {
        if (p >= t.size() || t[p] != '"') { if (err.empty()) err = "expected a string"; return false; }
        p++;
        out->clear();
        while (p < t.size()) {
            char c = t[p];
            if (c == '"') { p++; return true; }
            if (c == '\\') {
                if (p + 1 >= t.size()) break;
                char e = t[p + 1];
                p += 2;
                if (e == '"' || e == '\\' || e == '/') out->push_back(e);
                else if (e == 'n') out->push_back('\n');
                else if (e == 't') out->push_back('\t');
                else if (e == 'r') out->push_back('\r');
                else if (e == 'b') out->push_back('\b');
                else if (e == 'f') out->push_back('\f');
                else if (e == 'u') {
                    if (p + 4 > t.size()) { if (err.empty()) err = "short \\u escape"; return false; }
                    unsigned cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = t[p + k];
                        unsigned d;
                        if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a') + 10;
                        else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A') + 10;
                        else { if (err.empty()) err = "bad \\u escape"; return false; }
                        cp = cp * 16 + d;
                    }
                    p += 4;
                    if (cp < 0x80) out->push_back(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                } else { if (err.empty()) err = "bad escape"; return false; }
            } else {
                out->push_back(c);
                p++;
            }
        }
        if (err.empty()) err = "unterminated string";
        return false;
    }

    bool value(Json *out, size_t depth) {
        if (depth > kMaxDepth) { if (err.empty()) err = "nested deeper than " + std::to_string(kMaxDepth); return false; }
        if (++nodes > kMaxNodes) { if (err.empty()) err = "too many values"; return false; }
        ws();
        if (p >= t.size()) { if (err.empty()) err = "file ends mid-value"; return false; }
        char c = t[p];
        if (c == '{' || c == '[') {
            bool obj = c == '{';
            p++;
            out->kind = obj ? Json::OBJ : Json::ARR;
            ws();
            if (p < t.size() && t[p] == (obj ? '}' : ']')) { p++; return true; }
            for (;;) {
                ws();
                std::string key;
                if (obj) { if (!str(&key)) return false; ws(); if (p >= t.size() || t[p] != ':') { if (err.empty()) err = "expected : in object"; return false; } p++; }
                Json child;
                if (!value(&child, depth + 1)) return false;
                if (obj) out->obj.emplace_back(key, std::move(child)); else out->arr.push_back(std::move(child));
                ws();
                if (p < t.size() && t[p] == ',') { p++; continue; }
                if (p < t.size() && t[p] == (obj ? '}' : ']')) { p++; return true; }
                if (err.empty()) err = std::string("expected , or ") + (obj ? "}" : "]");
                return false;
            }
        }
        if (c == '"') { out->kind = Json::STR; return str(&out->str); }
        if (t.compare(p, 4, "true") == 0) { p += 4; out->kind = Json::BOOL; out->boolean = true; return true; }
        if (t.compare(p, 5, "false") == 0) { p += 5; out->kind = Json::BOOL; out->boolean = false; return true; }
        if (t.compare(p, 4, "null") == 0) { p += 4; out->kind = Json::NUL; return true; }
        double d = 0;
        if (!parse_json_number(t, &p, &d)) { if (err.empty()) err = "not a number, string, array, object, true, false or null"; return false; }
        out->kind = Json::NUM;
        out->num = d;
        return true;
    }
};

bool position_of(const Json &a, double *lat, double *lng) {
    if (a.kind != Json::ARR || a.arr.size() < 2) return false;
    if (a.arr[0].kind != Json::NUM || a.arr[1].kind != Json::NUM) return false;
    double x = a.arr[0].num, y = a.arr[1].num;                    /* GeoJSON order: [lng, lat] */
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (y < -90 || y > 90 || x < -1800 || x > 1800) return false; /* generous on lng: some files wrap */
    *lat = y; *lng = x;
    return true;
}

/* Sutherland-Hodgman against the axis-aligned region. Four passes, one per edge; a convex clip window
   means one pass per edge is exact, and it is what turns a world-sized polygon into the region rectangle. */
void clip_edge(const std::vector<geo_point> &in, bool lat_edge, double bound, bool keep_above,
               std::vector<geo_point> *out) {
    out->clear();
    size_t n = in.size();
    for (size_t i = 0; i < n; i++) {
        const geo_point &cur = in[i];
        const geo_point &prev = in[(i + n - 1) % n];
        double cv = lat_edge ? cur.lat : cur.lng;
        double pv = lat_edge ? prev.lat : prev.lng;
        bool cin = keep_above ? cv >= bound : cv <= bound;
        bool pin = keep_above ? pv >= bound : pv <= bound;
        if (cin) {
            if (!pin) {
                double t = (bound - pv) / (cv - pv);
                geo_point x;
                x.lat = prev.lat + t * (cur.lat - prev.lat);
                x.lng = prev.lng + t * (cur.lng - prev.lng);
                out->push_back(x);
            }
            out->push_back(cur);
        } else if (pin) {
            double t = (bound - pv) / (cv - pv);
            geo_point x;
            x.lat = prev.lat + t * (cur.lat - prev.lat);
            x.lng = prev.lng + t * (cur.lng - prev.lng);
            out->push_back(x);
        }
    }
}

/* Cheap pre-filter: does the ring's bounding box touch the region at all? A vertex test would be wrong here -
   at 1:110m the single Eurasia polygon crosses India with not one vertex inside a 24 x 20 degree box, and a
   ring that swallows the whole region must still be cut. */
bool bbox_touches(const std::vector<geo_point> &pts, const BasemapRegion &r) {
    double la0 = 1e9, la1 = -1e9, ln0 = 1e9, ln1 = -1e9;
    for (size_t i = 0; i < pts.size(); i++) {
        if (pts[i].lat < la0) la0 = pts[i].lat;
        if (pts[i].lat > la1) la1 = pts[i].lat;
        if (pts[i].lng < ln0) ln0 = pts[i].lng;
        if (pts[i].lng > ln1) ln1 = pts[i].lng;
    }
    return !(la1 < r.lat0 || la0 > r.lat1 || ln1 < r.lng0 || ln0 > r.lng1);
}

/* One ring of a Polygon / MultiPolygon: clip to the region, then core/geo Douglas-Peucker. */
void take_ring(const std::vector<Json> &raw, const BasemapRegion &r, std::vector<BasemapShape> *out, long *seen) {
    std::vector<geo_point> ring;
    ring.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        double lat = 0, lng = 0;
        if (!position_of(raw[i], &lat, &lng)) continue;
        geo_point g; g.lat = lat; g.lng = lng;
        ring.push_back(g);
    }
    if (ring.size() < 4) return;                                  /* a triangle is not worth drawing */
    (*seen)++;                                                     /* counted whether or not it survives the next two filters */
    if (!bbox_touches(ring, r)) return;                            /* none of it comes near this region */
    std::vector<geo_point> a = ring, b;
    clip_edge(a, false, r.lng0, true, &b);   a = b;               /* keep lng >= lng0 */
    clip_edge(a, false, r.lng1, false, &b);  a = b;               /* keep lng <= lng1 */
    clip_edge(a, true, r.lat0, true, &b);    a = b;               /* keep lat >= lat0 */
    clip_edge(a, true, r.lat1, false, &b);   a = b;               /* keep lat <= lat1 */
    if (a.size() < 4) return;
    /* close the ring so simplify cannot drop the seam, simplify, then hand back unique points */
    geo_point first = a[0], last = a[a.size() - 1];
    if (std::fabs(first.lat - last.lat) > 1e-9 || std::fabs(first.lng - last.lng) > 1e-9) a.push_back(first);
    std::vector<size_t> keep(a.size());
    size_t kept = geo_simplify(a.data(), a.size(), kLandTolKm, keep.data());
    BasemapShape shape;
    for (size_t i = 0; i < kept; i++) {
        const geo_point &p = a[keep[i]];
        if (!shape.pts.empty()) {
            const geo_point &q = shape.pts.back();
            if (std::fabs(p.lat - q.lat) < 1e-9 && std::fabs(p.lng - q.lng) < 1e-9) continue;
        }
        shape.pts.push_back(p);
    }
    if (shape.pts.size() >= 3) out->push_back(shape);
}

void walk_polygon(const std::vector<Json> &rings, const BasemapRegion &r, std::vector<BasemapShape> *out, long *seen) {
    if (rings.empty()) return;
    take_ring(rings[0].arr, r, out, seen);                        /* ring 0 is the outline; holes are skipped */
}

bool read_whole(const std::string &path, std::string *out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

void append_point(std::string *o, geo_point p, bool lead_comma) {
    std::string la, ln;
    if (!fmt_coord(p.lat, &la) || !fmt_coord(p.lng, &ln)) { o->append(lead_comma ? ",null" : "null"); return; }
    if (lead_comma) o->push_back(',');
    o->append("[").append(ln).append(",").append(la).append("]");
}

/* One line per corridor, not two: catalog.cpp pairs every route with its reverse and stores the partner's id
   on BOTH of them (routes[id].reverse_of = partner), so the lower id of the pair draws it. A route without a
   partner is its own corridor. */
static bool is_drawn_corridor(const Route &r) {
    if (r.n_stops < 2) return false;
    if (r.reverse_of < 0) return true;
    return r.id < r.reverse_of;
}

}  // namespace

/* Every [lng, lat] pair anywhere in the document must be finite and inside the declared bounds. The parser
   caps nesting at kMaxDepth, so this walk cannot overflow the stack however the file is shaped. */
static bool coords_ok(const Json &j, double lng0, double lat0, double lng1, double lat1, double slack) {
    if (j.kind == Json::NUM) return std::isfinite(j.num);
    if (j.kind == Json::ARR) {
        for (size_t i = 0; i < j.arr.size(); i++) {
            const Json &p = j.arr[i];
            /* A position is exactly [lng, lat] - two numbers. Anything else that is an array is a list of
               them (a line, a ring, a list of rings) and is walked down instead. */
            if (p.kind == Json::ARR && p.arr.size() == 2 && p.arr[0].kind == Json::NUM && p.arr[1].kind == Json::NUM) {
                double lng = p.arr[0].num, lat = p.arr[1].num;
                if (!std::isfinite(lng) || !std::isfinite(lat)) return false;
                if (lng < lng0 - slack || lng > lng1 + slack || lat < lat0 - slack || lat > lat1 + slack) return false;
            } else if (!coords_ok(p, lng0, lat0, lng1, lat1, slack)) return false;
        }
        return true;
    }
    if (j.kind == Json::OBJ) {
        for (size_t i = 0; i < j.obj.size(); i++) if (!coords_ok(j.obj[i].second, lng0, lat0, lng1, lat1, slack)) return false;
        return true;
    }
    return true;
}

const Json *Json::find(const char *key) const {
    if (kind != OBJ) return nullptr;
    for (size_t i = 0; i < obj.size(); i++) if (obj[i].first == key) return &obj[i].second;
    return nullptr;
}

bool json_parse(const std::string &text, Json *out, std::string *err) {
    if (text.size() > kMaxText) { if (err) *err = "input larger than 64 MB"; return false; }
    Reader rd(text);
    if (!rd.value(out, 0)) { if (err) *err = rd.err.empty() ? std::string("invalid JSON") : rd.err; return false; }
    rd.ws();
    if (rd.p != text.size()) { if (err) *err = "text after the JSON value"; return false; }
    return true;
}

BasemapRegion basemap_region(const App &app) {
    double lat0 = 90, lat1 = -90, lng0 = 180, lng1 = -180;
    for (size_t i = 0; i < app.cities.size(); i++) {
        const City &c = app.cities[i];
        if (c.pos.lat < lat0) lat0 = c.pos.lat;
        if (c.pos.lat > lat1) lat1 = c.pos.lat;
        if (c.pos.lng < lng0) lng0 = c.pos.lng;
        if (c.pos.lng > lng1) lng1 = c.pos.lng;
    }
    if (lat0 > lat1) { lat0 = 8; lat1 = 33; lng0 = 70; lng1 = 89; }   /* empty catalogue: a sane India box */
    double pad = 3.0;
    BasemapRegion r;
    r.lat0 = std::floor((lat0 - pad) / 2.0) * 2.0;
    r.lat1 = std::ceil((lat1 + pad) / 2.0) * 2.0;
    r.lng0 = std::floor((lng0 - pad) / 2.0) * 2.0;
    r.lng1 = std::ceil((lng1 + pad) / 2.0) * 2.0;
    return r;
}

bool basemap_land_from_geojson(const std::string &text, const BasemapRegion &region,
                               std::vector<BasemapShape> *out, std::string *err) {
    out->clear();
    Json root;
    std::string why;
    if (!json_parse(text, &root, &why)) { if (err) *err = "not valid JSON: " + why; return false; }
    const Json *type = root.find("type");
    if (root.kind != Json::OBJ || !type || type->kind != Json::STR) {
        if (err) *err = "not a JSON object with a \"type\" - a basemap input must be a GeoJSON FeatureCollection";
        return false;
    }
    if (type->str != "FeatureCollection") {
        if (err) *err = "\"type\" is \"" + type->str + "\", expected \"FeatureCollection\"";
        return false;
    }
    const Json *feats = root.find("features");
    if (!feats || feats->kind != Json::ARR) { if (err) *err = "no \"features\" array"; return false; }
    long seen = 0;
    for (size_t i = 0; i < feats->arr.size(); i++) {
        const Json *geom = feats->arr[i].find("geometry");
        if (!geom || geom->kind == Json::NUL || geom->kind != Json::OBJ) continue;
        const Json *gt = geom->find("type"), *co = geom->find("coordinates");
        if (!gt || gt->kind != Json::STR || !co || co->kind != Json::ARR) continue;
        if (gt->str == "Polygon") walk_polygon(co->arr, region, out, &seen);
        else if (gt->str == "MultiPolygon")
            for (size_t k = 0; k < co->arr.size(); k++)
                if (co->arr[k].kind == Json::ARR) walk_polygon(co->arr[k].arr, region, out, &seen);
    }
    if (out->empty()) {
        if (err) *err = "no land outline in that file touches the service region";
        return false;
    }
    if (err) *err = std::to_string(out->size()) + " of " + std::to_string(seen) + " outlines kept";
    return true;
}

std::string basemap_json(const App &app, const std::vector<BasemapShape> &land, const std::string &credit) {
    BasemapRegion r = basemap_region(app);
    std::string o;
    o.reserve(land.empty() ? 8192 : 65536);
    o.append("{\n  \"format\":\"").append(kFormat).append("\",\n");
    o.append("  \"generator\":\"SmartRoute --make-basemap (C++17 + core/geo, no third-party library)\",\n");
    o.append("  \"note\":\"OFFLINE MAP BACKGROUND. City dots and corridors are this project's SAMPLE data, ");
    o.append("not survey geometry. Coastlines appear only when a land GeoJSON was supplied. Nothing here is a live map.\",\n");
    std::string b0, b1, b2, b3;
    fmt_coord(r.lng0, &b0); fmt_coord(r.lat0, &b1); fmt_coord(r.lng1, &b2); fmt_coord(r.lat1, &b3);
    o.append("  \"bounds\":[").append(b0).append(",").append(b1).append(",").append(b2).append(",").append(b3).append("],\n");
    o.append("  \"grid_step_deg\":2,\n  \"graticule\":[");
    bool first_line = true;
    for (double g = r.lng0; g <= r.lng1 + 1e-9; g += 2.0) {
        geo_point a; a.lat = r.lat0; a.lng = g;
        geo_point b; b.lat = r.lat1; b.lng = g;
        if (!first_line) o.push_back(',');
        first_line = false;
        o.append("["); append_point(&o, a, false); append_point(&o, b, true); o.append("]");
    }
    for (double g = r.lat0; g <= r.lat1 + 1e-9; g += 2.0) {
        geo_point a; a.lat = g; a.lng = r.lng0;
        geo_point b; b.lat = g; b.lng = r.lng1;
        o.append(",["); append_point(&o, a, false); append_point(&o, b, true); o.append("]");
    }
    o.append("],\n  \"corridors\":[");
    bool first_cor = true;
    std::vector<geo_point> pts, tmp;
    std::vector<size_t> keep;
    for (size_t i = 0; i < app.routes.size(); i++) {
        const Route &rt = app.routes[i];
        if (!is_drawn_corridor(rt)) continue;                     /* one line per corridor, not per direction */
        pts.clear();
        for (int s = 0; s < rt.n_stops; s++) pts.push_back(rt.pts[s]);
        keep.resize(pts.size());
        size_t kept = geo_simplify(pts.data(), pts.size(), kCorridorTolKm, keep.data());
        tmp.clear();
        for (size_t k = 0; k < kept; k++) tmp.push_back(pts[keep[k]]);
        if (tmp.size() < 2) continue;
        if (!first_cor) o.push_back(',');
        first_cor = false;
        std::string from = rt.from_city >= 0 && static_cast<size_t>(rt.from_city) < app.cities.size()
                               ? std::string(app.cities[rt.from_city].name) : "?";
        std::string to = rt.to_city >= 0 && static_cast<size_t>(rt.to_city) < app.cities.size()
                             ? std::string(app.cities[rt.to_city].name) : "?";
        o.append("{\"name\":"); append_escaped(&o, from + " - " + to);
        o.append(",\"km\":").append(std::to_string(rt.km_nominal)).append(",\"pts\":[");
        for (size_t k = 0; k < tmp.size(); k++) append_point(&o, tmp[k], k != 0);
        o.append("]}");
    }
    o.append("],\n  \"cities\":[");
    bool first_city = true;
    for (size_t i = 0; i < app.cities.size(); i++) {
        const City &c = app.cities[i];
        if (!first_city) o.push_back(',');
        first_city = false;
        o.append("{\"name\":"); append_escaped(&o, c.name);
        o.append(",\"state\":"); append_escaped(&o, c.state);
        o.append(",\"at\":[");
        std::string la, ln;
        fmt_coord(c.pos.lat, &la); fmt_coord(c.pos.lng, &ln);
        o.append(ln).append(",").append(la).append("]}");
    }
    o.append("],\n  \"scale\":{\"bar_km\":100,\"at_lat\":");
    std::string mid;
    fmt_coord((r.lat0 + r.lat1) / 2.0, &mid);
    o.append(mid);
    o.append(",\"km_per_deg_lat\":110.574,\"km_per_deg_lng\":");
    std::string kpd;
    fmt_coord(111.320 * std::cos((r.lat0 + r.lat1) / 2.0 * 3.14159265358979323846 / 180.0), &kpd);
    o.append(kpd).append("},\n  \"land_credit\":");
    append_escaped(&o, credit);
    o.append(",\n  \"land\":[");
    for (size_t i = 0; i < land.size(); i++) {
        if (i) o.push_back(',');
        o.append("[");
        for (size_t k = 0; k < land[i].pts.size(); k++) append_point(&o, land[i].pts[k], k != 0);
        o.append("]");
    }
    o.append("]\n}\n");
    return o;
}

std::string basemap_generated(const App &app) {
    std::vector<BasemapShape> none;
    return basemap_json(app, none, "");
}

bool basemap_self_check(const std::string &text, long want_cities, long want_corridors, std::string *msg) {
    Json root;
    std::string why;
    if (!json_parse(text, &root, &why)) { if (msg) *msg = "self-check: the file this module just wrote does not parse back: " + why; return false; }
    const Json *f = root.find("format");
    if (!f || f->kind != Json::STR || f->str != kFormat) { if (msg) *msg = "self-check: missing or wrong \"format\" tag"; return false; }
    const Json *b = root.find("bounds");
    if (!b || b->kind != Json::ARR || b->arr.size() != 4) { if (msg) *msg = "self-check: \"bounds\" must be 4 numbers"; return false; }
    double lng0 = b->arr[0].num, lat0 = b->arr[1].num, lng1 = b->arr[2].num, lat1 = b->arr[3].num;
    for (size_t i = 0; i < b->arr.size(); i++)
        if (b->arr[i].kind != Json::NUM || !std::isfinite(b->arr[i].num)) { if (msg) *msg = "self-check: a bound is not a finite number"; return false; }
    if (!(lng0 < lng1 && lat0 < lat1)) { if (msg) *msg = "self-check: bounds are empty or reversed"; return false; }
    const double slack = 1e-4;
    const Json *cities = root.find("cities"), *cor = root.find("corridors"), *grid = root.find("graticule"), *land = root.find("land");
    if (!cities || cities->kind != Json::ARR || !cor || cor->kind != Json::ARR ||
        !grid || grid->kind != Json::ARR || !land || land->kind != Json::ARR) {
        if (msg) *msg = "self-check: cities / corridors / graticule / land must all be arrays";
        return false;
    }
    if (want_cities >= 0 && static_cast<long>(cities->arr.size()) != want_cities) {
        if (msg) *msg = "self-check: " + std::to_string(cities->arr.size()) + " city dots, expected " + std::to_string(want_cities);
        return false;
    }
    if (want_corridors >= 0 && static_cast<long>(cor->arr.size()) != want_corridors) {
        if (msg) *msg = "self-check: " + std::to_string(cor->arr.size()) + " corridors, expected " + std::to_string(want_corridors);
        return false;
    }
    if (cities->arr.empty() || cor->arr.empty() || grid->arr.empty()) {
        if (msg) *msg = "self-check: an empty catalogue would produce a blank background";
        return false;
    }
    for (size_t i = 0; i < cities->arr.size(); i++)
        if (!cities->arr[i].find("name") || !cities->arr[i].find("at")) { if (msg) *msg = "self-check: a city dot has no name or position"; return false; }
    for (size_t i = 0; i < land->arr.size(); i++) {
        if (land->arr[i].kind != Json::ARR || land->arr[i].arr.size() < 3) { if (msg) *msg = "self-check: a land ring is not a ring"; return false; }
    }
    if (!coords_ok(root, lng0, lat0, lng1, lat1, slack)) { if (msg) *msg = "self-check: a coordinate is outside the declared bounds"; return false; }
    if (msg) *msg = "self-check ok: " + std::to_string(cities->arr.size()) + " cities, " +
                    std::to_string(cor->arr.size()) + " corridors, " + std::to_string(grid->arr.size()) +
                    " grid lines, " + std::to_string(land->arr.size()) + " land rings";
    return true;
}

bool basemap_write(const App &app, const std::string &in_geojson, const std::string &out_path, std::string *msg) {
    long corridors = 0;
    for (size_t i = 0; i < app.routes.size(); i++) if (is_drawn_corridor(app.routes[i])) corridors++;
    std::vector<BasemapShape> land;
    std::string credit;
    if (!in_geojson.empty()) {
        std::string text;
        if (!read_whole(in_geojson, &text)) { if (msg) *msg = "cannot open " + in_geojson + " - nothing was written"; return false; }
        std::string why;
        if (!basemap_land_from_geojson(text, basemap_region(app), &land, &why)) {
            if (msg) *msg = in_geojson + ": " + why + " - nothing was written";
            return false;
        }
        credit = "Coastlines: Natural Earth 110m land (public domain), clipped and simplified by SmartRoute";
        if (msg) *msg = why;                                      /* "5 of 7 outlines kept" */
    }
    std::string text = basemap_json(app, land, credit);
    std::string check;
    if (!basemap_self_check(text, static_cast<long>(app.cities.size()), corridors, &check)) {
        if (msg) *msg = check + " - nothing was written";
        return false;
    }
    std::string tmp = out_path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { if (msg) *msg = "cannot write " + tmp; return false; }
        out << text;
        out.flush();
        if (!out) { std::remove(tmp.c_str()); if (msg) *msg = "write failed for " + tmp; return false; }
    }
    std::remove(out_path.c_str());                                /* Windows cannot rename onto an existing file */
    if (std::rename(tmp.c_str(), out_path.c_str()) != 0) {
        std::remove(tmp.c_str());
        if (msg) *msg = "cannot replace " + out_path;
        return false;
    }
    std::string tail = std::to_string(text.size()) + " bytes to " + out_path + "; " + check;
    if (msg->empty()) *msg = tail; else msg->append("; ").append(tail);
    return true;
}

}  // namespace sr
