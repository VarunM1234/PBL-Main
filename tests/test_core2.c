/*
 * PURPOSE: Unit tests for phase 2 core structures (trie, fenwick, lru, graph, union-find, sort, search, geo, sha256).
 * WHY THIS NAME: second batch of core tests.
 * CONNECTS TO: the core headers and tests/test.h
 * COMPLEXITY: n/a
 */
#include "test.h"
#include "ds_trie.h"
#include "ds_fenwick.h"
#include "ds_lru.h"
#include "ds_graph.h"
#include "ds_unionfind.h"
#include "ds_sort.h"
#include "ds_search.h"
#include "geo.h"
#include "sha256.h"
#include "qr.h"
#include <stdlib.h>

static void test_trie(void) {
    printf("Testing ds_trie...\n");
    ds_trie *t = ds_trie_create();
    const char *names[] = {"Delhi", "Dehradun", "Digha", "Mumbai", "Madurai", "Mysuru", "Goa"};
    for (int i = 0; i < 7; i++) CHECK_EQ_INT(ds_trie_insert(t, names[i], i + 1), DS_OK);
    char out[10][64]; int vals[10];
    size_t n = ds_trie_suggest(t, "De", 10, out, vals);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(out[0], "dehradun"); CHECK_EQ_STR(out[1], "delhi");
    CHECK_EQ_INT(vals[1], 1);
    CHECK_EQ_INT(ds_trie_suggest(t, "M", 2, out, vals), 2);          /* limit honoured */
    CHECK_EQ_STR(out[0], "madurai");
    CHECK_EQ_INT(ds_trie_suggest(t, "zzz", 10, out, vals), 0);
    CHECK_EQ_INT(ds_trie_suggest(t, "", 10, out, vals), 7);          /* empty prefix = all */
    int v = 0;
    CHECK(ds_trie_find(t, "DELHI", &v)); CHECK_EQ_INT(v, 1);         /* case-insensitive */
    CHECK(!ds_trie_find(t, "Del", &v));                              /* prefix is not a word */
    CHECK_EQ_INT(ds_trie_insert(t, "Bad#Name", 1), DS_ERR_ARG);
    CHECK_EQ_INT(ds_trie_insert(t, "Delhi", 99), DS_OK);             /* duplicate overwrites value */
    CHECK(ds_trie_find(t, "delhi", &v)); CHECK_EQ_INT(v, 99);
    ds_trie_destroy(t); ds_trie_destroy(NULL);
}

static void test_fenwick(void) {
    printf("Testing ds_fenwick...\n");
    ds_fenwick *f = ds_fenwick_create(91);
    for (size_t i = 0; i < 91; i++) ds_fenwick_add(f, i, (int64_t)(i + 1));
    CHECK_EQ_INT(ds_fenwick_prefix(f, 0), 1);
    CHECK_EQ_INT(ds_fenwick_prefix(f, 90), 91 * 92 / 2);
    CHECK_EQ_INT(ds_fenwick_range(f, 10, 20), (11 + 21) * 11 / 2);
    ds_fenwick_add(f, 5, -6);
    CHECK_EQ_INT(ds_fenwick_range(f, 5, 5), 0);
    CHECK_EQ_INT(ds_fenwick_range(f, 20, 10), 0);
    ds_fenwick_destroy(f);
    ds_fenwick *one = ds_fenwick_create(1);
    ds_fenwick_add(one, 0, 7);
    CHECK_EQ_INT(ds_fenwick_range(one, 0, 0), 7);
    ds_fenwick_destroy(one);
}

static void test_lru(void) {
    printf("Testing ds_lru...\n");
    ds_lru *c = ds_lru_create(2, sizeof(int), NULL);
    int a = 1, b = 2, d = 3;
    ds_lru_put(c, "a", &a); ds_lru_put(c, "b", &b);
    CHECK_EQ_INT(*(int *)ds_lru_get(c, "a"), 1);       /* a becomes most recent */
    ds_lru_put(c, "d", &d);                            /* evicts b */
    CHECK(ds_lru_get(c, "b") == NULL);
    CHECK(ds_lru_get(c, "a") != NULL);
    CHECK_EQ_INT(ds_lru_size(c), 2);
    int e = 9; ds_lru_put(c, "a", &e);                 /* overwrite, size unchanged */
    CHECK_EQ_INT(*(int *)ds_lru_get(c, "a"), 9);
    CHECK_EQ_INT(ds_lru_size(c), 2);
    ds_lru_destroy(c);
}

