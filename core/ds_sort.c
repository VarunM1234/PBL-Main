/*
 * PURPOSE: Sorting implementations.
 * WHY THIS NAME: implements ds_sort.h.
 * CONNECTS TO: ds_common.
 * COMPLEXITY: see header.
 */
#include "ds_sort.h"
#include <string.h>

#define AT(base, i, size) ((unsigned char *)(base) + (size_t)(i) * (size))

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size) {
    unsigned char tmp[64];
    while (size > 0) {                       /* swap in chunks of up to 64 bytes */
        size_t chunk = size < sizeof tmp ? size : sizeof tmp;
        memcpy(tmp, a, chunk); memcpy(a, b, chunk); memcpy(b, tmp, chunk);
        a += chunk; b += chunk; size -= chunk;
    }
}

/* ---------- merge sort (stable) ---------- */
static void merge_range(unsigned char *a, unsigned char *tmp, size_t n, size_t size, ds_cmp_fn cmp) {
    if (n < 2) return;
    size_t mid = n / 2;
    merge_range(a, tmp, mid, size, cmp);
    merge_range(a + mid * size, tmp, n - mid, size, cmp);
    size_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {               /* <= 0 keeps the left element first: stable */
        if (cmp(a + i * size, a + j * size) <= 0) memcpy(tmp + k++ * size, a + i++ * size, size);
        else memcpy(tmp + k++ * size, a + j++ * size, size);
    }
    while (i < mid) memcpy(tmp + k++ * size, a + i++ * size, size);
    while (j < n) memcpy(tmp + k++ * size, a + j++ * size, size);
    memcpy(a, tmp, n * size);
}

ds_status ds_merge_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp) {
    if (!cmp || size == 0 || (!base && n)) return DS_ERR_ARG;
    if (n < 2) return DS_OK;
    unsigned char *tmp = ds_xmalloc(n * size);
    if (!tmp) return DS_ERR_NOMEM;
    merge_range(base, tmp, n, size, cmp);
    free(tmp);
    return DS_OK;
}

/* ---------- quicksort (median of three, insertion sort for small ranges) ---------- */
static void insertion(unsigned char *a, size_t n, size_t size, ds_cmp_fn cmp) {
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && cmp(a + (j - 1) * size, a + j * size) > 0; j--)
            swap_bytes(a + (j - 1) * size, a + j * size, size);
}

/* Order a[lo], a[mid], a[hi] so the median ends in the middle slot. */
static void median_of_three(unsigned char *a, size_t lo, size_t hi, size_t size, ds_cmp_fn cmp) {
    size_t mid = lo + (hi - lo) / 2;
    if (cmp(AT(a, mid, size), AT(a, lo, size)) < 0) swap_bytes(AT(a, mid, size), AT(a, lo, size), size);
    if (cmp(AT(a, hi, size), AT(a, lo, size)) < 0) swap_bytes(AT(a, hi, size), AT(a, lo, size), size);
    if (cmp(AT(a, hi, size), AT(a, mid, size)) < 0) swap_bytes(AT(a, hi, size), AT(a, mid, size), size);
}

/* Hoare-style partition around the median stored at hi-1. Returns the pivot's final index. */
static size_t partition(unsigned char *a, size_t lo, size_t hi, size_t size, ds_cmp_fn cmp) {
    size_t mid = lo + (hi - lo) / 2;
    median_of_three(a, lo, hi, size, cmp);
    swap_bytes(AT(a, mid, size), AT(a, hi - 1, size), size);
    unsigned char *pivot = AT(a, hi - 1, size);
    size_t i = lo, j = hi - 1;
    for (;;) {
        while (cmp(AT(a, ++i, size), pivot) < 0) {}
        while (j > lo && cmp(AT(a, --j, size), pivot) > 0) {}
        if (i >= j) break;
        swap_bytes(AT(a, i, size), AT(a, j, size), size);
    }
    swap_bytes(AT(a, i, size), AT(a, hi - 1, size), size);
    return i;
}

void ds_quick_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp) {
    if (!base || !cmp || size == 0 || n < 2) return;
    unsigned char *a = base;
    size_t lo = 0, hi = n - 1;
    while (hi > lo) {                        /* loop on the bigger side, recurse on the smaller */
        if (hi - lo + 1 <= 16) { insertion(AT(a, lo, size), hi - lo + 1, size, cmp); return; }
        size_t p = partition(a, lo, hi, size, cmp);
        if (p - lo < hi - p) { if (p > lo) ds_quick_sort(AT(a, lo, size), p - lo, size, cmp); lo = p + 1; }
        else { ds_quick_sort(AT(a, p + 1, size), hi - p, size, cmp); if (p == 0) return; hi = p - 1; }
    }
}

/* ---------- heapsort ---------- */
static void sift_down(unsigned char *a, size_t root, size_t n, size_t size, ds_cmp_fn cmp) {
    for (;;) {
        size_t big = root, l = 2 * root + 1, r = l + 1;
        if (l < n && cmp(AT(a, l, size), AT(a, big, size)) > 0) big = l;
        if (r < n && cmp(AT(a, r, size), AT(a, big, size)) > 0) big = r;
        if (big == root) return;
        swap_bytes(AT(a, root, size), AT(a, big, size), size);
        root = big;
    }
}

void ds_heap_sort(void *base, size_t n, size_t size, ds_cmp_fn cmp) {
    if (!base || !cmp || size == 0 || n < 2) return;
    unsigned char *a = base;
    for (size_t i = n / 2; i-- > 0;) sift_down(a, i, n, size, cmp);   /* build max-heap */
    for (size_t end = n - 1; end > 0; end--) {
        swap_bytes(a, AT(a, end, size), size);                        /* max goes to the back */
        sift_down(a, 0, end, size, cmp);
    }
}

int ds_is_sorted(const void *base, size_t n, size_t size, ds_cmp_fn cmp) {
    for (size_t i = 1; i < n; i++)
        if (cmp(AT(base, i - 1, size), AT(base, i, size)) > 0) return 0;
    return 1;
}
