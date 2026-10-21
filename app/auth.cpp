/*
 * PURPOSE: Accounts, salted password hashing, login with lockout, role separation.
 * WHY THIS NAME: "auth" = authentication (who are you?).
 * CONNECTS TO: core/sha256 (EDUCATIONAL hashing; real systems use bcrypt/argon2), idx_email hash map.
 * COMPLEXITY: email lookup O(1) average; password hash O(iterations).
 */
#include "app.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>

extern "C" {
#include "sha256.h"
}

namespace sr {

static constexpr int HASH_ITERATIONS = 10000;
static constexpr int MAX_FAILED_LOGINS = 5;

static std::string lower_copy(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/* Spec 6.4 / 9: display text may not contain markup or control characters. Bytes >= 128 are allowed
   (UTF-8 letters of any language), so a Hindi name passes while "<img onerror=...>" does not. */
bool display_text_ok(const std::string &s) {
    for (unsigned char c : s) {
        if (c == '<' || c == '>' || c == '"' || c == '&' || c == '\\' || c == '`') return false;
        if (c < 32 || c == 127) return false;
    }
    return true;
}

static void hash_password(const char *salt_hex, const char *password, char out[65]) {
    sha256_salted_iter(reinterpret_cast<const uint8_t *>(salt_hex), std::strlen(salt_hex), password, HASH_ITERATIONS, out);
}

int App::add_user(const char *name, const char *email, const char *phone, int role, int company_id, const char *password, int staff_id) {
    User u;
    std::memset(&u, 0, sizeof u);
    u.id = (int)users.size() + 1;
    std::snprintf(u.name, sizeof u.name, "%s", name);
    std::snprintf(u.email, sizeof u.email, "%s", lower_copy(email).c_str());
    std::snprintf(u.phone, sizeof u.phone, "%s", phone);
    u.role = role;
    u.company_id = company_id;
    u.staff_id = staff_id;
    u.prefs = (1 << NK_COUNT) - 1;                          /* every alert category on */
    for (int i = 0; i < 4; i++) std::snprintf(u.salt + i * 8, 9, "%08x", (unsigned)(next_random() >> 16));
    hash_password(u.salt, password, u.hash);
    users.push(u);
    idx_email.put(u.email, u.id - 1);
    return u.id;
}

int App::add_guest() {
    /* Booking without an account (spec 6.4): a passenger row whose stored hash is "!" can never equal a
       64-char hex password digest, so this account is unreachable by login - it only exists behind the
       random session token handed out by /api/guest. */
    User u;
    std::memset(&u, 0, sizeof u);
    u.id = (int)users.size() + 1;
    std::snprintf(u.name, sizeof u.name, "Guest traveller");
    std::snprintf(u.email, sizeof u.email, "guest-%d@invalid.local", u.id);
    u.role = ROLE_PASSENGER;
    u.prefs = (1 << NK_COUNT) - 1;
    std::snprintf(u.hash, sizeof u.hash, "!");
    users.push(u);
    idx_email.put(u.email, u.id - 1);
    return u.id;
}

int App::guest_count() const {
    int n = 0;
    for (size_t i = 0; i < users.size(); i++) if (std::strcmp(users[i].hash, "!") == 0) n++;
    return n;
}

const User *App::user_by_id(int id) const {
    return (id >= 1 && (size_t)id <= users.size()) ? users.at((size_t)id - 1) : nullptr;
}

int App::user_id_by_email(const std::string &email) const {
    int *v = idx_email.get(lower_copy(email));
    return v ? *v + 1 : 0;
}

Result<int> App::register_passenger(const std::string &name, const std::string &email, const std::string &phone, const std::string &password) {
    if (name.empty() || name.size() > 39) return Result<int>::fail(Err::VALIDATION, "Name must be 1 to 39 characters");
    if (!display_text_ok(name)) return Result<int>::fail(Err::VALIDATION, "Name cannot contain < > \" & \\ or a backtick");
    size_t at = email.find('@');
    if (email.size() > 63 || at == std::string::npos || at == 0 || email.find('.', at) == std::string::npos)
        return Result<int>::fail(Err::VALIDATION, "Enter a valid email address");
    if (!display_text_ok(email)) return Result<int>::fail(Err::VALIDATION, "Email cannot contain < > \" & \\ or a backtick");
    if (phone.size() > 19) return Result<int>::fail(Err::VALIDATION, "Phone number is too long");
    if (!display_text_ok(phone)) return Result<int>::fail(Err::VALIDATION, "Phone cannot contain < > \" & \\ or a backtick");
    bool has_letter = false, has_digit = false;
    for (char c : password) { has_letter |= std::isalpha((unsigned char)c) != 0; has_digit |= std::isdigit((unsigned char)c) != 0; }
    if (password.size() < 8 || password.size() > 64 || !has_letter || !has_digit)
        return Result<int>::fail(Err::VALIDATION, "Password needs 8+ characters with a letter and a digit");
    if (idx_email.has(lower_copy(email))) return Result<int>::fail(Err::CONFLICT, "That email is already registered");
    return Result<int>::success(add_user(name.c_str(), email.c_str(), phone.c_str(), ROLE_PASSENGER, 0, password.c_str()));
}

Result<int> App::login(const std::string &email, const std::string &password, int expected_role) {
    static const char *GENERIC = "Invalid credentials";
    int uid = user_id_by_email(email);
    if (uid == 0) {                                     /* spend the same time as a real check */
        char dummy[65];
        hash_password("00000000000000000000000000000000", password.c_str(), dummy);
        return Result<int>::fail(Err::UNAUTHENTICATED, GENERIC);
    }
    User &u = users[(size_t)uid - 1];
    if (u.role != expected_role) return Result<int>::fail(Err::FORBIDDEN_ROLE, GENERIC);   /* wrong portal: same message */
    if (u.failed_logins >= MAX_FAILED_LOGINS)
        return Result<int>::fail(Err::UNAUTHENTICATED, "Too many failed attempts. Account locked (demo: delete data/save/users.sav to reset).");
    char h[65];
    hash_password(u.salt, password.c_str(), h);
    if (std::strcmp(h, u.hash) != 0) { u.failed_logins++; return Result<int>::fail(Err::UNAUTHENTICATED, GENERIC); }
    u.failed_logins = 0;
    session_user = uid;
    return Result<int>::success(uid, "Welcome, " + std::string(u.name));
}

}  // namespace sr