static void test_graph_uf(void) {
    printf("Testing ds_graph / ds_unionfind...\n");
    ds_graph *g = ds_graph_create(5);
    ds_graph_add_edge(g, 0, 1, 10, 100, 0);
    ds_graph_add_edge(g, 1, 2, 10, 100, 1);
    ds_graph_add_edge(g, 0, 2, 50, 10, 2);
    ds_graph_add_edge(g, 2, 3, 5, 5, 3);
    int64_t dist[5]; int pv[5], pe[5], legs[5], path[5];
    ds_graph_dijkstra(g, 0, 0, 0, dist, pv, pe);
    CHECK_EQ_INT(dist[2], 20); CHECK_EQ_INT(dist[3], 25); CHECK(dist[4] == DS_INF);
    CHECK_EQ_INT(ds_graph_path(pv, 0, 3, path, 5), 4);
    ds_graph_dijkstra(g, 0, 0, 60, dist, pv, pe);           /* layover makes direct edge best */
    CHECK_EQ_INT(dist[2], 50);
    ds_graph_dijkstra(g, 0, 1, 0, dist, pv, pe);            /* cheapest by paise */
    CHECK_EQ_INT(dist[2], 10);
    ds_graph_bfs_fewest_legs(g, 0, legs, pv, pe);
    CHECK_EQ_INT(legs[2], 1); CHECK_EQ_INT(legs[3], 2); CHECK_EQ_INT(legs[4], -1);
    CHECK_EQ_INT(ds_graph_path(pv, 0, 4, path, 5), 0);
    CHECK_EQ_INT(ds_graph_add_edge(g, 0, 9, 1, 1, 0), DS_ERR_ARG);
    ds_graph_destroy(g);

    ds_unionfind *u = ds_uf_create(6);
    CHECK_EQ_INT(ds_uf_count(u), 6);
    CHECK(ds_uf_unite(u, 0, 1)); CHECK(ds_uf_unite(u, 1, 2)); CHECK(!ds_uf_unite(u, 0, 2));
    CHECK(ds_uf_connected(u, 0, 2)); CHECK(!ds_uf_connected(u, 0, 5));
    CHECK_EQ_INT(ds_uf_count(u), 4);
    ds_uf_destroy(u);
}

typedef struct { int key, order; } rec_t;
static int cmp_key(const void *a, const void *b) { return ((const rec_t *)a)->key - ((const rec_t *)b)->key; }
static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

