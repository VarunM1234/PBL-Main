/*
 * PURPOSE: Prefix tree (trie) for city autosuggest: "Del" -> Delhi.
 * WHY THIS NAME: a "trie" (from re-TRIE-val) stores words letter by letter.
 * CONNECTS TO: app/catalog (city names), app/cli (search prompts).
 * COMPLEXITY: insert/find O(L), suggest O(L + k) (L = word length, k = output size).
 * INVARIANTS: children[c] is NULL or a node whose path spells the prefix;
 *             suggestions are produced by depth-first walk in slot order = alphabetical.
 * Alphabet (32 slots): a-z, space, hyphen, apostrophe, dot. Case-insensitive.
 */
#ifndef SMARTROUTE_DS_TRIE_H
#define SMARTROUTE_DS_TRIE_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ds_trie ds_trie;
ds_trie *ds_trie_create(void);
ds_status ds_trie_insert(ds_trie *t, const char *word, int value);
int ds_trie_find(const ds_trie *t, const char *word, int *value);          /* 1 found, 0 not */
size_t ds_trie_suggest(const ds_trie *t, const char *prefix, size_t max,
                       char out[][64], int values[]);
void ds_trie_destroy(ds_trie *t);
#ifdef __cplusplus
}
#endif
#endif
