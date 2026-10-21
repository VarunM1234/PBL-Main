/*
 * PURPOSE: Renders the boxed text ticket (with the ASCII route map) and small text summaries.
 * WHY THIS NAME: "ticket" = what the passenger shows at boarding.
 * CONNECTS TO: asciimap.hpp, app.hpp (booking, trip), cli.cpp, http_server.cpp.
 * COMPLEXITY: O(stops + seats).
 */
#ifndef SMARTROUTE_TICKET_HPP
#define SMARTROUTE_TICKET_HPP
#include <string>
#include "app.hpp"

namespace sr {

const char *booking_status_name(int s);
const char *pay_status_name(int s);
const char *bus_status_name(int s);
const char *staff_status_name(int s);
std::string cancellation_table();
std::string render_ticket(const App &app, const Booking &b, bool color);
std::string share_summary(const App &app, const Booking &b);        /* no personal data */
bool driver_phones_visible(const App &app, const Booking &b);       /* privacy rule 9.11 */

}  // namespace sr
#endif
