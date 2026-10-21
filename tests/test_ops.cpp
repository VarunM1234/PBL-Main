/*
 * PURPOSE: End-to-end test of the staff side (driver console + admin fleet portal) over real HTTP, in the
 *          order a human uses it: register/login -> the vendor publishes a run -> a passenger holds, books,
 *          pays and gets a QR boarding pass -> the driver signs in, starts the trip, sends a GPS fix, reports
 *          a delay, scans the pass and ends the run -> the platform admin reads the KPI, telemetry, coupon
 *          and traffic-scan endpoints. Replaces the old JavaScript smoke script (no JS in this project).
 * WHY THIS NAME: tests the /api/driver/... and /api/admin/... surface from app/ops.cpp through the server.
 * CONNECTS TO: app/http_server.cpp (sr::run_server and the /api routes), app/ops.cpp, app/app.hpp, tests/test.h.
 * COMPLEXITY: one request at a time (the server is single-threaded); O(checks) round trips.
 * SAFETY: the server runs IN-PROCESS on port 3199 with autosave OFF and save_dir pointed at a throwaway
 *          folder, so data/save (the real saved history) is never read or written. sr::run_server() has no
 *          stop button (it loops in select() until the process ends), so the test leaves through _Exit() after
 *          flushing and deleting the throwaway files; the OS closing the listening sockets IS the shutdown.
 */
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#include <winsock2.h>
#define SR_MKDIR(p) _mkdir(p)
typedef SOCKET sock_t;
static const sock_t SR_BAD_SOCK = INVALID_SOCKET;
static void sr_close(sock_t s) { closesocket(s); }
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#define SR_MKDIR(p) mkdir(p, 0755)
typedef int sock_t;
static const sock_t SR_BAD_SOCK = -1;
static void sr_close(sock_t s) { close(s); }
#endif
#include "../app/app.hpp"
#include "../app/basemap.hpp"
#include "../app/http_server.hpp"
#include "test.h"

using namespace sr;

static const char *PW = "Demo@1234";            /* the demo password from data/users_seed.csv */
static const int PORT = 3199;                    /* never 3000/8080: those are the human demo ports */
static const char *SAVE = "build/ops-save";      /* throwaway folder; data/save is never touched */

static int g_pass = 0, g_fail = 0;

/* ---------- just enough JSON reading (the server emits flat objects and arrays of flat objects) ---------- */

static std::string json_field(const std::string &j, const std::string &key);

/* ---------- PASS/FAIL reporting (same output shape as the smoke script this replaces) ---------- */

static void ok(const char *name, bool cond, const std::string &extra = std::string()) {
    CHECK(cond);                                                 /* the shared counter from test.h */
    g_pass += cond ? 1 : 0;
    g_fail += cond ? 0 : 1;
    if (extra.empty()) std::printf("%s  %s\n", cond ? "PASS" : "FAIL", name);
    else std::printf("%s  %s  %s\n", cond ? "PASS" : "FAIL", name, extra.c_str());
    std::fflush(stdout);
}

/* ---------- portable "mkdir -p" and save-folder cleanup (same helpers as test_app.cpp) ---------- */

static void make_dirs(const char *path) {
    std::string p = path;
    for (size_t i = p.find('/'); i != std::string::npos; i = p.find('/', i + 1)) {
        SR_MKDIR(p.substr(0, i).c_str());       /* ignore EEXIST */
    }
    SR_MKDIR(p.c_str());
}

static void clear_save_dir(const char *dir) {
    static const char *sections[] = {"meta", "users", "fleet", "trips", "holds", "coupons",
                                     "bookings", "payments", "notes", "shares", "delays", "gps", "crew"};
    for (const char *s : sections) {
        for (const char *ext : {".sav", ".sav.bak", ".sav.tmp"}) {
            std::remove((std::string(dir) + "/" + s + ext).c_str());
        }
    }
}

/* App::flush_outbox() appends a plain-text notification log into save_dir even when autosave is off,
   so the throwaway folder is scrubbed for it as well as for the .sav files. */
static void clear_ops_dir() {
    clear_save_dir(SAVE);
    std::remove((std::string(SAVE) + "/notifications.log").c_str());
}

/* ---------- the JSON reader ---------- */

