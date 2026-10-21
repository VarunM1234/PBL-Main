/*
 * PURPOSE: Union-Find (disjoint sets) with path compression and union by rank.
 * WHY THIS NAME: two operations: find a set's leader, union two sets.
 * CONNECTS TO: app/catalog ("is there any route A->B?", count of bus networks).
 * COMPLEXITY: near O(1) amortised (inverse Ackermann).
 * INVARIANTS: parent[root] == root; rank only grows at roots.
 */
#ifndef SMARTROUTE_DS_UNIONFIND_H
#define SMARTROUTE_DS_UNIONFIND_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ds_unionfind ds_unionfind;
ds_unionfind *ds_uf_create(size_t n);
int ds_uf_find(ds_unionfind *u, int x);
int ds_uf_unite(ds_unionfind *u, int a, int b);       /* 1 if merged, 0 if already joined */
int ds_uf_connected(ds_unionfind *u, int a, int b);
size_t ds_uf_count(const ds_unionfind *u);            /* number of separate sets */
void ds_uf_destroy(ds_unionfind *u);
#ifdef __cplusplus
}
#endif
#endif
