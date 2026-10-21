/*
 * PURPOSE: Result<T>: a value or an error code with a human-readable message (no exceptions).
 * WHY THIS NAME: "result" of an operation that can fail.
 * CONNECTS TO: booking, auth, seats, vendor, cli (they print Result::message).
 * COMPLEXITY: O(1).
 */
#ifndef SMARTROUTE_RESULT_HPP
#define SMARTROUTE_RESULT_HPP
#include <string>

namespace sr {

enum class Err { NONE, VALIDATION, NOT_FOUND, CONFLICT, UNAUTHENTICATED, FORBIDDEN_ROLE, SEAT_TAKEN,
                 HOLD_EXPIRED, BAD_STATE, PAYMENT_DECLINED, NOT_ELIGIBLE, IO_ERROR };

struct Unit {};

template <class T>
struct Result {
    bool ok = false;
    Err code = Err::NONE;
    std::string message;
    T value{};
    static Result success(const T &v, const std::string &msg = "") { Result r; r.ok = true; r.value = v; r.message = msg; return r; }
    static Result fail(Err c, const std::string &msg) { Result r; r.code = c; r.message = msg; return r; }
};

inline Result<Unit> okay(const std::string &msg = "") { return Result<Unit>::success(Unit{}, msg); }

}  // namespace sr
#endif
