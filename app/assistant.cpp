/*
 * PURPOSE: A rule-based trip assistant (the original project's "AI Copilot" idea, without any AI service):
 *          it understands city names (even partial ones), dates, and "cheapest / fastest / morning / sleeper"
 *          in a sentence, then answers from the real search and journey planner.
 * WHY THIS NAME: "assistant" = the chat helper tab.
 * CONNECTS TO: core/ds_trie (partial city names via suggest_cities), core/ds_search (KMP phrase test), search.cpp, clock.hpp.
 * COMPLEXITY: O(words x L) for city matching + the search it triggers.
 * HONESTY: it is not artificial intelligence. It matches words with fixed rules, and says so when it cannot understand.
 */
#include "app.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

extern "C" {
#include "ds_search.h"
}

namespace sr {

static std::vector<std::string> words_of(const std::string &text) {
    std::vector<std::string> w;
    std::string cur;
    for (char ch : text) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c) || ch == '-' || ch == '+') cur += (char)std::tolower(c);
        else if (!cur.empty()) { w.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) w.push_back(cur);
    for (std::string &s : w) {                                       /* "-" and "+" only matter inside dates like 2026-10-24 or +3 */
        while (!s.empty() && s.back() == '-') s.pop_back();
        while (!s.empty() && s[0] == '-') s.erase(0, 1);
    }
    return w;
}

static bool has(const std::vector<std::string> &w, const char *x) { return std::find(w.begin(), w.end(), x) != w.end(); }

/* ---- clock phrases (spec 6.8): "after 7 am", "before 9pm", "after 19:30", "after 12 am" ---- */

