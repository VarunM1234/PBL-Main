/*
 * PURPOSE: Binary search (lower/upper bound) and KMP substring search.
 * WHY THIS NAME: ds_search groups the searching algorithms.
 * CONNECTS TO: app/search (time-window filters), app/tracking (stop lookup), operator-name search.
 * COMPLEXITY: bounds O(log n); KMP O(n + m).
 * INVARIANTS: arrays given to the bound functions must already be sorted by the same cmp.
 */
#ifndef SMARTROUTE_DS_SEARCH_H
#define SMARTROUTE_DS_SEARCH_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
/* first index whose element is >= key (n if none) */
size_t ds_lower_bound(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *));
/* first index whose element is > key (n if none) */
size_t ds_upper_bound(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *));
long ds_binary_search(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *));          /* index or -1 */
/* Failure table: fail must hold strlen(pattern) ints. */
void ds_kmp_failure(const char *pattern, int *fail);
long ds_kmp_find(const char *text, const char *pattern);                /* first index or -1 */
size_t ds_kmp_find_all(const char *text, const char *pattern, size_t *out, size_t cap);
/* Case-insensitive contains (uses KMP on lower-cased copies). */
int ds_kmp_contains_nocase(const char *text, const char *pattern);
#ifdef __cplusplus
}
#endif
#endif
