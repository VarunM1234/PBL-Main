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
