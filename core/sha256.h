/*
 * PURPOSE: SHA-256 hash (FIPS 180-4) plus a salted, iterated password helper.
 * WHY THIS NAME: names the algorithm.
 * CONNECTS TO: app/auth. EDUCATIONAL ONLY: real systems use bcrypt/argon2/scrypt.
 * COMPLEXITY: O(len) per hash; password helper O(iterations).
 */
#ifndef SMARTROUTE_SHA256_H
#define SMARTROUTE_SHA256_H
#include "ds_common.h"
#ifdef __cplusplus
extern "C" {
#endif
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void sha256_hex(const uint8_t *data, size_t len, char out[65]);
/* hash = SHA256 repeated `iterations` times over (previous || salt || password). */
void sha256_salted_iter(const uint8_t *salt, size_t salt_len, const char *password,
                        int iterations, char out_hex[65]);
#ifdef __cplusplus
}
#endif
#endif