static void test_sort_search(void) {
    printf("Testing ds_sort / ds_search...\n");
    enum { N = 2000 };
    rec_t *r = malloc(N * sizeof *r);
    srand(7);
    for (int i = 0; i < N; i++) { r[i].key = rand() % 50; r[i].order = i; }
    ds_merge_sort(r, N, sizeof *r, cmp_key);
    CHECK(ds_is_sorted(r, N, sizeof *r, cmp_key));
    int stable = 1;
    for (int i = 1; i < N; i++) if (r[i-1].key == r[i].key && r[i-1].order > r[i].order) stable = 0;
    CHECK(stable);                                           /* merge sort keeps equal keys in order */
    for (int i = 0; i < N; i++) { r[i].key = rand() % 1000; r[i].order = i; }
    ds_quick_sort(r, N, sizeof *r, cmp_key); CHECK(ds_is_sorted(r, N, sizeof *r, cmp_key));
    for (int i = 0; i < N; i++) { r[i].key = N - i; }
    ds_quick_sort(r, N, sizeof *r, cmp_key); CHECK(ds_is_sorted(r, N, sizeof *r, cmp_key));
    for (int i = 0; i < N; i++) { r[i].key = 5; }
    ds_quick_sort(r, N, sizeof *r, cmp_key); CHECK(ds_is_sorted(r, N, sizeof *r, cmp_key));
    for (int i = 0; i < N; i++) { r[i].key = rand() % 300; }
    ds_heap_sort(r, N, sizeof *r, cmp_key); CHECK(ds_is_sorted(r, N, sizeof *r, cmp_key));
    free(r);
    int none[1] = {0};
    ds_merge_sort(none, 0, sizeof(int), cmp_int); ds_quick_sort(none, 1, sizeof(int), cmp_int);
    int arr[] = {1, 3, 3, 3, 7, 9};
    int k = 3;
    CHECK_EQ_INT(ds_lower_bound(arr, 6, sizeof(int), &k, cmp_int), 1);
    CHECK_EQ_INT(ds_upper_bound(arr, 6, sizeof(int), &k, cmp_int), 4);
    k = 10; CHECK_EQ_INT(ds_lower_bound(arr, 6, sizeof(int), &k, cmp_int), 6);
    k = 0;  CHECK_EQ_INT(ds_lower_bound(arr, 6, sizeof(int), &k, cmp_int), 0);
    k = 7;  CHECK_EQ_INT(ds_binary_search(arr, 6, sizeof(int), &k, cmp_int), 4);
    k = 8;  CHECK_EQ_INT(ds_binary_search(arr, 6, sizeof(int), &k, cmp_int), -1);
    CHECK_EQ_INT(ds_kmp_find("Highway Food Mall", "Food"), 8);
    CHECK_EQ_INT(ds_kmp_find("abc", "abcd"), -1);
    CHECK_EQ_INT(ds_kmp_find("abc", ""), -1);
    size_t all[8];
    CHECK_EQ_INT(ds_kmp_find_all("aaaa", "aa", all, 8), 3);  /* overlapping matches */
    CHECK_EQ_INT(all[2], 2);
    int fail[6]; ds_kmp_failure("ababab", fail);
    CHECK_EQ_INT(fail[5], 4);
    CHECK(ds_kmp_contains_nocase("Orbit Express (demo)", "EXPRESS"));
    CHECK(!ds_kmp_contains_nocase("Orbit", "xyz"));
}

static void test_geo_sha(void) {
    printf("Testing geo / sha256...\n");
    geo_point delhi = {28.6139, 77.2090}, jaipur = {26.9124, 75.7873};
    double d = geo_haversine_km(delhi, jaipur);
    CHECK(d > 225.0 && d < 245.0);                          /* straight-line Delhi-Jaipur ~ 235 km */
    CHECK_NEAR(geo_haversine_km(delhi, delhi), 0.0, 1e-9);
    geo_point line[4] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}};
    double cum[4]; geo_cumulative_km(line, 4, cum);
    CHECK_NEAR(cum[3], 3 * 111.19, 0.5);
    geo_point mid; size_t seg;
    CHECK(geo_point_at_km(line, cum, 4, cum[3] / 2, &mid, &seg));
    CHECK_NEAR(mid.lng, 1.5, 1e-6); CHECK_EQ_INT(seg, 1);
    geo_point_at_km(line, cum, 4, -5, &mid, &seg); CHECK_NEAR(mid.lng, 0, 1e-9);
    geo_point_at_km(line, cum, 4, 1e9, &mid, &seg); CHECK_NEAR(mid.lng, 3, 1e-9);
    geo_point zig[5] = {{0, 0}, {0.0001, 1}, {0, 2}, {1.0, 3}, {0, 4}};
    size_t keep[5];
    size_t k = geo_simplify(zig, 5, 0.5, keep);              /* tiny wiggle at index 1 dropped, spike and its neighbour kept */
    CHECK_EQ_INT(k, 4); CHECK_EQ_INT(keep[0], 0); CHECK_EQ_INT(keep[1], 2); CHECK_EQ_INT(keep[2], 3); CHECK_EQ_INT(keep[3], 4);
    char hex[65];
    sha256_hex((const uint8_t *)"abc", 3, hex);
    CHECK_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256_hex((const uint8_t *)"", 0, hex);
    CHECK_EQ_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* 56 bytes: padding edge */
    sha256_hex((const uint8_t *)two, 56, hex);
    CHECK_EQ_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    char h1[65], h2[65]; const uint8_t salt[2] = {1, 2}, salt2[2] = {1, 3};
    sha256_salted_iter(salt, 2, "Demo@1234", 100, h1);
    sha256_salted_iter(salt, 2, "Demo@1234", 100, h2); CHECK_EQ_STR(h1, h2);
    sha256_salted_iter(salt2, 2, "Demo@1234", 100, h2); CHECK(strcmp(h1, h2) != 0);
}