/* The value of "key" in the object text j: strings unescaped, numbers/booleans as written, {..} / [..] as a span. */
static std::string json_field(const std::string &j, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    size_t k = j.find(needle);
    while (k != std::string::npos) {
        size_t c = k + needle.size();
        while (c < j.size() && (j[c] == ' ' || j[c] == '\t')) c++;
        if (c < j.size() && j[c] == ':') {
            c++;
            while (c < j.size() && (j[c] == ' ' || j[c] == '\t')) c++;
            if (c >= j.size()) return "";
            if (j[c] == '"') {                                                   /* a string */
                std::string o;
                for (size_t i = c + 1; i < j.size(); i++) {
                    if (j[i] == '\\' && i + 1 < j.size()) {
                        char nx = j[++i];
                        o += (nx == 'n') ? '\n' : (nx == 't') ? '\t' : (nx == 'r') ? '\r' : nx;
                    } else if (j[i] == '"') return o;
                    else o += j[i];
                }
                return "";
            }
            if (j[c] == '{' || j[c] == '[') {                                   /* nested: hand back the whole span */
                char open = j[c], closing = (open == '{') ? '}' : ']';
                int depth = 0;
                bool in_str = false;
                for (size_t i = c; i < j.size(); i++) {
                    if (in_str) { if (j[i] == '\\') i++; else if (j[i] == '"') in_str = false; continue; }
                    if (j[i] == '"') in_str = true;
                    else if (j[i] == open) depth++;
                    else if (j[i] == closing) { if (--depth == 0) return j.substr(c, i - c + 1); }
                }
                return "";
            }
            size_t e = c;                                                        /* number / true / false / null */
            while (e < j.size() && j[e] != ',' && j[e] != '}' && j[e] != ']') e++;
            std::string o = j.substr(c, e - c);
            while (!o.empty() && (o.back() == ' ' || o.back() == '\r' || o.back() == '\n')) o.pop_back();
            return o;
        }
        k = j.find(needle, k + needle.size());
    }
    return "";
}

/* The elements of an array: the whole body when key is empty, else the array kept under "key". */
static std::vector<std::string> json_array(const std::string &j, const std::string &key) {
    size_t start = 0;
    if (!key.empty()) {
        std::string needle = "\"" + key + "\":";
        size_t k = j.find(needle);
        if (k == std::string::npos) return std::vector<std::string>();
        start = k + needle.size();
    }
    while (start < j.size() && (j[start] == ' ' || j[start] == '\t' || j[start] == '\r' || j[start] == '\n')) start++;
    if (start >= j.size() || j[start] != '[') return std::vector<std::string>();
    std::vector<std::string> out;
    int depth = 0;
    bool in_str = false;
    size_t elem = std::string::npos;
    for (size_t i = start; i < j.size(); i++) {
        char c = j[i];
        if (in_str) { if (c == '\\') i++; else if (c == '"') in_str = false; continue; }
        if (c == '"') { in_str = true; if (depth == 1 && elem == std::string::npos) elem = i; continue; }
        if (c == '[' || c == '{') { if (depth == 1 && elem == std::string::npos) elem = i; depth++; continue; }
        if (c == ']' || c == '}') {
            depth--;
            if (depth == 0) { if (elem != std::string::npos) out.push_back(j.substr(elem, i - elem + 1)); return out; }
            continue;
        }
        if (c == ',' && depth == 1) { if (elem != std::string::npos) out.push_back(j.substr(elem, i - elem)); elem = std::string::npos; continue; }
        if (depth == 1 && elem == std::string::npos && c != ' ' && c != '\t' && c != '\r' && c != '\n') elem = i;
    }
    return out;
}

static int as_int(const std::string &s) { return (int)std::strtol(s.c_str(), nullptr, 10); }

/* ---------- form encoding ---------- */

static std::string url_encode(const std::string &s) {
    static const char HEX[] = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '-' || c == '_' || c == '~') o += (char)c;
        else { o += '%'; o += HEX[c >> 4]; o += HEX[c & 15]; }
    }
    return o;
}

/* "k1=v1&k2=v2" with every name and value percent-encoded (spaces, the '|' inside the QR text, ...). */
static std::string form_from(std::initializer_list<std::pair<const char *, const char *> > kv) {
    std::string out;
    for (const auto &p : kv) {
        if (!out.empty()) out += "&";
        out += url_encode(p.first); out += "="; out += url_encode(p.second);
    }
    return out;
}

/* ---------- a very small HTTP/1.1 client over a plain socket ---------- */

struct Reply {
    int status = 0;
    std::string head;
    std::string body;
    bool good() const { return status != 0; }
    bool ok_json() const { return status == 200 && body.find("\"ok\":true") != std::string::npos; }
    std::string error() const { return json_field(body, "error"); }
    std::string token() const { return json_field(body, "token"); }
};

/* How many times a literal appears - used on the offline background, where counting the shapes in the
   text is a second, independent read of the same promise the JSON parser makes. */
static long count_in(const std::string &hay, const std::string &needle) {
    long n = 0;
    for (size_t i = hay.find(needle); i != std::string::npos; i = hay.find(needle, i + needle.size())) n++;
    return n;
}

static void set_timeouts(sock_t s) {
#ifdef _WIN32
    DWORD ms = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof ms);
#else
    timeval tv;
    tv.tv_sec = 15; tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
#endif
}

/* Abortive close: the answer is already in hand, and a RST keeps port 3199 free of TIME_WAIT rows. */
static void sr_close_abort(sock_t s) {
    linger lo;
    lo.l_onoff = 1; lo.l_linger = 0;
    setsockopt(s, SOL_SOCKET, SO_LINGER, (const char *)&lo, sizeof lo);
    sr_close(s);
}

