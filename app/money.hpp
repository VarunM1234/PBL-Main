/*
 * PURPOSE: Money in int64 paise, plus the pure fare / deposit / refund / duration formulas.
 * WHY THIS NAME: all price rules live here so they can be unit-tested without any UI.
 * CONNECTS TO: booking, search, ticket. Integer arithmetic only: no floating point for money.
 * COMPLEXITY: O(1).
 */
#ifndef SMARTROUTE_MONEY_HPP
#define SMARTROUTE_MONEY_HPP
#include <cstdint>
#include <string>

namespace sr {

std::string fmt_rs(int64_t paise);                                    /* "Rs 1,344.60" (Indian grouping) */
int64_t seat_fare_paise(int km_nominal, int rate_paise_per_km, bool lower_berth);
int64_t percent_round(int64_t amount, int64_t pct);                   /* (amount*pct + 50) / 100 */
int64_t tax_paise(int64_t taxable);                                   /* 5% rounded */
int64_t deposit_paise(int64_t total);                                 /* 20% rounded */
int refund_percent(int64_t ms_before_departure);                      /* 100 / 80 / 50 / 0 */
int64_t refund_paise(int64_t paid, int64_t total, int64_t insurance, int64_t already_refunded, int pct);
int planned_drive_minutes(int km, int avg_kmh);                       /* round(km / avg * 60) */
constexpr int64_t INSURANCE_PER_PASSENGER = 1500;

}  // namespace sr
#endif
