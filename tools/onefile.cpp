/*
 * tools/onefile.cpp - SmartRoute single-file generator.
 *
 * PURPOSE: concatenate the whole project (core/, app/, the assembled web page) into ONE
 *          compilable translation unit, <root>/main.cpp.  Pure C++17 port of the
 *          retired scripts/build_onefile.js helper (JavaScript is banned here).
 * WHY THIS NAME: "onefile" = the one-file build the Makefile documents.
 * CONNECTS TO: every core header and source, app header and source, build/web_page.html
 *          (web/index.html + web/vendor/leaflet.*, assembled by tools/page_gen.cpp).
 * INVARIANTS: deterministic and idempotent - same sources, byte-identical output;
 *             LF newlines; no timestamps, no randomness.  Optional files (core/qr.*,
 *             app/ops.cpp, app/assistant.cpp) are included iff they exist, so the
 *             same tool works in both project copies; every other listed input is
 *             required (missing file -> clear message + non-zero exit).
 * Usage:  g++ -std=c++17 -O2 tools/onefile.cpp -o build/onefile.exe
 *         ./build/onefile.exe [projectRoot]        (default: ".")
 */
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

// ---------- tiny string helpers (JS-equivalent whitespace = ASCII class) ----------
const char WS[] = " \t\n\v\f\r";

std::string rstrip(const std::string& s) { return s.substr(0, s.find_last_not_of(WS) + 1); }
std::string ltrim(const std::string& s) {
    size_t b = s.find_first_not_of(WS);
    return b == std::string::npos ? std::string() : s.substr(b);
}
std::string trim(const std::string& s) { return rstrip(ltrim(s)); }

std::string join(const std::vector<std::string>& v, char sep) {
    std::string out;
    for (size_t k = 0; k < v.size(); ++k) { if (k) out += sep; out += v[k]; }
    return out;
}

bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

bool readFile(const std::string& p, std::string& data) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    data = ss.str();
    return true;
}

// JS: text.split(/\r?\n/) - a lone trailing line-terminator yields a final "" entry.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> v;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        std::string tok = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
        if (!tok.empty() && tok.back() == '\r') tok.pop_back();
        v.push_back(tok);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return v;
}

