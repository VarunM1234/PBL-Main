/*
 * PURPOSE: Search algorithm implementations.
 * WHY THIS NAME: implements ds_search.h.
 * CONNECTS TO: ds_common.
 * COMPLEXITY: see header.
 */
#include "ds_search.h"
#include <ctype.h>
#include <string.h>

size_t ds_lower_bound(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *)) {
    size_t lo = 0, hi = n;                          /* answer lies in [lo, hi] */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp((const unsigned char *)base + mid * size, key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

size_t ds_upper_bound(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *)) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp((const unsigned char *)base + mid * size, key) <= 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

long ds_binary_search(const void *base, size_t n, size_t size, const void *key,
                      int (*cmp)(const void *, const void *)) {
    size_t i = ds_lower_bound(base, n, size, key, cmp);
    if (i < n && cmp((const unsigned char *)base + i * size, key) == 0) return (long)i;
    return -1;
}

void ds_kmp_failure(const char *p, int *fail) {
    size_t m = strlen(p);
    if (m == 0) return;
    fail[0] = 0;
    int k = 0;                                       /* length of the current border */
    for (size_t i = 1; i < m; i++) {
        while (k > 0 && p[i] != p[k]) k = fail[k - 1];
        if (p[i] == p[k]) k++;
        fail[i] = k;
    }
}

size_t ds_kmp_find_all(const char *text, const char *pat, size_t *out, size_t cap) {
    size_t m = strlen(pat), n = strlen(text), found = 0;
    if (m == 0 || m > n) return 0;
    int *fail = ds_xmalloc(m * sizeof(int));
    if (!fail) return 0;
    ds_kmp_failure(pat, fail);
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        while (k > 0 && text[i] != pat[k]) k = fail[k - 1];
        if (text[i] == pat[k]) k++;
        if ((size_t)k == m) {
            if (out && found < cap) out[found] = i + 1 - m;
            found++;
            k = fail[k - 1];                         /* allow overlapping matches */
        }
    }
    free(fail);
    return found;
}

long ds_kmp_find(const char *text, const char *pat) {
    size_t first = 0;
    size_t cnt = ds_kmp_find_all(text, pat, &first, 1);
    return cnt ? (long)first : -1;
}

int ds_kmp_contains_nocase(const char *text, const char *pat) {
    size_t n = strlen(text), m = strlen(pat);
    if (m == 0) return 1;
    char *t = ds_xmalloc(n + 1), *p = ds_xmalloc(m + 1);
    int result = 0;
    if (t && p) {
        for (size_t i = 0; i <= n; i++) t[i] = (char)tolower((unsigned char)text[i]);
        for (size_t i = 0; i <= m; i++) p[i] = (char)tolower((unsigned char)pat[i]);
        result = ds_kmp_find(t, p) >= 0;
    }
    free(t);
    free(p);
    return result;
}
