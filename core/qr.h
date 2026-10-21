/*
 * PURPOSE: QR Code encoder (byte mode, error correction level M, versions 1 to 10) written from the ISO 18004 rules.
 * WHY THIS NAME: "qr" = Quick Response code, the square barcode printed on the boarding pass.
 * CONNECTS TO: app/ticket and app/http_server (the pass shows a scannable code; the driver console scans its text).
 * COMPLEXITY: O(n * modules) for mask choice; Reed-Solomon over GF(256) is O(data x ecc).
 * INVARIANTS: output is a size x size matrix, 1 = dark module; size = 17 + 4 x version.
 * NOTE: mask choice uses penalty rules 1, 2 and 4 (runs, 2x2 blocks, dark balance). Any mask is a valid code.
 */
#ifndef SMARTROUTE_QR_H
#define SMARTROUTE_QR_H
#ifdef __cplusplus
extern "C" {
#endif
#define QR_MAX_SIZE 57                      /* version 10 */
#define QR_MAX_BYTES 213                    /* bytes of text version 10 / level M can hold is 213; we cap lower for safety */
/* Encodes text. modules must hold QR_MAX_SIZE*QR_MAX_SIZE bytes. Returns 0 on success and sets *size;
 * returns -1 for bad arguments and -2 when the text is too long. */
int qr_encode(const char *text, unsigned char *modules, int *size);
#ifdef __cplusplus
}
#endif
#endif