// ---------- line patterns (verbatim ports of the JS regular expressions) ----------
const std::regex RE_ANGLE(R"RX(^[ \t]*#[ \t]*include[ \t]*<([^>]+)>[ \t]*(/\*.*\*/)?[ \t]*$)RX");
const std::regex RE_QUOTED(R"RX(^[ \t]*#[ \t]*include[ \t]*"[^"]*"[ \t]*(/\*.*\*/)?[ \t]*$)RX");
const std::regex RE_IFNDEF(R"RX(^[ \t]*#[ \t]*ifndef[ \t]+([A-Z0-9_]+)[ \t]*$)RX");
const std::regex RE_DEFINE(R"RX(^[ \t]*#[ \t]*define[ \t]+([A-Z0-9_]+)[ \t]*(/\*.*)?[ \t]*$)RX");
const std::regex RE_ENDIFC(R"RX(^[ \t]*#[ \t]*endif[ \t]+/\*[ \t]*([A-Z0-9_]+)[ \t]*\*/[ \t]*$)RX");
const std::regex RE_CPLUS(R"RX(^[ \t]*#[ \t]*ifdef[ \t]+__cplusplus[ \t]*$)RX");
const std::regex RE_EXTERN(R"RX(^[ \t]*extern[ \t]+"C"[ \t]*\{[ \t]*$)RX");
const std::regex RE_BRACE(R"RX(^[ \t]*\}[ \t]*$)RX");
const std::regex RE_USING(R"RX(^[ \t]*using namespace [A-Za-z_:]+;[ \t]*$)RX");
const std::regex RE_PP_IF(R"RX(^#\s*(if|ifdef|ifndef)\b)RX");
const std::regex RE_PP_ENDIF(R"RX(^#\s*endif\b)RX");

// ---------- hoisted system includes (JS: a Set; kept in insertion order) ----------
std::vector<std::string> g_sys;
void addSystemInclude(const std::string& h) {
    if (std::find(g_sys.begin(), g_sys.end(), h) == g_sys.end()) g_sys.push_back(h);
}

struct Opts {
    bool stripGuard = false;
    bool stripUsing = false;
    bool patchCast = false;
};

// JS-equivalent String.replace(/re/g, fn): non-overlapping, left to right.
// (Groups in `m` are relative to the not-yet-consumed remainder of the line.)
std::string replaceFn(const std::string& s, const std::regex& re,
                      const std::function<std::string(const std::smatch&)>& fn) {
    std::string out, subject = s;
    std::smatch m;
    while (std::regex_search(subject, m, re)) {
        out += m.prefix().str();
        out += fn(m);
        subject = m.suffix().str();
        if (m[0].length() == 0) {            // guard against empty matches (JS bumps by 1)
            if (subject.empty()) break;
            out += subject[0];
            subject.erase(0, 1);
        }
    }
    out += subject;
    return out;
}

// ---------- C -> C++ cast patching (JS: patchVoidCasts) ----------
const std::string DECL_P =
    R"RX(((?:const\s+|unsigned\s+|signed\s+)*[A-Za-z_][A-Za-z_0-9_]*(?:\s+(?:const\s+|unsigned\s+|signed\s+)*[A-Za-z_][A-Za-z_0-9_]*)*\s*\*+\s*[A-Za-z_][A-Za-z_0-9_]*\s*))RX";
// ALLOC: JS uses (?<![\w>.])malloc etc; std::regex has no lookbehind, so we consume the
// optional bad prefix in group 3 and reject the match (return it unchanged) when present.
const std::string ALLOC_P =
    R"RX(((?:ds_x)?([\w>.]?)(?:malloc|calloc|realloc)))RX";
const std::regex RE_CAST_DECL(DECL_P + R"RX(=\s*)RX" + ALLOC_P + R"RX(\s*)RX");
const std::regex RE_CAST_COMMA(R"RX(,\s*)RX" + DECL_P + R"RX(=\s*)RX" + ALLOC_P + R"RX(\s*)RX");
const std::regex RE_MEMBER(R"RX(([A-Za-z_][A-Za-z_0-9_]*(?:->|\.)[A-Za-z_][A-Za-z_0-9_]*(?:\[[^\]]*\])?)\s*=\s*(ds_x(?:malloc|calloc|realloc))\s*\()RX");
const std::regex RE_FIELD(R"RX((?:->|\.)([A-Za-z_0-9]+))RX");
const std::regex RE_LISTDATA(R"RX(((?:const\s+|unsigned\s+|signed\s+)*[A-Za-z_][A-Za-z_0-9_]*(?:\s+[A-Za-z_][A-Za-z_0-9_]*)*\s*\*+\s*[A-Za-z_][A-Za-z_0-9_]*\s*=\s*)ds_list_data\s*\()RX");
const std::regex RE_HASHGET(R"RX(((?:const\s+|unsigned\s+|signed\s+)*[A-Za-z_][A-Za-z_0-9_]*(?:\s+[A-Za-z_][A-Za-z_0-9_]*)*\s*\*+\s*[A-Za-z_][A-Za-z_0-9_]*\s*=\s*)ds_hash_get\s*\()RX");
const std::regex RE_BASEPTR(R"RX((unsigned char\s*\*\s*[A-Za-z_][A-Za-z_0-9_]*\s*=\s*)base\b)RX");
const std::regex RE_SKIPLEAD(R"RX(^\s*(//|\*|/\*))RX");

const std::map<std::string, std::string> MEMBER_CASTS = {
    {"tree", "(int64_t *)"}, {"adj", "(adj_t *)"}, {"items", "(edge_t *)"},
    {"root", "(trie_node *)"}, {"child", "(trie_node *)"}, {"parent", "(int *)"},
    {"rank", "(unsigned char *)"}, {"data", "(char *)"}, {"entries", "(ds_ring_entry *)"},
    {"words", "(uint64_t *)"}, {"buckets", "(ds_hash_node **)"},
};

void declTypeAndName(const std::string& lhs, std::string& type, std::string& name) {
    size_t sp = lhs.find_last_of('*');
    if (sp == std::string::npos) { type = ""; name = trim(lhs); return; } // JS: lastIndexOf -> -1
    type = trim(lhs.substr(0, sp + 1));
    name = trim(lhs.substr(sp + 1));
}

std::string patchLine(const std::string& l) {
    if (std::regex_search(l, RE_SKIPLEAD)) return l;  // comment / continuation lines untouched
    std::string s = l;
    s = replaceFn(s, RE_CAST_DECL, [](const std::smatch& m) -> std::string {
        const std::string lhs = m[1].str(), alloc = m[2].str();
        if (!m[3].str().empty()) return m[0].str();   // lookbehind would have failed
        std::string type, name;
        declTypeAndName(lhs, type, name);
        return type + " " + name + " = (" + type + ") " + alloc;
    });
    s = replaceFn(s, RE_CAST_COMMA, [](const std::smatch& m) -> std::string {
        const std::string lhs = m[1].str(), alloc = m[2].str();
        if (!m[3].str().empty()) return m[0].str();
        std::string type, name; declTypeAndName(lhs, type, name);
        return ", " + type + " " + name + " = (" + type + ") " + alloc + "(";
    });
    s = replaceFn(s, RE_MEMBER, [](const std::smatch& m) -> std::string {
        const std::string lhs = m[1].str(), fn = m[2].str();
        std::smatch mm;
        if (std::regex_search(lhs, mm, RE_FIELD)) {
            auto it = MEMBER_CASTS.find(mm[1].str());
            if (it != MEMBER_CASTS.end()) return lhs + " = " + it->second + fn + "(";
        }
        return m[0].str();
    });
    s = replaceFn(s, RE_LISTDATA, [](const std::smatch& m) -> std::string {
        std::string type, name; declTypeAndName(m[1].str(), type, name);
        return m[1].str() + "(" + type + ")ds_list_data(";
    });
    s = replaceFn(s, RE_HASHGET, [](const std::smatch& m) -> std::string {
        std::string type, name; declTypeAndName(m[1].str(), type, name);
        return m[1].str() + "(" + type + ")ds_hash_get(";
    });
    s = std::regex_replace(s, RE_BASEPTR, "$1(unsigned char *)base");
    return s;
}

std::vector<std::string> patchVoidCasts(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (const std::string& l : lines) out.push_back(patchLine(l));
    return out;
}

// ---------- transform (guards / includes / verbatim #if blocks) ----------
std::vector<std::string> transform(const std::vector<std::string>& lines, const Opts& o) {
    std::vector<std::string> out;
    const size_t n = lines.size();
    size_t i = 0;
    int guards = 0;
    while (i < n) {
        const std::string line = rstrip(lines[i]);
        const std::string t = trim(line);

        if (o.stripGuard) {
            std::smatch m1;
            if (std::regex_search(line, m1, RE_IFNDEF)) {
                const std::string nxt = (i + 1 < n) ? rstrip(lines[i + 1]) : std::string();
                std::smatch m2;
                if (std::regex_search(nxt, m2, RE_DEFINE) && m1[1].str() == m2[1].str()) {
                    i += 2; ++guards; continue;
                }
            }
            if (std::regex_search(line, RE_ENDIFC)) { ++i; if (guards > 0) --guards; continue; }
            if (std::regex_search(t, RE_PP_ENDIF) && guards > 0) {
                int d = 0;
                bool pushedCloser = false;
                for (size_t k = i + 1; k < n; ++k) {
                    const std::string tt = trim(lines[k]);
                    if (std::regex_search(tt, RE_PP_IF)) ++d;
                    else if (std::regex_search(tt, RE_PP_ENDIF)) {
                        if (!d) { out.push_back(line); pushedCloser = true; break; }
                        --d;
                    }
                }
                if (pushedCloser) break;      // JS: i = lines.length; break
                --guards; ++i; continue;
            }
        }
        // (the JS stripExternC branches are dead code in this pipeline - stripCWrappers
        //  always runs first and no transform() call passes the flag; not ported.)
        if (std::regex_search(t, RE_PP_IF)) {   // copy whole conditional block verbatim
            const size_t start = i;
            int depth = 0;
            while (i < n) {
                const std::string tt = trim(lines[i]);
                if (std::regex_search(tt, RE_PP_IF)) ++depth;
                else if (std::regex_search(tt, RE_PP_ENDIF)) { --depth; if (!depth) { ++i; break; } }
                ++i;
            }
            for (size_t k = start; k < i; ++k) out.push_back(lines[k]);  // originals, untouched
            continue;
        }
        std::smatch am;
        if (std::regex_search(line, am, RE_ANGLE)) { addSystemInclude(trim(am[1].str())); ++i; continue; }
        if (std::regex_search(line, RE_QUOTED)) { ++i; continue; }
        if (o.stripUsing && std::regex_search(line, RE_USING)) { ++i; continue; }
        out.push_back(line);
        ++i;
    }
    return o.patchCast ? patchVoidCasts(out) : out;
}

// ---------- stripCWrappers pre-pass (JS: recursive) ----------
std::vector<std::string> stripCWrappers(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    const size_t n = lines.size();
    size_t i = 0;
    while (i < n) {
        const std::string line = (i < n ? rstrip(lines[i]) : std::string());
        const std::string nx = (i + 1 < n ? rstrip(lines[i + 1]) : std::string());
        const std::string af = (i + 2 < n ? rstrip(lines[i + 2]) : std::string());
        if (std::regex_search(line, RE_CPLUS) &&
            (std::regex_search(nx, RE_EXTERN) || std::regex_search(nx, RE_BRACE)) &&
            std::regex_search(af, RE_PP_ENDIF)) { i += 3; continue; }
        if (std::regex_search(line, RE_CPLUS) && std::regex_search(nx, RE_EXTERN)) {
            int depth = 0; size_t j = i;
            while (j < n) {
                const std::string tt = trim(lines[j]);
                if (std::regex_search(tt, RE_PP_IF)) ++depth;
                else if (std::regex_search(tt, RE_PP_ENDIF)) { --depth; if (!depth) break; }
                ++j;
            }
            size_t a = std::min(i + 2, n), b = std::min(j, n);
            const std::vector<std::string> sub(lines.begin() + a, lines.begin() + std::max(a, b));
            for (auto&& s : stripCWrappers(sub)) out.push_back(s);
            i = j + 1; continue;
        }
        if (std::regex_search(line, RE_EXTERN)) {
            int bal = 1; size_t j = i + 1;
            while (j < n && bal > 0) {
                for (char ch : lines[j]) {
                    if (ch == '{') ++bal;
                    else if (ch == '}') { --bal; if (!bal) break; }
                }
                ++j;
            }
            size_t a = std::min(i + 1, n), b = std::min(j > 0 ? j - 1 : 0, n);
            const std::vector<std::string> sub(lines.begin() + a, lines.begin() + std::max(a, b));
            for (auto&& s : stripCWrappers(sub)) out.push_back(s);
            i = j; continue;
        }
        out.push_back(lines[i]);
        ++i;
    }
    return out;
}

// ---------- site-specific fixes (JS: MANUAL_FIXES, applied after patchVoidCasts) ----------
struct Fix { std::regex re; std::string fmt; };
const std::map<std::string, std::vector<Fix>> MANUAL_FIXES = {
    {"core/ds_search.c", {{ std::regex(R"RX(, \*p = (ds_x(?:malloc|calloc|realloc))\()RX"), ", *p = (char *)$1(" }}},
    {"core/ds_sort.c",   {{ std::regex(R"RX(merge_range\(base, tmp,)RX"), "merge_range((unsigned char *)base, (unsigned char *)tmp," }}},
    {"core/ds_graph.c",  {{ std::regex(R"RX(^(\s*)(\S.*?)\s*=\s*\(edge_t\)\{([^}]*)\};\s*$)RX"), "$1{ edge_t sr_tmp__ = { $3 }; $2 = sr_tmp__; }" }}},
};

// ---------- emit helpers ----------
const char* ORDER_HINT[] = {
    "stdio.h", "stdlib.h", "string.h", "ctype.h", "math.h", "stddef.h", "stdint.h",
    "cstdio", "cstdlib", "cstring", "cctype", "cmath", "cstdint", "climits", "ctime",
    "algorithm", "chrono", "fstream", "sstream", "string", "vector", "type_traits"
};

int hintRank(const std::string& h) {
    const int n = static_cast<int>(sizeof(ORDER_HINT) / sizeof(ORDER_HINT[0]));
    for (int k = 0; k < n; ++k) if (h == ORDER_HINT[k]) return k;
    return n;
}

struct Entry { const char* name; bool optional; };

const Entry CORE_HEADERS[] = {
    {"ds_common.h", false}, {"ds_bitset.h", false}, {"ds_fenwick.h", false},
    {"ds_graph.h", false}, {"ds_hash.h", false}, {"ds_heap.h", false}, {"ds_list.h", false},
    {"ds_lru.h", false}, {"ds_queue.h", false}, {"ds_ring.h", false}, {"ds_search.h", false},
    {"ds_sort.h", false}, {"ds_stack.h", false}, {"ds_trie.h", false}, {"ds_unionfind.h", false},
    {"ds_vector.h", false}, {"geo.h", false}, {"sha256.h", false}, {"qr.h", true},
};
const Entry APP_HEADERS[] = {
    {"csv.hpp", false}, {"clock.hpp", false}, {"result.hpp", false}, {"money.hpp", false},
    {"dswrap.hpp", false}, {"models.hpp", false}, {"app.hpp", false}, {"asciimap.hpp", false},
    {"basemap.hpp", true},
    {"cli.hpp", false}, {"ticket.hpp", false}, {"http_server.hpp", false},
};
const Entry APP_SOURCES[] = {
    {"asciimap.cpp", false}, {"auth.cpp", false}, {"basemap.cpp", true}, {"booking.cpp", false}, {"catalog.cpp", false},
    {"cli.cpp", false}, {"clock.cpp", false}, {"coupons.cpp", false}, {"csv.cpp", false},
    {"money.cpp", false}, {"notifications.cpp", false}, {"ops.cpp", true}, {"payment.cpp", false},
    {"search.cpp", false}, {"seats.cpp", false}, {"storage.cpp", false}, {"ticket.cpp", false},
    {"tracking.cpp", false}, {"vendor.cpp", false}, {"assistant.cpp", true}, {"http_server.cpp", false},
};

const char* BANNER =
R"BNR(/* =====================================================================
 * SmartRoute - THE MAIN FILE: the entire project combined into one translation unit.
 * Generated by:  tools/onefile.cpp   (make onefile regenerates it whenever a source changes)
 *
 * Contains, in order: the C17 data-structure core (core/), the C++17 application
 * engine (app/), the HTTP + web server, the embedded web page (build/web_page.html:
 * web/index.html with Leaflet inlined, as a byte array) and main(). It is compiled as
 * C++17 - the former .c files are patched with explicit pointer casts so the same
 * source still builds in both forms.
 *
 * Build (MSYS2 UCRT64 / MinGW-w64, from the project root):
 *   g++ -std=c++17 -O2 -Wall -Wextra main.cpp -o bin/smartroute_onefile.exe -lm -lws2_32
 * Run:
 *   bin/smartroute_onefile.exe --demo --no-save
 *   bin/smartroute_onefile.exe --serve 8080 --open      (needs the data/ folder next to it)
 * ===================================================================== */
)BNR";

}  // namespace

