/*
 * PURPOSE: LRU cache implementation.
 * WHY THIS NAME: implements ds_lru.h.
 * CONNECTS TO: ds_hash, ds_list.
 * COMPLEXITY: O(1) average.
 */
#include "ds_lru.h"
#include "ds_hash.h"
#include "ds_list.h"
#include <string.h>

#define LRU_KEY_MAX 64

struct ds_lru {
    size_t capacity, value_size;
    void (*free_value)(void *);
    ds_hash *index;      /* key -> ds_list_node* */
    ds_list *recency;    /* entries: [key[64]][value bytes] */
};

ds_lru *ds_lru_create(size_t capacity, size_t value_size, void (*free_value)(void *)) {
    if (capacity == 0 || value_size == 0) return NULL;
    ds_lru *c = ds_xcalloc(1, sizeof *c);
    if (!c) return NULL;
    c->capacity = capacity;
    c->value_size = value_size;
    c->free_value = free_value;
    c->index = ds_hash_create_str(sizeof(ds_list_node *), NULL);
    c->recency = ds_list_create(LRU_KEY_MAX + value_size, NULL);
    if (!c->index || !c->recency) { ds_lru_destroy(c); return NULL; }
    return c;
}

/* Release the value inside a list entry, then drop the entry from both structures. */
static void drop_node(ds_lru *c, ds_list_node *node) {
    unsigned char *entry = ds_list_data(node);
    char key[LRU_KEY_MAX];
    memcpy(key, entry, LRU_KEY_MAX);
    if (c->free_value) c->free_value(entry + LRU_KEY_MAX);
    ds_hash_remove(c->index, key);
    ds_list_remove(c->recency, node);
}

/* Move a node to the front by re-inserting a copy (keeps list API small). */
static ds_list_node *touch(ds_lru *c, ds_list_node *node) {
    unsigned char *old = ds_list_data(node);
    unsigned char *copy = ds_xmalloc(LRU_KEY_MAX + c->value_size);
    if (!copy) return node;
    memcpy(copy, old, LRU_KEY_MAX + c->value_size);
    ds_list_node *fresh = ds_list_push_front(c->recency, copy);
    free(copy);
    if (!fresh) return node;
    ds_list_remove(c->recency, node);       /* value bytes were moved, not freed */
    ds_hash_put(c->index, (char *)ds_list_data(fresh), &fresh);
    return fresh;
}

void *ds_lru_get(ds_lru *c, const char *key) {
    if (!c || !key || strlen(key) >= LRU_KEY_MAX) return NULL;
    ds_list_node **slot = ds_hash_get(c->index, key);
    if (!slot) return NULL;
    ds_list_node *node = touch(c, *slot);
    return (unsigned char *)ds_list_data(node) + LRU_KEY_MAX;
}

ds_status ds_lru_put(ds_lru *c, const char *key, const void *value) {
    if (!c || !key || !value || strlen(key) >= LRU_KEY_MAX) return DS_ERR_ARG;
    ds_list_node **slot = ds_hash_get(c->index, key);
    if (slot) drop_node(c, *slot);                         /* overwrite = replace */
    while (ds_list_size(c->recency) >= c->capacity) drop_node(c, ds_list_back(c->recency));
    unsigned char *entry = ds_xcalloc(1, LRU_KEY_MAX + c->value_size);
    if (!entry) return DS_ERR_NOMEM;
    strncpy((char *)entry, key, LRU_KEY_MAX - 1);
    memcpy(entry + LRU_KEY_MAX, value, c->value_size);
    ds_list_node *node = ds_list_push_front(c->recency, entry);
    free(entry);
    if (!node) return DS_ERR_NOMEM;
    return ds_hash_put(c->index, key, &node);
}

size_t ds_lru_size(const ds_lru *c) { return c ? ds_list_size(c->recency) : 0; }

void ds_lru_destroy(ds_lru *c) {
    if (!c) return;
    if (c->recency) {
        while (ds_list_front(c->recency)) drop_node(c, ds_list_front(c->recency));
    }
    ds_hash_destroy(c->index);
    ds_list_destroy(c->recency);
    free(c);
}
