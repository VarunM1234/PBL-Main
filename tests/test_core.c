/*
 * PURPOSE: Comprehensive unit test suite for Phase 1 core data structures in C.
 * WHY THIS NAME: test_core.c validates all core/ C modules with edge cases and invariants.
 * CONNECTS TO: tests/test.h, core/ds_*.h
 * COMPLEXITY: Runs full unit tests in milliseconds.
 */

#include "test.h"
#include "../core/ds_common.h"
#include "../core/ds_vector.h"
#include "../core/ds_list.h"
#include "../core/ds_queue.h"
#include "../core/ds_stack.h"
#include "../core/ds_hash.h"
#include "../core/ds_heap.h"
#include "../core/ds_bitset.h"
#include "../core/ds_ring.h"

static void test_common_module(void) {
    printf("Testing ds_common...\n");
    CHECK_EQ_STR(ds_status_str(DS_OK), "OK");
    CHECK_EQ_STR(ds_status_str(DS_ERR_NOMEM), "Out of memory");
    CHECK_EQ_STR(ds_status_str(DS_ERR_ARG), "Invalid argument");
    CHECK_EQ_STR(ds_status_str(DS_ERR_EMPTY), "Container is empty");
    CHECK_EQ_STR(ds_status_str(DS_ERR_NOTFOUND), "Element not found");
    CHECK_EQ_STR(ds_status_str(DS_ERR_EXISTS), "Element already exists");
    CHECK_EQ_STR(ds_status_str(DS_ERR_FULL), "Container is full");

    void *ptr = ds_xmalloc(128);
    CHECK(ptr != NULL);
    ptr = ds_xrealloc(ptr, 256);
    CHECK(ptr != NULL);
    free(ptr);
}

static void test_vector_module(void) {
    printf("Testing ds_vector...\n");
    ds_vector *v = ds_vector_create(sizeof(int), NULL);
    CHECK(v != NULL);
    CHECK_EQ_INT(ds_vector_size(v), 0);

    // Empty pop
    int val = 0;
    CHECK_EQ_INT(ds_vector_pop(v, &val), DS_ERR_EMPTY);

    // Push 50 elements to trigger dynamic resizing
    for (int i = 0; i < 50; i++) {
        CHECK_EQ_INT(ds_vector_push(v, &i), DS_OK);
    }
    CHECK_EQ_INT(ds_vector_size(v), 50);

    for (int i = 0; i < 50; i++) {
        int *p = (int *)ds_vector_at(v, (size_t)i);
        CHECK(p != NULL);
        if (p) CHECK_EQ_INT(*p, i);
    }
    CHECK(ds_vector_at(v, 50) == NULL);

    // Insert at index 10
    int insert_val = 999;
    CHECK_EQ_INT(ds_vector_insert(v, 10, &insert_val), DS_OK);
    CHECK_EQ_INT(ds_vector_size(v), 51);
    CHECK_EQ_INT(*(int *)ds_vector_at(v, 10), 999);
    CHECK_EQ_INT(*(int *)ds_vector_at(v, 11), 10);

    // Remove from index 10
    CHECK_EQ_INT(ds_vector_remove(v, 10), DS_OK);
    CHECK_EQ_INT(ds_vector_size(v), 50);
    CHECK_EQ_INT(*(int *)ds_vector_at(v, 10), 10);

    // Pop element
    CHECK_EQ_INT(ds_vector_pop(v, &val), DS_OK);
    CHECK_EQ_INT(val, 49);
    CHECK_EQ_INT(ds_vector_size(v), 49);

    ds_vector_clear(v);
    CHECK_EQ_INT(ds_vector_size(v), 0);
    ds_vector_destroy(v);
}

