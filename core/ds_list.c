/*
 * PURPOSE: Implementation of generic doubly linked list.
 * WHY THIS NAME: ds_list.c implements ds_list.h.
 * CONNECTS TO: core/ds_list.h
 * COMPLEXITY: O(1) insertion and removal given node reference.
 */

#include "ds_list.h"
#include <string.h>

struct ds_list_node {
    struct ds_list_node *prev;
    struct ds_list_node *next;
    char data[1];
};

struct ds_list {
    ds_list_node *head;
    ds_list_node *tail;
    size_t elem_size;
    size_t size;
    void (*free_elem)(void *);
};

ds_list *ds_list_create(size_t elem_size, void (*free_elem)(void *)) {
    if (elem_size == 0) return NULL;
    ds_list *list = (ds_list *)ds_xmalloc(sizeof(ds_list));
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    list->elem_size = elem_size;
    list->size = 0;
    list->free_elem = free_elem;
    return list;
}

static ds_list_node *ds_list_create_node(const ds_list *list, const void *elem) {
    size_t alloc_size = sizeof(ds_list_node) - 1 + list->elem_size;
    ds_list_node *node = (ds_list_node *)ds_xmalloc(alloc_size);
    if (!node) return NULL;
    node->prev = NULL;
    node->next = NULL;
    memcpy(node->data, elem, list->elem_size);
    return node;
}

ds_list_node *ds_list_push_back(ds_list *list, const void *elem) {
    if (!list || !elem) return NULL;
    ds_list_node *node = ds_list_create_node(list, elem);
    if (!node) return NULL;
    if (!list->tail) {
        list->head = node;
        list->tail = node;
    } else {
        node->prev = list->tail;
        list->tail->next = node;
        list->tail = node;
    }
    list->size++;
    return node;
}

ds_list_node *ds_list_push_front(ds_list *list, const void *elem) {
    if (!list || !elem) return NULL;
    ds_list_node *node = ds_list_create_node(list, elem);
    if (!node) return NULL;
    if (!list->head) {
        list->head = node;
        list->tail = node;
    } else {
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->size++;
    return node;
}

ds_list_node *ds_list_insert_after(ds_list *list, ds_list_node *node, const void *elem) {
    if (!list || !node || !elem) return NULL;
    ds_list_node *new_node = ds_list_create_node(list, elem);
    if (!new_node) return NULL;
    new_node->prev = node;
    new_node->next = node->next;
    if (node->next) {
        node->next->prev = new_node;
    } else {
        list->tail = new_node;
    }
    node->next = new_node;
    list->size++;
    return new_node;
}

ds_list_node *ds_list_insert_before(ds_list *list, ds_list_node *node, const void *elem) {
    if (!list || !node || !elem) return NULL;
    ds_list_node *new_node = ds_list_create_node(list, elem);
    if (!new_node) return NULL;
    new_node->next = node;
    new_node->prev = node->prev;
    if (node->prev) {
        node->prev->next = new_node;
    } else {
        list->head = new_node;
    }
    node->prev = new_node;
    list->size++;
    return new_node;
}

ds_status ds_list_remove(ds_list *list, ds_list_node *node) {
    if (!list || !node) return DS_ERR_ARG;
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }
    if (list->free_elem) {
        list->free_elem(node->data);
    }
    free(node);
    list->size--;
    return DS_OK;
}

ds_list_node *ds_list_front(const ds_list *list) {
    return list ? list->head : NULL;
}

ds_list_node *ds_list_back(const ds_list *list) {
    return list ? list->tail : NULL;
}

ds_list_node *ds_list_next(const ds_list_node *node) {
    return node ? node->next : NULL;
}

ds_list_node *ds_list_prev(const ds_list_node *node) {
    return node ? node->prev : NULL;
}

void *ds_list_data(ds_list_node *node) {
    return node ? (void *)node->data : NULL;
}

size_t ds_list_size(const ds_list *list) {
    return list ? list->size : 0;
}

void ds_list_destroy(ds_list *list) {
    if (!list) return;
    ds_list_node *curr = list->head;
    while (curr) {
        ds_list_node *nxt = curr->next;
        if (list->free_elem) {
            list->free_elem(curr->data);
        }
        free(curr);
        curr = nxt;
    }
    free(list);
}
