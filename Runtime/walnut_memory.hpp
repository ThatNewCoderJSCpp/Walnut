#ifndef WALNUT_MEMORY_HPP
#define WALNUT_MEMORY_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../../Source/Memory/arena.hpp"

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace mem {

template <typename T>
using ptr = fizmo::ArenaPtr<T>;

template <typename T>
using array = fizmo::ArenaArray<T>;

class arena {
public:
    std::shared_ptr<fizmo::Arena> a;

    arena() : a(std::make_shared<fizmo::Arena>()) {}

    explicit arena(long long block_size) {
        fizmo::ArenaOptions o;
        o.block_size = static_cast<std::size_t>(block_size);
        a = std::make_shared<fizmo::Arena>(o);
    }

    arena(long long block_size, long long max_block_size, bool thread_safe) {
        fizmo::ArenaOptions o;
        o.block_size     = static_cast<std::size_t>(block_size);
        o.max_block_size = static_cast<std::size_t>(max_block_size);
        o.thread_safe    = thread_safe;
        a = std::make_shared<fizmo::Arena>(o);
    }

    void reset() { a->reset(); }
    void shrink_to_fit() { a->shrink_to_fit(); }
    void reserve(long long bytes) { a->reserve(static_cast<std::size_t>(bytes)); }

    long long bytes_used() const { return static_cast<long long>(a->bytes_used()); }
    long long bytes_reserved() const { return static_cast<long long>(a->bytes_reserved()); }
    long long object_count() const { return static_cast<long long>(a->object_count()); }
    long long handle_count() const { return static_cast<long long>(a->handle_count()); }
    long long block_count() const { return static_cast<long long>(a->stats().blocks); }
    long long generation() const { return static_cast<long long>(a->generation()); }
    long long block_size() const { return static_cast<long long>(a->options().block_size); }
    long long max_block_size() const { return static_cast<long long>(a->options().max_block_size); }
    bool thread_safe() const { return a->thread_safe(); }
    bool same(const arena& o) const { return a == o.a; }
};

template <typename T>
inline ptr<T> make_value(arena& ar, const T& v) { return ar.a->make<T>(v); }

template <typename T>
inline ptr<T> make_moved(arena& ar, T&& v) { return ar.a->make<T>(std::move(v)); }

template <typename T>
inline ptr<T> make_default(arena& ar) { return ar.a->make<T>(); }

template <typename T>
inline array<T> make_array(arena& ar, long long n) { return ar.a->make_array<T>(static_cast<std::size_t>(n)); }

template <typename T>
inline array<T> make_filled(arena& ar, long long n, const T& fill) { return ar.a->make_array<T>(static_cast<std::size_t>(n), fill); }

template <typename T, typename V>
inline array<T> make_from(arena& ar, const V& v) { return ar.a->make_array(v.begin(), v.end()); }

template <typename T>
inline bool ptr_valid(const ptr<T>& p) { return static_cast<bool>(p); }

template <typename T>
inline T& ptr_ref(const ptr<T>& p) { return *p.get_unchecked(); }

template <typename T>
inline T* ptr_get(const ptr<T>& p) { return p.get(); }

template <typename T>
inline long long ptr_generation(const ptr<T>& p) { return static_cast<long long>(p.generation()); }

template <typename T>
inline long long ptr_use_count(const ptr<T>& p) { return static_cast<long long>(p.use_count()); }

template <typename T>
inline bool ptr_same(const ptr<T>& p, const ptr<T>& q) { return p.get() == q.get(); }

template <typename T>
inline bool ptr_same_arena(const ptr<T>& p, const ptr<T>& q) { return p.same_arena(q); }

template <typename T>
inline bool arena_owns(const arena& ar, const ptr<T>& p) { return p.from(*ar.a); }

template <typename T>
inline bool array_valid(const array<T>& v) { return static_cast<bool>(v); }

template <typename T>
inline long long array_size(const array<T>& v) { return static_cast<long long>(v.size()); }

template <typename T>
inline long long array_stored_size(const array<T>& v) { return v.expired() ? 0 : static_cast<long long>(v.size()); }

template <typename T>
inline T& array_ref(const array<T>& v, long long i) { return v.data()[i]; }

template <typename T>
inline T* array_data(const array<T>& v) { return v.data(); }

template <typename T>
inline T* array_end(const array<T>& v) { return v.data() ? v.data() + v.size() : nullptr; }

template <typename T>
inline ptr<T> array_element(const array<T>& v, long long i) { return v.element(static_cast<std::size_t>(i)); }

template <typename T>
inline std::vector<T> array_to_vector(const array<T>& v) { return std::vector<T>(v.data(), v.data() + v.size()); }

class counts {
public:
    counts() noexcept = default;
    counts(const counts&) = delete;
    counts& operator=(const counts&) = delete;

    void add_strong() noexcept { m_strong.fetch_add(1, std::memory_order_relaxed); }
    bool release_strong() noexcept { return m_strong.fetch_sub(1, std::memory_order_acq_rel) == 1; }
    void add_weak() noexcept { m_weak.fetch_add(1, std::memory_order_relaxed); }
    bool release_weak() noexcept { return m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1; }

    bool try_add_strong() noexcept {
        long long c = m_strong.load(std::memory_order_relaxed);
        while (c > 0) {
            if (m_strong.compare_exchange_weak(c, c + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) return true;
        }
        return false;
    }

    long long strong_count() const noexcept { return m_strong.load(std::memory_order_acquire); }
    long long weak_count() const noexcept { return m_weak.load(std::memory_order_acquire); }

private:
    std::atomic<long long> m_strong{1};
    std::atomic<long long> m_weak{1};
};

} // namespace mem
} // namespace walnut_rt

#endif // WALNUT_MEMORY_HPP
