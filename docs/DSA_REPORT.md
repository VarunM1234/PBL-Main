# SmartRoute: Data Structures and Algorithms Report

Every structure below is **written by hand in `core/` (C)**, **used by the app (`app/`, C++)**, **unit-tested** (`tests/`) and cross-checked
against the STL on 10,000 seeded random operations (`tests/test_app.cpp`, STL allowed only there). Complexities are what the code does. The benchmark
table at the end is measured on **two** machines: the Linux sandbox the project was written in, and this Windows build machine, so the numbers can be
compared rather than trusted on one word. Where the original brief asked for something I did **not** build, the row says so.

Notation: *n* = number of elements, *L* = word length, *k* = output size, *V,E* = graph vertices/edges.

## Summary table

| # | Feature | Structure / algorithm | Time (typical) | Space | File | Used in |
|---|---|---|---|---|---|---|
| 1 | City autosuggest | Trie | insert/find O(L), suggest O(L+k) | O(total letters) | `ds_trie` | `catalog`, `cli`, `http_server` |
| 2 | PNR, e-mail, coupon, city, route, trip, bus, payment-key lookup | Hash table (chaining, FNV-1a, resize 0.75) | O(1) avg, O(n) worst | O(n) | `ds_hash` | most of `app/` |
| 3 | Seat availability, group seats | Bitset + popcount + k-run search | set/test O(1), count O(n/64), run O(n) | O(n/8) bytes | `ds_bitset` | `seats`, `search` |
| 4 | Hold expiry, balance deadlines, reminders | Min-heap with id index | push/pop/update/remove O(log n) | O(n) | `ds_heap` | `seats`, `booking`, `graph` |
| 5 | Waitlist, notification outbox | Circular-array queue | O(1) amortised | O(n) | `ds_queue` | `seats`, `notifications` |
| 6 | Vendor undo | Array stack | O(1) | O(n) | `ds_stack` | `vendor`, `geo` |
| 7 | Route stops | Doubly linked list | insert/remove at node O(1) | O(n) | `ds_list` | `catalog`, `ds_lru` (see limits) |
| 8 | Memoised bus position | LRU cache (hash + DLL) | O(1) | O(capacity) | `ds_lru` | `tracking` |
| 9 | Multi-leg planner | Graph, Dijkstra (heap), BFS | O((V+E) log V), O(V+E) | O(V+E) | `ds_graph` | `search` |
| 10 | "Same bus network?" | Union-Find | ~O(alpha(n)) | O(n) | `ds_unionfind` | `catalog`, `cli` |
| 11 | Sort results | Merge (stable), quick, heap sort | O(n log n) | O(n) / O(log n) / O(1) | `ds_sort` | `search`, `vendor` |
| 12 | Departure window, fare filter | Binary search (lower/upper bound) | O(log n) | O(1) | `ds_search` | `search` |
| 13 | Bus position on route | Cumulative km + binary search + interpolation | O(log s) | O(s) | `geo`, `ds_search` | `tracking` |
| 14 | Average speed, last 10 min | Ring buffer + running sum | O(1) amortised | O(capacity) | `ds_ring` | `tracking` |
| 15 | Vendor revenue by day | Fenwick tree | O(log n) | O(n) | `ds_fenwick` | `booking`, `vendor` |
| 16 | Staff auto-assign | Merge sort + greedy + min-heap | O(n log n) | O(n) | `vendor` | `vendor` |
| 17 | Map and GeoJSON simplification | Douglas-Peucker (explicit stack) | O(n log n) avg, O(n^2) worst | O(n) | `geo` | `asciimap`, `basemap` (rows 24 to 26) |
| 18 | Operator / amenity search | KMP | O(n+m) | O(m) | `ds_search` | `search` |
| 19 | Alert de-duplication | Hash set | O(1) avg | O(alerts) | `ds_hash` | `notifications` |

---

## 1. Trie: city autosuggest
**Problem.** Typing "Del" should list "Delhi" instantly. **Why a trie, not scanning an array:** a scan costs O(cities x L) per keystroke and
must sort; a trie walks only the typed letters, then a depth-first walk in slot order returns matches already alphabetical.
**Operations.** insert O(L), find O(L), suggest O(L+k); space O(sum of word lengths x 30 child pointers).
```
root -d-> D -e-> E -l-> L -h-> H -i-> I(word: Delhi)
              \-h-> ... r-a-d-u-n (Dehradun)      prefix "de" -> {dehradun, delhi}
```
**Worked example.** Cities {Delhi, Dehradun, Digha}; `suggest("De")` follows d,e then visits children `h` < `l` -> "dehradun", "delhi" (2 results).
**Invariant.** `child[c]` is NULL or a node whose path spells the prefix. **Viva.**
*Q: Why 30 slots?* a-z plus space, hyphen, apostrophe, dot; other characters are rejected with `DS_ERR_ARG`.
*Q: Cost of memory?* each node holds 30 pointers (240 B), so tries trade memory for speed; a compact map per node would save space.

