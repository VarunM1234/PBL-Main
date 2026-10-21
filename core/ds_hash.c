/*
 * PURPOSE: Implementation of generic separate-chaining hash table.
 * WHY THIS NAME: ds_hash.c implements ds_hash.h.
 * CONNECTS TO: core/ds_hash.h
 * COMPLEXITY: O(1) average lookup, insertion, and deletion.
 */

#include "ds_hash.h"
#include <string.h>

static uint64_t fnv1a_str(const void *key) {
    const char *str = *(const char *const *)key;
    uint64_t h = 14695981039346656037ULL;
    if (!str) return h;
    while (*str) {
        h ^= (uint64_t)(unsigned char)(*str++);
        h *= 1099511628211ULL;
    }
    return h;
}

static int str_eq(const void *k1, const void *k2) {
    const char *s1 = *(const char *const *)k1;
    const char *s2 = *(const char *const *)k2;
    if (!s1 || !s2) return s1 == s2;
    return strcmp(s1, s2) == 0;
}

typedef struct ds_hash_node {
    void *key;
    void *value;
    struct ds_hash_node *next;
} ds_hash_node;

struct ds_hash {
    ds_hash_node **buckets;
    size_t num_buckets;
    size_t size;
    size_t key_size;
    size_t value_size;
    int is_str_key;
    uint64_t (*hash_fn)(const void *);
    int (*eq_fn)(const void *, const void *);
    void (*free_value)(void *);
};

ds_hash *ds_hash_create(size_t key_size, size_t value_size,
                        uint64_t (*hash)(const void *),
                        int (*eq)(const void *, const void *),
                        void (*free_value)(void *)) {
    if (key_size == 0 || !hash || !eq) return NULL;
    ds_hash *h = (ds_hash *)ds_xmalloc(sizeof(ds_hash));
    if (!h) return NULL;
    h->num_buckets = 16;
    h->size = 0;
    h->key_size = key_size;
    h->value_size = value_size;
    h->is_str_key = 0;
    h->hash_fn = hash;
    h->eq_fn = eq;
    h->free_value = free_value;
    h->buckets = (ds_hash_node **)ds_xcalloc(h->num_buckets, sizeof(ds_hash_node *));
    if (!h->buckets) {
        free(h);
        return NULL;
    }
    return h;
}

ds_hash *ds_hash_create_str(size_t value_size, void (*free_value)(void *)) {
    ds_hash *h = ds_hash_create(sizeof(char *), value_size, fnv1a_str, str_eq, free_value);
    if (h) {
        h->is_str_key = 1;
    }
    return h;
}

static ds_status ds_hash_rehash(ds_hash *h) {
    size_t new_num = h->num_buckets * 2;
    ds_hash_node **new_buckets = (ds_hash_node **)ds_xcalloc(new_num, sizeof(ds_hash_node *));
    if (!new_buckets) return DS_ERR_NOMEM;

    for (size_t i = 0; i < h->num_buckets; i++) {
        ds_hash_node *curr = h->buckets[i];
        while (curr) {
            ds_hash_node *next = curr->next;
            uint64_t hash_val = h->hash_fn(curr->key);
            size_t b = (size_t)(hash_val % new_num);
            curr->next = new_buckets[b];
            new_buckets[b] = curr;
            curr = next;
        }
    }
    free(h->buckets);
    h->buckets = new_buckets;
    h->num_buckets = new_num;
    return DS_OK;
}

