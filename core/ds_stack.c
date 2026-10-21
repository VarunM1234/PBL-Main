/*
 * PURPOSE: Implementation of generic array-based stack.
 * WHY THIS NAME: ds_stack.c implements ds_stack.h.
 * CONNECTS TO: core/ds_stack.h
 * COMPLEXITY: O(1) amortised push, O(1) pop and top.
 */

#include "ds_stack.h"
#include <string.h>

struct ds_stack {
    char *data;
    size_t elem_size;
    size_t size;
    size_t capacity;
    void (*free_elem)(void *);
};

ds_stack *ds_stack_create(size_t elem_size, void (*free_elem)(void *)) {
    if (elem_size == 0) return NULL;
    ds_stack *s = (ds_stack *)ds_xmalloc(sizeof(ds_stack));
    if (!s) return NULL;
    s->elem_size = elem_size;
    s->size = 0;
    s->capacity = 8;
    s->free_elem = free_elem;
    s->data = (char *)ds_xmalloc(s->capacity * elem_size);
    if (!s->data) {
        free(s);
        return NULL;
    }
    return s;
}

ds_status ds_stack_push(ds_stack *s, const void *elem) {
    if (!s || !elem) return DS_ERR_ARG;
    if (s->size >= s->capacity) {
        size_t new_cap = s->capacity * 2;
        char *new_data = (char *)ds_xrealloc(s->data, new_cap * s->elem_size);
        if (!new_data) return DS_ERR_NOMEM;
        s->data = new_data;
        s->capacity = new_cap;
    }
    memcpy(s->data + (s->size * s->elem_size), elem, s->elem_size);
    s->size++;
    return DS_OK;
}

ds_status ds_stack_pop(ds_stack *s, void *out) {
    if (!s) return DS_ERR_ARG;
    if (s->size == 0) return DS_ERR_EMPTY;
    s->size--;
    void *src = s->data + (s->size * s->elem_size);
    if (out) {
        memcpy(out, src, s->elem_size);
    } else if (s->free_elem) {
        s->free_elem(src);
    }
    return DS_OK;
}

void *ds_stack_top(const ds_stack *s) {
    if (!s || s->size == 0) return NULL;
    return (void *)(s->data + ((s->size - 1) * s->elem_size));
}

size_t ds_stack_size(const ds_stack *s) {
    return s ? s->size : 0;
}

void ds_stack_destroy(ds_stack *s) {
    if (!s) return;
    if (s->free_elem) {
        for (size_t i = 0; i < s->size; i++) {
            s->free_elem(s->data + (i * s->elem_size));
        }
    }
    free(s->data);
    free(s);
}