## 2. Hash table
**Problem.** Find a booking by PNR, a user by e-mail, a coupon by code, a city by name, an idempotency key, a trip by "schedule:date" in O(1).
**Why not an array scan:** with 100,000 bookings a scan is 100,000 comparisons per lookup. **Design.** separate chaining, FNV-1a on the key bytes, bucket array doubles when load > 0.75.
**Worked example.** `"SRU86PRF2L"` hashes to a 64-bit value; `% buckets` picks the chain; average chain length stays <= 0.75.
```
buckets: [0]->() [1]->(A)->(D) [2]->() [3]->(B) ...      put/get/remove walk one chain
```
**Invariant.** every entry in bucket *b* has `hash(key) % buckets == b`; `size/buckets <= 0.75`. **Viva.**
*Q: Worst case?* all keys collide -> O(n); FNV-1a plus resizing makes that unlikely. *Q: Why chaining?* deletion is simple (unlink a node) and the table never "fills up".

## 3. Bitset: seat availability
**Problem.** Which seats are booked / held, how many are left, and where are *k* free seats side by side?
**Why a bitset:** 48 seats fit in one 64-bit word, so "seats left" is a popcount, not a loop over records; the group search is a sliding window over bits.
**Operations.** set/clear/test/flip O(1); count O(n/64); `find_clear_run(from,to,k)` O(to-from).
```
row 3 (2+2):  A B | C D        booked bits: 0 1 0 1  -> free run of length 1 only
```
**Worked example.** Sleeper row `L1 L2 L3` with L2 booked: `find_adjacent(2)` skips to the next row (index 3). Rows never cross decks.
**Invariant.** bits beyond `nbits` in the last word are zero. **Viva.** *Q: How do you count free seats?* capacity minus `popcount(booked) + popcount(held)`.
*Q: Why also hex?* `to_hex/from_hex` store the bitset compactly in save files.

## 4. Min-heap with id index: timers
**Problem.** Thousands of future events (hold expiry at +10 min, balance deadline, reminders) must fire in time order when the virtual clock advances;
some must be **cancelled** (booking paid) or **moved** (hold extended while paying).
**Why a heap, not a sorted list:** a sorted list makes insert O(n); the heap gives push/pop O(log n). The **id -> position map** (itself a resizing hash) makes `remove` and `update` O(log n) instead of O(n) search.
```
        3                array: [3, 5, 4, 9, 6]      parent(i) = (i-1)/2
      5   4          pop -> 3, move last (6) to root, sift down -> [4, 5, 6, 9]
    9  6
```
**Invariant.** `h[i] <= h[2i+1]` and `h[2i+2]`; `pos[id]` is the exact index of `id`. **Bug found by the benchmark:** the position map once had a fixed 64 buckets, so operations degraded to O(n/64) at 1,000,000 entries; fixed by resizing at load 0.75 and covered by a 200,000-entry regression test.
**Viva.** *Q: Why not `std::priority_queue`?* it has no remove/update by id. *Q: Heap vs BST?* same O(log n) but the array heap is cache-friendly and simpler.

## 5. Circular queue: waitlist and notification outbox
**Problem.** First come, first served: the first waitlisted user gets freed seats; alerts are delivered in creation order.
**Why a circular array:** O(1) enqueue/dequeue without shifting; it doubles when full (amortised O(1)). **Worked example.** capacity 4 holding C, D, E with `head = 2`: the items sit at indexes 2, 3, 0 (wrapping); enqueue F goes to index 1 and the queue is full.
```
index:   0   1   2   3
        [E] [ ] [C] [D]      head=2 (next to dequeue), items at (head + i) % capacity
```
**Invariant.** `size <= capacity`; indexes wrap modulo capacity. **Viva.** *Q: Why not a linked list?* array queue has better locality and no per-item allocation. *Q: When does it grow?* when `size == capacity`, it copies into a buffer twice as large in logical order.

