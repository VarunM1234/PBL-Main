/*
 * PURPOSE: IST calendar arithmetic (no DST) using the days-from-civil algorithm.
 * WHY THIS NAME: implements clock.hpp.
 * CONNECTS TO: clock.hpp
 * COMPLEXITY: O(1).
 */
#include "clock.hpp"
#include <cstdio>
#include <cstdlib>

namespace sr {

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

static int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int64_t day_number(int64_t utc_ms) { return floor_div(utc_ms + IST_OFFSET_MS, MS_DAY); }

Civil to_ist(int64_t utc_ms) {
    int64_t local = utc_ms + IST_OFFSET_MS;
    int64_t days = floor_div(local, MS_DAY);
    int64_t rem = local - days * MS_DAY;
    Civil c{};
    civil_from_days(days, &c.year, &c.month, &c.day);
    c.hour = (int)(rem / MS_HOUR);
    c.minute = (int)((rem % MS_HOUR) / MS_MIN);
    c.second = (int)((rem % MS_MIN) / 1000);
    c.weekday = (int)(((days % 7) + 11) % 7);       /* 1970-01-01 was a Thursday (4) */
    return c;
}

int64_t from_ist(int y, int m, int d, int hh, int mm) {
    return days_from_civil(y, m, d) * MS_DAY + hh * MS_HOUR + mm * MS_MIN - IST_OFFSET_MS;
}

static const char *WEEKDAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTH[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string fmt_date(int64_t ms) {
    Civil c = to_ist(ms);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s %02d %s %d", WEEKDAY[c.weekday], c.day, MONTH[c.month - 1], c.year);
    return buf;
}

std::string fmt_hm(int64_t ms) {
    Civil c = to_ist(ms);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
    return buf;
}

std::string fmt_datetime(int64_t ms) { return fmt_date(ms) + ", " + fmt_hm(ms) + " IST"; }

std::string fmt_hm_plus(int64_t ms, int64_t ref) {
    int64_t diff = day_number(ms) - day_number(ref);
    std::string s = fmt_hm(ms);
    if (diff > 0) s += " +" + std::to_string(diff);
    return s;
}

std::string fmt_duration(int minutes) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%dh%02dm", minutes / 60, minutes % 60);
    return buf;
}

bool parse_ist_date(const char *t, int *y, int *m, int *d) {
    int n = 0;
    if (std::sscanf(t, "%4d-%2d-%2d%n", y, m, d, &n) != 3 || t[n] != '\0') return false;
    if (*m < 1 || *m > 12 || *d < 1 || *d > 31 || *y < 1970 || *y > 2200) return false;
    Civil c = to_ist(from_ist(*y, *m, *d, 12, 0));      /* reject 31 Feb etc. by round-trip */
    return c.year == *y && c.month == *m && c.day == *d;
}

bool parse_ist_datetime(const char *t, int64_t *out) {
    int y, m, d, hh, mm, n = 0;
    if (std::sscanf(t, "%4d-%2d-%2d %2d:%2d%n", &y, &m, &d, &hh, &mm, &n) != 5 || t[n] != '\0') return false;
    char datebuf[16];
    std::snprintf(datebuf, sizeof datebuf, "%04d-%02d-%02d", y, m, d);
    int yy, mo, dd;
    if (!parse_ist_date(datebuf, &yy, &mo, &dd) || hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    *out = from_ist(y, m, d, hh, mm);
    return true;
}

int date_key(int64_t ms) {
    Civil c = to_ist(ms);
    return c.year * 10000 + c.month * 100 + c.day;
}

int64_t ist_midnight(int64_t ms) { return day_number(ms) * MS_DAY - IST_OFFSET_MS; }

}  // namespace sr