static bool all_digits(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

static int ampm_of(const std::string &s) { return s == "am" ? 0 : (s == "pm" ? 1 : -1); }

/* Tries to read a time of day starting at w[i] (the number token). On success returns minutes after
   midnight and sets *end to one past the consumed tokens; -1 if it does not look like a clock time. */
static int clock_time(const std::vector<std::string> &w, size_t i, size_t *end) {
    if (i >= w.size() || w[i].empty() || !std::isdigit((unsigned char)w[i][0])) return -1;
    std::string tok = w[i];
    int min = 0, ap = -1;
    size_t k = i + 1;
    size_t tail = tok.find_first_of("pm");                       /* "9pm" / "7am" in one token */
    if (tail != std::string::npos) {
        if (!all_digits(tok.substr(0, tail))) return -1;
        ap = ampm_of(tok.substr(tail));
        if (ap < 0) return -1;
        tok = tok.substr(0, tail);
    }
    if (!all_digits(tok)) return -1;
    int h = std::atoi(tok.c_str());
    if (h > 23) return -1;
    if (ap < 0 && k < w.size()) {
        if (ampm_of(w[k]) >= 0) { ap = ampm_of(w[k]); k++; }
        else if (all_digits(w[k]) && w[k].size() <= 2 && std::atoi(w[k].c_str()) <= 59) {
            int ap2 = (k + 1 < w.size()) ? ampm_of(w[k + 1]) : -1;
            if (ap2 >= 0) { min = std::atoi(w[k].c_str()); ap = ap2; k += 2; }        /* 7:30 am */
            else if (h >= 13) { min = std::atoi(w[k].c_str()); k += 1; }              /* 19:30 */
        }
    }
    if (ap == 0 && h == 12) h = 0;                                /* 12 am = midnight */
    if (ap == 1 && h < 12) h += 12;                               /* 12 pm = noon */
    *end = k;
    return h * 60 + min;
}

AssistantAnswer App::assistant(const std::string &text) {
    AssistantAnswer a;
    std::vector<std::string> w = words_of(text.substr(0, 300));
    static const char *STOP[] = {"the", "for", "and", "bus", "buses", "any", "get", "can", "you", "from", "via", "tom", "day", "now", "show", "find", "book", "need", "want", "ticket", "tickets"};
    struct Hit { size_t pos; int city; };
    std::vector<Hit> hits;
    for (size_t i = 0; i < w.size(); i++) {                          /* city names: exact (hash) or a unique prefix (trie) */
        int city = find_city(w[i]);
        if (city < 0 && w[i].size() >= 3 && !std::any_of(std::begin(STOP), std::end(STOP), [&](const char *s) { return w[i] == s; })) {
            char out[10][64]; int ids[10];
            if (suggest_cities(w[i].c_str(), 10, out, ids) == 1) city = ids[0];
        }
        if (city >= 0) hits.push_back(Hit{i, city});
    }
    int from = -1, to = -1;
    for (size_t h = 0; h < hits.size(); h++) {                       /* "from X" / "to Y" keywords decide direction */
        size_t before = hits[h].pos;
        if (before >= 1 && w[before - 1] == "from") from = hits[h].city;
        if (before >= 1 && (w[before - 1] == "to" || w[before - 1] == "for" || w[before - 1] == "towards")) to = hits[h].city;
    }
    for (size_t h = 0; h < hits.size() && (from < 0 || to < 0); h++) {    /* otherwise: order of mention */
        if (hits[h].city == from || hits[h].city == to) continue;
        if (from < 0 && to != hits[h].city) from = hits[h].city; else if (to < 0) to = hits[h].city;
    }
    if (from < 0 && to >= 0 && hits.size() >= 2) from = to;          /* "Delhi to Delhi": both mentions are the same city */
    if (to < 0 && from >= 0 && hits.size() >= 2) to = from;
    a.from = from; a.to = to;
    if (from < 0 && to >= 0) {
        a.text = "Where are you leaving from? Try: \"bus from Jaipur to " + std::string(cities[(size_t)to].name) + " tomorrow\".";
        return a;
    }
    if (from < 0) {
        a.text = "I can help you find a bus. Try: \"cheapest bus from Delhi to Jaipur tomorrow\" or \"fastest way from Mysuru to Madurai\". I understand city names, dates (today, tomorrow, +3, 2026-10-24) and words like cheapest, fastest, morning or sleeper. I am a rule-based helper, not an AI.";
        return a;
    }
    if (to < 0) {
        std::string list;
        for (size_t i = 0; i < routes.size(); i++) if (routes[i].from_city == from) list += (list.empty() ? "" : ", ") + std::string(cities[(size_t)routes[i].to_city].name);
        a.text = std::string(cities[(size_t)from].name) + " has direct buses to: " + (list.empty() ? "no city" : list) + ". Tell me where you want to go.";
        return a;
    }
    if (from == to) { a.text = "Please name two different cities."; return a; }
    int offset = 1;                                                  /* date: default tomorrow */
    bool assumed = true;
    int y, m, d;
    if (has(w, "today") || has(w, "now")) { offset = 0; assumed = false; }
    else if (has(w, "tomorrow") || has(w, "tmrw")) { offset = ds_kmp_contains_nocase(text.c_str(), "day after tomorrow") ? 2 : 1; assumed = false; }
    else if (has(w, "overmorrow")) { offset = 2; assumed = false; }
    for (size_t i = 0; i < w.size(); i++) {
        if (w[i][0] == '+' && w[i].size() > 1) { offset = std::atoi(w[i].c_str() + 1); assumed = false; }
        else if (w[i] == "in" && i + 2 < w.size() && std::isdigit((unsigned char)w[i + 1][0]) && w[i + 2].rfind("day", 0) == 0) { offset = std::atoi(w[i + 1].c_str()); assumed = false; }
        else if (parse_ist_date(w[i].c_str(), &y, &m, &d)) { offset = (int)(day_number(from_ist(y, m, d, 12, 0)) - day_number(clock.now())); assumed = false; }
    }
    if (offset < 0 || offset > 90) { a.text = "I can only look from today up to 90 days ahead."; return a; }
    a.date_key = date_key(clock.now() + (int64_t)offset * MS_DAY);
    SearchFilters f;
    SortKey sort = SORT_EARLIEST;
    std::string wants;
    if (has(w, "cheapest") || has(w, "cheap") || has(w, "budget") || has(w, "lowest") || has(w, "affordable")) { sort = SORT_CHEAPEST; wants = "cheapest first"; }
    else if (has(w, "fastest") || has(w, "quick") || has(w, "quickest") || has(w, "shortest")) { sort = SORT_FASTEST; wants = "fastest first"; }
    if (has(w, "morning")) f.dep_window = 1; else if (has(w, "afternoon")) f.dep_window = 2; else if (has(w, "evening") || has(w, "night") || has(w, "overnight")) f.dep_window = 3;
    if (has(w, "sleeper")) for (size_t i = 0; i < types.size(); i++) if (types[i].layout == LAYOUT_SLEEPER) f.type_mask |= 1u << i;
    if (has(w, "seater")) {
        for (size_t i = 0; i < types.size(); i++) if (types[i].layout != LAYOUT_SLEEPER) f.type_mask |= 1u << i;
        wants += (wants.empty() ? "seater coaches" : ", seater coaches");
    }
    /* coach makes live in the coach-type label ("Volvo-style Premium"): KMP, case-insensitive (structure 18) */
    static const char *MAKES[] = {"volvo", "scania", "mercedes", "electric"};
    for (const char *mk : MAKES) {
        if (!has(w, mk)) continue;
        for (size_t i = 0; i < types.size(); i++) {
            if (!ds_kmp_contains_nocase(types[i].label, mk)) continue;
            f.type_mask |= 1u << i;
            std::string nice(mk);
            nice[0] = (char)std::toupper((unsigned char)nice[0]);
            wants += (wants.empty() ? "" : ", ") + nice + (nice == "Electric" ? "-coach" : "-style");
        }
    }
    /* "after 7 am" / "before 9pm": departure time-of-day, minutes after midnight */
    for (size_t i = 0; i + 1 < w.size(); i++) {
        if (w[i] != "after" && w[i] != "before") continue;
        size_t end = i + 1;
        int mins = clock_time(w, i + 1, &end);
        if (mins < 0) continue;
        if (w[i] == "after") f.dep_after = mins; else f.dep_before = mins;
        char hhmm[16];
        std::snprintf(hhmm, sizeof hhmm, "%02d:%02d", mins / 60 % 24, mins % 60);
        wants += (wants.empty() ? "" : ", ") + std::string("leaving ") + w[i] + " " + hhmm;
        i = end - 1;
    }
    /* amenities: honour them when the catalogue has them, say so honestly when it has not (spec 6.8) */
    struct Ami { const char *word; const char *label; const char *needle; };
    static const Ami AMIS[] = {{"ac", "AC", "AC"}, {"air", "air conditioning", "AC"}, {"charging", "charging points", "Charging point"},
                               {"blanket", "blankets", "Blanket"}, {"snack", "snack box", "Snack box"}, {"cctv", "CCTV", "CCTV"},
                               {"wifi", "Wi-Fi", "Wi-Fi"}, {"wi-fi", "Wi-Fi", "Wi-Fi"}};
    for (const Ami &am : AMIS) {
        if (!has(w, am.word)) continue;
        bool in_catalogue = false;
        for (size_t i = 0; i < types.size(); i++) if (ds_kmp_contains_nocase(types[i].amenities, am.needle)) in_catalogue = true;
        if (!in_catalogue) {
            a.ignored.push_back(am.label);
            continue;
        }
        if (!f.amenity[0]) std::snprintf(f.amenity, sizeof f.amenity, "%s", am.needle);
    }
    a.sort_mode = sort; a.dep_window = f.dep_window; a.dep_after = f.dep_after; a.dep_before = f.dep_before;
    a.type_mask = f.type_mask; a.amenity = f.amenity;
    std::string ignore_note;
    for (size_t i = 0; i < a.ignored.size(); i++) ignore_note += (i ? ", " : "") + a.ignored[i];
    std::string route_txt = std::string(cities[(size_t)from].name) + " to " + cities[(size_t)to].name;
    std::string when = fmt_date(clock.now() + (int64_t)offset * MS_DAY) + (assumed ? " (I assumed tomorrow)" : "");
    if (find_route(from, to) < 0) {
        if (!same_network(from, to)) { a.text = "There is no bus route between " + route_txt + ": the two cities are in different bus networks."; return a; }
        Vec<PlanLeg> legs;
        int64_t mins = 0, paise = 0;
        Result<Unit> r = plan_journey(from, to, sort == SORT_CHEAPEST ? PLAN_CHEAPEST : PLAN_FASTEST, legs, &mins, &paise);
        if (!r.ok) { a.text = "No route found: " + r.message; return a; }
        std::string path = cities[(size_t)legs[0].from_city].name;
        for (size_t i = 0; i < legs.size(); i++) path += std::string(" -> ") + cities[(size_t)legs[i].to_city].name;
        a.text = "There is no direct bus " + route_txt + ", but you can connect: " + path + ", about " + fmt_duration((int)mins) + " in total including 60 minute layovers, lowest fares " + fmt_rs(paise) + " per seat. Use the Journey Planner tab for each leg.";
        return a;
    }
    Vec<SearchRow> rows;
    search(from, to, a.date_key, f, sort, rows);
    a.opened_search = rows.size() > 0;
    if (rows.size() == 0) {
        a.text = "No buses match for " + route_txt + " on " + when + ". Try another day or fewer filters.";
        if (!ignore_note.empty()) a.text += " (I ignored: " + ignore_note + " - not in the demo catalogue.)";
        return a;
    }
    a.text = std::to_string(rows.size()) + (rows.size() == 1 ? " bus" : " buses") + " for " + route_txt + " on " + when + (wants.empty() ? "" : ", " + wants) + ":\n";
    for (size_t i = 0; i < rows.size() && i < 3; i++) {
        const SearchRow &r = rows[i];
        a.text += "  " + std::to_string(i + 1) + ") " + bus_label(r.bus_idx) + ", " + types[(size_t)buses[(size_t)r.bus_idx].type_idx].label + ", " + fmt_hm(r.depart_ms) + " to " + fmt_hm_plus(r.arrive_ms, r.depart_ms) + ", from " + fmt_rs(r.fare_min) + ", " + std::to_string(r.seats_left) + " seats left\n";
    }
    a.text += "Press \"Open in Search\" to choose seats.";
    if (!ignore_note.empty()) a.text += " (Not in the demo catalogue, so I ignored: " + ignore_note + ".)";
    return a;
}

}  // namespace sr
