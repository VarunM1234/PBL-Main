/*
 * PURPOSE: Draws a schematic ASCII route map (78x18) and exports the route as GeoJSON.
 * WHY THIS NAME: "asciimap" = a map made of plain characters.
 * CONNECTS TO: core/geo (Douglas-Peucker simplify, row 17), ticket.cpp, cli.cpp, http_server.cpp.
 * COMPLEXITY: O(n log n) simplify + O(cells) drawing.
 */
#ifndef SMARTROUTE_ASCIIMAP_HPP
#define SMARTROUTE_ASCIIMAP_HPP
#include <string>
#include "app.hpp"

namespace sr {

/* bus_km < 0 hides the bus marker. Markers: G start/end, B boarding/dropping, T toll, P break, @ bus. */
std::string render_route_map(const App &app, const Route &route, double bus_km, bool color);
std::string route_geojson(const App &app, const Route &route);
bool export_geojson(const App &app, const Route &route, const std::string &path);
std::string journey_bar(const Route &route, double bus_km, int width = 40);   /* [S====P====T====E] */

}  // namespace sr
#endif
