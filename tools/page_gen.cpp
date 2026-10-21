/*
 * tools/page_gen.cpp - SmartRoute web page builder.
 *
 * PURPOSE: assemble the delivered web page. web/index.html is the authoring copy with two
 *          placeholder lines; this tool replaces them with the vendored Leaflet files so the
 *          result is ONE self-contained HTML document (the one exception to "no third-party
 *          code": Leaflet 1.9.4, BSD-2-Clause, kept in web/vendor/ with its licence).
 *          It can also emit the page as a C++ byte array (build/web_page.cpp), which is what
 *          the server compiles in, so the binary serves the page from any folder.
 * WHY THIS NAME: "page_gen" = generates the page (and its embedded form) the app serves.
 * CONNECTS TO: web/index.html, web/vendor/leaflet.css, web/vendor/leaflet.js; called by the
 *          Makefile and by tools/onefile.cpp.
 * INVARIANTS: deterministic, LF newlines, byte-identical output for the same inputs.
 *          Missing vendor files are NOT an error: the placeholders are simply left out and a
 *          note is printed. The page then shows the SVG schematic instead of the live map,
 *          which is the documented fallback (deleting Leaflet must leave a working page).
 * Usage:  g++ -std=c++17 -O2 -Wall -Wextra tools/page_gen.cpp -o build/page_gen.exe
 *         ./build/page_gen.exe <root> html   -> writes <root>/build/web_page.html
 *         ./build/page_gen.exe <root> bytes <out.cpp>   -> writes the sr_page_data array
 */
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool read_file(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void normalize_lf(std::string &s) {                     /* vendored files may carry CRLF */
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        o += s[i];
    }
    s.swap(o);
}

const char *CSS_MARK = "<script data-smartroute-inject=\"leaf-css\" src=\"#\"></script>";
const char *JS_MARK = "<script data-smartroute-inject=\"leaf-js\" src=\"#\"></script>";

/* Assemble web/index.html + web/vendor/leaflet.* into the single delivered page. */
std::string assemble(const std::string &root) {
    std::string page;
    if (!read_file(root + "/web/index.html", page)) {
        std::cerr << "page_gen: cannot read " << root << "/web/index.html\n";
        return std::string();
    }
    std::string css, js;
    bool have_css = read_file(root + "/web/vendor/leaflet.css", css);
    bool have_js = read_file(root + "/web/vendor/leaflet.js", js);
    normalize_lf(css);
    normalize_lf(js);
    if (!have_css || !have_js)
        std::cout << "page_gen: note, Leaflet vendor files absent - the page keeps the schematic fallback\n";
    std::string css_blk =
        "<style>\n/* Leaflet 1.9.4 (BSD-2-Clause) - vendored copy of web/vendor/leaflet.css, inlined by tools/page_gen.cpp */\n";
    if (have_css) css_blk += css;
    if (!css_blk.empty() && css_blk.back() != '\n') css_blk += '\n';
    css_blk += "</style>";
    std::string js_blk =
        "<script>\n/* Leaflet 1.9.4 (BSD-2-Clause) - vendored copy of web/vendor/leaflet.js, inlined by tools/page_gen.cpp */\n";
    if (have_js) {
        if (js.find("</script") != std::string::npos) {   /* would truncate the block: refuse, do not emit a broken page */
            std::cerr << "page_gen: web/vendor/leaflet.js contains </script>, cannot inline it\n";
            return std::string();
        }
        js_blk += js;
    }
    if (!js_blk.empty() && js_blk.back() != '\n') js_blk += '\n';
    js_blk += "</script>";
    size_t p = page.find(CSS_MARK);
    if (p != std::string::npos && have_css) page.replace(p, std::string(CSS_MARK).size(), css_blk);
    else if (p != std::string::npos) page.replace(p, std::string(CSS_MARK).size(), "");
    size_t q = page.find(JS_MARK);
    if (q != std::string::npos && have_js) page.replace(q, std::string(JS_MARK).size(), js_blk);
    else if (q != std::string::npos) page.replace(q, std::string(JS_MARK).size(), "");
    return page;
}

void emit_array(std::ostream &o, const std::string &page) {
    o << "extern const unsigned char sr_page_data[]; extern const unsigned long sr_page_len;\n";
    o << "const unsigned char sr_page_data[] = {\n";
    unsigned line = 0;
    for (size_t i = 0; i < page.size(); i++) {
        o << (unsigned)(unsigned char)page[i] << ',';
        if (++line == 28) { o << '\n'; line = 0; }
    }
    o << "0};\n";
    o << "const unsigned long sr_page_len = sizeof(sr_page_data) - 1;\n";
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: page_gen <root> html | page_gen <root> bytes <out.cpp>\n";
        return 2;
    }
    const std::string root = argv[1];
    const std::string mode = argv[2];
    const std::string page = assemble(root);
    if (page.empty()) return 1;
    if (mode == "html") {
        const std::string out = root + "/build/web_page.html";
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "page_gen: cannot write " << out << "\n"; return 1; }
        f << page;
        std::printf("page_gen: %s = %zu bytes (leaflet inlined)\n", out.c_str(), page.size());
        return 0;
    }
    if (mode == "bytes") {
        if (argc < 4) { std::cerr << "page_gen: bytes mode needs an output file\n"; return 2; }
        std::ofstream f(argv[3], std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "page_gen: cannot write " << argv[3] << "\n"; return 1; }
        f << "// generated from web/index.html + web/vendor/leaflet.* by the Makefile - do not edit\n";
        f << "#include <cstdlib>\n";
        emit_array(f, page);
        std::printf("page_gen: %s = page of %zu bytes as a C array\n", argv[3], page.size());
        return 0;
    }
    std::cerr << "page_gen: unknown mode '" << mode << "'\n";
    return 2;
}