static void test_list_module(void) {
    printf("Testing ds_list...\n");
    ds_list *list = ds_list_create(sizeof(int), NULL);
    CHECK(list != NULL);
    CHECK_EQ_INT(ds_list_size(list), 0);

    int a = 10, b = 20, c = 30;
    ds_list_node *n1 = ds_list_push_back(list, &b);
    ds_list_node *n0 = ds_list_push_front(list, &a);
    ds_list_node *n2 = ds_list_push_back(list, &c);

    CHECK_EQ_INT(ds_list_size(list), 3);
    CHECK_EQ_INT(*(int *)ds_list_data(ds_list_front(list)), 10);
    CHECK_EQ_INT(*(int *)ds_list_data(ds_list_back(list)), 30);

    int mid = 15;
    ds_list_node *nmid = ds_list_insert_after(list, n0, &mid);
    CHECK_EQ_INT(ds_list_size(list), 4);
    CHECK_EQ_INT(*(int *)ds_list_data(ds_list_next(n0)), 15);

    CHECK_EQ_INT(ds_list_remove(list, nmid), DS_OK);
    CHECK_EQ_INT(ds_list_size(list), 3);
    CHECK_EQ_INT(ds_list_remove(list, n0), DS_OK);
    CHECK_EQ_INT(ds_list_remove(list, n1), DS_OK);
    CHECK_EQ_INT(ds_list_remove(list, n2), DS_OK);
    CHECK_EQ_INT(ds_list_size(list), 0);

    ds_list_destroy(list);
}

static void test_queue_module(void) {
    printf("Testing ds_queue...\n");
    ds_queue *q = ds_queue_create(sizeof(int), NULL);
    CHECK(q != NULL);
    CHECK_EQ_INT(ds_queue_size(q), 0);

    int out = 0;
    CHECK_EQ_INT(ds_queue_dequeue(q, &out), DS_ERR_EMPTY);

    // Push 30 elements to verify circular buffer wrap-around and doubling
    for (int i = 0; i < 30; i++) {
        CHECK_EQ_INT(ds_queue_enqueue(q, &i), DS_OK);
    }
    CHECK_EQ_INT(ds_queue_size(q), 30);
    CHECK_EQ_INT(*(int *)ds_queue_peek(q), 0);

    for (int i = 0; i < 30; i++) {
        CHECK_EQ_INT(ds_queue_dequeue(q, &out), DS_OK);
        CHECK_EQ_INT(out, i);
    }
    CHECK_EQ_INT(ds_queue_size(q), 0);
    ds_queue_destroy(q);
}

static void test_stack_module(void) {
    printf("Testing ds_stack...\n");
    ds_stack *s = ds_stack_create(sizeof(int), NULL);
    CHECK(s != NULL);
    CHECK_EQ_INT(ds_stack_size(s), 0);

    int out = 0;
    CHECK_EQ_INT(ds_stack_pop(s, &out), DS_ERR_EMPTY);
    CHECK(ds_stack_top(s) == NULL);

    for (int i = 0; i < 25; i++) {
        CHECK_EQ_INT(ds_stack_push(s, &i), DS_OK);
    }
    CHECK_EQ_INT(ds_stack_size(s), 25);
    CHECK_EQ_INT(*(int *)ds_stack_top(s), 24);

    for (int i = 24; i >= 0; i--) {
        CHECK_EQ_INT(ds_stack_pop(s, &out), DS_OK);
        CHECK_EQ_INT(out, i);
    }
    CHECK_EQ_INT(ds_stack_size(s), 0);
    ds_stack_destroy(s);
}

