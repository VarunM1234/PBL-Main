/*
 * PURPOSE: The notification outbox (queue) and the single "bell" list with unread counts and preferences.
 * WHY THIS NAME: "notifications" = alerts shown to a user.
 * CONNECTS TO: core/ds_queue (outbox, row 5), core/ds_hash (dedupe set, row 19), storage (log file).
 * COMPLEXITY: notify O(1) average; flush O(k) for k queued alerts.
 */
#include "app.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace sr {

void App::notify(int user_id, int kind, const std::string &title, const std::string &body, const std::string &dedupe_key) {
    if (!dedupe_key.empty()) {
        if (alert_seen.has(dedupe_key)) return;           /* already sent once: drop duplicate */
        alert_seen.put(dedupe_key, 1);
    }
    Notification n;
    std::memset(&n, 0, sizeof n);
    n.user_id = user_id;
    n.kind = kind;
    std::snprintf(n.title, sizeof n.title, "%s", title.c_str());
    std::snprintf(n.body, sizeof n.body, "%s", body.c_str());
    std::snprintf(n.dedupe, sizeof n.dedupe, "%s", dedupe_key.c_str());
    n.created_ms = alert_time_ms ? alert_time_ms : clock.now();
    outbox.push(n);
}

void App::flush_outbox() {
    Notification n;
    std::ofstream log;
    if (!save_dir.empty()) log.open(save_dir + "/notifications.log", std::ios::app);
    while (outbox.pop(&n)) {
        const User *u = user_by_id(n.user_id);
        if (!u || !(u->prefs & (1 << n.kind))) continue;         /* user muted this category */
        n.id = (int)notes.size() + 1;
        notes.push(n);
        if (log) log << "[" << fmt_datetime(n.created_ms) << "] user " << n.user_id << " | " << n.title << " | " << n.body << "\n";
    }
}

int App::unread_count(int user_id) const {
    int c = 0;
    for (size_t i = 0; i < notes.size(); i++) if (notes[i].user_id == user_id && !notes[i].read) c++;
    return c;
}

void App::mark_all_read(int user_id) {
    for (size_t i = 0; i < notes.size(); i++) if (notes[i].user_id == user_id) notes[i].read = 1;
}

std::vector<const Notification *> App::notifications_for(int user_id) const {
    std::vector<const Notification *> out;
    for (size_t i = notes.size(); i-- > 0;) if (notes[i].user_id == user_id) out.push_back(notes.at(i));   /* newest first */
    return out;
}

void App::set_prefs(int user_id, int mask) {
    if (user_id >= 1 && (size_t)user_id <= users.size()) users[(size_t)user_id - 1].prefs = mask;
}

}  // namespace sr
