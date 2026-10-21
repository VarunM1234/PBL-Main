/*
 * PURPOSE: Implementation of generic growable array (vector).
 * WHY THIS NAME: ds_vector.c implements ds_vector.h.
 * CONNECTS TO: core/ds_vector.h
 * COMPLEXITY: Amortised O(1) push, O(1) random access, O(n) insert/remove.
 */

#include "ds_vector.h"
#include <string.h>

struct ds_vector {
    char *data;
    size_t elem_size;
    size_t size;
    size_t capacity;
    void (*free_elem)(void *);
};

ds_vector *ds_vector_create(size_t elem_size, void (*free_elem)(void *)) {
    if (elem_size == 0) return NULL;
    ds_vector *v = (ds_vector *)ds_xmalloc(sizeof(ds_vector));
    if (!v) return NULL;
    v->elem_size = elem_size;
    v->size = 0;
    v->capacity = 8;
    v->free_elem = free_elem;
    v->data = (char *)ds_xmalloc(v->capacity * elem_size);
    if (!v->data) {
        free(v);
        return NULL;
    }
    return v;
}

static ds_status ds_vector_ensure_capacity(ds_vector *v, size_t needed) {
    if (needed <= v->capacity) return DS_OK;
    size_t new_cap = v->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    char *new_data = (char *)ds_xrealloc(v->data, new_cap * v->elem_size);
    if (!new_data) return DS_ERR_NOMEM;
    v->data = new_data;
    v->capacity = new_cap;
    return DS_OK;
}

ds_status ds_vector_push(ds_vector *v, const void *elem) {
    if (!v || !elem) return DS_ERR_ARG;
    ds_status s = ds_vector_ensure_capacity(v, v->size + 1);
    if (s != DS_OK) return s;
    memcpy(v->data + (v->size * v->elem_size), elem, v->elem_size);
    v->size++;
    return DS_OK;
}

void *ds_vector_at(const ds_vector *v, size_t i) {
    if (!v || i >= v->size) return NULL;
    return (void *)(v->data + (i * v->elem_size));
}

ds_status ds_vector_pop(ds_vector *v, void *out) {
    if (!v) return DS_ERR_ARG;
    if (v->size == 0) return DS_ERR_EMPTY;
    v->size--;
    void *src = v->data + (v->size * v->elem_size);
    if (out) {
        memcpy(out, src, v->elem_size);
    } else if (v->free_elem) {
        v->free_elem(src);
    }
    return DS_OK;
}

ds_status ds_vector_insert(ds_vector *v, size_t i, const void *elem) {
    if (!v || !elem || i > v->size) return DS_ERR_ARG;
    ds_status s = ds_vector_ensure_capacity(v, v->size + 1);
    if (s != DS_OK) return s;
    if (i < v->size) {
        memmove(v->data + ((i + 1) * v->elem_size),
                v->data + (i * v->elem_size),
                (v->size - i) * v->elem_size);
    }
    memcpy(v->data + (i * v->elem_size), elem, v->elem_size);
    v->size++;
    return DS_OK;
}

ds_status ds_vector_remove(ds_vector *v, size_t i) {
    if (!v || i >= v->size) return DS_ERR_ARG;
    void *target = v->data + (i * v->elem_size);
    if (v->free_elem) {
        v->free_elem(target);
    }
    if (i + 1 < v->size) {
        memmove(target,
                v->data + ((i + 1) * v->elem_size),
                (v->size - i - 1) * v->elem_size);
    }
    v->size--;
    return DS_OK;
}

size_t ds_vector_size(const ds_vector *v) {
    return v ? v->size : 0;
}

void ds_vector_clear(ds_vector *v) {
    if (!v) return;
    if (v->free_elem) {
        for (size_t i = 0; i < v->size; i++) {
            v->free_elem(v->data + (i * v->elem_size));
        }
    }
    v->size = 0;
}

void ds_vector_destroy(ds_vector *v) {
    if (!v) return;
    ds_vector_clear(v);
    free(v->data);
    free(v);
}
