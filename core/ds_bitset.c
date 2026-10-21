/*
 * PURPOSE: Implementation of uint64_t word bitset.
 * WHY THIS NAME: ds_bitset.c implements ds_bitset.h.
 * CONNECTS TO: core/ds_bitset.h
 * COMPLEXITY: O(1) bit access, O(n/64) popcount, O(range) run scan.
 */

#include "ds_bitset.h"
#include <string.h>
#include <stdio.h>

struct ds_bitset {
    uint64_t *words;
    size_t nbits;
    size_t nwords;
};

static inline size_t popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (size_t)__builtin_popcountll(x);
#else
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (size_t)((x * 0x0101010101010101ULL) >> 56);
#endif
}

ds_bitset *ds_bitset_create(size_t nbits) {
    ds_bitset *bs = (ds_bitset *)ds_xmalloc(sizeof(ds_bitset));
    if (!bs) return NULL;
    bs->nbits = nbits;
    bs->nwords = (nbits + 63) / 64;
    if (bs->nwords == 0) bs->nwords = 1;
    bs->words = (uint64_t *)ds_xcalloc(bs->nwords, sizeof(uint64_t));
    if (!bs->words) {
        free(bs);
        return NULL;
    }
    return bs;
}

ds_status ds_bitset_set(ds_bitset *bs, size_t i) {
    if (!bs || i >= bs->nbits) return DS_ERR_ARG;
    bs->words[i / 64] |= (1ULL << (i % 64));
    return DS_OK;
}

ds_status ds_bitset_clear(ds_bitset *bs, size_t i) {
    if (!bs || i >= bs->nbits) return DS_ERR_ARG;
    bs->words[i / 64] &= ~(1ULL << (i % 64));
    return DS_OK;
}

int ds_bitset_test(const ds_bitset *bs, size_t i) {
    if (!bs || i >= bs->nbits) return 0;
    return (bs->words[i / 64] & (1ULL << (i % 64))) != 0;
}

ds_status ds_bitset_flip(ds_bitset *bs, size_t i) {
    if (!bs || i >= bs->nbits) return DS_ERR_ARG;
    bs->words[i / 64] ^= (1ULL << (i % 64));
    return DS_OK;
}

size_t ds_bitset_count(const ds_bitset *bs) {
    if (!bs) return 0;
    size_t count = 0;
    for (size_t i = 0; i < bs->nwords; i++) {
        count += popcount64(bs->words[i]);
    }
    return count;
}

long ds_bitset_find_clear_run(const ds_bitset *bs, size_t from, size_t to, size_t k) {
    if (!bs || k == 0 || from >= to || to > bs->nbits) return -1;
    if ((to - from) < k) return -1;

    size_t run_len = 0;
    size_t start_idx = from;

    for (size_t i = from; i < to; i++) {
        if (!ds_bitset_test(bs, i)) {
            if (run_len == 0) start_idx = i;
            run_len++;
            if (run_len == k) return (long)start_idx;
        } else {
            run_len = 0;
        }
    }
    return -1;
}

ds_status ds_bitset_to_hex(const ds_bitset *bs, char *buf, size_t cap) {
    if (!bs || !buf) return DS_ERR_ARG;
    size_t needed = bs->nwords * 16 + 1;
    if (cap < needed) return DS_ERR_NOMEM;

    char *p = buf;
    for (size_t i = 0; i < bs->nwords; i++) {
        snprintf(p, 17, "%016llx", (unsigned long long)bs->words[i]);
        p += 16;
    }
    *p = '\0';
    return DS_OK;
}

ds_status ds_bitset_from_hex(ds_bitset *bs, const char *hex) {
    if (!bs || !hex) return DS_ERR_ARG;
    size_t len = strlen(hex);
    if (len != bs->nwords * 16) return DS_ERR_ARG;

    for (size_t i = 0; i < bs->nwords; i++) {
        char chunk[17];
        memcpy(chunk, hex + (i * 16), 16);
        chunk[16] = '\0';
        unsigned long long val = 0;
        if (sscanf(chunk, "%llx", &val) != 1) {
            return DS_ERR_ARG;
        }
        bs->words[i] = (uint64_t)val;
    }

    if (bs->nbits % 64 != 0) {
        size_t excess = 64 - (bs->nbits % 64);
        uint64_t mask = (~0ULL) >> excess;
        bs->words[bs->nwords - 1] &= mask;
    }
    return DS_OK;
}

ds_bitset *ds_bitset_copy(const ds_bitset *bs) {
    if (!bs) return NULL;
    ds_bitset *copy = ds_bitset_create(bs->nbits);
    if (!copy) return NULL;
    memcpy(copy->words, bs->words, bs->nwords * sizeof(uint64_t));
    return copy;
}

void ds_bitset_destroy(ds_bitset *bs) {
    if (!bs) return;
    free(bs->words);
    free(bs);
}