## 6. Stack: vendor undo
**Problem.** "Undo my last status change / assignment." **Why a stack:** undo is last-in first-out. Each record stores (kind, bus, previous value, company). `undo_last` pops until it finds *this company's* record, parks others on a temporary stack and restores them in order, so one vendor never touches another's history.
**Operations.** push/pop O(1). **Worked example.** assign conductor #6 to DEMO-003 (push `{kind 2, bus 3, old -1}`), then undo -> slot returns to -1 and staff #6 becomes AVAILABLE again.
**Also used** as the explicit stack in Douglas-Peucker (row 17). **Not built:** ticket-screen "back navigation".
**Viva.** *Q: Why no redo?* not requested; it would need a second stack. *Q: Cost with many companies?* popping past other companies' records is O(m) in the number skipped.

## 7. Doubly linked list: route stops
**Problem.** A route is an ordered list of stops. **What exists:** every route builds a `ds_list` of stop indexes (insert/remove at a node O(1), traverse O(n)); the LRU cache's recency list is a `ds_list` too.
**Honest note:** the tracker walks the **array** of stops (needed for binary search), and "insert a break stop at run time" is **not** implemented, so the route list is built but only exercised by the unit tests and the LRU cache.
```
head <-> [Delhi 0] <-> [Toll 1 30] <-> [Manesar 45] <-> ... <-> [Jaipur 280] <-> tail
```
**Invariant.** `head->prev == NULL`, `tail->next == NULL`, `size` equals the node count. **Viva.** *Q: Why doubly linked?* O(1) removal given only the node (needed by the LRU). *Q: Array vs list for stops?* arrays allow binary search; lists allow O(1) insertion.

## 8. LRU cache: memoised bus position
**Problem.** Redrawing the tracking screen recomputes the same bus position. **Design.** hash (key -> list node) + doubly linked list (front = most recent); capacity 64; `get` moves the node to the front, `put` evicts the back when full.
**Worked example.** capacity 2: put a, put b, get a, put c -> evicts **b** (least recently used). **Scope:** only bus **position** is cached; ETA/traffic are cheap and are not cached.
**Invariant.** list size <= capacity; each list node has exactly one hash entry. **Viva.** *Q: Why both structures?* hash alone cannot order by recency; list alone cannot find by key in O(1). *Q: Key?* `"p<route>:<km x 10>"`.

## 9. Graph + Dijkstra + BFS: journey planner
**Problem.** No direct bus Delhi -> Ahmedabad? Find the best chain. Vertices = 20 cities, directed edges = 30 routes (weights: minutes, lowest fare).
**Dijkstra** with the min-heap: always settle the cheapest unsettled city; a **60-minute layover** is added for every leg that starts at a non-source city. **BFS** finds the fewest legs.
```
Delhi --376--> Jaipur --471--> Udaipur --332--> Ahmedabad        total = 376+471+332 + 2x60 = 1299 min
```
**Complexity.** Dijkstra O((V+E) log V) with the heap's `update` (decrease-key); BFS O(V+E). **Invariant.** weights >= 0 (Dijkstra needs it).
**Verified by tests.** Delhi->Ahmedabad 1299 min; Mysuru->Madurai (via Bengaluru, Chennai) 1246 min; Delhi->Goa "No route".
**Viva.** *Q: Why not Bellman-Ford?* no negative weights, so Dijkstra is faster. *Q: Why a layover trick works with Dijkstra?* it is a non-negative constant added to edge cost for non-source starts.

## 10. Union-Find: bus networks
**Problem.** "Is there *any* route between A and B?" and "how many separate networks?" **Why:** one union per route at load time, then every query is near O(1), no graph search needed.
**Design.** path compression in `find`, union by rank. **Result with the sample data: exactly 5 networks** (9 + 3 + 4 + 2 + 2 cities).
```
parent: [Mumbai]<-[Pune]   [Mumbai]<-[Goa]   find(Goa) -> Mumbai ; Delhi's root differs -> not connected
```
**Viva.** *Q: What does rank do?* keeps trees shallow by hanging the shorter under the taller. *Q: Complexity?* amortised O(alpha(n)), effectively constant.

