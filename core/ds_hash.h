/*
 * PURPOSE: Hash table using separate chaining, FNV-1a hashing, and dynamic resizing.
 * WHY THIS NAME: ds_hash provides O(1) key-value dictionary and set operations.
 * CONNECTS TO: PNR lookups, user email index, coupon verification, alert deduplication.
 * COMPLEXITY:
 *   - get/put/remove: average O(1), worst O(n)
 * INVARIANTS: load factor <= 0.75; all entries in bucket b have hash(key) % num_buckets == b.
 */

#ifndef SMARTROUTE_DS_HASH_H
#define SMARTROUTE_DS_HASH_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_hash ds_hash;

ds_hash *ds_hash_create_str(size_t value_size, void (*free_value)(void *));
ds_hash *ds_hash_create(size_t key_size, size_t value_size,
                        uint64_t (*hash)(const void *),
                        int (*eq)(const void *, const void *),
                        void (*free_value)(void *));

ds_status ds_hash_put(ds_hash *h, const void *key, const void *value);
void *ds_hash_get(const ds_hash *h, const void *key);
int ds_hash_contains(const ds_hash *h, const void *key);
ds_status ds_hash_remove(ds_hash *h, const void *key);
size_t ds_hash_size(const ds_hash *h);
void ds_hash_foreach(const ds_hash *h, void (*fn)(const void *key, void *value, void *ctx), void *ctx);
void ds_hash_destroy(ds_hash *h);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_HASH_H */
