/*
 * PURPOSE: Common status codes, error reporting, and checked memory allocation.
 * WHY THIS NAME: ds_common defines shared primitives across the core C library.
 * CONNECTS TO: All core/ data structure modules.
 * COMPLEXITY: O(1) for memory wrappers.
 */

#ifndef SMARTROUTE_DS_COMMON_H
#define SMARTROUTE_DS_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DS_OK = 0,
    DS_ERR_NOMEM,
    DS_ERR_ARG,
    DS_ERR_EMPTY,
    DS_ERR_NOTFOUND,
    DS_ERR_EXISTS,
    DS_ERR_FULL
} ds_status;

const char *ds_status_str(ds_status s);

void *ds_xmalloc(size_t size);
void *ds_xcalloc(size_t nmemb, size_t size);
void *ds_xrealloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SMARTROUTE_DS_COMMON_H */
