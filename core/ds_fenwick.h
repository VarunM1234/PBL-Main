/*
 * PURPOSE: Fenwick (Binary Indexed) tree: point update + prefix/range sums on int64.
 * WHY THIS NAME: named after Peter Fenwick, who described it.
 * CONNECTS TO: app/vendor (revenue per day over a 90-day horizon).
 * COMPLEXITY: add/prefix/range O(log n); space O(n).
 * INVARIANTS: tree[i] (1-based) holds the sum of the lowest_set_bit(i) elements ending at i.
 */
#ifndef SMARTROUTE_DS_FENWICK_H
#define SMARTROUTE_DS_FENWICK_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ds_fenwick ds_fenwick;
ds_fenwick *ds_fenwick_create(size_t n);
void ds_fenwick_add(ds_fenwick *f, size_t i, int64_t delta);   /* 0-based index */
int64_t ds_fenwick_prefix(const ds_fenwick *f, size_t i);      /* sum of [0..i] */
int64_t ds_fenwick_range(const ds_fenwick *f, size_t l, size_t r); /* sum of [l..r] */
void ds_fenwick_destroy(ds_fenwick *f);
#ifdef __cplusplus
}
#endif
#endif
