/*
 * PURPOSE: QR Code encoder implementation (see qr.h).
 * WHY THIS NAME: implements qr.h.
 * CONNECTS TO: nothing else in core/ (self-contained).
 * COMPLEXITY: see qr.h.
 */
#include "qr.h"
#include <string.h>

/* Level M tables for versions 1..10: ECC codewords per block and number of blocks. */
static const int ECC_PER_BLOCK[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
static const int NUM_BLOCKS[11] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

typedef struct {
    int version, size;
    unsigned char mod[QR_MAX_SIZE][QR_MAX_SIZE];   /* 1 = dark */
    unsigned char fn[QR_MAX_SIZE][QR_MAX_SIZE];    /* 1 = function pattern (not data) */
} qr_t;

static int raw_modules(int v) {                    /* data + ecc modules available in version v */
    int r = (16 * v + 128) * v + 64;
    if (v >= 2) {
        int na = v / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (v >= 7) r -= 36;
    }
    return r;
}

static int data_codewords(int v) { return raw_modules(v) / 8 - ECC_PER_BLOCK[v] * NUM_BLOCKS[v]; }

/* ---------- Reed-Solomon over GF(256), polynomial 0x11D ---------- */
static unsigned char gf_mul(unsigned char x, unsigned char y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (unsigned char)z;
}

static void rs_divisor(int degree, unsigned char *out) {
    memset(out, 0, (size_t)degree);
    out[degree - 1] = 1;
    unsigned char root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            out[j] = gf_mul(out[j], root);
            if (j + 1 < degree) out[j] ^= out[j + 1];
        }
        root = gf_mul(root, 2);
    }
}

static void rs_remainder(const unsigned char *data, int len, const unsigned char *div, int degree, unsigned char *out) {
    memset(out, 0, (size_t)degree);
    for (int i = 0; i < len; i++) {
        unsigned char factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (int j = 0; j < degree; j++) out[j] ^= gf_mul(div[j], factor);
    }
}

/* ---------- function patterns ---------- */
static void set_fn(qr_t *q, int x, int y, int dark) {
    if (x < 0 || y < 0 || x >= q->size || y >= q->size) return;
    q->mod[y][x] = (unsigned char)(dark != 0);
    q->fn[y][x] = 1;
}

static void draw_finder(qr_t *q, int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, dist = d > e ? d : e;
            set_fn(q, cx + dx, cy + dy, dist != 2 && dist != 4);   /* ring pattern with a white separator */
        }
}

static void draw_alignment(qr_t *q, int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, dist = d > e ? d : e;
            set_fn(q, cx + dx, cy + dy, dist != 1);
        }
}

static int align_positions(int v, int size, int *pos) {
    if (v == 1) return 0;
    int n = v / 7 + 2;
    int step = (v * 4 + 4 + (n * 2 - 2) - 1) / (n * 2 - 2) * 2;      /* ceil((4v+4)/(2n-2)) * 2 */
    pos[0] = 6;
    for (int i = n - 1, p = size - 7; i >= 1; i--, p -= step) pos[i] = p;
    return n;
}

static void draw_format(qr_t *q, int mask) {
    int data = (0 << 3) | mask;                    /* level M = 00 */
    int rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;
    for (int i = 0; i <= 5; i++) set_fn(q, 8, i, (bits >> i) & 1);
    set_fn(q, 8, 7, (bits >> 6) & 1);
    set_fn(q, 8, 8, (bits >> 7) & 1);
    set_fn(q, 7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) set_fn(q, 14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++) set_fn(q, q->size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) set_fn(q, 8, q->size - 15 + i, (bits >> i) & 1);
    set_fn(q, 8, q->size - 8, 1);                  /* the always-dark module */
}

static void draw_function_patterns(qr_t *q) {
    for (int i = 0; i < q->size; i++) { set_fn(q, 6, i, i % 2 == 0); set_fn(q, i, 6, i % 2 == 0); }   /* timing */
    draw_finder(q, 3, 3); draw_finder(q, q->size - 4, 3); draw_finder(q, 3, q->size - 4);
    int pos[8], n = align_positions(q->version, q->size, pos);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) draw_alignment(q, pos[i], pos[j]);
    draw_format(q, 0);                             /* reserve the format area; rewritten once the mask is known */
    if (q->version >= 7) {
        int rem = q->version;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = ((long)q->version << 12) | rem;
        for (int i = 0; i < 18; i++) {
            int dark = (int)((bits >> i) & 1), a = q->size - 11 + i % 3, b = i / 3;
            set_fn(q, a, b, dark); set_fn(q, b, a, dark);
        }
    }
}