## 11. Sorting: merge (stable), quick, heap
**Problem.** Sort results by departure, fare, duration, seats left. **Merge sort** is the production choice because it is **stable** (equal fares stay in departure order) and O(n log n) always. **Quicksort** uses median-of-three and insertion sort for ranges <= 16; **heap sort** is in-place O(n log n).
**Worked example.** [(Rs 620, 06:00), (Rs 700, 07:00), (Rs 620, 09:00)] sorted by fare -> 620@06:00, 620@09:00, 700@07:00 (stable).
**Invariant.** `cmp` returns <0, 0, >0. **Viva.** *Q: Why not just quicksort?* not stable, O(n^2) worst case. *Q: Space?* merge needs O(n) temp; heap sort O(1).

## 12. Binary search on sorted arrays: filters
**Problem.** "Departures between 18:00 and 24:00" and "fare <= Rs X". Results are pre-sorted by departure, so the window is **two `lower_bound` calls**; the fare cut sorts by fare and uses `upper_bound`.
**Complexity.** O(log n) to find the range, then O(k) to copy. **Worked example.** departures [06:00, 09:00, 13:30, 19:30, 23:00]; lower_bound(18:00)=3, lower_bound(24:00)=5 -> indexes 3..4.
**Viva.** *Q: lower vs upper bound?* first >= key vs first > key. *Q: Why not filter linearly?* fine for 10 buses, but this scales and demonstrates the technique; arrival-window and amenity filters are linear because arrivals are not sorted.

## 13. Prefix sums + binary search + interpolation: bus position
**Problem.** Given the bus is at km 87.5, where is it on the map? **Design.** `cum_km[i]` is the km of stop *i* (a prefix-sum array); `upper_bound` finds the segment in O(log s); linear interpolation of lat/lng inside it.
**Worked example.** stops at km 0, 30, 45, 105; km 87.5 -> segment (45,105), t = (87.5-45)/60 = 0.708 -> 70.8% of the way.
**Honest note:** *planned times at stops* use a direct proportional formula (distance fraction x drive minutes + completed breaks), not a binary search. **Viva.** *Q: Why binary search?* `cum_km` is sorted. *Q: Error?* positions are a straight-line schematic, not road geometry.

## 14. Ring buffer: average speed
**Problem.** Average of the last 10 simulated minutes of speed samples, with O(1) work per sample. **Design.** fixed capacity (64); `push` overwrites the oldest; `evict_before(t)` drops samples older than 10 min; a **running sum** makes the mean O(1).
**Worked example.** samples 55, 55, 15 (jam) -> sum 125, count 3, mean 41.7 km/h. **Invariant.** `running_sum` equals the sum of the live samples. **Viva.** *Q: Why not recompute the sum?* O(n) per query. *Q: Floating-point drift?* the running sum accumulates tiny errors, so it is reset to exactly 0 whenever eviction empties the buffer.

## 15. Fenwick tree: vendor revenue by day
**Problem.** "Total money collected between day a and day b" over a 120-day horizon, with payments and refunds arriving all the time (one tree per company).
**Why:** a prefix-sum array makes updates O(n); a plain array makes range queries O(n); the Fenwick tree makes **both O(log n)**.
```
index (1-based): 1 2 3 4 5 6 7 8     tree[i] covers (i - lowbit(i), i]   e.g. tree[8] = sum(1..8), tree[6] = sum(5..6)
```
**Worked example.** pay +62000 on day 3, refund -5000 on day 4: `range(3,4)` = 57000. **Viva.** *Q: lowbit?* `i & -i`. *Q: Point update?* add delta to i, then i += lowbit(i).

## 16. Greedy staff assignment (merge sort + min-heap)
**Problem.** Assign drivers/conductors to a company's trips for one day with **no overlap** and 8 h rest. **Algorithm.** sort trips by start (merge sort); a min-heap of staff keyed by "free at"; for each trip take the earliest-free person, assign if `free_at <= start`, then push them back with `free_at = trip end + 8 h`; otherwise report "no free driver". Off-duty staff are never put on the heap.
**Complexity.** O(n log n). **Worked example (demo data):** Orbit has 6 trips tomorrow but 2 drivers: 4 trips get a driver, 2 are reported unstaffed. **Viva.** *Q: Is greedy optimal?* handing each trip (in start order) to the earliest-free person minimises the number of people needed, as in classic interval partitioning; the 8 h rest rule just lengthens each interval. *Q: Overlap detection?* implicit: `free_at > start` means busy.

