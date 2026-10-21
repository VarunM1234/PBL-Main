/*
 * PURPOSE: Interactive console screens and the scripted --demo run.
 * WHY THIS NAME: "cli" = command-line interface.
 * CONNECTS TO: app.hpp (all services), ticket.hpp, asciimap.hpp, main.cpp.
 * COMPLEXITY: n/a (user interface).
 */
#ifndef SMARTROUTE_CLI_HPP
#define SMARTROUTE_CLI_HPP
#include "app.hpp"

namespace sr {

struct CliOptions { bool color = false; bool autosave = true; };

int run_cli(App &app, const CliOptions &opt);     /* interactive menus; returns process exit code */
int run_demo(App &app, const CliOptions &opt);    /* scripted, reproducible session (no prompts) */

}  // namespace sr
#endif
