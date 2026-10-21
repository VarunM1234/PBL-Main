/*
 * PURPOSE: Union-Find implementation.
 * WHY THIS NAME: implements ds_unionfind.h.
 * CONNECTS TO: ds_common.
 * COMPLEXITY: near O(1).
 */
#include "ds_unionfind.h"

struct ds_unionfind { size_t n, sets; int *parent; unsigned char *rank; };

ds_unionfind *ds_uf_create(size_t n) {
    if (n == 0) return NULL;
    ds_unionfind *u = ds_xcalloc(1, sizeof *u);
    if (!u) return NULL;
    u->parent = ds_xmalloc(n * sizeof(int));
    u->rank = ds_xcalloc(n, 1);
    if (!u->parent || !u->rank) { ds_uf_destroy(u); return NULL; }
    for (size_t i = 0; i < n; i++) u->parent[i] = (int)i;
    u->n = n;
    u->sets = n;
    return u;
}

int ds_uf_find(ds_unionfind *u, int x) {
    if (!u || x < 0 || (size_t)x >= u->n) return -1;
    int root = x;
    while (u->parent[root] != root) root = u->parent[root];
    while (u->parent[x] != root) {                 /* path compression */
        int next = u->parent[x];
        u->parent[x] = root;
        x = next;
    }
    return root;
}

int ds_uf_unite(ds_unionfind *u, int a, int b) {
    int ra = ds_uf_find(u, a), rb = ds_uf_find(u, b);
    if (ra < 0 || rb < 0 || ra == rb) return 0;
    if (u->rank[ra] < u->rank[rb]) { int t = ra; ra = rb; rb = t; }
    u->parent[rb] = ra;                            /* shorter tree under taller tree */
    if (u->rank[ra] == u->rank[rb]) u->rank[ra]++;
    u->sets--;
    return 1;
}

int ds_uf_connected(ds_unionfind *u, int a, int b) {
    int ra = ds_uf_find(u, a), rb = ds_uf_find(u, b);
    return ra >= 0 && ra == rb;
}

size_t ds_uf_count(const ds_unionfind *u) { return u ? u->sets : 0; }

void ds_uf_destroy(ds_unionfind *u) {
    if (!u) return;
    free(u->parent);
    free(u->rank);
    free(u);
}