## 17. Douglas-Peucker: simplifying the drawn route
**Problem.** Draw the polyline with few points. **Algorithm.** keep the first and last point, find the point farthest from the segment between them; if it is farther than the tolerance (1 km) keep it and recurse on both halves, else drop all points between. Implemented with an **explicit stack** (no recursion depth risk).
**Complexity.** O(n log n) average, O(n^2) worst. **Worked example (tested):** points (0,0),(0.0001,1),(0,2),(1,3),(0,4), tolerance 0.5 km -> index 1 dropped, indexes 0,2,3,4 kept.
**Viva.** *Q: Why not keep every point?* clutter and cost. *Q: Tolerance unit?* kilometres, using a flat projection that is accurate at route scale.

## 18. KMP: substring search
**Problem.** Operator filter ("express" matches "Orbit Express (demo)") and amenity filter ("AC" in the amenities list). **Design.** failure table of the pattern (longest proper prefix that is also a suffix) so the text pointer never moves backwards: O(n+m); overlapping matches are supported; the app lower-cases both sides for case-insensitive search.
**Worked example.** pattern "ababab" -> failure [0,0,1,2,3,4]. **Viva.** *Q: Why not `strstr`?* worst case O(n*m). *Q: What does fail[i] mean?* the length of the longest border of `pattern[0..i]`.
*(Stop-name search was in the brief; only operator and amenity search are wired in.)*

## 19. Hash set: alert de-duplication
**Problem.** A 30 km jump or a replay must not send the same alert twice. **Design.** key `"u<user>:t<trip>:s<stop>:e<event>"` (or `...:jam:<level>`, `...:delay:<minutes>`) in a hash; `notify()` drops the alert if the key exists. The keys are restored from saved notifications after a restart.
**Complexity.** O(1) average. **Worked example (tested).** `fire_alerts(0,30)` creates 6 alerts in km order; calling it again creates none.
**Viva.** *Q: Why include the user in the key?* each passenger gets each alert once. *Q: Memory growth?* one entry per alert; production systems would expire old keys.

---

## Added for the console features (rows 20 to 23)

These were added when the driver, admin and ticket features of the original project were rebuilt.

### 20. QR code encoder: GF(256) arithmetic and Reed-Solomon (`core/qr.c`)
**Problem.** The boarding pass must carry a scannable code; the driver's scanner reads it. **Design.** Text is packed in byte mode, padded, split into blocks, and each block gets error-correction codewords from **polynomial division over GF(256)** (reduction polynomial 0x11D). Blocks are interleaved, placed in a zig-zag over the grid around the fixed finder, timing and alignment patterns, then one of 8 masks is chosen by a penalty score (runs of 5+, 2x2 blocks, dark balance) and format / version bits are written.
**Complexity.** Reed-Solomon O(data x ecc); mask search O(8 x modules); size = 17 + 4 x version (21 to 57 modules). **Worked example.** "SR|SRU86PRF2L|1|0" is 17 bytes -> version 2 (25 x 25 = 625 modules; 44 codewords of which 16 are error correction).
**Verification.** During development every version from 1 to 10 (including the multi-block versions 4, 6, 9, 10 and the version-info bits of 7+) was decoded correctly by an independent QR reader; structural checks are in the unit tests.
**Viva.** *Q: Why can a damaged code still scan?* the Reed-Solomon codewords let the reader correct up to about 15% damage at level M. *Q: Why 8 masks?* a mask breaks up large dark or light areas that confuse scanners; any mask is valid, the best-scoring one is used. **Limit:** the penalty rule for finder-like patterns is not implemented, which affects scan robustness slightly, not validity.

### 21. Nearest point on a polyline (`geo_nearest_km`)
**Problem.** A driver reports latitude / longitude; where is the bus along the route? **Design.** For each segment, project the point onto it (flat projection, parameter clamped to 0..1), keep the smallest distance, and report the prefix-sum km of that point. **Complexity.** O(n) for n stops. **Use.** Fixes more than 25 km from the route are refused; the tracker never moves backwards.
**Viva.** *Q: Why clamp the parameter?* a point before the start or past the end should snap to the end point. *Q: Why not binary search?* distance to the line is not monotonic along the route, so every segment is checked.

### 22. Interval overlap for crew assignment
**Problem.** A driver or conductor must never be on two runs at the same time. **Rule.** Two trips overlap exactly when `a.depart < b.arrive && b.depart < a.arrive`. Assigning a person checks every other live trip that already has them (O(trips)); the greedy auto-assigner (row 16) avoids overlaps by construction. **Worked example (tested).** DEMO-001 runs Jaipur->Delhi 20:00 to 02:16 and Delhi->Jaipur 23:00 to 05:16 on the same date: driver 2 can take the first but not also the second.
**Viva.** *Q: Why strict inequalities?* a run ending exactly when another starts is not an overlap. *Q: Can this be faster?* sorting each person's trips and checking neighbours is O(log n) per assignment.