/* ---------- placing data ---------- */
static void place_codewords(qr_t *q, const unsigned char *data, int len) {
    int i = 0;
    for (int right = q->size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;                 /* skip the vertical timing column */
        for (int vert = 0; vert < q->size; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j, upward = ((right + 1) & 2) == 0, y = upward ? q->size - 1 - vert : vert;
                if (!q->fn[y][x] && i < len * 8) { q->mod[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1; i++; }
            }
    }
}

static int mask_bit(int m, int x, int y) {
    switch (m) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(qr_t *q, int m) {           /* XOR again to undo */
    for (int y = 0; y < q->size; y++)
        for (int x = 0; x < q->size; x++)
            if (!q->fn[y][x] && mask_bit(m, x, y)) q->mod[y][x] ^= 1;
}

static long lab(long v) { return v < 0 ? -v : v; }

static long penalty(const qr_t *q) {
    long p = 0;
    int dark = 0, n = q->size;
    for (int pass = 0; pass < 2; pass++)           /* rule 1: runs of 5+ equal modules in rows, then columns */
        for (int a = 0; a < n; a++) {
            int run = 1;
            for (int b = 1; b < n; b++) {
                int cur = pass ? q->mod[b][a] : q->mod[a][b], prev = pass ? q->mod[b - 1][a] : q->mod[a][b - 1];
                if (cur == prev) { run++; if (run == 5) p += 3; else if (run > 5) p += 1; } else run = 1;
            }
        }
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            dark += q->mod[y][x];
            if (x + 1 < n && y + 1 < n && q->mod[y][x] == q->mod[y][x + 1] && q->mod[y][x] == q->mod[y + 1][x] && q->mod[y][x] == q->mod[y + 1][x + 1]) p += 3;   /* rule 2 */
        }
    long total = (long)n * n, k = (lab(dark * 20L - total * 10L) + total - 1) / total - 1;   /* rule 4: balance */
    return p + (k > 0 ? k : 0) * 10;
}

/* ---------- main ---------- */
int qr_encode(const char *text, unsigned char *modules, int *size) {
    if (!text || !modules || !size) return -1;
    int len = (int)strlen(text), v = 0;
    for (int cand = 1; cand <= 10 && !v; cand++) {
        int cc_bits = cand >= 10 ? 16 : 8;
        if (4 + cc_bits + 8 * len <= data_codewords(cand) * 8) v = cand;
    }
    if (!v || len > QR_MAX_BYTES) return -2;
    unsigned char buf[4096] = {0};
    int nbits = 0, cap = data_codewords(v) * 8, cc = v >= 10 ? 16 : 8;
#define PUT(val, count) do { for (int _i = (count) - 1; _i >= 0; _i--) { buf[nbits >> 3] |= (unsigned char)((((val) >> _i) & 1) << (7 - (nbits & 7))); nbits++; } } while (0)
    PUT(4, 4);                                      /* byte mode */
    PUT(len, cc);
    for (int i = 0; i < len; i++) PUT((unsigned char)text[i], 8);
    int term = cap - nbits < 4 ? cap - nbits : 4;
    nbits += term;                                  /* terminator (zeros) */
    nbits = (nbits + 7) / 8 * 8;
    for (unsigned char pad = 0xEC; nbits < cap; pad ^= 0xEC ^ 0x11) { buf[nbits >> 3] = pad; nbits += 8; }
    int ecc = ECC_PER_BLOCK[v], blocks = NUM_BLOCKS[v], total = raw_modules(v) / 8;
    int short_blocks = blocks - total % blocks, short_len = total / blocks, dcw = data_codewords(v);
    unsigned char div[32], rem[32], final_cw[1024];
    rs_divisor(ecc, div);
    unsigned char block_data[10][256], block_ecc[10][32];
    int lens[10], k = 0;
    for (int b = 0; b < blocks; b++) {
        lens[b] = short_len - ecc + (b < short_blocks ? 0 : 1);
        memcpy(block_data[b], buf + k, (size_t)lens[b]);
        k += lens[b];
        rs_remainder(block_data[b], lens[b], div, ecc, rem);
        memcpy(block_ecc[b], rem, (size_t)ecc);
    }
    (void)dcw;
    int out = 0;                                    /* interleave data codewords, then ecc codewords */
    for (int i = 0; i < short_len - ecc + 1; i++)
        for (int b = 0; b < blocks; b++)
            if (i < lens[b]) final_cw[out++] = block_data[b][i];
    for (int i = 0; i < ecc; i++)
        for (int b = 0; b < blocks; b++) final_cw[out++] = block_ecc[b][i];
    static qr_t q;
    memset(&q, 0, sizeof q);
    q.version = v; q.size = 17 + 4 * v;
    draw_function_patterns(&q);
    place_codewords(&q, final_cw, out);
    int best = 0;
    long best_pen = -1;
    for (int m = 0; m < 8; m++) {
        apply_mask(&q, m);
        draw_format(&q, m);
        long p = penalty(&q);
        if (best_pen < 0 || p < best_pen) { best_pen = p; best = m; }
        apply_mask(&q, m);
    }
    apply_mask(&q, best);
    draw_format(&q, best);
    for (int y = 0; y < q.size; y++) memcpy(modules + y * q.size, q.mod[y], (size_t)q.size);
    *size = q.size;
    return 0;
}