/* One request per connection: the server answers with "Connection: close" and hangs up. */
static bool http_call(const char *method, const std::string &path, const std::string &body,
                      const std::string &token, Reply *out) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SR_BAD_SOCK) return false;
    set_timeouts(s);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)PORT);
    if (connect(s, (sockaddr *)&addr, sizeof addr) != 0) { sr_close(s); return false; }

    std::string req = std::string(method) + " " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1:" + std::to_string(PORT) + "\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        + (token.empty() ? std::string() : "Authorization: Bearer " + token + "\r\n")
        + "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < req.size()) {
        int n = (int)send(s, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0) { sr_close(s); return false; }
        sent += (size_t)n;
    }

    std::string raw;
    char buf[4096];
    size_t header_end = std::string::npos, body_start = 0, want = 0;
    long content_len = -1;
    bool done = false;
    for (int guard = 0; guard < 8192 && !done; guard++) {
        int n = (int)recv(s, buf, (int)sizeof buf, 0);
        if (n == 0) break;                                                      /* the server hung up */
        if (n < 0) { sr_close_abort(s); return false; }
        raw.append(buf, (size_t)n);
        if (raw.size() > 8u * 1024u * 1024u) break;                             /* runaway guard */
        if (header_end == std::string::npos) {
            header_end = raw.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            body_start = header_end + 4;
            std::string head = raw.substr(0, header_end), lower;
            lower.reserve(head.size());
            for (char ch : head) lower += (char)std::tolower((unsigned char)ch);
            size_t cl = lower.find("content-length:");
            if (cl != std::string::npos) content_len = std::strtol(head.c_str() + cl + 15, nullptr, 10);
        }
        want = content_len < 0 ? 0 : (size_t)content_len;
        if (content_len >= 0 && raw.size() >= body_start + want) done = true;
    }
    sr_close_abort(s);
    if (header_end == std::string::npos) return false;

    std::string head = raw.substr(0, header_end);
    size_t sp = head.find(' ');
    if (sp == std::string::npos) return false;
    out->head = head;
    out->status = (int)std::strtol(head.c_str() + sp + 1, nullptr, 10);
    out->body = raw.substr(body_start, content_len < 0 ? std::string::npos : want);
    return out->status != 0;
}

static bool get_api(const std::string &path, const std::string &token, Reply *out) {
    return http_call("GET", path, "", token, out) && out->good();
}
static bool post_api(const std::string &path, const std::string &body, const std::string &token, Reply *out) {
    return http_call("POST", path, body, token, out) && out->good();
}

/* ---------- helpers for the golden path ---------- */

/* Sign in through the form API (which portal the role names decides) and keep the bearer token. */
static bool sign_in(const char *email, const char *role, std::string *token) {
    Reply r;
    if (!post_api("/api/login", form_from({{"email", email}, {"password", PW}, {"role", role}}), "", &r) || !r.ok_json()) return false;
    *token = r.token();
    return !token->empty();
}

