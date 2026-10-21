# SmartRoute in C / C++ with hand-written Data Structures (+ HTML screens)

An Indian intercity bus booking, fleet and live-tracking **demo**, rebuilt from a TypeScript/React/Node project into
**C (data-structure library) + C++ (application) + HTML (screens)**. No Java, no Python, no Node, no database and
no third-party C/C++ library - the single exception is Leaflet 1.9.4 (BSD-2-Clause), inlined into the web page at
build time and nowhere else.

> **Everything is DEMO:** operators, buses, staff, phone numbers, fares, stops and tolls are sample data.
> Payments, traffic and GPS are simulated. No money is ever charged and no card number is ever asked for.

## What you can do

| As a passenger | As a vendor (separate login) |
|---|---|
| search 30 directed routes between 20 cities (city autosuggest) | see your buses, drivers, conductors and their derived status |
| filter and sort results | assign staff (off-duty / double-booked staff are refused) |
| pick seats on a seat map, find N adjacent seats | set bus status (maintenance hides the bus), **undo** last change |
| hold seats for 10 minutes, join a waitlist when sold out | auto-assign staff for tomorrow (greedy + min-heap, no overlaps) |
| pay in full or pre-book (20% now, balance 48 h before departure) | see manifests (phone numbers masked until 24 h before departure) |
| coupons (SMART50, EXPRESS10), optional insurance | report a delay, start / finish trips |
| text ticket with an ASCII route map, cancel with exact refund | demo revenue (Fenwick tree range query) |
| live-track a **simulated** bus: breaks, tolls, jam, ETA, alerts | |
| plan multi-leg journeys (Dijkstra / BFS) | |

Two front ends share **the same C++ engine** (no duplicated logic):
1. **Console app** (`bin/smartroute`) is the required core.
2. **HTML web UI** (`bin/smartroute --serve`) is a small C++ HTTP server plus `web/index.html`. The page's JavaScript only draws screens and calls `/api/...`; every price, seat, hold, payment, alert and vendor rule is computed in C++.

### The web UI follows the original PBL_1-main interface
`web/index.html` reuses the original project's stylesheet unchanged (colours, fonts, dark mode, nav bar, bell dropdown, search card, bus cards with the journey rail, seat layout with driver cab and deck switch, steps bar, fare breakdown, boarding-pass ticket card, tracker, ops tables, sign-in modal, landing sections). Tabs, as in the original: **Search**, **My Bookings (n)**, **Track Bus**, **Fleet Portal** (becomes **Company Console** for operators), **Driver**, **Trip Assistant** (the original's "AI Copilot"), plus a **Journey Planner** tab.

Features taken from the original and rebuilt in C++ (all logic in the engine, the page only draws):
- **Passenger:** search with coach-type filter, seat hold with countdown, group seats, pre-booking, coupons, simulated payment, boarding-pass ticket with a **real QR code** (encoded by `core/qr.c`), print / save as PDF, cancel with refund preview, **share a live trip link** (public read-only page that shows no passenger details; rotate or stop it any time), notifications bell with preferences.
- **Driver console:** duty list (only trips whose crew includes you), start / end trip, GPS fixes (range-checked, snapped to the route, stored in a trail), delay reports, **boarding check-in** by scanning the QR text or typing the booking reference (the pass then shows a BOARDED stamp), manifest.
- **Platform admin:** control-tower totals, today's fleet telemetry, revenue by corridor, **coupon controls** (issue, enable / disable), delay log, DEMO traffic scan.
- **Operator:** today's runs with per-trip crew assignment (a person cannot be on two overlapping runs), off-duty toggle (refused while booked on a run today), start / finish, delay reports, masked manifests, fleet and staff tables, undo.
- **Trip assistant:** rule-based (no AI service): understands city names (even partial), dates and words like cheapest, fastest, morning, sleeper; answers from the real search and journey planner.

