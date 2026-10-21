/*
 * PURPOSE: Graph, Dijkstra and BFS implementation.
 * WHY THIS NAME: implements ds_graph.h.
 * CONNECTS TO: ds_heap, ds_queue.
 * COMPLEXITY: see header.
 */
#include "ds_graph.h"
#include "ds_heap.h"
#include "ds_queue.h"

typedef struct { int to; int id; int64_t w_min, w_paise; } edge_t;
typedef struct { edge_t *items; size_t count, cap; } adj_t;
struct ds_graph { size_t n; adj_t *adj; };

ds_graph *ds_graph_create(size_t n) {
    if (n == 0) return NULL;
    ds_graph *g = ds_xcalloc(1, sizeof *g);
    if (!g) return NULL;
    g->adj = ds_xcalloc(n, sizeof(adj_t));
    if (!g->adj) { free(g); return NULL; }
    g->n = n;
    return g;
}

size_t ds_graph_size(const ds_graph *g) { return g ? g->n : 0; }

ds_status ds_graph_add_edge(ds_graph *g, int u, int v, int64_t wm, int64_t wp, int id) {
    if (!g || u < 0 || v < 0 || (size_t)u >= g->n || (size_t)v >= g->n || wm < 0 || wp < 0)
        return DS_ERR_ARG;
    adj_t *a = &g->adj[u];
    if (a->count == a->cap) {                       /* grow the neighbour array */
        size_t ncap = a->cap ? a->cap * 2 : 4;
        edge_t *p = ds_xrealloc(a->items, ncap * sizeof(edge_t));
        if (!p) return DS_ERR_NOMEM;
        a->items = p;
        a->cap = ncap;
    }
    a->items[a->count++] = (edge_t){v, id, wm, wp};
    return DS_OK;
}

/* Try to improve vertex v through edge e from u; keeps heap and arrays in sync. */
static void relax(ds_heap *heap, const edge_t *e, int u, int src, int use_paise,
                  int64_t layover, int64_t *dist, int *pv, int *pe) {
    int64_t w = use_paise ? e->w_paise : e->w_min;
    int64_t cand = dist[u] + w + (u != src ? layover : 0);
    if (cand >= dist[e->to]) return;
    int was_inf = (dist[e->to] == DS_INF);
    dist[e->to] = cand;
    pv[e->to] = u;
    pe[e->to] = e->id;
    if (was_inf) ds_heap_push(heap, cand, (uint64_t)e->to);
    else ds_heap_update(heap, (uint64_t)e->to, cand);
}

ds_status ds_graph_dijkstra(const ds_graph *g, int src, int use_paise, int64_t layover,
                            int64_t *dist, int *pv, int *pe) {
    if (!g || src < 0 || (size_t)src >= g->n || !dist || !pv || !pe) return DS_ERR_ARG;
    for (size_t i = 0; i < g->n; i++) { dist[i] = DS_INF; pv[i] = -1; pe[i] = -1; }
    ds_heap *heap = ds_heap_create();
    if (!heap) return DS_ERR_NOMEM;
    dist[src] = 0;
    ds_heap_push(heap, 0, (uint64_t)src);
    int64_t prio; uint64_t id;
    while (ds_heap_pop_min(heap, &prio, &id) == DS_OK) {
        int u = (int)id;
        for (size_t k = 0; k < g->adj[u].count; k++)
            relax(heap, &g->adj[u].items[k], u, src, use_paise, layover, dist, pv, pe);
    }
    ds_heap_destroy(heap);
    return DS_OK;
}

ds_status ds_graph_bfs_fewest_legs(const ds_graph *g, int src, int *legs, int *pv, int *pe) {
    if (!g || src < 0 || (size_t)src >= g->n || !legs || !pv || !pe) return DS_ERR_ARG;
    for (size_t i = 0; i < g->n; i++) { legs[i] = -1; pv[i] = -1; pe[i] = -1; }
    ds_queue *q = ds_queue_create(sizeof(int), NULL);
    if (!q) return DS_ERR_NOMEM;
    legs[src] = 0;
    ds_queue_enqueue(q, &src);
    int u;
    while (ds_queue_dequeue(q, &u) == DS_OK) {
        for (size_t k = 0; k < g->adj[u].count; k++) {
            const edge_t *e = &g->adj[u].items[k];
            if (legs[e->to] != -1) continue;
            legs[e->to] = legs[u] + 1;
            pv[e->to] = u;
            pe[e->to] = e->id;
            ds_queue_enqueue(q, &e->to);
        }
    }
    ds_queue_destroy(q);
    return DS_OK;
}

size_t ds_graph_path(const int *pv, int src, int dst, int *out, size_t cap) {
    if (!pv || !out || src < 0 || dst < 0) return 0;
    size_t len = 0;
    for (int v = dst; v != -1; v = pv[v]) {          /* walk back from dst to src */
        if (len >= cap) return 0;
        out[len++] = v;
        if (v == src) break;
    }
    if (len == 0 || out[len - 1] != src) return 0;   /* never reached src */
    for (size_t i = 0; i < len / 2; i++) {           /* reverse into src..dst order */
        int t = out[i]; out[i] = out[len - 1 - i]; out[len - 1 - i] = t;
    }
    return len;
}

void ds_graph_destroy(ds_graph *g) {
    if (!g) return;
    for (size_t i = 0; i < g->n; i++) free(g->adj[i].items);
    free(g->adj);
    free(g);
}
