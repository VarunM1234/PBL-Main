/*
 * PURPOSE: Program entry: reads options, loads data + saved state, then runs the CLI, the demo or the web server.
 * WHY THIS NAME: main() is where every C/C++ program starts.
 * CONNECTS TO: cli.hpp, http_server.hpp, app.hpp. The ONLY place that reads the system clock (once).
 * COMPLEXITY: n/a.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include "app.hpp"
#include "basemap.hpp"
#include "cli.hpp"
#include "http_server.hpp"

using namespace sr;

static void make_dir(const std::string &path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

static void enable_ansi_colors() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004);   /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
#endif
}

/* Launch robustness: the CSVs live in the project's `data` folder, which `run-web.cmd` reaches by cd-ing first.
   If the exe is double-clicked (working directory = bin), look for cities.csv next to the exe and one level up
   before giving up, so the demo still starts instead of dying with "could not open data/cities.csv". */
static bool has_catalog(const std::string &dir) {
    std::FILE *f = std::fopen((dir + "/cities.csv").c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
static std::string exe_dir(const char *argv0) {
    std::string p(argv0 ? argv0 : "");
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}
static std::string resolve_data(const std::string &wanted, const char *argv0) {
    if (has_catalog(wanted)) return wanted;
    std::string ed = exe_dir(argv0);
    std::string beside = ed + "/data", above = ed + "/../data";
    if (has_catalog(beside)) return beside;
    if (has_catalog(above)) return above;
    return wanted;                                                   /* unchanged: load_catalog prints the real error */
}

static void usage() {
    std::printf("SmartRoute (C/C++ + DSA) options:\n"
                "  --demo             scripted, reproducible session (fixed clock, no saving)\n"
                "  --serve [PORT]     start the HTML web UI + JSON API (default port 8080)\n"
                "  --open             with --serve: open the page in your browser\n"
                "  --now \"YYYY-MM-DD HH:MM\"   set the virtual clock (IST)\n"
                "  --sync-clock       start the virtual clock at the real time instead of the saved clock\n"
                "  --data DIR         data folder (default: data)\n"
                "  --no-save          do not read or write data/save\n"
                "  --no-color         plain output (no ANSI colours)\n"
                "  --make-basemap [OUT.json]                 write the offline map background from the CSVs\n"
                "  --make-basemap IN.geojson OUT.json        ...including coastlines from a GeoJSON of land polygons\n"
                "  --help             this text\n");
}

int main(int argc, char **argv) {
    bool demo = false, serve = false, no_save = false, color = true, sync_clock = false, open_browser = false;
    bool make_basemap = false;
    std::vector<std::string> mb_args;                            /* --make-basemap [IN.geojson] OUT.json */
    int port = 8080;
    std::string data = "data", now_text;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "--serve") { serve = true; if (i + 1 < argc && argv[i + 1][0] != '-') port = std::atoi(argv[++i]); }
        else if (a == "--now" && i + 1 < argc) now_text = argv[++i];
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--no-save") no_save = true;
        else if (a == "--open") open_browser = true;
        else if (a == "--no-color") color = false;
        else if (a == "--sync-clock") sync_clock = true;
        else if (a == "--make-basemap") { make_basemap = true; while (i + 1 < argc && argv[i + 1][0] != '-') mb_args.push_back(argv[++i]); }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::printf("Unknown option: %s\n", a.c_str()); usage(); return 2; }
    }
    if (port < 1 || port > 65535) { std::printf("Port must be 1 to 65535.\n"); return 2; }
    if (data == "data") data = resolve_data(data, argv[0]);              /* double-clicked exe: find the folder anyway */
    if (color) enable_ansi_colors();
    App app;
    int64_t start_ms = 0;
    if (!now_text.empty()) {
        if (!parse_ist_datetime(now_text.c_str(), &start_ms)) { std::printf("--now must look like \"2026-10-19 10:00\".\n"); return 2; }
    } else if (demo) {
        parse_ist_datetime("2026-10-19 10:00", &start_ms);                 /* fixed clock: demo output is identical every run */
    } else {
        start_ms = (int64_t)std::time(nullptr) * 1000;                     /* the only system-clock read in the program */
    }
    app.clock.set(start_ms);
    std::string err;
    if (!app.load_catalog(data, &err)) { std::printf("Could not load the data files:\n  %s\n(See README: Troubleshooting > CSV errors.)\n", err.c_str()); return 1; }
    if (make_basemap) {                                            /* build tool: writes the offline background and exits */
        std::string in_path, out_path = "web/basemap.json";
        if (mb_args.size() == 1) out_path = mb_args[0];
        else if (mb_args.size() >= 2) { in_path = mb_args[0]; out_path = mb_args[1]; }
        if (mb_args.size() > 2) { std::printf("Too many names after --make-basemap.\n"); usage(); return 2; }
        std::string m;
        bool done = sr::basemap_write(app, in_path, out_path, &m);
        std::printf("%s\n%s\n", m.c_str(), done ? "Offline map background written - the tracker draws it under the tiles."
                                                : "Nothing was written (the line above says why).");
        return done ? 0 : 1;
    }
    if (!demo && !no_save) {
        app.save_dir = data + "/save";
        make_dir(app.save_dir);
        std::string msgs;
        bool loaded = app.load_saved(&msgs);
        if (!msgs.empty()) std::printf("Save-file notice: %s\n", msgs.c_str());
        if (loaded) std::printf("Loaded saved state (virtual clock continues at %s).\n", fmt_datetime(app.clock.now()).c_str());
        if (!now_text.empty() || sync_clock || !loaded) app.clock.set(start_ms);
        app.process_events();
    }
    CliOptions opt;
    opt.color = color;
    opt.autosave = !no_save && !demo;
    if (demo) return run_demo(app, opt);
    if (serve) return run_server(app, port, data == "data" ? "web" : data + "/../web", opt.autosave, open_browser);
    return run_cli(app, opt);
}
