/*
 * PURPOSE: Binary min-heap keyed by int64_t priority with O(log n) decrease-key / remove by id.
 * WHY THIS NAME: ds_heap provides priority queue operations with id tracking.
 * CONNECTS TO: Seat-hold expiry (row 4), simulator event queue, and Dijkstra algorithm (row 9).
 * COMPLEXITY:
 *   - peek_min: O(1)
 *   - push/pop/update/remove: O(log n)
 * INVARIANTS:
 *   - heap[i].priority <= heap[2*i + 1].priority and heap[2*i + 2].priority.
 *   - pos_map maintains exact index of each active id in the heap array.
 */

#ifndef SMARTROUTE_DS_HEAP_H
#define SMARTROUTE_DS_HEAP_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_heap ds_heap;

ds_heap *ds_heap_create(void);
ds_status ds_heap_push(ds_heap *h, int64_t priority, uint64_t id);
ds_status ds_heap_peek_min(const ds_heap *h, int64_t *priority, uint64_t *id);
ds_status ds_heap_pop_min(ds_heap *h, int64_t *priority, uint64_t *id);
ds_status ds_heap_update(ds_heap *h, uint64_t id, int64_t new_priority);
ds_status ds_heap_remove(ds_heap *h, uint64_t id);
size_t ds_heap_size(const ds_heap *h);
void ds_heap_destroy(ds_heap *h);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_HEAP_H */
