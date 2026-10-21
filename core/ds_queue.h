/*
 * PURPOSE: Circular array FIFO queue that grows by doubling.
 * WHY THIS NAME: ds_queue provides FIFO waitlist and notification outbox semantics.
 * CONNECTS TO: Waitlist per sold-out trip (row 5) and notification dispatch queue.
 * COMPLEXITY:
 *   - enqueue: amortised O(1)
 *   - dequeue/peek: O(1)
 * INVARIANTS: head and tail wrap around capacity modulo; size <= capacity.
 */

#ifndef SMARTROUTE_DS_QUEUE_H
#define SMARTROUTE_DS_QUEUE_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_queue ds_queue;

ds_queue *ds_queue_create(size_t elem_size, void (*free_elem)(void *));
ds_status ds_queue_enqueue(ds_queue *q, const void *elem);
ds_status ds_queue_dequeue(ds_queue *q, void *out);
void *ds_queue_peek(const ds_queue *q);
size_t ds_queue_size(const ds_queue *q);
void ds_queue_destroy(ds_queue *q);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_QUEUE_H */
