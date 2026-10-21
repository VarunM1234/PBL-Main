/*
 * PURPOSE: Tier 2 - tiny HTTP/JSON server that exposes the SAME App engine to the HTML web UI.
 * WHY THIS NAME: "http_server" = serves web pages and a JSON API over TCP.
 * CONNECTS TO: app.hpp (all logic stays in the engine), web/index.html (the HTML screens).
 * COMPLEXITY: O(request size) + engine cost. Single-threaded: one request at a time.
 */
#ifndef SMARTROUTE_HTTP_SERVER_HPP
#define SMARTROUTE_HTTP_SERVER_HPP
#include <string>
#include "app.hpp"

namespace sr {

/* Runs until the process is stopped. Returns a non-zero exit code if the port cannot be opened. */
int run_server(App &app, int port, const std::string &web_dir, bool autosave, bool open_browser = false);

}  // namespace sr
#endif
