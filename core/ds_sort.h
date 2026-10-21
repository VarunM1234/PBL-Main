/*
 * PURPOSE: Hand-written generic sorts: stable merge sort, quicksort, heapsort.
 * WHY THIS NAME: ds_sort groups the O(n log n) sorting algorithms.
 * CONNECTS TO: app/search (result sorting), app/vendor (trips by start time), bench.
 * COMPLEXITY: merge O(n log n) always + O(n) extra space, stable;
 *             quick O(n log n) average, O(n^2) worst (median-of-three makes it rare);
 *             heap O(n log n) always, O(1) extra space, not stable.
 * INVARIANTS: cmp returns <0, 0, >0 like strcmp.
 */
#ifndef SMARTROUTE_DS_SORT_H
#define SMARTROUTE_DS_SORT_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*ds_cmp_fn)(const void *, const void *);
ds_status ds_merge_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp);
void ds_quick_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp);
void ds_heap_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp);
int ds_is_sorted(const void *base, size_t n, size_t size, ds_cmp_fn cmp);
#ifdef __cplusplus
}
#endif
#endif
