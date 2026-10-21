/*
 * PURPOSE: Implementation of indexed binary min-heap.
 * WHY THIS NAME: ds_heap.c implements ds_heap.h.
 * CONNECTS TO: core/ds_heap.h
 * COMPLEXITY: O(log n) push, pop, update, remove (the id->position map doubles at load factor 0.75, so lookups stay O(1) average).
 */

#include "ds_heap.h"
#include <string.h>

typedef struct {
    int64_t priority;
    uint64_t id;
} ds_heap_entry;

typedef struct ds_pos_entry {
    uint64_t id;
    size_t pos;
    struct ds_pos_entry *next;
} ds_pos_entry;

struct ds_heap {
    ds_heap_entry *tree;
    size_t size;
    size_t capacity;

    ds_pos_entry **pos_map;
    size_t map_buckets;
    size_t map_count;            /* entries in pos_map; load factor kept <= 0.75 */
};

static uint64_t hash_id(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static size_t *find_pos_ptr(const ds_heap *h, uint64_t id) {
    size_t b = (size_t)(hash_id(id) % h->map_buckets);
    ds_pos_entry *curr = h->pos_map[b];
    while (curr) {
        if (curr->id == id) return &curr->pos;
        curr = curr->next;
    }
    return NULL;
}

/* Double the bucket array and re-link every entry. On allocation failure the old table is kept (slower, still correct). */
static void map_grow(ds_heap *h) {
    size_t nb = h->map_buckets * 2;
    ds_pos_entry **nm = (ds_pos_entry **)ds_xcalloc(nb, sizeof(ds_pos_entry *));
    if (!nm) return;
    for (size_t i = 0; i < h->map_buckets; i++) {
        ds_pos_entry *e = h->pos_map[i];
        while (e) {
            ds_pos_entry *next = e->next;
            size_t b = (size_t)(hash_id(e->id) % nb);
            e->next = nm[b];
            nm[b] = e;
            e = next;
        }
    }
    free(h->pos_map);
    h->pos_map = nm;
    h->map_buckets = nb;
}

static int map_set(ds_heap *h, uint64_t id, size_t pos) {
    size_t b = (size_t)(hash_id(id) % h->map_buckets);
    ds_pos_entry *curr = h->pos_map[b];
    while (curr) {
        if (curr->id == id) {
            curr->pos = pos;
            return 1;
        }
        curr = curr->next;
    }
    if (h->map_count + 1 > h->map_buckets * 3 / 4) { map_grow(h); b = (size_t)(hash_id(id) % h->map_buckets); }
    ds_pos_entry *new_entry = (ds_pos_entry *)ds_xmalloc(sizeof(ds_pos_entry));
    if (!new_entry) return 0;
    new_entry->id = id;
    new_entry->pos = pos;
    new_entry->next = h->pos_map[b];
    h->pos_map[b] = new_entry;
    h->map_count++;
    return 1;
}

static void map_remove(ds_heap *h, uint64_t id) {
    size_t b = (size_t)(hash_id(id) % h->map_buckets);
    ds_pos_entry *curr = h->pos_map[b];
    ds_pos_entry *prev = NULL;
    while (curr) {
        if (curr->id == id) {
            if (prev) prev->next = curr->next;
            else h->pos_map[b] = curr->next;
            free(curr);
            h->map_count--;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

ds_heap *ds_heap_create(void) {
    ds_heap *h = (ds_heap *)ds_xmalloc(sizeof(ds_heap));
    if (!h) return NULL;
    h->size = 0;
    h->capacity = 16;
    h->tree = (ds_heap_entry *)ds_xmalloc(h->capacity * sizeof(ds_heap_entry));
    if (!h->tree) {
        free(h);
        return NULL;
    }
    h->map_buckets = 64;
    h->map_count = 0;
    h->pos_map = (ds_pos_entry **)ds_xcalloc(h->map_buckets, sizeof(ds_pos_entry *));
    if (!h->pos_map) {
        free(h->tree);
        free(h);
        return NULL;
    }
    return h;
}

static void swap_nodes(ds_heap *h, size_t i, size_t j) {
    ds_heap_entry tmp = h->tree[i];
    h->tree[i] = h->tree[j];
    h->tree[j] = tmp;
    map_set(h, h->tree[i].id, i);
    map_set(h, h->tree[j].id, j);
}

static void sift_up(ds_heap *h, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (h->tree[idx].priority < h->tree[parent].priority) {
            swap_nodes(h, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

static void sift_down(ds_heap *h, size_t idx) {
    while (2 * idx + 1 < h->size) {
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        size_t smallest = idx;

        if (h->tree[left].priority < h->tree[smallest].priority) {
            smallest = left;
        }
        if (right < h->size && h->tree[right].priority < h->tree[smallest].priority) {
            smallest = right;
        }

        if (smallest != idx) {
            swap_nodes(h, idx, smallest);
            idx = smallest;
        } else {
            break;
        }
    }
}

ds_status ds_heap_push(ds_heap *h, int64_t priority, uint64_t id) {
    if (!h) return DS_ERR_ARG;
    if (find_pos_ptr(h, id) != NULL) return DS_ERR_EXISTS;

    if (h->size >= h->capacity) {
        size_t new_cap = h->capacity * 2;
        ds_heap_entry *new_tree = (ds_heap_entry *)ds_xrealloc(h->tree, new_cap * sizeof(ds_heap_entry));
        if (!new_tree) return DS_ERR_NOMEM;
        h->tree = new_tree;
        h->capacity = new_cap;
    }

    size_t idx = h->size++;
    h->tree[idx].priority = priority;
    h->tree[idx].id = id;
    if (!map_set(h, id, idx)) { h->size--; return DS_ERR_NOMEM; }   /* could not index the new id: undo */
    sift_up(h, idx);
    return DS_OK;
}

ds_status ds_heap_peek_min(const ds_heap *h, int64_t *priority, uint64_t *id) {
    if (!h) return DS_ERR_ARG;
    if (h->size == 0) return DS_ERR_EMPTY;
    if (priority) *priority = h->tree[0].priority;
    if (id) *id = h->tree[0].id;
    return DS_OK;
}

ds_status ds_heap_pop_min(ds_heap *h, int64_t *priority, uint64_t *id) {
    if (!h) return DS_ERR_ARG;
    if (h->size == 0) return DS_ERR_EMPTY;
    if (priority) *priority = h->tree[0].priority;
    if (id) *id = h->tree[0].id;

    map_remove(h, h->tree[0].id);

    if (h->size == 1) {
        h->size = 0;
        return DS_OK;
    }

    h->tree[0] = h->tree[h->size - 1];
    h->size--;
    map_set(h, h->tree[0].id, 0);
    sift_down(h, 0);
    return DS_OK;
}

ds_status ds_heap_update(ds_heap *h, uint64_t id, int64_t new_priority) {
    if (!h) return DS_ERR_ARG;
    size_t *pos_ptr = find_pos_ptr(h, id);
    if (!pos_ptr) return DS_ERR_NOTFOUND;
    size_t idx = *pos_ptr;
    int64_t old_prio = h->tree[idx].priority;
    h->tree[idx].priority = new_priority;
    if (new_priority < old_prio) {
        sift_up(h, idx);
    } else if (new_priority > old_prio) {
        sift_down(h, idx);
    }
    return DS_OK;
}

ds_status ds_heap_remove(ds_heap *h, uint64_t id) {
    if (!h) return DS_ERR_ARG;
    size_t *pos_ptr = find_pos_ptr(h, id);
    if (!pos_ptr) return DS_ERR_NOTFOUND;
    size_t idx = *pos_ptr;

    map_remove(h, id);

    if (idx == h->size - 1) {
        h->size--;
        return DS_OK;
    }

    int64_t old_prio = h->tree[idx].priority;
    h->tree[idx] = h->tree[h->size - 1];
    h->size--;
    map_set(h, h->tree[idx].id, idx);

    if (h->tree[idx].priority < old_prio) {
        sift_up(h, idx);
    } else {
        sift_down(h, idx);
    }
    return DS_OK;
}

size_t ds_heap_size(const ds_heap *h) {
    return h ? h->size : 0;
}

void ds_heap_destroy(ds_heap *h) {
    if (!h) return;
    for (size_t i = 0; i < h->map_buckets; i++) {
        ds_pos_entry *curr = h->pos_map[i];
        while (curr) {
            ds_pos_entry *next = curr->next;
            free(curr);
            curr = next;
        }
    }
    free(h->pos_map);
    free(h->tree);
    free(h);
}
