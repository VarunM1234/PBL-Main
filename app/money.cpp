/*
 * PURPOSE: Implements the fare and refund formulas (see money.hpp).
 * WHY THIS NAME: implements money.hpp.
 * CONNECTS TO: clock.hpp (MS_HOUR).
 * COMPLEXITY: O(1).
 */
#include "money.hpp"
#include "clock.hpp"
#include <algorithm>

namespace sr {

/* Indian digit grouping: 1,23,456. */
static std::string group_indian(int64_t rupees) {
    std::string s = std::to_string(rupees);
    if (s.size() <= 3) return s;
    std::string last3 = s.substr(s.size() - 3), rest = s.substr(0, s.size() - 3), out;
    while (rest.size() > 2) { out = "," + rest.substr(rest.size() - 2) + out; rest.resize(rest.size() - 2); }
    return rest + out + "," + last3;
}

std::string fmt_rs(int64_t paise) {
    bool neg = paise < 0;
    int64_t a = neg ? -paise : paise;
    char frac[8];
    std::snprintf(frac, sizeof frac, ".%02d", (int)(a % 100));
    return std::string(neg ? "-" : "") + "Rs " + group_indian(a / 100) + frac;
}

int64_t seat_fare_paise(int km, int rate, bool lower_berth) {
    int64_t units = ((int64_t)km * rate + 500) / 1000;            /* round(km*rate/1000) */
    int64_t rupees = units * 10;                                  /* rounded to nearest Rs 10 */
    if (lower_berth) rupees = (rupees * 110 + 50) / 100;          /* lower berth x1.10 */
    return rupees * 100;
}

int64_t percent_round(int64_t amount, int64_t pct) { return (amount * pct + 50) / 100; }
int64_t tax_paise(int64_t taxable) { return percent_round(taxable, 5); }
int64_t deposit_paise(int64_t total) { return percent_round(total, 20); }

int refund_percent(int64_t ms) {
    if (ms >= 72 * MS_HOUR) return 100;
    if (ms >= 24 * MS_HOUR) return 80;
    if (ms >= 6 * MS_HOUR) return 50;
    return 0;
}

int64_t refund_paise(int64_t paid, int64_t total, int64_t insurance, int64_t already, int pct) {
    int64_t refundable = std::min(paid, total - insurance);        /* insurance is never refunded */
    if (refundable < 0) refundable = 0;
    int64_t amount = refundable * pct / 100 - already;
    return amount < 0 ? 0 : amount;
}

int planned_drive_minutes(int km, int avg) { return (km * 60 + avg / 2) / avg; }

}  // namespace sr
