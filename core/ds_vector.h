/*
 * PURPOSE: Generic dynamic array container that grows exponentially.
 * WHY THIS NAME: ds_vector mirrors standard vector semantics in pure C.
 * CONNECTS TO: search results, stop buffers, sort benchmarks.
 * COMPLEXITY:
 *   - push/pop: amortised O(1)
 *   - at: O(1)
 *   - insert/remove: O(n)
 * INVARIANTS: capacity >= size; data buffer holds size * elem_size bytes.
 */

#ifndef SMARTROUTE_DS_VECTOR_H
#define SMARTROUTE_DS_VECTOR_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_vector ds_vector;

ds_vector *ds_vector_create(size_t elem_size, void (*free_elem)(void *));
ds_status ds_vector_push(ds_vector *v, const void *elem);
void *ds_vector_at(const ds_vector *v, size_t i);
ds_status ds_vector_pop(ds_vector *v, void *out);
ds_status ds_vector_insert(ds_vector *v, size_t i, const void *elem);
ds_status ds_vector_remove(ds_vector *v, size_t i);
size_t ds_vector_size(const ds_vector *v);
void ds_vector_clear(ds_vector *v);
void ds_vector_destroy(ds_vector *v);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_VECTOR_H */