ds_status ds_hash_put(ds_hash *h, const void *key, const void *value) {
    if (!h || !key) return DS_ERR_ARG;

    const void *lookup_key = h->is_str_key ? (const void *)&key : key;
    uint64_t hash_val = h->hash_fn(lookup_key);
    size_t b = (size_t)(hash_val % h->num_buckets);

    ds_hash_node *curr = h->buckets[b];
    while (curr) {
        if (h->eq_fn(curr->key, lookup_key)) {
            if (value && h->value_size > 0) {
                if (h->free_value) h->free_value(curr->value);
                memcpy(curr->value, value, h->value_size);
            }
            return DS_OK;
        }
        curr = curr->next;
    }

    if ((double)(h->size + 1) / (double)h->num_buckets > 0.75) {
        ds_hash_rehash(h);
        b = (size_t)(hash_val % h->num_buckets);
    }

    ds_hash_node *new_node = (ds_hash_node *)ds_xmalloc(sizeof(ds_hash_node));
    if (!new_node) return DS_ERR_NOMEM;

    if (h->is_str_key) {
        const char *src_str = (const char *)key;
        char *dup_str = (char *)ds_xmalloc(strlen(src_str) + 1);
        if (!dup_str) {
            free(new_node);
            return DS_ERR_NOMEM;
        }
        strcpy(dup_str, src_str);
        new_node->key = ds_xmalloc(sizeof(char *));
        if (!new_node->key) {
            free(dup_str);
            free(new_node);
            return DS_ERR_NOMEM;
        }
        *(char **)new_node->key = dup_str;
    } else {
        new_node->key = ds_xmalloc(h->key_size);
        if (!new_node->key) {
            free(new_node);
            return DS_ERR_NOMEM;
        }
        memcpy(new_node->key, key, h->key_size);
    }

    if (value && h->value_size > 0) {
        new_node->value = ds_xmalloc(h->value_size);
        if (!new_node->value) {
            if (h->is_str_key) free(*(char **)new_node->key);
            free(new_node->key);
            free(new_node);
            return DS_ERR_NOMEM;
        }
        memcpy(new_node->value, value, h->value_size);
    } else {
        new_node->value = NULL;
    }

    new_node->next = h->buckets[b];
    h->buckets[b] = new_node;
    h->size++;
    return DS_OK;
}

void *ds_hash_get(const ds_hash *h, const void *key) {
    if (!h || !key) return NULL;
    const void *lookup_key = h->is_str_key ? (const void *)&key : key;
    uint64_t hash_val = h->hash_fn(lookup_key);
    size_t b = (size_t)(hash_val % h->num_buckets);

    ds_hash_node *curr = h->buckets[b];
    while (curr) {
        if (h->eq_fn(curr->key, lookup_key)) {
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

int ds_hash_contains(const ds_hash *h, const void *key) {
    if (!h || !key) return 0;
    const void *lookup_key = h->is_str_key ? (const void *)&key : key;
    uint64_t hash_val = h->hash_fn(lookup_key);
    size_t b = (size_t)(hash_val % h->num_buckets);

    ds_hash_node *curr = h->buckets[b];
    while (curr) {
        if (h->eq_fn(curr->key, lookup_key)) {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

ds_status ds_hash_remove(ds_hash *h, const void *key) {
    if (!h || !key) return DS_ERR_ARG;
    const void *lookup_key = h->is_str_key ? (const void *)&key : key;
    uint64_t hash_val = h->hash_fn(lookup_key);
    size_t b = (size_t)(hash_val % h->num_buckets);

    ds_hash_node *curr = h->buckets[b];
    ds_hash_node *prev = NULL;
    while (curr) {
        if (h->eq_fn(curr->key, lookup_key)) {
            if (prev) {
                prev->next = curr->next;
            } else {
                h->buckets[b] = curr->next;
            }
            if (h->free_value && curr->value) {
                h->free_value(curr->value);
            }
            if (h->is_str_key) {
                free(*(char **)curr->key);
            }
            free(curr->key);
            free(curr->value);
            free(curr);
            h->size--;
            return DS_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    return DS_ERR_NOTFOUND;
}

size_t ds_hash_size(const ds_hash *h) {
    return h ? h->size : 0;
}

void ds_hash_foreach(const ds_hash *h, void (*fn)(const void *key, void *value, void *ctx), void *ctx) {
    if (!h || !fn) return;
    for (size_t i = 0; i < h->num_buckets; i++) {
        ds_hash_node *curr = h->buckets[i];
        while (curr) {
            const void *k = h->is_str_key ? *(const char *const *)curr->key : curr->key;
            fn(k, curr->value, ctx);
            curr = curr->next;
        }
    }
}

void ds_hash_destroy(ds_hash *h) {
    if (!h) return;
    for (size_t i = 0; i < h->num_buckets; i++) {
        ds_hash_node *curr = h->buckets[i];
        while (curr) {
            ds_hash_node *next = curr->next;
            if (h->free_value && curr->value) {
                h->free_value(curr->value);
            }
            if (h->is_str_key) {
                free(*(char **)curr->key);
            }
            free(curr->key);
            free(curr->value);
            free(curr);
            curr = next;
        }
    }
    free(h->buckets);
    free(h);
}
