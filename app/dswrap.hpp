/*
 * PURPOSE: Small RAII (auto-cleanup) C++ wrappers around the C data-structure library in core/.
 * WHY THIS NAME: "dswrap" = data-structure wrappers.
 * CONNECTS TO: every app/ file. Stored element types must be plain structs (memcpy-safe).
 * COMPLEXITY: same as the wrapped C structure; wrappers add no extra cost.
 */
#ifndef SMARTROUTE_DSWRAP_HPP
#define SMARTROUTE_DSWRAP_HPP

#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#include "ds_bitset.h"
#include "ds_hash.h"
#include "ds_heap.h"
#include "ds_queue.h"
#include "ds_stack.h"
#include "ds_vector.h"

namespace sr {

/* Growable array of plain structs (wraps ds_vector). Elements live contiguously. */
template <class T>
class Vec {
    static_assert(std::is_trivially_copyable<T>::value, "Vec<T> needs a plain struct");
    ds_vector *v_;
public:
    Vec() : v_(ds_vector_create(sizeof(T), nullptr)) { if (!v_) std::abort(); }
    ~Vec() { ds_vector_destroy(v_); }
    Vec(const Vec &) = delete;
    Vec &operator=(const Vec &) = delete;
    size_t size() const { return ds_vector_size(v_); }
    bool empty() const { return size() == 0; }
    bool push(const T &x) { return ds_vector_push(v_, &x) == DS_OK; }
    T *at(size_t i) const { return static_cast<T *>(ds_vector_at(v_, i)); }
    T &operator[](size_t i) const { T *p = at(i); if (!p) std::abort(); return *p; }
    T *data() const { return size() ? at(0) : nullptr; }
    T *begin() const { return data(); }
    T *end() const { return data() ? data() + size() : nullptr; }
    bool remove(size_t i) { return ds_vector_remove(v_, i) == DS_OK; }
    void clear() { ds_vector_clear(v_); }
};

/* String-keyed hash map of plain values (wraps ds_hash). */
template <class V>
class Map {
    static_assert(std::is_trivially_copyable<V>::value, "Map<V> needs a plain value");
    ds_hash *h_;
public:
    Map() : h_(ds_hash_create_str(sizeof(V), nullptr)) { if (!h_) std::abort(); }
    ~Map() { ds_hash_destroy(h_); }
    Map(const Map &) = delete;
    Map &operator=(const Map &) = delete;
    bool put(const std::string &k, const V &v) { return ds_hash_put(h_, k.c_str(), &v) == DS_OK; }
    V *get(const std::string &k) const { return static_cast<V *>(ds_hash_get(h_, k.c_str())); }
    bool has(const std::string &k) const { return ds_hash_contains(h_, k.c_str()) != 0; }
    bool remove(const std::string &k) { return ds_hash_remove(h_, k.c_str()) == DS_OK; }
    size_t size() const { return ds_hash_size(h_); }
    void clear() { ds_hash_destroy(h_); h_ = ds_hash_create_str(sizeof(V), nullptr); if (!h_) std::abort(); }
};

/* FIFO queue of plain structs (wraps ds_queue). */
template <class T>
class Queue {
    ds_queue *q_;
public:
    Queue() : q_(ds_queue_create(sizeof(T), nullptr)) { if (!q_) std::abort(); }
    ~Queue() { ds_queue_destroy(q_); }
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;
    bool push(const T &x) { return ds_queue_enqueue(q_, &x) == DS_OK; }
    bool pop(T *out) { return ds_queue_dequeue(q_, out) == DS_OK; }
    T *peek() const { return static_cast<T *>(ds_queue_peek(q_)); }
    size_t size() const { return ds_queue_size(q_); }
};

/* LIFO stack of plain structs (wraps ds_stack). */
template <class T>
class Stack {
    ds_stack *s_;
public:
    Stack() : s_(ds_stack_create(sizeof(T), nullptr)) { if (!s_) std::abort(); }
    ~Stack() { ds_stack_destroy(s_); }
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;
    bool push(const T &x) { return ds_stack_push(s_, &x) == DS_OK; }
    bool pop(T *out) { return ds_stack_pop(s_, out) == DS_OK; }
    size_t size() const { return ds_stack_size(s_); }
};

/* Min-heap with ids (wraps ds_heap). */
class MinHeap {
    ds_heap *h_;
public:
    MinHeap() : h_(ds_heap_create()) { if (!h_) std::abort(); }
    ~MinHeap() { ds_heap_destroy(h_); }
    MinHeap(const MinHeap &) = delete;
    MinHeap &operator=(const MinHeap &) = delete;
    bool push(int64_t prio, uint64_t id) { return ds_heap_push(h_, prio, id) == DS_OK; }
    bool peek(int64_t *prio, uint64_t *id) const { return ds_heap_peek_min(h_, prio, id) == DS_OK; }
    bool pop(int64_t *prio, uint64_t *id) { return ds_heap_pop_min(h_, prio, id) == DS_OK; }
    bool update(uint64_t id, int64_t prio) { return ds_heap_update(h_, id, prio) == DS_OK; }
    bool remove(uint64_t id) { return ds_heap_remove(h_, id) == DS_OK; }
    size_t size() const { return ds_heap_size(h_); }
};

/* Bit set (wraps ds_bitset); copyable. */
class Bits {
    ds_bitset *b_;
public:
    explicit Bits(size_t n) : b_(ds_bitset_create(n)) { if (!b_) std::abort(); }
    Bits(const Bits &o) : b_(ds_bitset_copy(o.b_)) { if (!b_) std::abort(); }
    Bits &operator=(const Bits &) = delete;
    ~Bits() { ds_bitset_destroy(b_); }
    void set(size_t i) { ds_bitset_set(b_, i); }
    void clear(size_t i) { ds_bitset_clear(b_, i); }
    bool test(size_t i) const { return ds_bitset_test(b_, i) != 0; }
    size_t count() const { return ds_bitset_count(b_); }
    long find_clear_run(size_t from, size_t to, size_t k) const { return ds_bitset_find_clear_run(b_, from, to, k); }
    std::string hex() const { char buf[256]; return ds_bitset_to_hex(b_, buf, sizeof buf) == DS_OK ? buf : ""; }
    bool from_hex(const char *s) { return ds_bitset_from_hex(b_, s) == DS_OK; }
    ds_bitset *raw() const { return b_; }
};

}  // namespace sr
#endif
