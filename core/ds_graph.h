/*
 * PURPOSE: Weighted directed graph (adjacency lists) with Dijkstra and BFS.
 * WHY THIS NAME: cities are vertices, bus routes are directed edges.
 * CONNECTS TO: app/search (journey planner), ds_heap (priority queue for Dijkstra).
 * COMPLEXITY: Dijkstra O((V+E) log V); BFS O(V+E); path rebuild O(path length).
 * INVARIANTS: every edge has u,v in [0,n); weights >= 0 (needed for Dijkstra).
 */
#ifndef SMARTROUTE_DS_GRAPH_H
#define SMARTROUTE_DS_GRAPH_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ds_graph ds_graph;
#define DS_INF INT64_C(0x3fffffffffffffff)
ds_graph *ds_graph_create(size_t n_vertices);
ds_status ds_graph_add_edge(ds_graph *g, int u, int v, int64_t w_minutes, int64_t w_paise, int edge_id);
/* dist[], prev_vertex[], prev_edge[] must each hold n_vertices entries.
 * use_paise=0: weight = minutes; 1: weight = paise.
 * layover is added for every leg that starts at a vertex other than src. */
ds_status ds_graph_dijkstra(const ds_graph *g, int src, int use_paise, int64_t layover,
                            int64_t *dist, int *prev_vertex, int *prev_edge);
ds_status ds_graph_bfs_fewest_legs(const ds_graph *g, int src, int *legs,
                                   int *prev_vertex, int *prev_edge);
/* Writes src..dst into out_path; returns length or 0 when unreachable. */
size_t ds_graph_path(const int *prev_vertex, int src, int dst, int *out_path, size_t cap);
size_t ds_graph_size(const ds_graph *g);
void ds_graph_destroy(ds_graph *g);
#ifdef __cplusplus
}
#endif
#endif
