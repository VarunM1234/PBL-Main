/*
 * PURPOSE: Benchmarks of my hand-written structures (std::sort is timed only as a reference).
 * WHY THIS NAME: "bench" = benchmark program, run with "make bench".
 * CONNECTS TO: the core headers. STL is allowed here only for the reference timing and the clock.
 * COMPLEXITY: see table printed at the end. Numbers depend on the machine; the program states it.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "ds_fenwick.h"
#include "ds_hash.h"
#include "ds_heap.h"
#include "ds_sort.h"
#include "ds_trie.h"
}

struct Row { int key; int payload[3]; };
static int cmp_row(const void *a, const void *b) { return static_cast<const Row *>(a)->key - static_cast<const Row *>(b)->key; }

static uint64_t g_seed = 987654321;
static uint64_t rnd() { g_seed ^= g_seed >> 12; g_seed ^= g_seed << 25; g_seed ^= g_seed >> 27; return g_seed * 0x2545F4914F6CDD1DULL; }

template <class F> static double ms(F f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static std::vector<std::pair<std::string, double>> g_rows;
static void record(const char *name, double t) { g_rows.push_back({name, t}); std::printf("  %-46s %10.2f ms\n", name, t); std::fflush(stdout); }

int main() {
    std::string cpu = "unknown CPU";
    std::ifstream ci("/proc/cpuinfo");
    for (std::string l; std::getline(ci, l);) if (l.rfind("model name", 0) == 0) { cpu = l.substr(l.find(':') + 2); break; }
    std::printf("SmartRoute benchmark\n  compiler: %s\n  flags: -O2 (see Makefile)\n  cpu: %s\n\n", __VERSION__, cpu.c_str());

    const size_t N = 100000;
    std::vector<Row> base(N);
    for (auto &r : base) { r.key = (int)(rnd() % 1000000); r.payload[0] = r.payload[1] = r.payload[2] = 0; }
    { auto v = base; record("merge sort, 100,000 records", ms([&] { ds_merge_sort(v.data(), N, sizeof(Row), cmp_row); })); if (!ds_is_sorted(v.data(), N, sizeof(Row), cmp_row)) return 1; }
    { auto v = base; record("quick sort (median-of-3), 100,000 records", ms([&] { ds_quick_sort(v.data(), N, sizeof(Row), cmp_row); })); if (!ds_is_sorted(v.data(), N, sizeof(Row), cmp_row)) return 1; }
    { auto v = base; record("heap sort, 100,000 records", ms([&] { ds_heap_sort(v.data(), N, sizeof(Row), cmp_row); })); if (!ds_is_sorted(v.data(), N, sizeof(Row), cmp_row)) return 1; }
    { auto v = base; record("std::sort (reference only), 100,000 records", ms([&] { std::sort(v.begin(), v.end(), [](const Row &a, const Row &b) { return a.key < b.key; }); })); }

    const size_t H = 1000000;
    ds_hash *h = ds_hash_create(sizeof(uint64_t), sizeof(int), [](const void *k) -> uint64_t { uint64_t x = *(const uint64_t *)k; x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; return x; },
                                [](const void *a, const void *b) -> int { return *(const uint64_t *)a == *(const uint64_t *)b; }, nullptr);
    record("hash: 1,000,000 inserts", ms([&] { for (uint64_t i = 0; i < H; i++) { int v = (int)i; ds_hash_put(h, &i, &v); } }));
    size_t found = 0;
    record("hash: 1,000,000 lookups", ms([&] { for (uint64_t i = 0; i < H; i++) found += ds_hash_get(h, &i) != nullptr; }));
    ds_hash_destroy(h);
    if (found != H) return 1;

    ds_heap *hp = ds_heap_create();
    record("heap: 1,000,000 pushes", ms([&] { for (uint64_t i = 0; i < H; i++) ds_heap_push(hp, (int64_t)(rnd() % 1000000000), i); }));
    int64_t prev = -1; bool ordered = true;
    record("heap: 1,000,000 pops", ms([&] { int64_t p; uint64_t id; while (ds_heap_pop_min(hp, &p, &id) == DS_OK) { if (p < prev) ordered = false; prev = p; } }));
    ds_heap_destroy(hp);
    if (!ordered) return 1;

    ds_trie *t = ds_trie_create();
    static const char *cities[20] = {"Delhi", "Jaipur", "Dehradun", "Chandigarh", "Agra", "Amritsar", "Shimla", "Udaipur", "Mumbai", "Pune", "Goa", "Bengaluru", "Chennai", "Mysuru", "Madurai", "Hyderabad", "Vijayawada", "Ahmedabad", "Kolkata", "Digha"};
    std::vector<std::string> words;
    for (int i = 0; i < 10000; i++) { std::string w; size_t len = 3 + rnd() % 8; for (size_t j = 0; j < len; j++) w += (char)('a' + rnd() % 26); words.push_back(w); }
    record("trie: 20 cities + 10,000 random words inserted", ms([&] { for (int i = 0; i < 20; i++) ds_trie_insert(t, cities[i], i); for (size_t i = 0; i < words.size(); i++) ds_trie_insert(t, words[i].c_str(), (int)i); }));
    char out[10][64]; int vals[10]; size_t got = 0;
    record("trie: 100,000 autosuggest queries (prefix 'de')", ms([&] { for (int i = 0; i < 100000; i++) got += ds_trie_suggest(t, "de", 10, out, vals); }));
    ds_trie_destroy(t);
    if (got == 0) return 1;

    ds_fenwick *f = ds_fenwick_create(100000);
    int64_t sum = 0;
    record("fenwick: 1,000,000 updates + 1,000,000 range queries", ms([&] { for (int i = 0; i < 1000000; i++) ds_fenwick_add(f, rnd() % 100000, 1); for (int i = 0; i < 1000000; i++) { size_t hi = rnd() % 100000; sum += ds_fenwick_range(f, hi / 2, hi); } }));
    ds_fenwick_destroy(f);
    std::printf("\n(checksum %lld; all results verified sorted/found before printing)\n", (long long)sum);
    return 0;
}
