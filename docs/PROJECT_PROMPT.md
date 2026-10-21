# SmartRoute (C / C++ / DSA + HTML): the complete project prompt

This is a detailed prompt (a specification plus working rules) that describes the project **as it now exists**, including every
change made while converting it from the original React / TypeScript / Express / SQLite project (`PBL_1-main`).
Give it to an AI assistant (or a teammate) to **rebuild** the project, **continue** it, **review** it, or **prepare a viva**.
Everything stated as a number or behaviour below was measured or tested in this repository; the last sections list what was *not* verified.

---

## 0. The short version (paste this if you need only one paragraph)

> Build and maintain **SmartRoute**, an Indian intercity bus booking, fleet and live-tracking **demo**, as **C (data-structure library) + C++ (application) + one HTML page**.
> No Java, Python, Node, database or third-party library in the C/C++ code (the one exception: Leaflet 1.9.4 inlined in the HTML for the map).
> The C++ engine owns every rule (seats, holds, fares, payments, refunds, coupons, tracking, alerts, crew, assistant, QR); a hand-written HTTP server exposes it
> as JSON; `web/index.html` only draws screens. 23 data structures are implemented by hand and used for real work. Everything is labelled DEMO: payments, traffic
> and GPS are simulated. The web screens must match the original app's screens (search with filters, ticket with crew, tracker with a map and stop timeline,
> driver and admin consoles). Money is `int64` paise, time is a virtual IST clock. Prove behaviour with tests (unit, sanitizer, real-browser) and report limits honestly.

---

## 1. Role, goal and audience

**Role of the assistant:** a careful senior C/C++ engineer and teacher working with a student at Graphic Era (PBL / capstone context).

**Goal:** a working, explainable project where *the data structures are the engine*, not decoration. The student must be able to explain, in a viva, why each
structure was chosen, its complexity, and how it failed or was tested.

**Working style required of the assistant**
1. Explain the reasoning and the trade-offs, not only the code. Name assumptions.
2. Find root causes; never paper over a failure. Reproduce, fix, and add a test that would have caught it.
3. Never invent results, test counts, citations or screenshots. Run the thing, then report what happened, including failures.
4. Say plainly what was **not** verified (see section 12).
5. Prefer maintainable approaches. Challenge weak ideas politely (for example "AI" is not a feature by itself; the assistant here is rule-based and says so).
6. Do not ask the user to share secrets; demo passwords are public demo data only.

---

## 2. Hard constraints

| Rule | Detail |
|---|---|
| Languages | C17 for `core/` (the data-structure library), C++17 for `app/` (application, HTTP server), HTML + CSS + JavaScript for `web/index.html` (draws screens only). |
| Forbidden | Java, Python, Node, any database (SQLite etc.), any third-party library in C/C++. No `npm` packages. |
| Only exception | **Leaflet 1.9.4** (BSD-2-Clause) is inlined in `web/index.html` for the tracker map (licence kept in `web/vendor/LEAFLET-LICENSE.txt`). Deleting it must leave a working page (an SVG map is drawn instead). A tiny `package.json` exists *only* as a launcher shortcut (`npm run dev` -> `run-web.cmd`); it is optional and deletable. |
| Standard library | Only C / C++ standard library. The HTTP server uses plain sockets (Winsock on Windows, POSIX elsewhere). |
| Money | `int64_t` **paise** everywhere. Never floating point for money. Page shows rupees with two decimals. |
| Time | A virtual `Clock` in IST (UTC+5:30). The real system clock is read **once**, in `main`. Tests and demos are repeatable with `--now "YYYY-MM-DD HH:MM"`. |
| DEMO labelling | Every operator, bus, person, phone, fare, toll and stop is sample data. Payments are a DEMO gateway: no card number is ever asked for, no money moves, tickets say "DEMO PAYMENT". Traffic and GPS are simulated and labelled. |
| Compile flags | `-Wall -Wextra -Wpedantic -Werror` (C17 / C++17). Warnings are build failures. |
| Ownership | The C++ engine decides everything; the page never computes a price, seat state, refund or permission. |
| Sessions | In memory only; single-threaded server; localhost only; no TLS (stated as a limit). |
| Passwords | Salted, iterated SHA-256 (10,000 rounds), **educational only** (real systems: bcrypt / argon2). |

---

## 3. Architecture and repository layout

Two front ends share the same engine: the **console app** (`bin/smartroute`) and the **web UI** (`bin/smartroute --serve`).

```
smartroute/
  Makefile  README.md  .gitignore  package.json  run-web.cmd  run-web.sh
  docs/   DSA_REPORT.md  PROJECT_PROMPT.md (this file)
  data/   cities.csv points.csv routes.csv bus_types.csv companies.csv buses.csv staff.csv
          schedules.csv coupons.csv users_seed.csv      (sample data, edit-able)
  data/save/                                             (runtime saves, git-ignored)
  core/   19 data-structure modules (each .h + .c): ds_common ds_vector ds_list ds_queue ds_stack ds_hash
          ds_heap ds_trie ds_bitset ds_fenwick ds_ring ds_lru ds_graph ds_unionfind ds_sort ds_search
          geo sha256 qr
  app/    dswrap.hpp clock money result models csv catalog auth search seats booking coupons payment
          tracking notifications vendor storage ticket asciimap cli ops assistant basemap http_server main
  tests/  test.h test_core.c test_core2.c test_app.cpp bench.cpp
  web/    index.html (the whole UI, Leaflet inlined)   basemap.json (offline map background)   vendor/LEAFLET-LICENSE.txt
```

* `core/` knows nothing about buses. `app/` wraps the C structures in small C++ templates (`dswrap.hpp`) and never re-implements them.
* The Makefile turns `web/index.html` into a C++ byte array (`od` + `sed`, no Python) so the binary serves the page from any folder.
* The HTTP server is a single-threaded `select()` event loop: IPv4 + IPv6 loopback listeners, up to 40 concurrent clients, incremental request parsing,
  15 s idle drop, 16 KB header / 64 KB body limits, CORS for the page itself and `Origin: null`.
