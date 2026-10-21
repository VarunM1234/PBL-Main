/*
 * PURPOSE: Doubly linked list supporting O(1) splice, insert, and remove.
 * WHY THIS NAME: ds_list provides standard linked list semantics in pure C.
 * CONNECTS TO: Route stop sequence (section 6 row 7) and LRU recency list.
 * COMPLEXITY:
 *   - push/insert/remove given node: O(1)
 *   - traversal: O(n)
 * INVARIANTS: head->prev == NULL; tail->next == NULL; size equals count of nodes.
 */

#ifndef SMARTROUTE_DS_LIST_H
#define SMARTROUTE_DS_LIST_H

#include "ds_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_list ds_list;
typedef struct ds_list_node ds_list_node;

ds_list *ds_list_create(size_t elem_size, void (*free_elem)(void *));
ds_list_node *ds_list_push_back(ds_list *list, const void *elem);
ds_list_node *ds_list_push_front(ds_list *list, const void *elem);
ds_list_node *ds_list_insert_after(ds_list *list, ds_list_node *node, const void *elem);
ds_list_node *ds_list_insert_before(ds_list *list, ds_list_node *node, const void *elem);
ds_status ds_list_remove(ds_list *list, ds_list_node *node);

ds_list_node *ds_list_front(const ds_list *list);
ds_list_node *ds_list_back(const ds_list *list);
ds_list_node *ds_list_next(const ds_list_node *node);
ds_list_node *ds_list_prev(const ds_list_node *node);
void *ds_list_data(ds_list_node *node);
size_t ds_list_size(const ds_list *list);
void ds_list_destroy(ds_list *list);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_LIST_H */