int main(int argc, char* argv[]) {
    std::string root = (argc > 1) ? argv[1] : std::string(".");
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    const std::string coreDir = root + "/core";
    const std::string appDir = root + "/app";
    const std::string webPath = root + "/build/web_page.html";      /* index.html + inlined Leaflet, made by tools/page_gen.cpp */
    const std::string outPath = root + "/main.cpp";

    auto fail = [](const std::string& msg) -> int { std::cerr << "onefile: " << msg << "\n"; return 1; };

    // ---- resolve the per-copy file lists (optional files included iff present) ----
    std::vector<std::string> coreHeaders, coreSources, appHeaders, appSources;
    for (const Entry& e : CORE_HEADERS) {
        const std::string p = coreDir + "/" + e.name;
        if (fileExists(p)) {
            coreHeaders.push_back(e.name);
        } else if (!e.optional) {
            return fail("required input file missing: " + p);
        } else {
            std::cout << "onefile: note, optional file absent, skipping: " << p << "\n";
        }
    }
    // coreSources: one .c per accepted header (h -> c), as the JS did
    for (const std::string& h : coreHeaders) {
        std::string c = h;
        c.replace(c.size() - 2, 2, ".c");
        coreSources.push_back(c);
    }
    for (const Entry& e : APP_HEADERS) {
        if (!fileExists(appDir + "/" + e.name)) return fail("required input file missing: " + appDir + "/" + e.name);
        appHeaders.push_back(e.name);
    }
    for (const Entry& e : APP_SOURCES) {
        const std::string p = appDir + "/" + e.name;
        if (fileExists(p)) appSources.push_back(e.name);
        else if (!e.optional) return fail("required input file missing: " + p);
        else std::cout << "onefile: note, optional file absent, skipping: " << p << "\n";
    }
    if (!fileExists(webPath)) return fail("required input file missing: " + webPath + " (run `make` first - tools/page_gen.cpp builds it from web/index.html + web/vendor/leaflet.*)");
    if (!fileExists(appDir + "/main.cpp")) return fail("required input file missing: " + appDir + "/main.cpp");

    // ---- pass 1: transform every file (records system includes into g_sys) ----
    std::map<std::string, std::vector<std::string>> bodies;   // key: "core/x.h" / "app/x.cpp"
    auto loadTo = [&](const std::string& rel, const std::string& disk, const Opts& o, bool preC) -> bool {
        std::string text;
        if (!readFile(disk, text)) return false;
        std::vector<std::string> lines = splitLines(text);
        if (preC) lines = stripCWrappers(lines);
        std::vector<std::string> body = transform(lines, o);
        if (auto it = MANUAL_FIXES.find(rel); it != MANUAL_FIXES.end()) {
            for (const Fix& f : it->second)
                for (std::string& l : body) l = std::regex_replace(l, f.re, f.fmt);
        }
        bodies[rel] = std::move(body);
        return true;
    };

    Opts optHdr; optHdr.stripGuard = true;
    Opts optAppHdr; optAppHdr.stripGuard = true; optAppHdr.stripUsing = true;
    Opts optCoreC; optCoreC.patchCast = true;
    Opts optAppCpp; optAppCpp.stripUsing = true;
    Opts optMain;

    for (const std::string& h : coreHeaders)
        if (!loadTo("core/" + h, coreDir + "/" + h, optHdr, true)) return fail("cannot read core/" + h);
    for (const std::string& h : appHeaders)
        if (!loadTo("app/" + h, appDir + "/" + h, optAppHdr, true)) return fail("cannot read app/" + h);
    for (const std::string& c : coreSources)
        if (!loadTo("core/" + c, coreDir + "/" + c, optCoreC, false)) return fail("cannot read core/" + c);
    for (const std::string& c : appSources)
        if (!loadTo("app/" + c, appDir + "/" + c, optAppCpp, false)) return fail("cannot read app/" + c);
    if (!loadTo("app/main.cpp", appDir + "/main.cpp", optMain, false)) return fail("cannot read app/main.cpp");

    // ---- pass 2: emit ----
    std::sort(g_sys.begin(), g_sys.end(), [](const std::string& a, const std::string& b) {
        const int ra = hintRank(a), rb = hintRank(b);
        return ra != rb ? ra < rb : a < b;
    });

    std::string out(BANNER);
    out += "/* ---- system headers (deduped from the whole project) ---- */\n";
    for (const std::string& h : g_sys) out += "#include <" + h + ">\n";
    out += "\n";

    size_t sections = 0;
    auto section = [&](const std::string& label, const std::string& key) {
        out += "\n/* ===================== " + label + " ===================== */\n";
        out += trim(join(bodies[key], '\n')) + "\n";
        ++sections;
    };
    for (const std::string& h : coreHeaders) section("core/" + h, "core/" + h);
    for (const std::string& h : appHeaders) section("app/" + h, "app/" + h);
    for (const std::string& c : coreSources) section("core/" + c + " (C core, compiled as C++)", "core/" + c);
    for (const std::string& c : appSources) section("app/" + c, "app/" + c);

    std::string html;
    if (!readFile(webPath, html)) return fail("cannot read " + webPath);
    out += "\n/* ===================== the web page (index.html + inlined Leaflet), embedded ===================== */\n";
    out += "const unsigned char sr_page_data[] = {\n";
    for (char ch : html) { out += std::to_string(static_cast<unsigned>(static_cast<unsigned char>(ch))); out += ','; }
    out += "0};\n";
    out += "const unsigned long sr_page_len = sizeof(sr_page_data) - 1;\n";
    section("app/main.cpp (entry point)", "app/main.cpp");

    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f) return fail("cannot write " + outPath);
    f << out;
    f.close();

    std::cout << "wrote " << outPath << ": " << out.size() << " bytes, "
              << sections << " sections, " << g_sys.size() << " hoisted system includes\n";
    return 0;
}