* Persistence: versioned, FNV-1a checksummed, written atomically (temp file then rename), with `.bak` fallback on corruption. Sections include users,
  trips, holds, bookings, notifications, coupons, shares, delays, gps, crew. Older save files are upgraded on load (new demo accounts are added).

---

## 4. Data, accounts and golden numbers

**Catalogue:** 20 cities, 15 corridors (30 directed routes), 14 companies, 34 buses, 84 staff (drivers and conductors), 68 daily schedules,
5 bus networks, 2 coupons (`SMART50`, `EXPRESS10`).
Bus networks: {Delhi, Jaipur, Dehradun, Chandigarh, Agra, Amritsar, Shimla, Udaipur, Ahmedabad} {Mumbai, Pune, Goa} {Bengaluru, Chennai, Mysuru, Madurai} {Hyderabad, Vijayawada} {Kolkata, Digha}.
"Delhi to Goa" must answer *No route* (different networks).

**Demo accounts** (all passwords `Demo@1234`):
`user@smartroute.in`, `pilot@smartroute.in` (passengers) · `driver@smartroute.in` (Ravi Kumar, bus DEMO-001, Orbit Express) ·
`admin@smartroute.in` (platform admin) · `vendor@smartroute.in` (Orbit Express) and `vendor.<company-slug>@smartroute.in` for the other 13 companies.

**Golden test vectors (must always hold):**
* Delhi to Jaipur, AC Sleeper seats L1 + U1, coupon `SMART50`, insurance: base 130200, discount 5000, tax 6260, insurance 3000, **total 134460**, deposit 26892, balance 107568 (paise).
* Planned durations (minutes): delhi-jaipur 376, delhi-dehradun 377, delhi-agra 250, mumbai-pune 184, kolkata-digha 282, bengaluru-mysuru 173, bengaluru-chennai 411, chennai-madurai 542.
* Journey planner: Delhi to Ahmedabad = 3 legs (via Jaipur, Udaipur), 1299 min; Mysuru to Madurai = 3 legs, 1246 min.
* Same-day jam on Delhi to Jaipur adds exactly 14 minutes at the destination.

---

## 5. The 23 data structures (each hand-written, each used for real work, each tested)

| # | Structure (module) | Used for | Why it fits |
|---|---|---|---|
| 1 | Trie (`ds_trie`) | city auto-suggest, partial city names in the assistant | prefix search in O(length) |
| 2 | Hash table, FNV-1a, chaining, resize at load 0.75 (`ds_hash`) | PNR, email, coupon, route lookups; session tokens | O(1) average |
| 3 | Bitset with popcount and k-run (`ds_bitset`) | seat availability, "find N seats together" | 1 bit per seat, fast counting |
| 4 | Min-heap with id index (`ds_heap`) | hold expiry, Dijkstra, crew auto-assign | O(log n) decrease / remove by id |
| 5 | Circular queue (`ds_queue`) | waitlist, notification outbox | O(1) FIFO without shifting |
| 6 | Stack (`ds_stack`) | vendor undo, Douglas-Peucker | LIFO |
| 7 | Doubly linked list (`ds_list`) | route stops, LRU recency | O(1) splice |
| 8 | LRU cache (`ds_lru`) | memoised bus position | hash + list |
| 9 | Graph, Dijkstra, BFS (`ds_graph`) | journey planner | shortest time / fewest hops |
| 10 | Union-Find (`ds_unionfind`) | bus-network membership ("different networks") | near-O(1) connectivity |
| 11 | Merge / quick / heap sort (`ds_sort`) | result sorting (stable merge sort for ties) | compare 3 algorithms |
| 12 | Binary search (`ds_search`) | time-window and fare filters | O(log n) on sorted rows |
| 13 | Prefix sums + interpolation (`geo`) | bus position on a route from distance | O(log n) lookup |
| 14 | Ring buffer (`ds_ring`) | rolling average speed | fixed memory |
| 15 | Fenwick tree (`ds_fenwick`) | vendor revenue by day, range query | O(log n) update and query |
| 16 | Greedy assignment: merge sort + min-heap (`ops`) | auto-assign crew with no overlaps | interval scheduling |
| 17 | Douglas-Peucker with an explicit stack (`geo`) | map / GeoJSON simplification | no recursion depth risk |
| 18 | KMP (`ds_search`) | operator / amenity / phrase search, case-insensitive coach-make match | O(n+m) |
| 19 | Hash set (`ds_hash`) | alert de-duplication | O(1) membership |
| 20 | QR encoder (`core/qr.c`) | boarding-pass QR: Reed-Solomon over GF(256), versions 1-10, level M | deterministic, verified by decoding |
| 21 | Nearest point on a polyline (`geo_nearest_km`) | snap a driver's GPS fix to the route | projection per segment |
| 22 | Interval overlap check (`ops`) | a crew member cannot be on two overlapping runs | sorted intervals |
| 23 | Rule-based assistant: trie + hash + KMP + clock scanner (`assistant`) | cities, dates, cheapest / fastest, sleeper / seater, coach make, "after 7 am", amenities | no AI service; says so |

Also: SHA-256 (salted, educational), Haversine distance, and the per-stop arrival estimate (section 6.7).

---

## 6. Engine rules (all in C++; the page never decides these)

### 6.1 Seats and holds
* A trip has a seat bitset; layouts differ per coach type (sleeper lower / upper decks, seaters with an aisle).
* A user may hold at most **6 seats per trip**. A hold lasts **10 minutes** and expires through the heap; expiry frees the bit and notifies.
* "Find N together" uses the bitset k-run search. A full trip offers a waitlist (queue) that promotes on cancellation.
* No double booking: a booking may only use seats that are the user's own live holds.