### 23. Rule-based assistant (trie + hash + KMP)
**Problem.** "cheapest bus from del to jai tomorrow". **Design.** Words are matched to cities by hash lookup, or by a **unique trie prefix** (so "del" is Delhi); "from X" / "to Y" decide direction; a KMP phrase test spots "day after tomorrow"; the answer comes from the real search or the Dijkstra planner. **It is not AI:** fixed rules, and it says so.
**Viva.** *Q: Why require a unique prefix?* "m" could be Mumbai, Madurai or Mysuru; guessing would be wrong. *Q: What happens with an unknown sentence?* it explains what it understands.

---

## Added for the offline map background (rows 24 to 26)

The tracker has to show *something* when no tile server answers, so this project generates its own map background: `app/basemap.cpp` reads a GeoJSON of
land outlines, turns it into one small JSON file (`web/basemap.json`, also served at `/basemap.json`), and the page draws it in a Leaflet pane *under* the
tiles. Three pieces of it are real algorithms rather than glue, and they are rows 24 to 26. The rest of the feature is page-side JavaScript (trying three
tile servers, remembering the one that worked, asking OSRM for road geometry) and is deliberately **not** claimed as a data structure here: it is a
selection procedure over HTTP results, not a structure with a complexity claim. One part of that page-side procedure is worth naming because it was found
by measuring rather than guessing: before a tile server is believed, two busy tiles from opposite sides of the world are fetched and their pixels compared
(FNV-1a over a 48x48 canvas read-back), because a blocked provider answers `200` with **one identical notice picture** for every coordinate - measured here,
CARTO's "API KEY REQUIRED" tile is 2,049 bytes at any coordinate. Counting tile errors cannot see that; identical pixels can.

Measured on the served file this machine: bounds `[68, 6, 92, 36]` degrees, 29 graticule lines (13 meridians + 16 parallels at 2 degrees), 15 corridors,
20 cities, one 100 km scale bar, no land rings (`make basemap LAND=<file.geojson>` adds them) - 3,493 bytes.

### 24. Recursive-descent JSON reader (`json_parse` in `app/basemap.cpp`)
**Problem.** The generator must read a GeoJSON file someone else downloaded, and it must also *re-read its own output* before writing it (row 26), and the
server hands that file to the browser. So it needs a parser that cannot be crashed by a hostile file. **Design.** A classic recursive-descent reader over the
input string: `value -> object | array | string | number | true | false | null`, each case dispatching on the next non-space character. Strings handle the
escapes JSON defines, including `\u` (so `"K\u00f6ln"` and the UTF-8 spelling of "Köln" read to the same bytes). Numbers are **parsed by hand** - digits,
optional fraction, optional exponent - because `strtod` is locale-dependent and a machine whose decimal separator is "," would silently write a file whose
coordinates use commas. Object members are kept in **insertion order**, so re-reading and re-writing the same document reproduces it byte for byte.
**Memory and stack safety (the point of the design).** Three hard caps: `kMaxText` 64 MB of input, `kMaxDepth` 32 nesting levels, `kMaxNodes` 3,000,000
values. A file that exceeds one is refused with a message (`"nested deeper than 32"`, `"too many values"`) - it is *not* followed, so `[[[[[...` cannot
overflow the stack and a 100 MB array of nulls cannot eat the RAM. `json_parse` returns `false` and a message; it never throws and never calls `exit`.
**Complexity.** O(bytes) time, O(depth) call stack plus O(values) for the tree; lookups in an object are O(members) (`find` walks the small vector - fine for
a GeoJSON feature, and it keeps insertion order, which a hash map would lose).
**Worked example (tested).** The seven-feature stand-in file parses to `FeatureCollection` with 7 `features`; the reader reports `geometry.type` as `Polygon`,
`MultiPolygon` and `Point`, and the last one is ignored because a Point is not an outline. A 60-open-bracket file is refused, not followed.
**Viva.** *Q: Why not pull in a JSON library?* the project forbids third-party libraries in C/C++, and the page's own parser is separate. *Q: Why hand-parse
numbers?* locale: `strtod` on a ","-locale machine reads `28.6139` as `28`. *Q: What is the worst file?* a legal one just under all three caps - it is read
into memory, which is exactly what the caps bound.

