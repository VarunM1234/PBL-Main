/*
 * PURPOSE: Word-level bitset (uint64_t words) with popcount, run search, and hex serialization.
 * WHY THIS NAME: ds_bitset manages compact seat availability and bitmask operations.
 * CONNECTS TO: Seat layout tracking (row 3), group seat booking search, save files.
 * COMPLEXITY:
 *   - set/clear/test/flip: O(1)
 *   - count (popcount): O(n/64)
 *   - find_clear_run: O(to - from)
 * INVARIANTS: bits beyond nbits in the last word are always kept zero.
 */

#ifndef SMARTROUTE_DS_BITSET_H
#define SMARTROUTE_DS_BITSET_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_bitset ds_bitset;

ds_bitset *ds_bitset_create(size_t nbits);
ds_status ds_bitset_set(ds_bitset *bs, size_t i);
ds_status ds_bitset_clear(ds_bitset *bs, size_t i);
int ds_bitset_test(const ds_bitset *bs, size_t i);
ds_status ds_bitset_flip(ds_bitset *bs, size_t i);
size_t ds_bitset_count(const ds_bitset *bs);
long ds_bitset_find_clear_run(const ds_bitset *bs, size_t from, size_t to, size_t k);
ds_status ds_bitset_to_hex(const ds_bitset *bs, char *buf, size_t cap);
ds_status ds_bitset_from_hex(ds_bitset *bs, const char *hex);
ds_bitset *ds_bitset_copy(const ds_bitset *bs);
void ds_bitset_destroy(ds_bitset *bs);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_BITSET_H */