### 6.2 Fare, tax, coupons, insurance, pre-booking
* Fare per seat comes from distance and the coach type's rate (upper / lower berth differ). **Tax 5 %.** Insurance is a flat amount per passenger and is **not refundable**.
* `SMART50`: flat Rs 50 off a fare above Rs 500, once per account. `EXPRESS10`: 10 % of the base fare above Rs 800, capped at Rs 150, only on premium coach types, limited uses.
* **Pre-booking** (deposit): pay 20 % now, the balance **48 hours before departure**, otherwise the booking lapses.

### 6.3 Payment and cancellation
* DEMO gateway with outcomes (success / failure). Pending payment holds the seats; failure releases them.
* Refund tiers by hours before departure: **>= 72 h 100 %**, **24 to 72 h 80 %**, **6 to 24 h 50 %**, **under 6 h 0 %**; insurance is never refunded. A preview is shown before cancelling.

### 6.4 Accounts, guests and lookup
* Roles: passenger, vendor (operator), driver, platform admin. The header **Login** accepts any account and opens the right screens; the Driver and Fleet screens keep strict role sign-in boxes (wrong role = "Invalid credentials").
* 5 wrong passwords lock an account.
* **Booking needs no account**: `POST /api/guest` creates a *guest* (a passenger row whose stored hash is `!`, which can never match, so it cannot be signed into; reachable only by its random session token). Guests are named "Guest traveller".
* **Public lookup** `GET /api/lookup?q=` finds trips by booking reference (hash table) or the phone used at checkout (newest-first scan comparing the **last 10 digits**, so `+91 98765 43210` equals `9876543210`). It returns route, time and status only, never names.
* Names, passenger names and contacts must not contain `< > " & \ ` backtick or control characters (letters of any language, digits, `. ' - @ +` are fine).

### 6.5 Tickets, QR and boarding
* The ticket JSON carries route, times, boarding / dropping stops, passengers, totals, break stations, crew (driver and conductor **names always; phone numbers only within 24 h of departure for paid bookings**), a text ticket with an ASCII route map, and the QR matrix produced by `core/qr.c`.
* Driver check-in by QR text or booking reference marks seats boarded; the passenger's ticket then shows a **BOARDED** stamp with a greyed QR.
* **Share links**: a public read-only live-trip page by random token, with no names or PNR; rotatable and revocable (the old link must stop working).

### 6.6 Tracking and simulation
* A trip is SCHEDULED, RUNNING or COMPLETED. The simulator moves the bus at the route's average speed; it stops at **meal breaks** for their listed minutes and **never at toll plazas**.
* A traffic **jam** slows the next 5 km to 15 km/h (about +14 minutes on Delhi to Jaipur); a reported delay is a **floor** (never added twice).
* A delay report (driver or operator) replaces the current delay, notifies every passenger with a live booking and carries the driver's note (max 120 characters stored) into that message.
* Alerts are **crossing-based** (the bus passing a distance, never a timer): departed, boarding point ahead (5 km) and reached, break ahead (20 km), stopped and resumed, toll ahead (about 25 minutes, "the bus does not stop") and crossed, drop-off ahead and reached, completed, plus delay-level alerts during a jam. They are de-duplicated by the hash set and queued in the notification outbox with per-category preferences (booking, journey, breaks, tolls ...).
* Driver GPS fixes are range-checked (latitude / longitude / speed / heading), must be within 25 km of the route, are snapped to the route with nearest-point-on-polyline, stored in a trail, and stale fixes never overwrite newer ones.

