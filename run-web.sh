#!/usr/bin/env bash
# PURPOSE: One command to build (if needed), start the SmartRoute web server and open the browser.
# WHY THIS NAME: "run-web" starts the web UI.  Use in Git Bash / MSYS2 UCRT64 / Linux / macOS:  ./run-web.sh [PORT]
# CONNECTS TO: Makefile (build), bin/smartroute --serve --open
set -e
cd "$(dirname "$0")"
PORT="${1:-8080}"
if ! command -v g++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
  echo "g++ or make not found. Install them first (see README section 1), then run this again."; exit 1
fi
if ! make all; then
  # The link fails with "Permission denied" while another SmartRoute window runs from the same exe.
  # Do not stop the dev flow then - fall back to the existing build.
  [ -x bin/smartroute ] || { echo "Build failed and no previous bin/smartroute exists. Copy the messages above and send them to me."; exit 1; }
  echo
  echo "NOTE: could not rebuild bin/smartroute - another SmartRoute window is using it. Starting the existing build."
fi
echo
echo "Starting SmartRoute on http://localhost:${PORT} - keep this window open, close it (or press Ctrl+C) to stop."
exec ./bin/smartroute --serve "${PORT}" --open