#include "ds_heap.h"

/* Regression: the heap id index once stayed at 64 buckets, making 100,000+ entries crawl. */
static void test_heap_scale(void) {
    printf("Testing ds_heap at scale...\n");
    ds_heap *h = ds_heap_create();
    enum { N = 200000 };
    for (uint64_t i = 0; i < N; i++) CHECK_EQ_INT(ds_heap_push(h, (int64_t)((i * 7919u) % 100003u), i), DS_OK);
    CHECK_EQ_INT(ds_heap_size(h), N);
    CHECK_EQ_INT(ds_heap_update(h, 5, -1), DS_OK);
    CHECK_EQ_INT(ds_heap_remove(h, 6), DS_OK);
    int64_t prev = -2, p; uint64_t id; int ordered = 1; size_t popped = 0;
    while (ds_heap_pop_min(h, &p, &id) == DS_OK) { if (p < prev) ordered = 0; prev = p; popped++; }
    CHECK(ordered); CHECK_EQ_INT(popped, N - 1);
    ds_heap_destroy(h);
}

/* QR: structure checks here; the code was also decoded with an independent QR reader during development. */
static void test_qr_nearest(void) {
    printf("Testing qr / geo_nearest_km...\n");
    static unsigned char m[QR_MAX_SIZE * QR_MAX_SIZE], m2[QR_MAX_SIZE * QR_MAX_SIZE];
    int size = 0, size2 = 0;
    CHECK_EQ_INT(qr_encode("SR|SRU86PRF2L|1|0", m, &size), 0);
    CHECK_EQ_INT(size, 25);                                       /* 17 bytes of text need version 2 at level M */
    CHECK_EQ_INT(m[0 * size + 0], 1); CHECK_EQ_INT(m[1 * size + 1], 0); CHECK_EQ_INT(m[2 * size + 2], 1);       /* finder pattern */
    CHECK_EQ_INT(m[0 * size + size - 1], 1); CHECK_EQ_INT(m[(size - 1) * size + 0], 1);
    int timing_ok = 1;
    for (int i = 8; i < size - 8; i++) if (m[6 * size + i] != (i % 2 == 0)) timing_ok = 0;
    CHECK(timing_ok);                                             /* timing pattern alternates */
    CHECK_EQ_INT(m[(size - 8) * size + 8], 1);                    /* the always-dark module */
    CHECK_EQ_INT(qr_encode("SR|SRU86PRF2L|1|0", m2, &size2), 0);
    CHECK(size2 == size && memcmp(m, m2, (size_t)size * (size_t)size) == 0);   /* deterministic */
    CHECK_EQ_INT(qr_encode("A", m, &size), 0); CHECK_EQ_INT(size, 21);
    char big[300]; memset(big, 66, sizeof big); big[250] = 0;
    CHECK_EQ_INT(qr_encode(big, m, &size), -2);
    CHECK_EQ_INT(qr_encode(NULL, m, &size), -1);
    geo_point line[4] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}};
    double cum[4], along = 0; geo_cumulative_km(line, 4, cum);
    double off = geo_nearest_km(line, cum, 4, (geo_point){0.1, 1.5}, &along);
    CHECK_NEAR(along, 1.5 * 111.19, 0.5); CHECK_NEAR(off, 0.1 * 110.574, 0.3);
    off = geo_nearest_km(line, cum, 4, (geo_point){0, -2}, &along);   /* before the start: clamps to the first point */
    CHECK_NEAR(along, 0.0, 1e-6);
    off = geo_nearest_km(line, cum, 4, (geo_point){0, 9}, &along);    /* after the end: clamps to the last point */
    CHECK_NEAR(along, cum[3], 1e-6);
    (void)off;
}

int main(void) {
    printf("SMARTROUTE PHASE 2: REMAINING CORE\n==================================\n");
    test_trie(); test_fenwick(); test_lru(); test_graph_uf(); test_sort_search(); test_geo_sha(); test_heap_scale(); test_qr_nearest();
    return test_report();
}