/* The listener appears as soon as run_server has bound the port; retry until it answers. */
static void wait_for_server(const std::atomic<int> &server_rc) {
    for (int i = 0; i < 150; i++) {
        if (server_rc.load() != -1) return;                                     /* run_server gave up: port busy */
        Reply r;
        if (get_api("/api/cities", "", &r) && r.status == 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));        /* let a failing bind report itself */
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    make_dirs("build");
    make_dirs(SAVE);
    clear_ops_dir();

    App app;
    int64_t ms = 0;
    parse_ist_datetime("2026-10-19 10:00", &ms);                /* fixed virtual clock: the same story every run */
    app.clock.set(ms);
    std::string err;
    if (!app.load_catalog("data", &err)) {
        std::printf("FAIL  engine catalogue load  %s\n", err.c_str());
        std::fflush(stdout);
        return 1;
    }
    app.save_dir = SAVE;                                         /* never data/save */

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("FAIL  winsock could not start\n"); return 1; }
#endif

    std::atomic<int> server_rc(-1);
    std::thread server([&app, &server_rc] { server_rc.store(run_server(app, PORT, "web", /*autosave*/ false, /*open browser*/ false)); });
    server.detach();
    wait_for_server(server_rc);

    Reply r;
    bool up = get_api("/api/cities", "", &r) && r.status == 200;
    /* server_rc stays -1 while run_server is inside its select() loop; anything else means our own bind failed,
       in which case some other process owns 3199 and must not be allowed to answer for this test. */
    bool ours = up && server_rc.load() == -1;
    ok("server listens on port 3199", ours, ours ? std::string("cities answered by our own thread")
                                                 : ("bind=" + std::to_string(server_rc.load())));
    if (!ours) {
        std::printf("\n%d passed, %d failed (the server did not start, so nothing else was attempted)\n", g_pass, g_fail);
        std::fflush(stdout);
        clear_ops_dir();
        std::_Exit(1);
    }

    /* a brand-new passenger account: registration is the front door of the passenger side */
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%lu", (unsigned long)(std::time(nullptr) & 0xffffff));
    std::string new_email = std::string("ops-smoke-") + stamp + "@smartroute.test";
    Reply reg;
    post_api("/api/register", form_from({{"name", "Ops Tester"}, {"email", new_email.c_str()},
                                         {"phone", "9999900011"}, {"password", "OpsDemo@2026"}}), "", &reg);
    ok("passenger register", reg.ok_json(), reg.ok_json() ? new_email : reg.error());

    /* 1. passenger signs in; the vendor creates and publishes the runs */
    std::string user_tok, vendor_tok, driver_tok, admin_tok;
    ok("passenger login", sign_in("user@smartroute.in", "passenger", &user_tok));
    ok("vendor login", sign_in("vendor@smartroute.in", "vendor", &vendor_tok));

    Reply va;
    post_api("/api/vendor/autoassign", "", vendor_tok, &va);                  /* tomorrow's runs, created with a crew */
    ok("vendor trip creation (auto-assign)", va.ok_json(),
       "assigned=" + json_field(va.body, "assigned") + (va.ok_json() ? std::string() : va.error()));

    get_api("/api/vendor/runs", vendor_tok, &r);
    std::vector<std::string> vruns = json_array(r.body, "");
    ok("vendor run list", r.status == 200 && !vruns.empty(), "runs=" + std::to_string(vruns.size()));

    int publish_trip = 0;
    for (const std::string &row : vruns) {
        if (json_field(row, "status") == "SCHEDULED") { publish_trip = as_int(json_field(row, "id")); break; }
    }
    Reply crew;
    if (publish_trip) {
        post_api("/api/vendor/crew", form_from({{"trip", std::to_string(publish_trip).c_str()},
                                                {"driver", "-1"}, {"conductor", "6"}}), vendor_tok, &crew);
    }
    ok("vendor publishes the crew on a run", crew.ok_json(),
       "trip=" + std::to_string(publish_trip) + (crew.ok_json() ? std::string(" conductor=6") : crew.error()));

    get_api("/api/vendor/runs", vendor_tok, &r);
    bool published = false;
    for (const std::string &row : json_array(r.body, "")) {
        if (as_int(json_field(row, "id")) == publish_trip && json_field(row, "conductor") == "Pawan Kumar") published = true;
    }
    ok("published crew is on the run", published, "trip=" + std::to_string(publish_trip));

    /* 2. the driver signs in and takes the first duty run that still has a free seat */
    ok("driver login", sign_in("driver@smartroute.in", "driver", &driver_tok));
    get_api("/api/state", driver_tok, &r);
    std::string user_obj = json_field(r.body, "user");
    ok("state reports role=driver", user_obj.find("\"role\":\"driver\"") != std::string::npos, user_obj);

    get_api("/api/driver/trips", driver_tok, &r);
    std::vector<std::string> runs = json_array(r.body, "");
    ok("driver duty list", !runs.empty(), "runs=" + std::to_string(runs.size()));

    int trip_id = 0, boarding = -1, dropping = -1;
    std::vector<std::string> free_seats;
    for (const std::string &row : runs) {
        int id = as_int(json_field(row, "id"));                                /* duty rows expose "id", not "trip_id" */
        Reply t;
        if (!get_api("/api/trip?id=" + std::to_string(id), user_tok, &t)) continue;
        std::vector<std::string> seats = json_array(t.body, "seats");
        std::vector<std::string> stops = json_array(t.body, "stops");
        std::vector<std::string> avail;
        for (const std::string &s : seats) if (as_int(json_field(s, "state")) == 0) avail.push_back(json_field(s, "label"));
        if (avail.empty()) continue;
        int b = -1, d = -1;
        for (const std::string &s : stops) if (as_int(json_field(s, "type")) == ST_BOARDING) { b = as_int(json_field(s, "idx")); break; }
        for (const std::string &s : stops) if (as_int(json_field(s, "type")) == ST_DROPPING) d = as_int(json_field(s, "idx"));
        trip_id = id; free_seats = avail; boarding = b; dropping = d;
        break;                                                                 /* the same choice the smoke script makes */
    }
    ok("duty row exposes an on-sale trip", trip_id > 0 && boarding >= 0 && dropping >= 0,
       "trip=" + std::to_string(trip_id) + " boarding=" + std::to_string(boarding) +
       " dropping=" + std::to_string(dropping) + " seats=" + std::to_string(free_seats.size()));

    /* 3. hold + book + pay a seat on that very trip, then read the boarding pass */
    int booking_id = 0;
    std::string pnr, qr_text;
    bool booking_reported = false, paid = false;
    for (size_t i = 0; i < free_seats.size() && i < 6 && !booking_reported; i++) {
        std::string seat = free_seats[i];
        Reply hold;
        if (!post_api("/api/hold", form_from({{"trip", std::to_string(trip_id).c_str()}, {"seats", seat.c_str()}}), user_tok, &hold) || !hold.ok_json()) continue;
        Reply bk;
        post_api("/api/bookings", form_from({{"trip", std::to_string(trip_id).c_str()}, {"seats", seat.c_str()},
                                             {"name1", "Smoke Tester"}, {"age1", "30"}, {"gender1", "M"},
                                             {"mode", "full"}, {"contact", "9999999999"},
                                             {"boarding", std::to_string(boarding).c_str()},
                                             {"dropping", std::to_string(dropping).c_str()}}), user_tok, &bk);
        booking_reported = true;
        ok("create booking on the driven trip", bk.ok_json(), bk.ok_json() ? ("seat=" + seat + " pnr=" + json_field(bk.body, "pnr")) : bk.error());
        if (!bk.ok_json()) break;
        booking_id = as_int(json_field(bk.body, "booking"));
        pnr = json_field(bk.body, "pnr");
        Reply pay;
        post_api("/api/pay", form_from({{"booking", std::to_string(booking_id).c_str()}, {"kind", "full"}, {"outcome", "success"}}), user_tok, &pay);
        paid = pay.ok_json();
        ok("pay booking", paid, pay.error());
    }
    if (!booking_reported) ok("create booking on the driven trip", false, "no seat could be held");
    if (!booking_id) ok("pay booking", false, "no booking");

    if (booking_id) {
        Reply tk;
        get_api("/api/ticket?pnr=" + url_encode(pnr), user_tok, &tk);
        qr_text = json_field(tk.body, "qr_text");
        ok("ticket issued with a QR payload", !qr_text.empty(), tk.error() + " payload=" + qr_text.substr(0, 48));
        std::vector<std::string> qr_rows = json_array(tk.body, "qr");
        ok("ticket draws the QR bitmap", !qr_rows.empty() && json_field(tk.body, "boarding_state") == "VALID",
           "rows=" + std::to_string(qr_rows.size()));
        if (qr_text.empty()) qr_text = pnr;                                    /* driver_scan accepts the bare reference too */
    } else {
        ok("ticket issued with a QR payload", false, "no booking");
        ok("ticket draws the QR bitmap", false, "no booking");
    }

    /* 4. driver console actions */
    Reply start;
    post_api("/api/driver/start", form_from({{"trip", std::to_string(trip_id).c_str()}}), driver_tok, &start);
    ok("driver start trip", start.ok_json(), start.error());

    Reply fix;
    post_api("/api/driver/simulate-fix", form_from({{"trip", std::to_string(trip_id).c_str()}}), driver_tok, &fix);
    ok("driver GPS fix (DEMO +5 km)", fix.ok_json(),
       fix.ok_json() ? ("lat=" + json_field(fix.body, "lat") + " lng=" + json_field(fix.body, "lng")) : fix.error());

    Reply delay;
    post_api("/api/driver/delay", form_from({{"trip", std::to_string(trip_id).c_str()}, {"reason", "TRAFFIC"},
                                             {"minutes", "20"}, {"note", "smoke"}}), driver_tok, &delay);
    ok("driver reports a delay", delay.ok_json(), delay.error());

    get_api("/api/driver/manifest?trip=" + std::to_string(trip_id), driver_tok, &r);
    std::vector<std::string> manifest = json_array(r.body, "");
    bool listed = false;
    for (const std::string &p : manifest) if (json_field(p, "name") == "Smoke Tester") listed = true;
    ok("manifest lists the passenger", listed, "rows=" + std::to_string(manifest.size()));

    Reply scan;
    if (!qr_text.empty()) {
        post_api("/api/driver/scan", form_from({{"code", qr_text.c_str()}}), driver_tok, &scan);
    }
    ok("QR boarding check-in", scan.body.find("\"found\":true") != std::string::npos,
       qr_text.empty() ? std::string("no payload") : (json_field(scan.body, "boarding_state") + scan.error()));

    get_api("/api/driver/trail?trip=" + std::to_string(trip_id), driver_tok, &r);
    std::vector<std::string> trail = json_array(r.body, "");
    ok("GPS trail", r.status == 200 && !trail.empty(), "fixes=" + std::to_string(trail.size()));

    Reply ended;
    post_api("/api/driver/end", form_from({{"trip", std::to_string(trip_id).c_str()}}), driver_tok, &ended);
    ok("driver ends trip", ended.ok_json(), ended.error());

    /* 5. admin fleet portal: KPI, telemetry, coupons, DEMO traffic scan */
    ok("admin login", sign_in("admin@smartroute.in", "admin", &admin_tok));
    static const char *panels[] = {"summary", "fleet", "revenue", "coupons", "delays"};
    for (const char *what : panels) {
        Reply panel;
        std::string name = std::string("admin ") + what;
        get_api(std::string("/api/admin/") + what, admin_tok, &panel);
        ok(name.c_str(), panel.status == 200 && panel.body.find("\"error\"") == std::string::npos,
           "http=" + std::to_string(panel.status));
    }

    Reply refused;
    get_api("/api/admin/summary", user_tok, &refused);
    ok("admin summary refuses a passenger",
       refused.status == 401 || refused.body.find("\"ok\":false") != std::string::npos,
       "http=" + std::to_string(refused.status));

    Reply kpi;
    get_api("/api/admin/summary", admin_tok, &kpi);
    long long revenue = std::strtoll(json_field(kpi.body, "revenue").c_str(), nullptr, 10);
    ok("admin KPI counts the fare taken", revenue > 0, "revenue_paise=" + std::to_string(revenue));

    Reply fleet;
    get_api("/api/admin/fleet", admin_tok, &fleet);
    bool seen_run = false, delayed = false;
    for (const std::string &row : json_array(fleet.body, "")) {
        if (as_int(json_field(row, "id")) != trip_id) continue;
        seen_run = json_field(row, "status") == "COMPLETED";
        delayed = as_int(json_field(row, "delay")) >= 20;
    }
    ok("admin telemetry shows the finished run", seen_run, "trip=" + std::to_string(trip_id));
    ok("admin telemetry carries the delay", delayed, "trip=" + std::to_string(trip_id) + " delay>=20 min");

    std::string code = std::string("OPS") + stamp;               /* unique per run: cannot clash with a stale save */
    Reply add;
    post_api("/api/admin/coupon-add", form_from({{"code", code.c_str()}, {"type", "PERCENT"}, {"value", "10"},
                                                 {"cap", "100"}, {"min", "1"}}), admin_tok, &add);
    ok("admin coupon-add", add.ok_json(), add.ok_json() ? code : add.error());

    Reply off;
    post_api("/api/admin/coupon-state", form_from({{"code", code.c_str()}, {"active", "0"}}), admin_tok, &off);
    ok("admin coupon disable", off.ok_json(), off.error());

    Reply traffic;
    post_api("/api/admin/scan-traffic", "", admin_tok, &traffic);
    ok("admin scan-traffic", traffic.ok_json(), "changed=" + json_field(traffic.body, "changed"));

    /* 6. guests, public lookup, time-of-day search, fare cap, drop-off ETA, abuse throttle (spec 6.4 / 6.7 / 6.8 / 7) */
    Reply g;
    post_api("/api/guest", "", "", &g);
    std::string guest_tok = g.token();
    ok("POST /api/guest issues a guest session",
       g.ok_json() && !guest_tok.empty() && g.body.find("\"guest\":true") != std::string::npos, g.error());

    Reply guest_login;
    post_api("/api/login", form_from({{"email", json_field(g.body, "name").c_str()}, {"password", PW}, {"role", "passenger"}}), "", &guest_login);
    ok("nonsense email cannot be signed into", guest_login.status == 401, "http=" + std::to_string(guest_login.status));

    Reply search_plain, search_after;
    get_api("/api/search?from=Delhi&to=Jaipur&date=2026-10-21", "", &search_plain);
    ok("search answers with a fare cap",
       search_plain.status == 200 && std::strtod(json_field(search_plain.body, "fare_cap").c_str(), nullptr) > 0,
       "fare_cap=" + json_field(search_plain.body, "fare_cap"));
    get_api("/api/search?from=Delhi&to=Jaipur&date=2026-10-21&after=420", "", &search_after);   /* "leaving after 7 am" */
    bool after_ok = search_after.status == 200;
    for (const std::string &row : json_array(search_after.body, "rows")) {
        std::string dep = json_field(row, "dep");                       /* "HH:MM" of the departure */
        int hh = as_int(dep.substr(0, 2)), mm = as_int(dep.substr(3, 2));
        if (hh * 60 + mm < 7 * 60) after_ok = false;
    }
    ok("search honours after=420", after_ok, "rows=" + std::to_string(json_array(search_after.body, "rows").size()));

    std::vector<std::string> rows = json_array(search_plain.body, "rows");
    int guest_trip = 0, guest_board = -1, guest_drop = -1;
    std::string guest_seat;
    for (const std::string &row : rows) {
        int id = as_int(json_field(row, "trip"));
        Reply t;
        if (!get_api("/api/trip?id=" + std::to_string(id), guest_tok, &t)) continue;
        std::vector<std::string> stops = json_array(t.body, "stops");
        std::string seat;
        for (const std::string &s : json_array(t.body, "seats"))
            if (as_int(json_field(s, "state")) == 0) { seat = json_field(s, "label"); break; }
        if (seat.empty()) continue;
        for (const std::string &s : stops) {
            if (as_int(json_field(s, "type")) == ST_BOARDING && guest_board < 0) guest_board = as_int(json_field(s, "idx"));
            if (as_int(json_field(s, "type")) == ST_DROPPING) guest_drop = as_int(json_field(s, "idx"));
        }
        if (guest_board < 0 || guest_drop < 0) continue;
        guest_trip = id; guest_seat = seat;
        break;
    }
    ok("guest can browse an on-sale trip", guest_trip > 0, "trip=" + std::to_string(guest_trip));

    Reply g_hold;
    post_api("/api/hold", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"seats", guest_seat.c_str()}}), guest_tok, &g_hold);
    ok("guest holds a seat", g_hold.ok_json(), g_hold.error());
    Reply g_book;
    post_api("/api/bookings", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"seats", guest_seat.c_str()},
                                         {"name1", "Guest One"}, {"age1", "31"}, {"gender1", "M"},
                                         {"mode", "full"}, {"contact", "+91 98765 43210"},
                                         {"boarding", std::to_string(guest_board).c_str()},
                                         {"dropping", std::to_string(guest_drop).c_str()}}), guest_tok, &g_book);
    std::string g_pnr = json_field(g_book.body, "pnr");
    ok("guest books without an account", g_book.ok_json(), g_book.ok_json() ? g_pnr : g_book.error());
    int g_booking_id = as_int(json_field(g_book.body, "booking"));
    Reply g_pay;
    post_api("/api/pay", form_from({{"booking", std::to_string(g_booking_id).c_str()}, {"kind", "full"}, {"outcome", "success"}}), guest_tok, &g_pay);
    ok("guest pays (demo gateway)", g_pay.ok_json(), g_pay.error());

    Reply by_ref;
    get_api("/api/lookup?q=" + url_encode(g_pnr), "", &by_ref);
    ok("lookup by booking reference", by_ref.status == 200 && as_int(json_field(by_ref.body, "trip")) == guest_trip,
       "trip=" + json_field(by_ref.body, "trip"));
    Reply by_phone;
    get_api("/api/lookup?q=9876543210", "", &by_phone);
    ok("lookup by the last 10 phone digits (+91 stored)", by_phone.status == 200 && as_int(json_field(by_phone.body, "trip")) == guest_trip,
       "trip=" + json_field(by_phone.body, "trip"));
    Reply by_spaced;
    get_api("/api/lookup?q=" + url_encode("+91 98765 43210"), "", &by_spaced);
    ok("lookup ignores spacing in the phone", by_spaced.status == 200 && as_int(json_field(by_spaced.body, "trip")) == guest_trip,
       "http=" + std::to_string(by_spaced.status));
    Reply no_hit;
    get_api("/api/lookup?q=ZZZNOPE", "", &no_hit);
    ok("lookup misses say not found", no_hit.status == 404, "http=" + std::to_string(no_hit.status));

    bool throttled = false;
    for (int i = 0; i < 24; i++) {                                      /* the limit is 20 per minute: the flood must hit 429 */
        Reply spam;
        get_api("/api/lookup?q=1111111111", "", &spam);
        if (spam.status == 429) throttled = true;
    }
    ok("lookup throttles past 20 per minute", throttled);

    ok("lookup never exposes names", by_phone.body.find("Guest One") == std::string::npos && by_phone.body.find("\"pnr\"") == std::string::npos,
       "body checked");

    /* Regression (found in the browser): an abandoned 10-minute seat hold must not block the seat
     * forever. The server's virtual clock now follows real time, and /api/clock advances it the same
     * way, so the hold-expiry event fires and the seat is free for the next visitor. */
    Reply g2;
    post_api("/api/guest", "", "", &g2);
    std::string ghost_tok = g2.token();
    Reply t0;
    get_api("/api/trip?id=" + std::to_string(guest_trip), ghost_tok, &t0);
    std::string ghost_seat;
    for (const std::string &s : json_array(t0.body, "seats")) {
        if (json_field(s, "state") == "0") { ghost_seat = json_field(s, "label"); break; }   /* first free seat */
    }
    ok("the trip still has a free seat to hold", !ghost_seat.empty(), "seats checked");
    Reply gh_hold;
    post_api("/api/hold", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"seats", ghost_seat.c_str()}}), ghost_tok, &gh_hold);
    ok("a second guest can hold a free seat", gh_hold.ok_json(), ghost_seat + ": " + gh_hold.error());
    Reply t_held;
    get_api("/api/trip?id=" + std::to_string(guest_trip), ghost_tok, &t_held);
    bool shown_held = false;
    for (const std::string &s : json_array(t_held.body, "seats"))
        if (json_field(s, "label") == ghost_seat && json_field(s, "state") == "3") shown_held = true;  /* held by this very token */
    ok("the hold shows its seat to its owner", shown_held, ghost_seat);
    Reply adv;
    post_api("/api/clock", form_from({{"minutes", "11"}}), "", &adv);                    /* past the 10-minute hold */
    ok("the clock can be advanced", adv.ok_json(), adv.error());
    Reply g3;
    post_api("/api/guest", "", "", &g3);
    Reply re_hold;
    post_api("/api/hold", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"seats", ghost_seat.c_str()}}), g3.token(), &re_hold);
    ok("an expired hold releases the seat (no ghost hold)", re_hold.ok_json(), ghost_seat + ": " + re_hold.error());

    Reply track_drop;
    get_api("/api/track?trip=" + std::to_string(guest_trip) + "&drop=" + std::to_string(guest_drop), "", &track_drop);
    std::vector<std::string> tstops = json_array(track_drop.body, "stops");
    bool eta_ok = track_drop.status == 200 && !tstops.empty();
    for (const std::string &s : tstops)
        if (json_field(s, "planned_ms").empty() || json_field(s, "eta_ms").empty()) eta_ok = false;
    ok("track stops carry planned_ms + eta_ms", eta_ok, "stops=" + std::to_string(tstops.size()));
    ok("track returns drop-off details",
       !json_field(track_drop.body, "drop_name").empty() && as_int(json_field(track_drop.body, "drop_eta_ms")) > 0,
       "drop=" + json_field(track_drop.body, "drop_name"));
    ok("track reports fix telemetry fields",
       track_drop.body.find("\"fixes\"") != std::string::npos && track_drop.body.find("\"avg_kmh\"") != std::string::npos,
       "fields present");
    /* spec 6.9: the simulator panel and the auto stream are for the passenger who holds a booking on
       this trip, so the engine marks ownership. Anonymous /api/track must say false (share viewers). */
    Reply track_owner;
    get_api("/api/track?trip=" + std::to_string(guest_trip), guest_tok, &track_owner);
    ok("track marks the booking holder as owner", track_owner.status == 200 && track_owner.body.find("\"owner\":true") != std::string::npos,
       "http=" + std::to_string(track_owner.status));
    ok("track hides ownership from anonymous viewers", track_drop.body.find("\"owner\":false") != std::string::npos,
       "http=" + std::to_string(track_drop.status));
    Reply sim_anon;
    post_api("/api/sim", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"action", "tick"}}), "", &sim_anon);
    ok("simulating needs a signed-in passenger", sim_anon.status == 401, "http=" + std::to_string(sim_anon.status));
    Reply sim_other;
    post_api("/api/guest", "", "", &g);                                      /* a second, booking-free session for the negative case */
    std::string other_tok = g.token();
    post_api("/api/sim", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"action", "tick"}}), other_tok, &sim_other);
    ok("a passenger cannot simulate someone else's trip", sim_other.status == 404, "http=" + std::to_string(sim_other.status));
    Reply sim_own;
    post_api("/api/sim", form_from({{"trip", std::to_string(guest_trip).c_str()}, {"action", "tick"}}), guest_tok, &sim_own);
    ok("the holder can simulate their own trip", sim_own.status == 200 && sim_own.body.find("\"owner\":true") != std::string::npos,
       "http=" + std::to_string(sim_own.status));

    /* The delivered page must be the ASSEMBLED one: tools/page_gen.cpp inlines Leaflet's CSS and JS into
       web/index.html and writes build/web_page.html, while the authoring copy on disk keeps two
       <script src="#"> placeholders. Serving that copy gives a tracker with no map library at all, and a
       stream that reopens a failed ifstream silently yields zero bytes - both are caught here. */
    Reply page;
    http_call("GET", "/", "", "", &page);
    ok("GET / delivers a full page", page.status == 200 && page.body.size() > 300000,
       "http=" + std::to_string(page.status) + " bytes=" + std::to_string(page.body.size()));
    ok("the page has Leaflet inlined, no unfilled placeholders",
       page.body.find(".leaflet-container") != std::string::npos && page.body.find("src=\"#\"") == std::string::npos,
       "checked");

    /* ---------- the offline map background (spec 8): GET /basemap.json ----------
       The tracker draws this under the tiles, so it has to be served as JSON, hold the whole published
       network, and be the very document the generator's own self-check approves. */
    Reply bm;
    http_call("GET", "/basemap.json", "", "", &bm);
    std::string bm_head_lower;
    for (char ch : bm.head) bm_head_lower += (char)std::tolower((unsigned char)ch);
    ok("GET /basemap.json answers 200 as JSON", bm.status == 200 && bm_head_lower.find("application/json") != std::string::npos,
       "http=" + std::to_string(bm.status));
    ok("the served background carries the whole network (20 city dots, 15 corridors)",
       count_in(bm.body, "\"at\":[") == 20 && count_in(bm.body, "\"pts\":[") == 15,
       "dots=" + std::to_string(count_in(bm.body, "\"at\":[")) + " corridors=" + std::to_string(count_in(bm.body, "\"pts\":[")));
    std::string bm_msg;
    ok("what the server hands out passes the generator's own self-check",
       basemap_self_check(bm.body, 20, 15, &bm_msg), bm_msg);
    std::ifstream bm_file("web/basemap.json", std::ios::binary);
    std::ostringstream bm_ss;
    bm_ss << bm_file.rdbuf();
    ok("the file on disk is what gets served (a fresh copy is picked up without a rebuild)",
       bm.body == bm_ss.str(), "bytes=" + std::to_string(bm.body.size()));
    ok("and the same document comes out of the program with no file at all",
       basemap_generated(app) == bm.body, "bytes=" + std::to_string(basemap_generated(app).size()));
    ok("the background states its own limits in the file",
       bm.body.find("OFFLINE MAP BACKGROUND") != std::string::npos && bm.body.find("not survey geometry") != std::string::npos,
       "note present");
    Reply bm_post;
    http_call("POST", "/basemap.json", "x=1", "", &bm_post);
    ok("the background is read-only (POST refused)", bm_post.status == 404, "http=" + std::to_string(bm_post.status));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::fflush(stdout);

    /* remove only the throwaway save files (with autosave off only the notification log can appear); data/save is untouched */
    clear_ops_dir();
    std::fflush(stderr);
    /* run_server() never returns (it loops in select()), so the server thread is stopped by ending the process;
       every socket it owns is closed by the OS, which frees port 3199 again. */
    std::_Exit(g_fail == 0 ? 0 : 1);
    return g_fail == 0 ? 0 : 1;                                     /* not reached: _Exit() never comes back */
}