### 25. Sutherland-Hodgman clipping of land outlines to the service region (`basemap_land_from_geojson`)
**Problem.** A world coastline file is useless here: the background covers the rectangle around this project's 20 cities, and drawing the planet would put
thousands of points on screen. **Design.** The region comes from the catalogue (`basemap_region`): the bounding box of every city, padded and snapped outward
onto the 2-degree grid so the graticule lines land exactly on the frame. Each ring is then clipped against that rectangle by **Sutherland-Hodgman**: four
passes, one per edge (keep `lng >= lng0`, then `lng <= lng1`, then `lat >= lat0`, then `lat <= lat1`); a pass walks the polygon and for each edge pair
(inside, inside) keeps the second point, (inside, outside) keeps the intersection, (outside, inside) keeps the intersection and the second point,
(outside, outside) keeps nothing. Because the clip window is convex and axis-aligned, four passes are *exact*, and the output is always closed.
**The pre-filter is a bounding-box test, not a vertex test** - deliberately. A test on vertices alone would throw away two real cases: the Natural Earth
Eurasia outline at 1:110 m, whose bounding box overlaps India while not one of its coarse vertices falls inside the region, and a polygon *bigger than the
window* (the whole world), which contains no vertex of the region but must still be cut to it. `bbox_touches` catches both; only genuinely far-away rings
(lng 0-5) are skipped before any arithmetic.
**Then row 17 does the shrinking.** The clipped ring is closed so Douglas-Peucker cannot drop the seam, simplified with `core/geo` at an 8 km tolerance
(coastline detail at that tolerance is invisible at this zoom and it saves most of the points), and duplicate points removed. Corridors use the same
simplifier at 1 km, which collapses the sample straight lines to their endpoints.
**Complexity.** Clipping O(4 x points) per ring, so O(rings x points) overall; simplification O(n log n) average, O(n^2) worst (row 17).
**Worked example (tested, 5 of 7 outlines kept).** Inside blob kept; a rectangle spanning the west edge clipped to it; a world-sized rectangle cut down to
the region bounds; a MultiPolygon where only one of its two parts touches the region contributes one shape; a ring whose box overlaps but has no vertex
inside still comes out clipped; a far-away ring and a Point contribute nothing. Every kept point is asserted inside the region (`all_inside`).
**Viva.** *Q: Why not clip to a polygon instead of a rectangle?* the window *is* a rectangle, and a convex window is the case Sutherland-Hodgman solves
exactly. *Q: What breaks with a concave subject polygon?* nothing here - the algorithm tolerates it; it is a concave *window* that breaks it.
*Q: Why 8 km and not 1 km?* the land is scenery; at 1 km the file grows by an order of magnitude for detail no one can see at 2 degrees per city.

### 26. Self-check before an atomic write (`basemap_self_check` + `basemap_write`)
**Problem.** This is the only part of the project that writes a file the browser then trusts. A half-written or wrong background is worse than none: the map
would draw an ocean where Delhi is, or the page would get JSON it cannot parse.
**Design.** The generator produces text, then **re-reads its own output with row 24's parser** and verifies the promises the page depends on: the format tag
`smartroute-basemap/1`, the bounds, the city count against the catalogue, the corridor count (one per pair - `catalog.cpp` writes the partner's id on *both*
directed routes, so the check counts the lower id of each pair), and every coordinate finite and inside the bounds. Only if that passes is the text written -
to `<out>.tmp`, flushed and closed, then `std::remove(out)` + `std::rename(tmp, out)` (Windows cannot rename onto an existing file, hence the remove). Any
failure path deletes the temp file and leaves the previous file untouched.
**Determinism, which the check depends on.** No clock, no random, no hash iteration order, no locale: the same catalogue plus the same GeoJSON gives
**byte-identical** output, so "did my change break the file?" is answered by `cmp`, and the server's embedded fallback (`basemap_generated`) is provable
equal to the file on disk (a test asserts it).
**Complexity.** O(file) for the self-check, dominated by the parse it re-does; the write is O(bytes).
**Verification (measured on this machine).** `bin/test_app.exe` prints: `offline map background: 645 checks, 600 of them randomly corrupted inputs`. The 45
non-fuzz checks cover the document promises, exact coordinates, determinism (`doc == basemap_json(...)` twice), the clipping of the seven stand-in outlines,
`all_inside`, the UTF-8 credit, the reader refusing eight deliberately broken inputs and a 60-deep nesting, and the writer leaving `"PREVIOUS GOOD FILE"`
intact after a rejection. The 600 fuzz rounds mutate the stand-in file one way of six (truncate, flip a byte to another printable byte, delete a byte,
insert junk from a fixed list, turn a number odd - `1e999`, `-0`, `0x10`, a 22-digit integer, a lone ".", `NaN` - or duplicate a 64-byte chunk), seeded by a
fixed xorshift so the run repeats; every round asserts either "accepted, all points inside the region, and the resulting document passes its own check" or
"refused, with a reason and no leftover shapes". **On this machine that ran under the strict fallback build (`-O0 -g
-D_GLIBCXX_ASSERTIONS -fstack-protector-strong`), not under ASan + UBSan: MSYS2 UCRT64 gcc has no `libasan` / `libubsan` and no clang here.** The ASan +
UBSan pass over these same 600 inputs still has to be run on Linux or with clang, and is listed as not verified.
**Viva.** *Q: Why re-parse the output instead of asserting while building it?* because the guarantee the page needs is about the *bytes*, not about the
builder's internals - the same check a reviewer would run by hand. *Q: Why tmp + rename instead of writing in place?* a crash or a full disk mid-write then
leaves the old file, never a truncated one. *Q: What does the remove-before-rename cost?* a short window where the file does not exist; the server answers
from the generated fallback, so the page cannot see it.

