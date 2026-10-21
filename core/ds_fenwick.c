/*
 * PURPOSE: Fenwick tree implementation.
 * WHY THIS NAME: implements ds_fenwick.h.
 * CONNECTS TO: ds_common.
 * COMPLEXITY: O(log n) per operation.
 */
#include "ds_fenwick.h"

struct ds_fenwick { size_t n; int64_t *tree; };

ds_fenwick *ds_fenwick_create(size_t n) {
    if (n == 0) return NULL;
    ds_fenwick *f = ds_xcalloc(1, sizeof *f);
    if (!f) return NULL;
    f->tree = ds_xcalloc(n + 1, sizeof(int64_t));
    if (!f->tree) { free(f); return NULL; }
    f->n = n;
    return f;
}

void ds_fenwick_add(ds_fenwick *f, size_t i, int64_t delta) {
    if (!f || i >= f->n) return;
    for (size_t k = i + 1; k <= f->n; k += k & (~k + 1)) f->tree[k] += delta;
}

int64_t ds_fenwick_prefix(const ds_fenwick *f, size_t i) {
    if (!f) return 0;
    if (i >= f->n) i = f->n - 1;
    int64_t sum = 0;
    for (size_t k = i + 1; k > 0; k -= k & (~k + 1)) sum += f->tree[k];
    return sum;
}

int64_t ds_fenwick_range(const ds_fenwick *f, size_t l, size_t r) {
    if (!f || l > r || l >= f->n) return 0;
    int64_t hi = ds_fenwick_prefix(f, r);
    int64_t lo = (l == 0) ? 0 : ds_fenwick_prefix(f, l - 1);
    return hi - lo;
}

void ds_fenwick_destroy(ds_fenwick *f) {
    if (!f) return;
    free(f->tree);
    free(f);
}
