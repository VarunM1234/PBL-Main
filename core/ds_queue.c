/*
 * PURPOSE: Implementation of generic circular-array FIFO queue.
 * WHY THIS NAME: ds_queue.c implements ds_queue.h.
 * CONNECTS TO: core/ds_queue.h
 * COMPLEXITY: Amortised O(1) enqueue, O(1) dequeue and peek.
 */

#include "ds_queue.h"
#include <string.h>

struct ds_queue {
    char *data;
    size_t elem_size;
    size_t capacity;
    size_t head;
    size_t size;
    void (*free_elem)(void *);
};

ds_queue *ds_queue_create(size_t elem_size, void (*free_elem)(void *)) {
    if (elem_size == 0) return NULL;
    ds_queue *q = (ds_queue *)ds_xmalloc(sizeof(ds_queue));
    if (!q) return NULL;
    q->elem_size = elem_size;
    q->capacity = 8;
    q->head = 0;
    q->size = 0;
    q->free_elem = free_elem;
    q->data = (char *)ds_xmalloc(q->capacity * elem_size);
    if (!q->data) {
        free(q);
        return NULL;
    }
    return q;
}

static ds_status ds_queue_grow(ds_queue *q) {
    size_t new_cap = q->capacity * 2;
    char *new_data = (char *)ds_xmalloc(new_cap * q->elem_size);
    if (!new_data) return DS_ERR_NOMEM;
    for (size_t i = 0; i < q->size; i++) {
        size_t idx = (q->head + i) % q->capacity;
        memcpy(new_data + (i * q->elem_size),
               q->data + (idx * q->elem_size),
               q->elem_size);
    }
    free(q->data);
    q->data = new_data;
    q->head = 0;
    q->capacity = new_cap;
    return DS_OK;
}

ds_status ds_queue_enqueue(ds_queue *q, const void *elem) {
    if (!q || !elem) return DS_ERR_ARG;
    if (q->size >= q->capacity) {
        ds_status s = ds_queue_grow(q);
        if (s != DS_OK) return s;
    }
    size_t tail = (q->head + q->size) % q->capacity;
    memcpy(q->data + (tail * q->elem_size), elem, q->elem_size);
    q->size++;
    return DS_OK;
}

ds_status ds_queue_dequeue(ds_queue *q, void *out) {
    if (!q) return DS_ERR_ARG;
    if (q->size == 0) return DS_ERR_EMPTY;
    void *src = q->data + (q->head * q->elem_size);
    if (out) {
        memcpy(out, src, q->elem_size);
    } else if (q->free_elem) {
        q->free_elem(src);
    }
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return DS_OK;
}

void *ds_queue_peek(const ds_queue *q) {
    if (!q || q->size == 0) return NULL;
    return (void *)(q->data + (q->head * q->elem_size));
}

size_t ds_queue_size(const ds_queue *q) {
    return q ? q->size : 0;
}

void ds_queue_destroy(ds_queue *q) {
    if (!q) return;
    if (q->free_elem) {
        for (size_t i = 0; i < q->size; i++) {
            size_t idx = (q->head + i) % q->capacity;
            q->free_elem(q->data + (idx * q->elem_size));
        }
    }
    free(q->data);
    free(q);
}
