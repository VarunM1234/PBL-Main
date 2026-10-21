/*
 * PURPOSE: Implementation of timestamped rolling-window ring buffer.
 * WHY THIS NAME: ds_ring.c implements ds_ring.h.
 * CONNECTS TO: core/ds_ring.h
 * COMPLEXITY: O(1) push and mean, amortised O(1) eviction.
 */

#include "ds_ring.h"

typedef struct {
    int64_t t_ms;
    double value;
} ds_ring_entry;

struct ds_ring {
    ds_ring_entry *entries;
    size_t capacity;
    size_t head;
    size_t count;
    double running_sum;
};

ds_ring *ds_ring_create(size_t capacity) {
    if (capacity == 0) return NULL;
    ds_ring *r = (ds_ring *)ds_xmalloc(sizeof(ds_ring));
    if (!r) return NULL;
    r->capacity = capacity;
    r->head = 0;
    r->count = 0;
    r->running_sum = 0.0;
    r->entries = (ds_ring_entry *)ds_xcalloc(capacity, sizeof(ds_ring_entry));
    if (!r->entries) {
        free(r);
        return NULL;
    }
    return r;
}

void ds_ring_push(ds_ring *r, int64_t t_ms, double value) {
    if (!r) return;
    if (r->count == r->capacity) {
        r->running_sum -= r->entries[r->head].value;
        r->entries[r->head].t_ms = t_ms;
        r->entries[r->head].value = value;
        r->running_sum += value;
        r->head = (r->head + 1) % r->capacity;
    } else {
        size_t idx = (r->head + r->count) % r->capacity;
        r->entries[idx].t_ms = t_ms;
        r->entries[idx].value = value;
        r->running_sum += value;
        r->count++;
    }
}

void ds_ring_evict_before(ds_ring *r, int64_t t_ms) {
    if (!r) return;
    while (r->count > 0 && r->entries[r->head].t_ms < t_ms) {
        r->running_sum -= r->entries[r->head].value;
        r->head = (r->head + 1) % r->capacity;
        r->count--;
    }
    if (r->count == 0) {
        r->running_sum = 0.0;
    }
}

size_t ds_ring_count(const ds_ring *r) {
    return r ? r->count : 0;
}

double ds_ring_mean(const ds_ring *r) {
    if (!r || r->count == 0) return 0.0;
    return r->running_sum / (double)r->count;
}

void ds_ring_destroy(ds_ring *r) {
    if (!r) return;
    free(r->entries);
    free(r);
}
