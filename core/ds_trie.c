/*
 * PURPOSE: Trie implementation (see ds_trie.h).
 * WHY THIS NAME: implements ds_trie.h.
 * CONNECTS TO: ds_common.
 * COMPLEXITY: see header.
 */
#include "ds_trie.h"
#include <ctype.h>
#include <string.h>

#define TRIE_SLOTS 30

typedef struct trie_node {
    struct trie_node *child[TRIE_SLOTS];
    int is_word;
    int value;
} trie_node;

struct ds_trie { trie_node *root; };

/* Map a character to a slot 0..29, or -1 when it is not allowed. */
static int slot_of(char ch) {
    int c = tolower((unsigned char)ch);
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c == ' ') return 26;
    if (c == '-') return 27;
    if (c == '\'') return 28;
    if (c == '.') return 29;
    return -1;
}

static char char_of(int slot) {
    if (slot < 26) return (char)('a' + slot);
    static const char extra[4] = {' ', '-', '\'', '.'};
    return extra[slot - 26];
}

ds_trie *ds_trie_create(void) {
    ds_trie *t = ds_xcalloc(1, sizeof *t);
    if (!t) return NULL;
    t->root = ds_xcalloc(1, sizeof *t->root);
    if (!t->root) { free(t); return NULL; }
    return t;
}

ds_status ds_trie_insert(ds_trie *t, const char *word, int value) {
    if (!t || !word || !*word) return DS_ERR_ARG;
    trie_node *cur = t->root;
    for (const char *p = word; *p; p++) {
        int s = slot_of(*p);
        if (s < 0) return DS_ERR_ARG;
        if (!cur->child[s]) {
            cur->child[s] = ds_xcalloc(1, sizeof(trie_node));
            if (!cur->child[s]) return DS_ERR_NOMEM;
        }
        cur = cur->child[s];
    }
    cur->is_word = 1;
    cur->value = value;
    return DS_OK;
}

/* Walk down following the prefix; NULL if the path does not exist. */
static const trie_node *walk(const ds_trie *t, const char *s) {
    const trie_node *cur = t->root;
    for (const char *p = s; *p; p++) {
        int sl = slot_of(*p);
        if (sl < 0 || !cur->child[sl]) return NULL;
        cur = cur->child[sl];
    }
    return cur;
}

int ds_trie_find(const ds_trie *t, const char *word, int *value) {
    if (!t || !word) return 0;
    const trie_node *n = walk(t, word);
    if (!n || !n->is_word) return 0;
    if (value) *value = n->value;
    return 1;
}

typedef struct { size_t max, count; char (*out)[64]; int *values; } collect_ctx;

/* Depth-first collection; buf holds the word built so far. */
static void collect(const trie_node *n, char *buf, size_t len, collect_ctx *c) {
    if (c->count >= c->max) return;
    if (n->is_word) {
        memcpy(c->out[c->count], buf, len);
        c->out[c->count][len] = '\0';
        if (c->values) c->values[c->count] = n->value;
        c->count++;
    }
    for (int s = 0; s < TRIE_SLOTS && c->count < c->max; s++) {
        if (!n->child[s] || len >= 62) continue;
        buf[len] = char_of(s);
        collect(n->child[s], buf, len + 1, c);
    }
}

size_t ds_trie_suggest(const ds_trie *t, const char *prefix, size_t max,
                       char out[][64], int values[]) {
    if (!t || !prefix || !out || max == 0 || strlen(prefix) > 60) return 0;
    const trie_node *start = walk(t, prefix);
    if (!start) return 0;
    char buf[64];
    size_t len = 0;
    for (const char *p = prefix; *p; p++) buf[len++] = (char)tolower((unsigned char)*p);
    collect_ctx c = {max, 0, out, values};
    collect(start, buf, len, &c);
    return c.count;
}

static void free_node(trie_node *n) {
    if (!n) return;
    for (int i = 0; i < TRIE_SLOTS; i++) free_node(n->child[i]);
    free(n);
}

void ds_trie_destroy(ds_trie *t) {
    if (!t) return;
    free_node(t->root);
    free(t);
}
