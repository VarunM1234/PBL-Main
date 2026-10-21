/*
 * PURPOSE: Coupon validation and discount calculation (one coupon per booking).
 * WHY THIS NAME: "coupons" = promo codes.
 * CONNECTS TO: idx_coupon (hash lookup by code, row 2), money.hpp, booking.cpp.
 * COMPLEXITY: O(1) lookup + O(uses) to count a user's earlier redemptions.
 */
#include "app.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace sr {

static std::string upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

/* True when `code` appears in the ';'-separated allowed list (or the list is ALL). */
static bool type_allowed(const char *allowed, const char *code) {
    if (std::strcmp(allowed, "ALL") == 0) return true;
    for (const std::string &item : split_fields(allowed, ';')) if (item == code) return true;
    return false;
}

Result<int64_t> App::coupon_discount(const char *code, int64_t base, const BusType &bt, int user_id) const {
    int *ci = idx_coupon.get(upper(code));
    if (!ci) return Result<int64_t>::fail(Err::VALIDATION, "Unknown coupon code");
    const Coupon &c = coupons[(size_t)*ci];
    if (!c.active) return Result<int64_t>::fail(Err::VALIDATION, "This coupon is not active");
    if (clock.now() > (base_day + c.valid_days) * MS_DAY) return Result<int64_t>::fail(Err::VALIDATION, "This coupon has expired");
    if (base < c.min_base) return Result<int64_t>::fail(Err::VALIDATION, "Coupon needs a minimum fare of " + fmt_rs(c.min_base));
    if (!type_allowed(c.allowed, bt.code)) return Result<int64_t>::fail(Err::VALIDATION, "Coupon is not valid for this bus type");
    int total_uses = 0, user_uses = 0;
    for (size_t i = 0; i < coupon_uses.size(); i++) {
        if (coupon_uses[i].coupon_idx != *ci) continue;
        total_uses++;
        if (coupon_uses[i].user_id == user_id) user_uses++;
    }
    if (total_uses >= c.max_uses) return Result<int64_t>::fail(Err::VALIDATION, "This coupon is fully redeemed");
    if (user_uses >= c.per_user_limit) return Result<int64_t>::fail(Err::VALIDATION, "You have already used this coupon the maximum number of times");
    int64_t discount = c.is_percent ? percent_round(base, c.value) : c.value;
    if (c.is_percent && c.max_discount > 0) discount = std::min(discount, c.max_discount);
    return Result<int64_t>::success(std::min(discount, base));        /* never more than the base fare */
}

}  // namespace sr
