/*
 * PURPOSE: Implementation of common memory allocation and status string utilities.
 * WHY THIS NAME: ds_common.c implements ds_common.h functions.
 * CONNECTS TO: core/ds_common.h
 * COMPLEXITY: O(1) memory allocation checks.
 */

#include "ds_common.h"

const char *ds_status_str(ds_status s) {
    switch (s) {
        case DS_OK: return "OK";
        case DS_ERR_NOMEM: return "Out of memory";
        case DS_ERR_ARG: return "Invalid argument";
        case DS_ERR_EMPTY: return "Container is empty";
        case DS_ERR_NOTFOUND: return "Element not found";
        case DS_ERR_EXISTS: return "Element already exists";
        case DS_ERR_FULL: return "Container is full";
        default: return "Unknown error";
    }
}

void *ds_xmalloc(size_t size) {
    if (size == 0) return NULL;
    return malloc(size);
}

void *ds_xcalloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    return calloc(nmemb, size);
}

void *ds_xrealloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}
