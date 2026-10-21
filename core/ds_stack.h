/*
 * PURPOSE: Array-based LIFO stack for undo operations and state navigation.
 * WHY THIS NAME: ds_stack provides classic LIFO stack primitives in pure C.
 * CONNECTS TO: Vendor undo history (row 6) and screen back navigation.
 * COMPLEXITY:
 *   - push/pop/top: O(1) amortised
 * INVARIANTS: top element is at index size - 1.
 */

#ifndef SMARTROUTE_DS_STACK_H
#define SMARTROUTE_DS_STACK_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_stack ds_stack;

ds_stack *ds_stack_create(size_t elem_size, void (*free_elem)(void *));
ds_status ds_stack_push(ds_stack *s, const void *elem);
ds_status ds_stack_pop(ds_stack *s, void *out);
void *ds_stack_top(const ds_stack *s);
size_t ds_stack_size(const ds_stack *s);
void ds_stack_destroy(ds_stack *s);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_STACK_H */
