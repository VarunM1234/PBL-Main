/*
 * PURPOSE: Lightweight test assertion framework for C and C++ tests.
 * WHY THIS NAME: test.h is the standard single-header test helper convention.
 * CONNECTS TO: test_core.c, test_app.cpp
 * COMPLEXITY: O(1) per check.
 */

#ifndef SMARTROUTE_TEST_H
#define SMARTROUTE_TEST_H

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(cond) do { \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: Condition failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
    long long _va = (long long)(a); \
    long long _vb = (long long)(b); \
    if (_va == _vb) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s == %s (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, _va, _vb); \
    } \
} while (0)

#define CHECK_EQ_STR(a, b) do { \
    const char *_sa = (const char *)(a); \
    const char *_sb = (const char *)(b); \
    if (_sa && _sb && strcmp(_sa, _sb) == 0) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
                __FILE__, __LINE__, #a, #b, _sa ? _sa : "(null)", _sb ? _sb : "(null)"); \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    double _da = (double)(a); \
    double _db = (double)(b); \
    double _diff = fabs(_da - _db); \
    if (_diff <= (eps)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: |%s - %s| <= %f (|%f - %f| = %f)\n", \
                __FILE__, __LINE__, #a, #b, (double)(eps), _da, _db, _diff); \
    } \
} while (0)

static inline int test_report(void) {
    printf("\n========================================\n");
    printf("TEST RESULTS: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");
    return g_tests_failed == 0 ? 0 : 1;
}

#endif /* SMARTROUTE_TEST_H */