static void test_hash_module(void) {
    printf("Testing ds_hash...\n");
    ds_hash *h = ds_hash_create_str(sizeof(int), NULL);
    CHECK(h != NULL);
    CHECK_EQ_INT(ds_hash_size(h), 0);

    int v1 = 101, v2 = 202, v3 = 303;
    CHECK_EQ_INT(ds_hash_put(h, "delhi", &v1), DS_OK);
    CHECK_EQ_INT(ds_hash_put(h, "jaipur", &v2), DS_OK);
    CHECK_EQ_INT(ds_hash_put(h, "agra", &v3), DS_OK);

    CHECK_EQ_INT(ds_hash_size(h), 3);
    CHECK(ds_hash_contains(h, "delhi"));
    CHECK(ds_hash_contains(h, "jaipur"));
    CHECK(!ds_hash_contains(h, "mumbai"));

    int *got = (int *)ds_hash_get(h, "jaipur");
    CHECK(got != NULL);
    if (got) CHECK_EQ_INT(*got, 202);

    // Overwrite value
    int v2_new = 999;
    CHECK_EQ_INT(ds_hash_put(h, "jaipur", &v2_new), DS_OK);
    CHECK_EQ_INT(*(int *)ds_hash_get(h, "jaipur"), 999);

    // Remove
    CHECK_EQ_INT(ds_hash_remove(h, "delhi"), DS_OK);
    CHECK_EQ_INT(ds_hash_size(h), 2);
    CHECK(!ds_hash_contains(h, "delhi"));

    // Add 100 items to test rehash
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        CHECK_EQ_INT(ds_hash_put(h, key, &i), DS_OK);
    }
    CHECK_EQ_INT(ds_hash_size(h), 102);
    CHECK_EQ_INT(*(int *)ds_hash_get(h, "key_42"), 42);

    ds_hash_destroy(h);
}

