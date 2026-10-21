/*
 * PURPOSE: Fixed-capacity ring buffer of timestamped values with running sum for rolling averages.
 * WHY THIS NAME: ds_ring provides circular sliding-window telemetry buffers.
 * CONNECTS TO: Average speed of last 10 minutes of GPS fixes (row 14).
 * COMPLEXITY:
 *   - push: O(1)
 *   - evict_before: amortised O(1)
 *   - mean/count: O(1)
 * INVARIANTS: running_sum equals sum of all active values currently in the window.
 */

#ifndef SMARTROUTE_DS_RING_H
#define SMARTROUTE_DS_RING_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_ring ds_ring;

ds_ring *ds_ring_create(size_t capacity);
void ds_ring_push(ds_ring *r, int64_t t_ms, double value);
void ds_ring_evict_before(ds_ring *r, int64_t t_ms);
size_t ds_ring_count(const ds_ring *r);
double ds_ring_mean(const ds_ring *r);
void ds_ring_destroy(ds_ring *r);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_RING_H */