### 6.7 Per-stop arrival estimate (`eta_at_stop`)
For a stop at distance *s* while the bus is at *k*: time = (jam part of the remaining distance at 15 km/h) + (the rest at the route's average speed) + the break minutes of every meal halt **still ahead** of *s* + the rest of a break in progress; a reported delay is a floor. Before departure the estimate equals the plan exactly; at the destination it equals the headline ETA.
The tracker shows "Arrives 1:41 am, 31m" and an **On time / Running 1h 18m late** pill for every stop (late = 5 minutes or more).

### 6.8 Search, filters and the assistant
* Search by cities and date (today to 90 days), coach type, departure window, **departure time-of-day range** (minutes after / before), max fare, AC, sleeper, amenity, operator substring, seats available; sort by cheapest, earliest, fastest, most seats. The response includes `fare_cap` (the dearest seat on that route) for the slider.
* Assistant understands: partial city names (unique trie prefix), `from X to Y`, today / tomorrow / +N / YYYY-MM-DD / day after tomorrow, cheapest / fastest / **affordable**, morning, sleeper / seater, **Volvo / Scania / Mercedes / electric**, **"after 7 am", "before 9pm", "after 19:30", "after 12 am" (midnight)**, AC, charging, blanket, Wi-Fi.
  If the catalogue lacks something asked for (Wi-Fi) it **says so and ignores it** instead of returning nothing. It returns its parse (sort, time window, coach-type list, amenity) so the page can run the same search.
* The assistant is **not AI**; every screen that shows it says "rule-based, not an AI service".

### 6.9 Operator, driver and admin operations
* Operator (vendor): today's runs with per-trip crew (overlap-checked), off-duty toggle (refused while booked on a run today), start / finish, delay report, masked manifests, fleet and staff tables, undo (stack), auto-assign (greedy).
* Driver: duty list (only trips whose crew includes the driver), start / end, GPS fixes, delay report with cause (heavy traffic, weather, breakdown, roadwork, other) and note, manifest, boarding check-in.
* Admin: confirmed revenue and booked passes, trips today, **seat occupancy for the next 7 days** (sold / all seats), delay reports in 24 h, devices reporting (a GPS fix in the last 10 minutes), fleet telemetry for every trip today, revenue by corridor (Fenwick), coupon controls (issue, enable / disable), delay log, DEMO traffic scan.

---

## 7. HTTP server and API

* Endpoints (JSON; form-encoded POST bodies; `Authorization: Bearer <token>`): `/api/meta /api/cities /api/state /api/clock /api/login /api/register /api/guest /api/logout /api/search /api/trip /api/hold /api/quote /api/bookings /api/pay /api/cancel /api/ticket /api/waitlist /api/track /api/sim /api/plan /api/adjacent /api/assistant /api/lookup /api/notifications /api/notifications/read /api/prefs`, plus groups `/api/driver/*`, `/api/admin/*`, `/api/vendor/*` and `/api/share/*`. The shared public page uses `GET /?share=<token>`.
* `/api/track?trip=&drop=` returns per-stop `planned_ms`, `eta_ms`, `late_min`, `next_stop`, `via`, drop-off details (`drop_name`, `km_to_drop`, `drop_eta_ms`), `fixes`, `last_fix`, `avg_kmh`, lat / lng and the ASCII map.
* **Abuse limits** (found by probing, then fixed): max 2,000 accounts, 300 guests, 5,000 sessions (HTTP 429); public lookup max 20 per minute (429); header over 16 KB or body over 64 KB refused; idle connections dropped after 15 s.
* The server prints one line per page load, POST and error (for example `POST /api/login -> 401`) so a stuck page can be diagnosed from the server window.
* If the server is not running, the page says: "Cannot reach the SmartRoute server at ... The server program is not running (its window was closed or stopped, or this is an old tab). Start it again with run-web.cmd (Windows) or make serve, then reload this page".

---

## 8. Web UI specification (matches the running original, screen by screen)

General: the original stylesheet is reused (CSS variables, fonts Outfit / Plus Jakarta Sans / JetBrains Mono, dark mode, `.bus-card`, journey rail, seat matrix,
ticket card, ops tables). Light and dark themes (theme button is an icon). Responsive down to a phone width with no sideways overflow. **Every value the page
shows is HTML-escaped**; the page was tested with hostile text on every screen. Notice banner (`Signed out as X. Saved passes stay in this browser; sign in again to sync them.` with Dismiss).

**Navigation (one row):** Bus Tickets · My Bookings (n) · Track Bus · Planner · Fleet Portal (Company Console for operators) · Driver · **Copilot** (red) · theme · bell · Login / Sign out Name · role badge (guest shows "guest").

**Bus Tickets (home and results)**
* Hero, "Live Corridor Search" card: From, swap, To, date (today to +90 days), coach type, Search buses; Today / Tomorrow / +2 Days; **Smart Search** bar ("Ask: e.g. 'Volvo coach leaving after 7 AM with Wi-Fi' or 'Affordable sleeper'") with the note that it is rule-based.
* **DIRECT ROUTES** chips shown as `Delhi ↔ Jaipur` (8 shown, then "+7 more routes" / "Show fewer routes"); the current pair is highlighted.
* Results layout: left **Filters** card (Reset; Coach features: Air Conditioned (AC), Sleeper Berths; **Max fare** slider with value; Corridor trust note), right: "**N express buses found** (Delhi → Jaipur · 2026-10-24)" and a segmented sort (Cheapest · Earliest · Fastest · Most seats). Bus cards: operator, coach type, "No ratings yet (demo data)", journey rail with break / toll / stop dots, "Live GPS" badge, amenities, "From Rs N", "N seats left", **Select Seats →**.
* Active Smart Search filters are listed ("leaving after 05:00, Volvo-style ...") with a **Clear** button; a manual search clears them; changing route resets the slider.
* Landing sections below: Track your bus, offers (readable headings on coloured cards), routes.

**Seats / checkout / payment:** steps bar (Search, Seats, Passengers, Payment, Ticket); seat matrix with driver cab and deck switch; hold countdown; group seats; passenger details; boarding and dropping points; coupon with Apply; insurance; fare breakdown; pay in full or pre-book; DEMO payment page. Booking works without an account (guest session created on demand; role badge says guest).

**Ticket:** digital boarding pass with the real QR and BOARDED stamp; facts grid; DEMO PAYMENT badge; **ON THIS COACH** (Driver and Conductor with **Call** links `tel:`), the privacy note; primary red **Track live on Delhi → Jaipur →**, **Expand route** (lettered stops with times), **My trips**, **Share trip**, **Wake me at <drop-off>** + **Enable device alerts**, Pay balance / Cancel booking / Share summary, **Download / print PDF**, text ticket with ASCII map. Each viewed ticket is saved in this browser ("saved passes").

**My Bookings:** list from the account plus **Saved on this device** passes (with Track this bus) when signed out.

**Track Bus (original layout)**
1. Trip lookup card: "Track a booking" - booking reference or checkout phone.
2. Wake-me card: checkbox "Wake me at <stop>", Enable device alerts, "Works while this page stays open; sound/vibration support varies by device." Fires about 20 km before the stop (notification, beep, vibration, banner).
3. Left: title `Delhi → Jaipur via Manesar, Highway Food Mall`, subtitle, corridor tabs (one per tracked trip), DEMO simulation badge, "% completed" with "x of y km · demo position", segmented progress bar with stop ticks, "Origin" / "z km to go", **Estimated bus location** card (amber left border). Controls: **EN | हि** toggle, **Auto-follow bus**, **Fit to route**, Street map toggle. **Leaflet map**: lettered teardrop pins coloured by type (origin green, boarding blue, toll orange, meal halt purple, dropping violet, destination red), a bus marker, dashed blue route, red travelled trail; street-map background from three public tile servers tried in turn (**Esri, CARTO, OpenStreetMap**: a server fails after 4 errors with nothing loaded, after 6 s of silence, or when its pixels fail the **notice-tile probe** - two busy tiles from opposite sides of the world that come back identical are the provider's own error picture, not a map; the working one is remembered in `localStorage`), plus an **offline background** (`web/basemap.json`, built into the program, served at `/basemap.json`, drawn in a pane below the tiles: 2-degree grid, corridors, 20 labelled city dots, scale bar; neutral paper colour, and sea blue only when the file really contains land outlines) and a **status chip** on the map (`Esri map ✓ · road ✓` / `loading map…` / red `Map tiles unavailable` with **Retry map**) and a caption when nothing loads.
4. Right: **ESTIMATED ARRIVAL** ("Arrives 12:30 pm, 6h 6m", km to the stop, On time / Running Nm late pill, DEMO TRAFFIC note) and **Vehicle telemetry** (speed, next stop).
5. Stop timeline: lettered coloured circles, name, "Meal halt · 15 min halt · dep 06:54", km, "Arrives 6:50 am, 26m", On time / Running late pill; delayed rows tinted; passed stops greyed with "Passed".
6. **Driver GPS Stream Simulator** (dashed card): DEMO_MODE badge, Advance GPS, Inject Traffic Jam (+N min computed from the route), Pause / Resume stream, speed x60 / x120 / x300, Reset, status line "Nothing here is real GPS ...". Simulator controls work only for the booking's owner.
7. **Live road map:** the stream is **LIVE by default for the booking's owner** (a tick a second at x60; badge LIVE / PAUSED; Pause / Resume; Advance GPS and jam do not stop it; it stops by itself when the trip completes); other viewers have no simulator buttons but the page polls every 5 s; the admin trip map also polls and updates in place without flicker. **Road geometry:** the browser asks public routing servers in turn (`router.project-osrm.org`, then `routing.openstreetmap.de`, GeoJSON) for the road between the first and last stop, simplifies it (Douglas-Peucker, about 20 m) and caches it in `localStorage`; the bus and the stop pins are then placed by **distance travelled along that road**. If OSRM or the tiles are unreachable the route stays a straight line and the caption says so; a failed request is retried after a minute. (The C++ engine has no routing service; this sends two coordinates to a third party, and the README says so.)
8. Hindi (हि) translates the tracker labels and the 20 city names; other stop names stay English.

**Planner:** multi-leg journeys by Dijkstra (fastest) with legs, waits and total time; clear "No route" / "different networks" answers.

**Copilot:** chat with the rule-based assistant, example chips, honest wording, **Open in Search** runs the same filtered search (coach make, time window, amenity).

**Driver console:** Duty list (route, coach type, bus, date, status badge, progress, km, plan / live); trip header with Start trip / End trip; **GPS position** (Auto-share my location uses the browser's geolocation and is refused when more than 25 km from the route; Send one fix now = simulated device; Lat / Lng / km/h + Send coordinates); **Report a delay** (cause, minutes, note for passengers); **Boarding check-in** (Load manifest, **Scan QR with camera** via `BarcodeDetector` where supported, paste field, Validate enabled from 6 characters); Recent positions.

**Admin ("Operator control tower"):** four cards (Confirmed revenue, Trips today, Seat occupancy, Delay reports), "N scheduled trips today · M reporting", fleet telemetry table (selectable rows) with a **map panel for the selected trip**, revenue by corridor, coupon controls, delay log, Scan traffic delays.

**Operator console, shared trip page, sign-in modal:** operator runs / crew / fleet / staff; public shared page (no names, no PNR; "no longer active" after rotation); sign-in modal with Sign in / Register tabs, a dashed **"Demo accounts (dev build only) - tap one to fill the form"** box (Passenger, Driver, Admin, Vendor) and the note "Booking works without an account. Ops consoles need one: sign in as driver@smartroute.in or admin@smartroute.in ...".

---

## 9. Security and robustness requirements

* Escape every dynamic value in HTML; never use `innerHTML` with raw user text. Verified with `<img onerror>`, `<svg onload>`, `<script>` payloads in account name, passenger name, contact, delay note, search boxes, lookup and Copilot across passenger, driver, admin and operator screens.
* Validate on the server (section 6.4); limit account / guest / session counts and public lookups (section 7); cap request sizes; drop idle connections.
* No path traversal (the page is embedded in the binary; the server serves no files).
* Privacy: shared links and lookups expose no names; crew phone numbers only within 24 h for paid bookings; operators see masked manifests.
* Passwords salted + iterated; lockout after 5 failures. PNRs, salts and tokens come from a seeded pseudo-random generator, so they are **predictable** (a stated demo limit).

---

## 10. Testing and verification (what "done" means)

Run and report real output; never state a number you did not measure.

| Check | Command / tool | Expected now |
|---|---|---|
| Core data-structure tests | `make test` | 548 passed |
| Core stress (200,000-entry heap, hash, etc.) | `make test` | 200114 passed |
| Application tests (golden fares, pre-booking edges, planner, alerts, persistence, crew, coupons, share, assistant, QR, guests, lookup, ETA, time filters, name rules, delay note, basemap generator) | `make test` | 1180 passed |
| Warnings | `make all` | none (`-Werror`) |
| Sanitizers | `make debug` (builds the **server too**, with ASan + UBSan + leak check) | all tests pass, no reports |
| Demo reproducibility | `bin/smartroute --demo` twice, compare | identical |
| Real-browser checks | headless Chromium (Playwright script used in the sandbox, **not shipped** because the project has no Python) | 43 + 15 checks (booking as guest, filters, smart search, ticket, tracker, simulator, Hindi, lookup by phone, sign-in / out, admin, driver start / fix / delay / scan, BOARDED stamp, share and rotation, operator, planner, Copilot, dark mode, phone width) |
| Offline background | real browser; tiles blocked / working; a Natural-Earth-shaped stand-in file; file unreachable | 11 checks: file served as JSON (20 cities, 15 corridors); the background really draws (re-counted here: **67 shapes** = 1 frame + 29 graticule lines + 15 corridors + 20 city dots + 2 scale-bar parts, with the 20 city labels added only while tiles are down; the sandbox row said "77 layers" and its counting method was not recorded, so quote the breakdown, not a bare total); neutral colour (not sea blue) without land data; labels visible only without tiles; pane below tiles; chip + caption say so; with land data sea blue + shapes + credit; unreachable file = no errors |
| Basemap generator (C++) | `make test` / `make debug` | **645** checks, **600 of them fuzzed inputs** (the test prints its own count: `offline map background: 645 checks, 600 of them randomly corrupted inputs`): default file (20 cities, 15 corridors, exact coordinates, deterministic), stand-in Natural Earth (5 outlines kept of 7, nothing outside the region, world-sized polygon cut at the edge, UTF-8), point budget, malformed / nested / missing / wrong files give messages and leave no half-written file; 600 randomly corrupted files (truncated, flipped, deleted, junk, odd numbers) never crash and anything accepted is valid JSON. **The "under ASan + UBSan" part is only true on Linux / clang** - on Windows this ran green under `make debug`'s strict fallback (see section 12) |
| Map tiles | generated tiles served for each provider's URL | 8 checks: tiles render as 256x256 under the route; Esri blocked -> CARTO; both blocked -> OpenStreetMap; Esri silent for 6 s -> moves on; the next visit remembers the working provider; everything blocked -> red chip + Retry; Retry works once the network is back. **Plus the notice-tile probe (added after measuring the real internet here):** a provider that answers `200` with the *same* picture for two unrelated busy tiles is not drawing a map, so it is rejected too and the chip names it |
| Live road map | stand-in routing servers in OSRM's format (first one down, second answers) | 15 checks: bus moves by itself and stays on the road (worst 11 m off), Pause / Resume, cache = 0 requests on the next visit, tile-failure caption, viewer follows by polling, admin map live, both servers down = straight line |
| Injection probe | hostile text everywhere | 18 checks, nothing executes |
| Abuse probes | malformed / oversized / flood / spam requests against the sanitizer server | 16 / 16, no crash, no sanitizer report |

Also required: a clean-room check (unzip, `make all test`, run the browser checks against that copy) before handing over a zip.

### Every row above re-measured on Windows (this machine, 2026-10-20)

The rows above were written in the Linux sandbox. They were all re-run here on the Windows build machine
(MSYS2 UCRT64 gcc / g++ 16.1.0, `-Wall -Wextra -Wpedantic -Werror -O2`), against a copy of the repository in a
fresh folder, so the table is no longer inherited. Measured, not estimated:

| Check | How it was run here | Result |
|---|---|---|
| Clean-room build | copy of the repo (3.3 MB) to a new folder, `make all test` | **548 / 200,114 / 1,180 passed, 0 failed**, no compiler warnings, no errors |
| Test counts again in place | `bin/test_app.exe` here | 1180 passed, 0 failed (the basemap test prints its own `645 checks, 600 ... randomly corrupted inputs`) |
| Demo reproducibility | `bin/smartroute.exe --demo --now "..."` twice, `cmp` | **byte-identical, 13,669 bytes** |
| Benchmarks | `make bench` | see the two-machine table in `docs/DSA_REPORT.md`; the bench refuses to print unless its results verify (checksum 249906269041) |
| Account / guest / session caps | raw sockets + `curl` flood against throwaway servers on ports 8124-8128 (`--no-save`, nothing written to `data/`) | HTTP **429 at the 1,983rd registration** on a fresh server (= the 2,000 cap, 18 accounts are seeded); guests 429 at the **301st** request; sessions 429 at the **5,000th** |
| Lookup throttling | 30 lookup requests in a minute | 20 pass, the **21st onwards is 429** (limit 20 / minute) |
| Request size caps | 23 KB header, 70 KB body, malformed request line, 4 KB random header, truncated body | oversized header and body are answered **400**, then Windows resets the socket before the answer is read (see section 12); malformed line → 400; unknown path → 404; bad credentials → 401; **no crash at any point** |
| Idle drop | one held-open connection, timed | server closed it at **16.6 s** (the 15 s rule plus the select tick) |
| Concurrent clients | 45 sockets at once | **40 answered 200, 5 got ECONNRESET** - the documented drop-the-oldest at `MAX_CLIENTS = 40`; the server stayed alive and healthy afterwards |
| Injection probe | `<img onerror>`, `<svg onload>`, `<script>` payloads typed into name, passenger name, contact, delay note, search, lookup, Copilot, then the DOM read | the engine **refuses markup in names and contacts** at register / book time; the driver's free-text delay note is stored verbatim and rendered as **text** (`esc()`); a `window.__pwn` canary stayed **0**; `[onerror],[onload],img[src="x"]` matched **0 nodes**; all **65** `innerHTML` assignments in the page were read and every operand passes through `esc()` or is static markup |
| Real-browser golden path | the page against `bin/smartroute.exe --serve` in the browser tool | about **32** checks: guest → search → filters + fare slider → seat hold → quote → demo pay → ticket + QR → sign out → saved pass → lookup by phone → tracker chips → live map → simulator → driver start / fix / delay / scan → BOARDED stamp → share + rotate → operator → planner → Copilot → dark mode → phone width. **No screenshots**: this host's browser tool returns `NATIVE_BROWSER_VIEWPORT_UNAVAILABLE` for screenshots and pointer input, so every check read the DOM and computed styles (`evaluate_script` / `take_snapshot`) |
| Map with the **real** network (nothing stubbed) | live tiles and live OSRM from this machine, in the browser | Esri World_Topo_Map tile `200` / 19,770 B and a *different* 21,186 B tile elsewhere (a real map); Esri World_Imagery the same; **OSRM `code: Ok`, 284,891 m of road geometry for Delhi-Jaipur, 3,365 points, 217 min** - and the second routing server returns byte-identical geometry; **9 of 9 tiles painted**; pane `leaflet-sr-basemap-pane` computed z-index **150** under the tile pane's **200**; 67 background shapes; city labels at opacity **0** while tiles paint and **1** when they stop; chip **"Esri map ✓ · road ✓"**; caption "Map style: Esri · 39 of 280 km · bus positions simulated (DEMO). Offline background (20 cities, 15 corridors, 2° grid) is drawn underneath and shows itself when tiles stop." |
| Blocked providers that answer `200` | the same browser, the same machine | **CARTO serves one identical 2,049-byte "API KEY REQUIRED / carto.com/basemaps/apikey" picture for every coordinate** - error counting cannot see it, so the new pixel probe rejects it (measured: `TILE_PROBE` = Esri `true`, CARTO `false`); with Esri taken out of the chain the page ends on the red chip reading "Map tiles unavailable · CARTO answered with its own notice tile, not a map · showing the offline background · Retry map", the caption switching to "No map tiles yet ... this is the offline map background (67 shapes from /basemap.json ...)", labels back to opacity 1, and **Retry restoring Esri** (re-probed from scratch). The aerial style is probed separately (`World_Imagery = true`, chip "Esri satellite map ✓"). `tile.openstreetmap.org` served *real* tiles to the browser (29,226 B / 52,298 B for the two probe tiles) but returned its identical 6,987-byte "403 Access blocked / osm.wiki:Blocked" notice to a command-line request from the same IP - which is why the probe compares pixels rather than trusting the status code |

Two behaviours were found here and deliberately **left as they are**, because changing them is not obviously right:

* An oversized request is answered with `400` and then Windows resets the connection before most clients can
  read the body of the answer, so `curl` reports a reset rather than the status. The server really does reply;
  a raw socket reading until EOF sees the `400`. Fixing the *client-side* experience would mean delaying the
  close, which is a real change to a working event loop - so it is documented instead.
* When the **total account cap** (2,000) is the reason a guest session is refused, the answer still says
  "guest limit", because `h_guest` checks both conditions in one branch. The status code (429) and the effect
  are correct; only the wording is imprecise.

Ports 8124-8128 were opened for these probes and released; port 3000 (the student's own server) was never
touched, and `data/save` was never written (`--no-save` and throwaway save paths).

---

## 11. Running it (Windows first) and troubleshooting

* One-time: install MSYS2, then in the **MSYS2 UCRT64** shell `pacman -S mingw-w64-ucrt-x86_64-gcc make`.
* Easiest: double-click **`run-web.cmd`** (sets the PATH, builds, starts the server, opens the browser). Or `npm run dev` / `npm start` (only runs `run-web.cmd`; `npm install` has nothing to install; on Linux / macOS use `npm run dev:unix`).
* By hand (PowerShell): `$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:Path"`, then `make all`, then `.\bin\smartroute.exe --serve 8080 --open`. **Keep that window open: it is the server.**
* "Cannot reach the SmartRoute server at http://localhost:NNNN": the server program is not running (window closed or stopped) or the tab is old. Start it again and open the address it prints. To check: `curl.exe http://localhost:8080/api/meta` while the server window is open; the server window prints a line per page load, POST and error.
* `npm install` alone does nothing useful by design: the engine is C++ and must be compiled, which needs the compiler from the one-time step.

---

## 12. Honest limits (state these; do not hide them)

* Windows: the web server **has** been run here - `bin/smartroute.exe --serve <port> --no-save` on ports 8124 to 8142, plus `make test-ops`, which drives the real HTTP server end to end, and the browser checks below. What the assistant has *not* done is double-click `run-web.cmd` itself; it is a batch file that runs exactly the two steps that were run by hand here (`make all`, then `bin\smartroute.exe --serve <port> --open`), and the student uses it as his normal start.
* Real OSRM answers and real map tiles **are verified from this machine** (see the "Map with the real network" row in section 10): Esri serves real tiles, both routing servers return the same Delhi-Jaipur road geometry, and CARTO is now detected as an "API KEY REQUIRED" notice wall rather than believed. Still **not verified**: browser notifications / sound / vibration, camera QR scan (`BarcodeDetector` is missing in Chrome on Windows desktop), real browser geolocation on a phone, real phones.
* Not reproduced from the original: real payments (Razorpay), Firebase, Socket.IO live push (the page re-reads every 5 seconds and on each action), a real AI copilot, server-side road geometry, real traffic colouring, ratings, full Hindi, PWA / service worker, real GPS devices. Terminal app has no driver / admin screens (web only).
* Single-threaded localhost server, no TLS, predictable demo PRNG for PNRs / tokens, educational password hashing.
* Wake-me is a page-open alarm, not a background push.
* Road lines are straight until OSRM answers (from this machine it does, in about a second, and the chip says so); stop coordinates are sample data.
* Third-party map services are outside this project's control and one of them is effectively unusable: **CARTO's
  public basemap tiles now need an API key** (it answers `200` with a notice tile instead of a map), and
  **OpenStreetMap's tile servers block traffic that does not identify a browser** (from this machine a
  command-line request gets an "Access blocked" tile while the browser gets real tiles). Esri is the only one
  of the three that works unconditionally here, which is why it is first in the chain and why the page checks
  pixels instead of trusting status codes.
* Sanitizers: `make debug` builds with ASan + UBSan + leak checking where those runtimes exist. On this
  Windows machine MSYS2 UCRT64 gcc ships neither `libasan` nor `libubsan` and clang is not installed, so the
  target says so and falls back to `-O0 -g -D_GLIBCXX_ASSERTIONS -fstack-protector-strong`; the whole suite is
  green under that build. **The real ASan + UBSan pass over these binaries has not been run on Windows** - it
  needs Linux/macOS or a clang toolchain.

---

## 13. What was changed in this project (history, in order)

1. **Conversion (7 phases):** original React / TypeScript / Express / SQLite -> C data-structure library, C++ engine, console app, hand-written HTTP server, one HTML page. All 19 library modules, the 23 structures, CSV catalogue, atomic saves, QR encoder, assistant.
2. **Server freeze fixed:** the old one-connection-at-a-time server blocked on browsers' idle spare connections. Rewritten as a `select()` event loop (many clients, incremental parsing, idle drop, 400 on bad input). Page embedded in the binary.
3. **Windows start-up:** `run-web.cmd`, README Windows section, explanation that `npm install` fails because the project is not a Node project.
4. **First real-browser look:** screenshots exposed a cramped nav bar, dark-on-dark offer headings and a stretched shared-page badge; fixed. A delayed redraw after a driver scan was fixed.
5. **Parity with the running original** (from the student's screenshots): results page (direct-route chips, filters, sort), Smart Search, ticket with crew and actions, tracker (lookup, wake-me, corridor tabs, progress, location card, arrival, telemetry, stop timeline, simulator), sign-in modal, driver console, admin tower, Copilot, notice banner, saved passes, Hindi toggle.
6. **Booking without an account:** guest sessions, public lookup by reference or phone, saved passes in the browser.
7. **Engine additions:** `eta_at_stop`, departure time-of-day filter, fare cap, assistant understanding of clock phrases, coach make, seater, affordable, Wi-Fi honesty, admin seat-occupancy / delay-24 h / reporting figures, crew names always visible with phones gated.
8. **Map:** Leaflet inlined; lettered pins, bus marker, trail, EN / हि, Auto-follow, Fit to route; a CSS rule that stretched Leaflet's SVGs (hid the route line, blew up the attribution flag) fixed; **road geometry from OSRM** with the bus placed along the road and a straight-line fallback.
9. **Stale sanitizer binary found:** `make debug` built only the tests, so an earlier "server is sanitizer-clean" claim used an out-of-date binary. `make debug` now builds the server too, and the server was re-checked.
10. **Phone lookup bug:** raw-digit comparison missed `+91 ...`; now last-10-digit matching.
11. **Pre-departure ETA drift:** some routes differed from the plan by seconds; "nothing has happened yet" now returns the plan exactly.
12. **Launch friction:** optional `package.json` (`npm run dev`), clearer "cannot reach the server" message, per-request server log lines.
13. **Live road map:** the tracker stream now starts by itself (it used to wait for a button); second routing server, browser cache and a simplified road; an explicit caption when map tiles cannot load; simulator controls hidden for non-owners; a Leaflet error (a zoom animation firing after its map was removed) fixed by cancelling animations before removing a map.
14. **Map background:** after "no live map in the background": tiles were verified to render correctly when served (no CSS conflict), so the map now tries three tile servers, detects silent (firewalled) servers, remembers the working one and shows a status chip with Retry instead of an unexplained blank.
15. **Offline map background (a separate file):** `web/basemap.json` + `app/basemap.*` (a small JSON reader, Sutherland-Hodgman clipping, the project's Douglas-Peucker, a self-check before writing) + `--make-basemap` / `make basemap`; the page draws it below the tiles. A first version used sea blue everywhere, which put inland Delhi in an ocean; the default is now neutral and blue appears only with real land data. The real Natural Earth download could not be tested here (no network): only a stand-in file of the same structure.
16. **Audit and hardening:** probing found unlimited unauthenticated account creation and unthrottled lookup guessing -> caps and a 20 / minute limit (429); markup in names accepted -> validation + tests; Smart Search filters silently persisting, slider value carrying across routes, never-retried failed road request, duplicate tracked trips, and a stale "ticket" stage that showed "Booking not found" after signing out (seen as a `GET /api/ticket -> 404` in the new server log) -> fixed in the page. **The driver's "Note for passengers" was stored and saved but never shown to anyone**: it now appears in the passenger's notification ("Driver note: ...", and a changed note is a new message) and on the tracker's arrival card, with a test. Verified with the abuse probes, the injection probe and the full test suites.
17. **Notice tiles rejected (found by measuring the real internet):** the tile failover counted errors and silence, so a provider answering `200` with a picture *of its own error message* passed. Measured here: CARTO returns one identical 2,049-byte "API KEY REQUIRED" tile for every coordinate, and OpenStreetMap returns an identical "403 Access blocked" tile to non-browser clients from this machine. The page now compares the pixels of two busy tiles from opposite sides of the world before believing a provider; when they match it moves on, names the offender in the chip ("CARTO answered with its own notice tile, not a map"), takes back the caption credit it gave a moment earlier, forgets the remembered preference, and re-probes from scratch on **Retry map**. Verified in a real browser against the live internet: the rejection, the failover, the offline background appearing underneath with labels back at opacity 1, and Retry restoring Esri.
18. **Every section 10 row re-measured on Windows:** the numbers in the table were inherited from the Linux sandbox. They were all run again here (clean-room build, `--demo` byte comparison, `make bench`, the abuse floods against throwaway servers, the injection probe, the browser golden path, the real-network map), the results are in the "Every row above re-measured on Windows" subsection, and two stale claims in section 12 were corrected. The basemap generator's check count was fixed from 636 to the **645** the test actually prints.
19. **Both copies rebuilt and compared:** `web/index.html` **268,840 B** assembles to `build/web_page.html` **430,670 B** and `main.cpp` **1,979,633 B / 72 sections**, in each copy; `bin/smartroute.exe` **1,711,410 B** and `bin/smartroute_onefile.exe` **1,319,857 B** differ between the copies by only 4 and 3 bytes (the PE link timestamp). Suites green in both: 548 / 200,114 / 1,180 / 72, zero failures. During the sync an unrelated process removed the 13 lines that draw the DEMO bar and the virtual clock from `web/index.html` in the canonical copy; no build step writes that file, so the loss came from outside the tree and was restored from the mirror, after which the map, the probe and the clock were walked again in the browser.

---

## 14. Acceptance checklist for any future change

- [ ] Builds with no warnings (`make all`); `make test` all green; `make debug` clean.
- [ ] Money stays `int64` paise; no real-time clock reads outside `main`.
- [ ] No rule moved into the page; the page escapes everything it prints.
- [ ] New behaviour has a test (a golden number where money or time is involved) and the README / this prompt is updated.
- [ ] The real-browser checks still pass; screenshots are compared with the original where the UI changed.
- [ ] Unverified items are named in the answer; nothing is claimed that was not run.