static void test_heap_module(void) {
    printf("Testing ds_heap...\n");
    ds_heap *hp = ds_heap_create();
    CHECK(hp != NULL);
    CHECK_EQ_INT(ds_heap_size(hp), 0);

    int64_t prio = 0;
    uint64_t id = 0;
    CHECK_EQ_INT(ds_heap_peek_min(hp, &prio, &id), DS_ERR_EMPTY);

    CHECK_EQ_INT(ds_heap_push(hp, 50, 1), DS_OK);
    CHECK_EQ_INT(ds_heap_push(hp, 20, 2), DS_OK);
    CHECK_EQ_INT(ds_heap_push(hp, 80, 3), DS_OK);
    CHECK_EQ_INT(ds_heap_push(hp, 10, 4), DS_OK);
    CHECK_EQ_INT(ds_heap_push(hp, 30, 5), DS_OK);

    // Duplicate id check
    CHECK_EQ_INT(ds_heap_push(hp, 99, 1), DS_ERR_EXISTS);
    CHECK_EQ_INT(ds_heap_size(hp), 5);

    // Min element should be id 4 (priority 10)
    CHECK_EQ_INT(ds_heap_peek_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(prio, 10);
    CHECK_EQ_INT(id, 4);

    // Update id 3 priority from 80 down to 5 (becomes new min)
    CHECK_EQ_INT(ds_heap_update(hp, 3, 5), DS_OK);
    CHECK_EQ_INT(ds_heap_peek_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(prio, 5);
    CHECK_EQ_INT(id, 3);

    // Pop minimums in sorted order
    CHECK_EQ_INT(ds_heap_pop_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(id, 3); // 5
    CHECK_EQ_INT(ds_heap_pop_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(id, 4); // 10
    CHECK_EQ_INT(ds_heap_pop_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(id, 2); // 20

    // Remove id 1
    CHECK_EQ_INT(ds_heap_remove(hp, 1), DS_OK);
    CHECK_EQ_INT(ds_heap_size(hp), 1);
    CHECK_EQ_INT(ds_heap_pop_min(hp, &prio, &id), DS_OK);
    CHECK_EQ_INT(id, 5); // 30
    CHECK_EQ_INT(ds_heap_size(hp), 0);

    ds_heap_destroy(hp);
}

static void test_bitset_module(void) {
    printf("Testing ds_bitset...\n");
    ds_bitset *bs = ds_bitset_create(100);
    CHECK(bs != NULL);
    CHECK_EQ_INT(ds_bitset_count(bs), 0);

    CHECK_EQ_INT(ds_bitset_set(bs, 0), DS_OK);
    CHECK_EQ_INT(ds_bitset_set(bs, 63), DS_OK);
    CHECK_EQ_INT(ds_bitset_set(bs, 64), DS_OK);
    CHECK_EQ_INT(ds_bitset_set(bs, 99), DS_OK);

    CHECK(ds_bitset_test(bs, 0));
    CHECK(ds_bitset_test(bs, 63));
    CHECK(ds_bitset_test(bs, 64));
    CHECK(ds_bitset_test(bs, 99));
    CHECK(!ds_bitset_test(bs, 1));
    CHECK(!ds_bitset_test(bs, 50));
    CHECK_EQ_INT(ds_bitset_count(bs), 4);

    // Flip
    CHECK_EQ_INT(ds_bitset_flip(bs, 50), DS_OK);
    CHECK(ds_bitset_test(bs, 50));
    CHECK_EQ_INT(ds_bitset_flip(bs, 50), DS_OK);
    CHECK(!ds_bitset_test(bs, 50));

    // Clear
    CHECK_EQ_INT(ds_bitset_clear(bs, 63), DS_OK);
    CHECK(!ds_bitset_test(bs, 63));
    CHECK_EQ_INT(ds_bitset_count(bs), 3);

    // Find clear run of 3 adjacent seats in range [10, 20)
    CHECK_EQ_INT(ds_bitset_find_clear_run(bs, 10, 20, 3), 10);
    // Mark bit 11 as occupied
    ds_bitset_set(bs, 11);
    CHECK_EQ_INT(ds_bitset_find_clear_run(bs, 10, 20, 3), 12);

    // Hex serialization
    char hex[64];
    CHECK_EQ_INT(ds_bitset_to_hex(bs, hex, sizeof(hex)), DS_OK);
    ds_bitset *bs2 = ds_bitset_create(100);
    CHECK_EQ_INT(ds_bitset_from_hex(bs2, hex), DS_OK);
    CHECK_EQ_INT(ds_bitset_count(bs2), ds_bitset_count(bs));
    CHECK(ds_bitset_test(bs2, 0));
    CHECK(ds_bitset_test(bs2, 99));

    ds_bitset_destroy(bs2);
    ds_bitset_destroy(bs);
}

static void test_ring_module(void) {
    printf("Testing ds_ring...\n");
    ds_ring *r = ds_ring_create(5);
    CHECK(r != NULL);
    CHECK_EQ_INT(ds_ring_count(r), 0);
    CHECK_NEAR(ds_ring_mean(r), 0.0, 0.001);

    ds_ring_push(r, 1000, 50.0);
    ds_ring_push(r, 2000, 60.0);
    ds_ring_push(r, 3000, 70.0);

    CHECK_EQ_INT(ds_ring_count(r), 3);
    CHECK_NEAR(ds_ring_mean(r), 60.0, 0.001);

    // Push 3 more to overflow capacity 5
    ds_ring_push(r, 4000, 80.0);
    ds_ring_push(r, 5000, 90.0);
    ds_ring_push(r, 6000, 100.0); // overwrites oldest (50.0)

    CHECK_EQ_INT(ds_ring_count(r), 5);
    // Elements: 60, 70, 80, 90, 100 => mean = 80.0
    CHECK_NEAR(ds_ring_mean(r), 80.0, 0.001);

    // Evict before 4500 (removes 60, 70, 80)
    ds_ring_evict_before(r, 4500);
    CHECK_EQ_INT(ds_ring_count(r), 2);
    // Remaining: 90, 100 => mean = 95.0
    CHECK_NEAR(ds_ring_mean(r), 95.0, 0.001);

    ds_ring_destroy(r);
}

int main(void) {
    printf("========================================\n");
    printf("SMARTROUTE PHASE 1: CORE DATA STRUCTURES\n");
    printf("========================================\n");

    test_common_module();
    test_vector_module();
    test_list_module();
    test_queue_module();
    test_stack_module();
    test_hash_module();
    test_heap_module();
    test_bitset_module();
    test_ring_module();

    return test_report();
}