**Not reproduced from the original** (they need Firebase, Socket.IO, Razorpay or an AI service): real payments (payment is a labelled demo gateway, no card number is ever asked for), live push over Socket.IO (the page re-reads every 5 seconds and on each action), the Gemini-powered copilot (the Trip Assistant is rule-based and says so on every screen it appears on), route-alternative picking, PWA / service worker, and real GPS (the driver's "device" is simulated or typed). What *is* there: booking with or without an account (a guest session and a public lookup by booking reference or the last 10 phone digits), per-stop arrival estimates, a **Hindi / English** label toggle on the tracker, saved passes in the browser, and the **real Leaflet road map** with **Auto-follow bus**, **Fit to route**, lettered stop pins, the bus marker at the position the engine reports, a blue dashed corridor and a red travelled trail. The corridor line follows **real roads**: the page asks a public OSRM server for the driving geometry through the route's stops (two servers, tried in order), simplifies it in the browser to roughly 500 points and caches it per trip, then places the bus and the pins along that road; if neither server answers, the dashed straight line between the stops is kept and the caption says so. Tiles are tried in the order **Esri, then CARTO, then OpenStreetMap**; a source that returns 4 errors, or stays silent for 6 seconds with nothing drawn, is abandoned for the next one. A source that answers *successfully* with rubbish is also abandoned: before a provider is believed, two busy tiles far apart (northern India and western Europe, z=7) are fetched and their pixels compared, because a blocked server can reply HTTP 200 with the same notice picture for every coordinate - measured from this machine, CARTO does exactly that with an "API KEY REQUIRED" tile, and OpenStreetMap does it to plain command-line requests with a "403 Access blocked" tile. Identical pixels at two places on Earth is not a map, so the chain moves on and the chip says which server answered with notices. The working provider is remembered in `localStorage` (separately for the street and aerial styles), and a chip on the map states what actually loaded - red "Map tiles unavailable" with a **Retry map** button when nothing did (Retry re-probes from scratch). Traffic colours are not reproduced: nothing here knows about live traffic. The original's Google Fonts are linked as before; offline the page falls back to system fonts.

**Third-party calls and licences.** Everything on this machine stays here (the engine, the server, the sessions, the save file) *except* two things the page does on your behalf, both only when the tracker is open and both labelled in the UI: (1) map **tiles** are fetched from Esri / CARTO / OpenStreetMap tile servers, which see the tile coordinates of the corridor being drawn plus your normal browser headers; (2) **road geometry** is fetched from a public OSRM routing server (`router.project-osrm.org`, falling back to `routing.openstreetmap.de`), which is sent the **latitude and longitude of that route's stops** - the only data that leaves the machine for the map, and no name, phone number or booking reference is ever included. **Leaflet 1.9.4** (BSD-2-Clause, `web/vendor/LEAFLET-LICENSE.txt`) is the project's only third-party library, and it is *inlined into the delivered page at build time* by `tools/page_gen.cpp`, so the page never downloads it from a CDN. Tile and routing use needs an internet connection. Without one the map does **not** go blank: the page draws the **offline map background** it gets from `/basemap.json` - a 2-degree graticule over the declared bounds, every published corridor, one labelled dot per city in the catalogue and a 100 km scale bar, all in a Leaflet pane at z-index 150, i.e. *under* the tile pane, so real tiles cover it as soon as they paint and the city labels fade out at that moment. That file is written by this program itself (`make basemap`, `--make-basemap` in `app/basemap.cpp`), and if it is missing from disk the server generates the identical document on the spot. Sea-blue paper and coastlines appear only when the file really contains land rings (supply one with `make basemap LAND=<file.geojson>`; the generator clips and simplifies it and refuses anything that is not GeoJSON). If even `/basemap.json` is unreachable, the map falls back to the schematic SVG route map drawn from the engine's own coordinates, and the caption says which of the three the reader is looking at.

## 1. Install the tools

**Windows (Git Bash / MSYS2)** *(built and run on Windows with MSYS2 UCRT64 gcc/g++ 16.1.0 - see "Honest limits" for exactly what was measured there)*
1. Install MSYS2 from https://www.msys2.org
2. Open the **MSYS2 UCRT64** shell and run: `pacman -S mingw-w64-ucrt-x86_64-gcc make`
3. Check: `make check-tools` (should print gcc, g++ and make versions).

**Linux:** `sudo apt install build-essential` (Debian/Ubuntu) or your distro's equivalent. **macOS:** `xcode-select --install`.

## 2. Build, run, test

**Windows quick start**: the same two commands you know from Node projects work, from the folder that contains `Makefile`:

```powershell
npm install              # once; installs nothing - it only prints the next command
npm run dev              # build + start + open the browser (default port 8080)
npm run dev -- 8081      # another port; also npm run build / npm test / npm start
```

The app itself stays pure C/C++ with zero npm dependencies: `package.json` is only a launcher that forwards to the batch files below. The direct equivalents, which do build + start + open the browser in one step:

```powershell
.\run-web.cmd            # double-click it, or run it in PowerShell. Default port 8080.
.\run-web.cmd 8081       # another port
make serve               # if you already have the MSYS2 UCRT64 shell open
```

`run-web.cmd` is a plain Windows batch file: it puts `C:\msys64\ucrt64\bin` first on `PATH` for that window only, runs `make all`, then `bin\smartroute.exe --serve <port> --open`. Keep the window open while you use the page; close it (or Ctrl+C) to stop. It needs the C/C++ tools from section 1 and nothing else. If you would rather do it by hand in PowerShell:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:Path"   # so make/g++ are found
make all                                                          # -> bin\smartroute.exe
.\bin\smartroute.exe --serve 8080 --open                           # or --demo, or no flag for the console app
```

**Linux / macOS / Git Bash:**

```bash
make                 # build bin/smartroute
make run             # interactive console app
make demo            # scripted, reproducible end-to-end session (fixed clock)
make serve           # builds, starts the web server and opens http://localhost:8080 in your browser (Ctrl+C to stop)
./run-web.sh         # same thing as one command (Git Bash / Linux / macOS); ./run-web.sh 8081 uses another port
make test            # all unit tests (exit code is non-zero if anything fails)
make test-ops        # the staff flow over real HTTP: driver + admin + booking + QR check-in (port 3199)
make bench           # benchmarks of my data structures
make basemap         # rewrite web/basemap.json from the catalogue (add LAND=ne_110m_land.geojson for coastlines)
make debug           # the same tests in build-debug/ + bin-debug/, with AddressSanitizer + UBSan where the
                     # compiler has those runtimes; MSYS2/MinGW GCC ships neither libasan nor libubsan, so
                     # there it falls back to -O0 debug info + libstdc++ container assertions + stack protector
                     # and says which mode it used. Also builds the sanitizer/fallback server binary.
make clean
```

The current green run on this machine (MSYS2 UCRT64 g++ 16.1.0, `make test` then `make test-ops`): 548 core checks,
200,114 second-round core checks, **1,180 application checks** - 645 of them the offline-map-background section,
600 of those feeding the generator randomly corrupted GeoJSON - and 72 over-HTTP checks, 0 failures in every binary.
Options: `--now "2026-10-19 10:00"` (set the virtual clock, IST), `--sync-clock`, `--no-save`, `--no-color`, `--data DIR`, `--serve PORT`, `--help`.

### The one-file build (`main.cpp`)

`main.cpp` is the **whole project in one file**: the C data-structure core, the C++ engine (including `ops.cpp`,
`assistant.cpp` and the QR encoder), the HTTP server and the web page as a byte array. It is generated, never
hand-edited:

```bash
make onefile                         # builds the generator, regenerates main.cpp, compiles it with one g++ call
# the generator is a C++ program of this project (tools/onefile.cpp), so by hand it is:
g++ -std=c++17 -O2 tools/onefile.cpp -o bin/onefile_gen.exe     # the combining tool itself
./bin/onefile_gen.exe .              # rewrites main.cpp from core/, app/ and web/index.html
g++ -std=c++17 -O2 main.cpp -o bin/smartroute_onefile.exe -lm -lws2_32   # Windows; drop -lws2_32 on Linux/macOS
./bin/smartroute_onefile.exe --demo --no-save
./bin/smartroute_onefile.exe --serve 8080 --open                          # needs the data/ folder next to it
```

`make run1 / demo1 / serve1` are the one-file twins of `run / demo / serve`. Inside `main.cpp` the former `.c`
files are compiled as C++, so the malloc returns carry explicit pointer casts; the logic is byte-for-byte the
same engine the split build uses. `make` also copies the UCRT64 runtime DLLs next to the executables in `bin/`,
so they do not pick up an older MinGW's DLLs from `PATH`.

`make test-ops` runs `tests/test_ops.cpp`: it starts the real HTTP server on a private test port (3199), then
drives the whole staff flow over sockets - book, pay, QR ticket, driver start / GPS fix / delay / manifest /
boarding check-in / end, then the admin KPIs, telemetry, coupons and traffic scan - and prints PASS/FAIL lines
like the other test binaries. It never touches `data/save`, so it is safe to run while your own server runs on
8080. (Run `netstat -ano | findstr :3199` after it if you want proof nothing is left listening.)

**Demo logins** (every password is `Demo@1234`). Each account type has its own sign-in; using one on the wrong screen just says "Invalid credentials":

| Account | Email | What it opens |
|---|---|---|
| Passenger | `user@smartroute.in` or `pilot@smartroute.in` | search, book, My Bookings, Track Bus, share links |
| Driver | `driver@smartroute.in` (Ravi Kumar, Orbit Express, bus DEMO-001) | Driver console: only trips he is assigned to |
| Platform admin | `admin@smartroute.in` | Fleet Portal: all operators, revenue by corridor, coupons, delay log |
| Operator (vendor) | `vendor@smartroute.in` (Orbit Express) and `vendor.<company-slug>@smartroute.in` for the other 13 | Company Console: only that company's fleet, runs and crew |

In the console app press **T** at the main menu for the **time machine** (advance the virtual clock by `30m`, `2h`, `3d`) to watch holds expire, reminders arrive and refunds change. The web UI has the same buttons at the top. In the web server the virtual clock also follows real time in the background, so a 10-minute seat hold expires on its own even if nobody presses a button (the time machine still works on top of that - it only moves the clock forward).

## 3. Words you will meet

- **struct / pointer**: a bundle of fields / the address of something in memory.
- **paise**: money is stored as whole paise (Rs 1 = 100 paise) in `int64_t`, never floating point.
- **IST**: India Standard Time, UTC+5:30. All times come from a *virtual* `Clock`, so tests and demos are repeatable.
- **Big-O**: how the work grows with the input size. Every data structure's header states its Big-O; `docs/DSA_REPORT.md` explains it.
- **heap / hash / trie / bitset**: see the report. Each is written by hand in `core/` and used by the app.

## 4. File table (file -> what it does -> why it is named that)

| File | What it does | Why this name |
|---|---|---|
| `core/ds_common` | status codes, checked allocation | "common" bits shared by every structure |
| `core/ds_vector` | growable array | like C++ `vector` |
| `core/ds_list` | doubly linked list | list with prev/next |
| `core/ds_queue` | circular-array FIFO queue (waitlist, notification outbox) | queue = first in, first out |
| `core/ds_stack` | array stack (vendor undo, Douglas-Peucker) | stack = last in, first out |
| `core/ds_hash` | hash table, chaining, FNV-1a, resize at 0.75 | hash = key to bucket |
| `core/ds_heap` | min-heap with id index (hold expiry, Dijkstra, auto-assign) | heap = priority queue |
| `core/ds_trie` | prefix tree for city autosuggest | "re-TRIE-val" tree |
| `core/ds_bitset` | seat availability bits, popcount, k-adjacent run search | a set stored as bits |
| `core/ds_fenwick` | Fenwick tree: point update, range sum (vendor revenue) | named after Peter Fenwick |
| `core/ds_ring` | ring buffer with running sum (average speed, last 10 min) | circular buffer |
| `core/ds_lru` | LRU cache = hash + doubly linked list (bus position memo) | Least Recently Used |
| `core/ds_graph` | graph, Dijkstra, BFS (journey planner) | cities = vertices, routes = edges |
| `core/ds_unionfind` | disjoint sets (bus networks) | union + find |
| `core/ds_sort` | merge (stable), quick, heap sort | sorting algorithms |
| `core/ds_search` | lower/upper bound, KMP substring search | searching algorithms |
| `core/geo` | Haversine, position on a polyline, Douglas-Peucker | geographic maths |
| `core/sha256` | SHA-256 + salted iterated helper (educational) | the algorithm's name |
| `app/dswrap.hpp` | C++ RAII wrappers around the C structures | "data-structure wrappers" |
| `app/clock` | virtual clock, IST date maths | clock |
| `app/money` | fares, tax, deposit, refund formulas (pure functions) | money rules |
| `app/result.hpp` | `Result<T>`: value or error + message | result of an operation |
| `app/csv` | hand-written CSV reader with line-numbered errors | CSV |
| `app/models.hpp` | plain records (City, Route, Bus, Booking, Trip, ...) | data models |
| `app/app.hpp` | the engine class `App` (declares every service) | the application |
| `app/catalog` | loads `data/*.csv`, builds routes/stops/trie/indexes/graph | reference catalogue |
| `app/auth` | accounts, salted hashing, lockout, roles | authentication |
| `app/seats` | trips, seat layouts, holds (heap), waitlist (queue) | seats |
| `app/search` | search, filters, sorting, journey planner | search |
| `app/booking`, `coupons`, `payment` | quote, create, status machine, cancel/refund, coupons, simulated gateway | the booking life cycle |
| `app/tracking` | simulator, ETA, crossing-based alerts | tracking |
| `app/notifications` | outbox queue, bell list, preferences, dedupe | notifications |
| `app/vendor` | staff status, assignment, undo, trips, auto-assign | operator side |
| `app/storage` | save/load with checksums (replaces SQLite) | storage |
| `app/ticket`, `asciimap` | boxed ticket, ASCII map, GeoJSON export | ticket / map |
| `app/basemap` | the offline map background: bounds, 2-degree graticule, corridor lines, city dots, scale bar, optional land GeoJSON clipping (Sutherland-Hodgman) + a self-check of the file it wrote | "base map" (what the map shows when no tile server answers) |
| `app/ops` | driver console, check-in, per-trip crew, off-duty rules, coupons, share links, admin figures | "operations" (the original's opsRoutes) |
| `app/assistant` | the rule-based trip assistant | the chat helper |
| `core/qr` | QR code encoder (Reed-Solomon over GF(256)) | "Quick Response" code |
| `app/cli`, `main` | console screens + `--demo`; program entry | command-line interface |
| `app/http_server` | tiny HTTP/JSON server for the web UI | HTTP server |
| `web/index.html` | the HTML screens (also compiled into the program, so the server works from any folder) | the page a web server shows first |
| `web/basemap.json` | the offline map background the page draws under the map tiles; served at `/basemap.json`, and regenerated by `make basemap` (the server can also build the same document on the spot) | base map data |
| `run-web.sh`, `run-web.cmd` | build + start the web server + open the browser (Git Bash / Linux / macOS, and Windows) | "run the web UI" |
| `data/*.csv` | cities, points, routes, bus types, companies, buses, staff, schedules, coupons, seed users | catalogue data |
| `tests/` | `test.h` (assert macros), `test_core.c`, `test_core2.c`, `test_app.cpp`, `bench.cpp` | tests and benchmark |
| `docs/DSA_REPORT.md` | complexity, diagrams, worked examples, viva questions | the DSA defence document |
| `docs/PROJECT_PROMPT.md` | the assignment brief this build follows: constraints, data structures, engine rules, API, screen-by-screen UI spec, honest limits | the project spec |

## 5. How one booking travels through the code

1. **Search** (`search.cpp`): city names -> trie/hash; route -> schedules -> trips created lazily; results pre-sorted by departure (merge sort), time window by binary search.
2. **Hold** (`seats.cpp`): chosen seats become bits in the trip's *held* bitset; each hold is a heap event that fires in 10 minutes.
3. **Quote** (`booking.cpp` + `money.cpp`): integer fare formulas, coupon, 5% tax, insurance, deposit.
4. **Booking** (`create_booking`): checks the seats are *your* live holds; status `PENDING_PAYMENT`.
5. **Payment** (`payment.cpp`): simulated gateway; idempotency key `b<id>:k<kind>:a<attempt>` stops a double-submit; holds become sold seats; status `CONFIRMED` or `PREBOOKED`; balance deadline and reminders go on the heap.
6. **Ticket** (`ticket.cpp`): boxed text + ASCII map. **Cancel**: refund preview, status machine, seats freed, waitlist notified.

## 6. Change the data by editing CSV

Add a **city**: new row in `cities.csv` (ids must run 1,2,3,...) and a row in `points.csv`. Add a **route**: row in `routes.csv` (the reverse route is created automatically). Add a **bus**: row in `buses.csv`, then **two rows** in `schedules.csv` (forward and reverse; reverse slug is `to-from`). Staff go in `staff.csv`. Errors are reported with file name and line number.

## 7. Troubleshooting

- **`g++: command not found` / `make: command not found`**: install the tools (section 1); on Windows use the *UCRT64* shell, not the plain MSYS shell.
- **`Could not load the data files: data/routes.csv: line 7: ...`**: open that file at that line; the message says what is wrong (wrong column count, unknown city, bad number).
- **`Save-file notice: bookings.sav: checksum mismatch`**: the file was edited or damaged; the program recovers from `bookings.sav.bak` automatically, or starts that section empty. To start fresh delete `data/save/*`.
- **The page says "SmartRoute's server is not running"** (or nothing opens): the page only draws screens; the C++ server must be running. Run `make serve` (or `./run-web.sh`) from the project folder, leave that window open, and open `http://localhost:8080`. You may also double-click `web/index.html` while the server runs on port 8080: it finds the server by itself and shows a clear screen with these steps plus a Retry button when it cannot. If the window says "Could not listen on port 8080", another program uses the port: run `bin/smartroute --serve 8081` and open that port. The server answers one request at a time but never waits on idle browser connections (they are dropped after 15 s). On Windows, allow the firewall prompt if one appears (it only listens on localhost).
- **Account locked**: 5 wrong passwords lock an account until the save file is reset (`delete data/save/users.sav`).

## 8. Assumptions (choices I made where the brief was silent)

- Bus **cruise speed in the simulator = the route's average km/h** (not a flat 55), so an un-jammed trip arrives exactly on schedule and any delay comes from the jam or a vendor report.
- The booking **clock is virtual**: it starts at the real time (read once in `main`), or at `--now`, or at the saved clock; saved clocks continue across restarts.
- Trips are created lazily on first search; search accepts dates 0 to 90 days ahead.
- A coupon redemption is recorded at the first successful payment and is **not** restored on cancellation.
- Boarding points sit at km 1 and 3, dropping points at km total-3 and total-1.
- Alert times are the *planned* clock time at which the bus reaches that km.
- Request bodies of the web API are form-encoded; responses are JSON written by a small hand-written writer.

## 9. Honest limits

- **Demo data and simulations**: no real operators, payments, traffic or GPS; the ASCII/SVG map is a straight-line schematic, not a road map.
- **Single-threaded, one-user server**: the HTTP server handles one request at a time, has no TLS, and listens on localhost only.
- **Educational security**: password hashing is salted iterated SHA-256 (real systems use bcrypt/argon2); PNRs and salts come from a seeded pseudo-random generator, so they are predictable.
- **Built and run on Windows** with MSYS2 UCRT64 gcc/g++ 16.1.0: `make all`, `make test`, `make test-ops`,
  `make debug`, `make onefile`, `make basemap`, the console demo and the web server were all executed here.
  The same sources are what a Linux/macOS build compiles; on those systems `make debug` additionally gets
  AddressSanitizer + UBSan (see the next bullet).
- **Sanitizers on this machine**: MSYS2 UCRT64 GCC ships neither `libasan` nor `libubsan` (and clang's compiler-rt
  is not installed), so `-fsanitize=address,undefined` compiles but cannot link, and WSL is not installed either.
  `make debug` probes for the runtimes and, when they are missing, says so and uses the strict fallback
  (`-O0 -g -D_GLIBCXX_ASSERTIONS -fstack-protector-strong`). Under that build the whole suite is green
  (548 + 200,114 + 1,180 checks, 0 failures) including the 600 randomly corrupted GeoJSON inputs - re-run
  after the tile-probe change, where `make debug` exits 0 and the basemap test prints its own
  `offline map background: 645 checks, 600 of them randomly corrupted inputs`. The real
  ASan + UBSan pass still has to be run on Linux/macOS or a clang toolchain.
- **Browser-checked UI**: the page was walked in a real browser against `bin/smartroute.exe --serve` - guest
  session, search, seat hold, quote, pay, ticket, sign out, the Track Bus run chips, the live map and the offline
  map background: pane `sr-basemap` computed z-index 150 under the tile pane's 200, 67 shapes drawn from the
  served `/basemap.json`, city labels fading to opacity 0 the moment tiles paint and back to 1 when they stop,
  `bm-sea` + land polygons only when the file really carries land rings, and the honest captions when
  `/basemap.json` is unreachable. Those checks read the DOM and computed styles; no screenshots were taken.
- **The map was checked against the real internet, not stubs**: Esri's World_Topo_Map tile answered `200` with
  19,770 bytes (and a different picture for a different tile), Esri World_Imagery likewise, OSRM's public
  routing server answered `code: Ok` with 284,891 m of road geometry for Delhi-Jaipur (3,365 points, and the
  second server returns the identical geometry), 9 of 9 tiles painted, the chip read "Esri map check · road
  check", and the caption credited only what was actually drawing. CARTO answered `200` for every coordinate
  with one identical 2,049-byte "API KEY REQUIRED" notice, which is why the page compares two tiles before it
  believes a provider; `tile.openstreetmap.org` served real tiles to the browser but returned its identical
  "403 Access blocked / osm.wiki:Blocked" notice to a command-line request from the same machine.
- **Not verified anywhere**: browser notifications / sound / vibration, camera QR scanning (`BarcodeDetector`
  is absent in Chrome on Windows desktop), real device geolocation beyond the browser prompt, real phones, and
  the ASan + UBSan pass described above.
- **Not implemented**: ticket "back navigation" with the stack, inserting a break stop into a route's linked list (the list is built but the tracker walks the array), the segment-tree / DP stretch goals, stop-name search, caching of ETA and traffic (only bus position is cached), and the console (terminal) app does not have the driver / admin screens (they are in the web UI). See the list under "Not reproduced from the original" above.
- Playing the simulator in the console runs 30 ticks instantly (no sleeping).
# PBL-Main
