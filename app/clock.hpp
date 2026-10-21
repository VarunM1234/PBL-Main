/*
 * PURPOSE: Virtual clock (UTC epoch milliseconds) and Asia/Kolkata (IST, UTC+5:30) date helpers.
 * WHY THIS NAME: all business logic asks the Clock for "now" so tests and demos are reproducible.
 * CONNECTS TO: booking, seats, tracking, storage, cli. Never reads the system time itself.
 * COMPLEXITY: O(1) for every function.
 */
#ifndef SMARTROUTE_CLOCK_HPP
#define SMARTROUTE_CLOCK_HPP
#include <cstdint>
#include <string>

namespace sr {

constexpr int64_t MS_MIN = 60 * 1000LL;
constexpr int64_t MS_HOUR = 60 * MS_MIN;
constexpr int64_t MS_DAY = 24 * MS_HOUR;
constexpr int64_t IST_OFFSET_MS = 5 * MS_HOUR + 30 * MS_MIN;

struct Civil { int year, month, day, hour, minute, second, weekday; };  /* weekday: 0 = Sunday */

class Clock {
    int64_t now_ms_;
public:
    explicit Clock(int64_t start_ms = 0) : now_ms_(start_ms) {}
    int64_t now() const { return now_ms_; }
    void set(int64_t ms) { now_ms_ = ms; }
    void advance(int64_t ms) { now_ms_ += ms; }
};

Civil to_ist(int64_t utc_ms);
int64_t from_ist(int year, int month, int day, int hour, int minute);
std::string fmt_datetime(int64_t utc_ms);              /* "Mon 20 Oct 2026, 06:30 IST" */
std::string fmt_date(int64_t utc_ms);                  /* "Mon 20 Oct 2026" */
std::string fmt_hm(int64_t utc_ms);                    /* "06:30" */
std::string fmt_hm_plus(int64_t utc_ms, int64_t ref);  /* "03:46 +1" when on a later IST date than ref */
std::string fmt_duration(int minutes);                 /* "6h16m" */
bool parse_ist_datetime(const char *text, int64_t *out_ms);   /* "YYYY-MM-DD HH:MM" */
bool parse_ist_date(const char *text, int *y, int *m, int *d);
int date_key(int64_t utc_ms);                          /* IST calendar date as yyyymmdd */
int64_t ist_midnight(int64_t utc_ms);                  /* UTC ms of 00:00 IST on that IST date */
int64_t day_number(int64_t utc_ms);                    /* whole IST days since 1970-01-01 */

}  // namespace sr
#endif