---

## Benchmarks (measured; `make bench`)

Two machines, both measured with `make bench` (single run, not averaged). The original run is the one the project was written against; the second is this repository's Windows build machine (Intel Core Ultra 5 225H, MSYS2 UCRT64 **gcc 16.1.0**, `-O2`) and is the one to quote if anyone re-runs it here. `bench` prints `cpu: unknown CPU` on Windows - the compiler and flags it prints are real, the CPU name is not available to it.

| Test | Sandbox: Xeon @ 2.10 GHz, gcc 13.3.0 (ms) | This machine: Core Ultra 5 225H, gcc 16.1.0 (ms) |
|---|---:|---:|
| merge sort, 100,000 records | 15.0 | 12.54 |
| quick sort (median-of-3), 100,000 records | 12.3 | 11.11 |
| heap sort, 100,000 records | 23.4 | 23.14 |
| `std::sort` (reference only), 100,000 records | 6.4 | 6.98 |
| hash: 1,000,000 inserts | 390.8 | 515.54 |
| hash: 1,000,000 lookups | 46.9 | 72.02 |
| heap: 1,000,000 pushes | 402.8 | 406.61 |
| heap: 1,000,000 pops | 1262.8 | 1737.25 |
| trie: 20 cities + 10,000 random words inserted | 1.9 | 6.05 |
| trie: 100,000 autosuggest queries | 87.9 | 82.81 |
| Fenwick: 1,000,000 updates + 1,000,000 range queries | 44.1 | 51.28 |

The Windows run verified its results the same way (the bench refuses to print unless every sort really is sorted and every key really is found; checksum 249906269041). The two runs are the same order of magnitude, and where they differ it is the memory-heavy cases - hash inserts, heap pops - not the algorithmic ones, which is what you would expect from a different CPU and a different standard library rather than a different complexity.

My sorts are within about 2 to 4 times of `std::sort` (which is heavily tuned); that is the price of a generic `memcpy`-based, comparator-callback design. On the Windows run the spread is 1.6x (quick) to 3.3x (heap sort).

## What I would change at 10 million users

A single process with in-memory arrays and text files cannot serve 10 million users. I would (1) replace the text save files with a database that gives transactions and row-level locking, so two buyers cannot take the same seat (today the single thread is what prevents that); (2) keep the *hold* idea but store holds in a shared low-latency store with expiry (the heap becomes a timer service); (3) shard trips by route and date so each shard owns its seat bitsets; (4) put search behind an index and cache popular route/date results (the LRU idea at larger scale) instead of building results per request; (5) use a real payment provider with webhooks and keep the idempotency-key design; (6) replace the educational SHA-256 hashing with bcrypt/argon2 and real sessions over TLS; (7) run many stateless API servers behind a load balancer, and compute the live-tracking stream from real GPS with a message queue instead of a simulator. The data structures here (bitset for seats, heap for timers, graph for planning) still apply per shard; what changes is where the state lives and who coordinates it.
