/*
 * PURPOSE: LRU cache: hash table (key -> node) + doubly linked list (recency order).
 * WHY THIS NAME: "Least Recently Used" entries are evicted first.
 * CONNECTS TO: app/tracking (memoised ETA / geometry), ds_hash, ds_list.
 * COMPLEXITY: get/put O(1) average.
 * INVARIANTS: list front = most recent; size <= capacity; every list node has one hash entry.
 * Keys are C strings up to 63 chars. Values are copied (value_size bytes).
 */
#ifndef SMARTROUTE_DS_LRU_H
#define SMARTROUTE_DS_LRU_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ds_lru ds_lru;
ds_lru *ds_lru_create(size_t capacity, size_t value_size, void (*free_value)(void *));
void *ds_lru_get(ds_lru *c, const char *key);                 /* marks as most recent */
ds_status ds_lru_put(ds_lru *c, const char *key, const void *value);
size_t ds_lru_size(const ds_lru *c);
void ds_lru_destroy(ds_lru *c);
#ifdef __cplusplus
}
#endif
#endif
