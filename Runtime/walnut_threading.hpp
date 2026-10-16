#ifndef WALNUT_THREADING_HPP
#define WALNUT_THREADING_HPP

#include "walnut_runtime.hpp"

#include <atomic>
#include <memory>
#include <limits>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace walnut_rt {
namespace threading {

struct api {};

inline unsigned long long id_number(std::thread::id id) {
    return static_cast<unsigned long long>(std::hash<std::thread::id>{}(id));
}

inline void sleep_for(long long milliseconds) {
    if (milliseconds > 0) std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline void sleep_for_micros(long long microseconds) {
    if (microseconds > 0) std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

inline void yield() { std::this_thread::yield(); }

inline unsigned long long current_id() { return id_number(std::this_thread::get_id()); }

inline unsigned long long hardware_threads() {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : n;
}

class thread {
public:
    thread() = default;

    explicit thread(std::function<void()> body) : m_thread([body = std::move(body)]() {
        try {
            body();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "walnut: an exception escaped a thread: %s\n", e.what());
            std::fflush(stderr);
            std::abort();
        } catch (...) {
            std::fprintf(stderr, "walnut: an exception escaped a thread\n");
            std::fflush(stderr);
            std::abort();
        }
    }) {}

    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
    thread(thread&& other) noexcept = default;

    thread& operator=(thread&& other) noexcept {
        if (this != &other) {
            if (m_thread.joinable()) m_thread.join();
            m_thread = std::move(other.m_thread);
        }
        return *this;
    }

    ~thread() {
        if (m_thread.joinable()) m_thread.join();
    }

    void join() {
        if (!m_thread.joinable()) fail("join() on a thread that isn't running or was already joined or detached");
        if (m_thread.get_id() == std::this_thread::get_id()) fail("a thread can't join itself");
        m_thread.join();
    }

    void detach() {
        if (!m_thread.joinable()) fail("detach() on a thread that isn't running or was already joined or detached");
        m_thread.detach();
    }

    bool joinable() const noexcept { return m_thread.joinable(); }

    unsigned long long id() const { return m_thread.joinable() ? id_number(m_thread.get_id()) : 0ULL; }

private:
    std::thread m_thread;
};

template <typename W>
using mutex_of = std::remove_reference_t<decltype(std::declval<W&>().native)>;

template <typename M>
inline bool try_lock_for(M& m, long long milliseconds) {
    return m.try_lock_for(std::chrono::milliseconds(milliseconds < 0 ? 0 : milliseconds));
}

template <typename W>
class lock_guard {
public:
    explicit lock_guard(W& m) : m_lock(m.native) {}

private:
    std::lock_guard<mutex_of<W>> m_lock;
};

template <typename W>
class unique_lock {
public:
    unique_lock() = default;
    explicit unique_lock(W& m) : lk(m.native) {}
    unique_lock(W& m, bool lock_now) : lk(m.native, std::defer_lock) { if (lock_now) lk.lock(); }

    void lock() {
        if (!lk.mutex()) fail("lock() on a UniqueLock that has no mutex");
        if (lk.owns_lock()) fail("lock() on a UniqueLock that already holds its mutex");
        lk.lock();
    }

    void unlock() {
        if (!lk.owns_lock()) fail("unlock() on a UniqueLock that doesn't hold its mutex");
        lk.unlock();
    }

    bool try_lock() {
        if (!lk.mutex()) fail("try_lock() on a UniqueLock that has no mutex");
        if (lk.owns_lock()) fail("try_lock() on a UniqueLock that already holds its mutex");
        return lk.try_lock();
    }

    bool owns_lock() const noexcept { return lk.owns_lock(); }

    std::unique_lock<mutex_of<W>> lk;
};

template <typename W>
class shared_lock {
public:
    shared_lock() = default;
    explicit shared_lock(W& m) : lk(m.native) {}
    shared_lock(W& m, bool lock_now) : lk(m.native, std::defer_lock) { if (lock_now) lk.lock(); }

    void lock() {
        if (!lk.mutex()) fail("lock() on a SharedLock that has no mutex");
        if (lk.owns_lock()) fail("lock() on a SharedLock that already holds its mutex");
        lk.lock();
    }

    void unlock() {
        if (!lk.owns_lock()) fail("unlock() on a SharedLock that doesn't hold its mutex");
        lk.unlock();
    }

    bool try_lock() {
        if (!lk.mutex()) fail("try_lock() on a SharedLock that has no mutex");
        if (lk.owns_lock()) fail("try_lock() on a SharedLock that already holds its mutex");
        return lk.try_lock();
    }

    bool owns_lock() const noexcept { return lk.owns_lock(); }

    std::shared_lock<mutex_of<W>> lk;
};

class condition_variable {
public:
    template <typename L>
    void wait(L& lock) {
        if (!lock.lk.owns_lock()) fail("wait() needs a UniqueLock that holds its mutex");
        m_cv.wait(lock.lk);
    }

    template <typename L>
    void wait(L& lock, const std::function<bool()>& ready) {
        if (!lock.lk.owns_lock()) fail("wait() needs a UniqueLock that holds its mutex");
        m_cv.wait(lock.lk, ready);
    }

    template <typename L>
    bool wait_for(L& lock, long long milliseconds) {
        if (!lock.lk.owns_lock()) fail("wait_for() needs a UniqueLock that holds its mutex");
        return m_cv.wait_for(lock.lk, std::chrono::milliseconds(milliseconds < 0 ? 0 : milliseconds)) == std::cv_status::no_timeout;
    }

    template <typename L>
    bool wait_for(L& lock, long long milliseconds, const std::function<bool()>& ready) {
        if (!lock.lk.owns_lock()) fail("wait_for() needs a UniqueLock that holds its mutex");
        return m_cv.wait_for(lock.lk, std::chrono::milliseconds(milliseconds < 0 ? 0 : milliseconds), ready);
    }

    void notify_one() noexcept { m_cv.notify_one(); }
    void notify_all() noexcept { m_cv.notify_all(); }

private:
    std::condition_variable_any m_cv;
};

template <typename W, typename = void>
struct atomic_storage {
    using type = W;
};

template <std::size_t B, mp::sign S>
struct atomic_storage<wint<B, S>> {
    static_assert(B <= 64, "Atomic only holds integers of up to 64 bits");
    using type = std::conditional_t<S == mp::sign::is_signed,
        std::conditional_t<(B <= 8), std::int8_t, std::conditional_t<(B <= 16), std::int16_t, std::conditional_t<(B <= 32), std::int32_t, std::int64_t>>>,
        std::conditional_t<(B <= 8), std::uint8_t, std::conditional_t<(B <= 16), std::uint16_t, std::conditional_t<(B <= 32), std::uint32_t, std::uint64_t>>>>;
};

inline std::memory_order order_of(long long o) noexcept {
    switch (o) {
        case 0:  return std::memory_order_relaxed;
        case 1:  return std::memory_order_acquire;
        case 2:  return std::memory_order_release;
        case 3:  return std::memory_order_acq_rel;
        default: return std::memory_order_seq_cst;
    }
}

inline std::memory_order load_order(long long o) noexcept {
    const std::memory_order m = order_of(o);
    return (m == std::memory_order_release || m == std::memory_order_acq_rel) ? std::memory_order_seq_cst : m;
}

inline std::memory_order store_order(long long o) noexcept {
    const std::memory_order m = order_of(o);
    return (m == std::memory_order_acquire || m == std::memory_order_acq_rel) ? std::memory_order_seq_cst : m;
}

inline std::memory_order failure_order(std::memory_order m) noexcept {
    if (m == std::memory_order_acq_rel) return std::memory_order_acquire;
    if (m == std::memory_order_release) return std::memory_order_relaxed;
    return m;
}

struct wait_bucket {
    std::mutex mutex;
    std::condition_variable cv;
};

inline wait_bucket& bucket_for(const void* address) noexcept {
    static wait_bucket buckets[64];
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(address);
    return buckets[(a >> 4) % 64];
}

inline void park_while(const void* address, const std::function<bool()>& unchanged) {
    wait_bucket& b = bucket_for(address);
    std::unique_lock<std::mutex> lock(b.mutex);
    while (unchanged()) b.cv.wait(lock);
}

inline void unpark(const void* address) {
    wait_bucket& b = bucket_for(address);
    { std::lock_guard<std::mutex> lock(b.mutex); }
    b.cv.notify_all();
}

class claim_table {
public:
    claim_table() = default;
    explicit claim_table(long long n) : m_size(n > 0 ? static_cast<std::size_t>(n) : 0), m_cells(new std::atomic<long long>[m_size ? m_size : 1]) {
        for (std::size_t i = 0; i < m_size; ++i) m_cells[i].store(std::numeric_limits<long long>::max(), std::memory_order_relaxed);
    }

    void mark(long long i) noexcept { m_cells[static_cast<std::size_t>(i)].store(-1, std::memory_order_relaxed); }

    bool take_first(long long i) noexcept {
        std::atomic<long long>& c = m_cells[static_cast<std::size_t>(i)];
        if (c.load(std::memory_order_relaxed) == -1) return false;
        return c.exchange(-1, std::memory_order_relaxed) != -1;
    }

private:
    std::size_t m_size = 0;
    std::unique_ptr<std::atomic<long long>[]> m_cells;
};

struct parker {
    static void park_while(const void* address, std::function<bool()> unchanged) { walnut_rt::threading::park_while(address, unchanged); }
    static void unpark(const void* address) { walnut_rt::threading::unpark(address); }
};

class spin_lock {
public:
    spin_lock() noexcept = default;
    spin_lock(const spin_lock&) = delete;
    spin_lock& operator=(const spin_lock&) = delete;

    void lock() noexcept {
        for (int spins = 0; m_flag.test_and_set(std::memory_order_acquire); ++spins) {
            if (spins > 64) std::this_thread::yield();
        }
    }

    bool try_lock() noexcept { return !m_flag.test_and_set(std::memory_order_acquire); }
    void unlock() noexcept { m_flag.clear(std::memory_order_release); }

private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

template <typename W>
class atomic {
public:
    using native_type = typename atomic_storage<W>::type;

    atomic() : m_value(native_type{}) {}
    explicit atomic(const W& v) : m_value(to(v)) {}

    W load() const { return from(m_value.load()); }
    void store(const W& v) { m_value.store(to(v)); }
    W exchange(const W& v) { return from(m_value.exchange(to(v))); }

    bool compare_exchange(W& expected, const W& desired) {
        native_type e = to(expected);
        const bool ok = m_value.compare_exchange_strong(e, to(desired));
        if (!ok) expected = from(e);
        return ok;
    }

    W fetch_add(const W& v) { return from(m_value.fetch_add(to(v))); }
    W fetch_sub(const W& v) { return from(m_value.fetch_sub(to(v))); }
    W fetch_and(const W& v) { return from(m_value.fetch_and(to(v))); }
    W fetch_or(const W& v) { return from(m_value.fetch_or(to(v))); }
    W fetch_xor(const W& v) { return from(m_value.fetch_xor(to(v))); }

    W increment() { return from(static_cast<native_type>(m_value.fetch_add(native_type(1)) + native_type(1))); }
    W decrement() { return from(static_cast<native_type>(m_value.fetch_sub(native_type(1)) - native_type(1))); }
    W add(const W& v) { return from(static_cast<native_type>(m_value.fetch_add(to(v)) + to(v))); }
    W sub(const W& v) { return from(static_cast<native_type>(m_value.fetch_sub(to(v)) - to(v))); }

    W load_with(long long o) const { return from(m_value.load(load_order(o))); }
    void store_with(const W& v, long long o) { m_value.store(to(v), store_order(o)); }
    W exchange_with(const W& v, long long o) { return from(m_value.exchange(to(v), order_of(o))); }

    bool compare_exchange_with(W& expected, const W& desired, bool weak, long long o) {
        native_type e = to(expected);
        const std::memory_order m = order_of(o);
        const bool ok = weak ? m_value.compare_exchange_weak(e, to(desired), m, failure_order(m))
                             : m_value.compare_exchange_strong(e, to(desired), m, failure_order(m));
        if (!ok) expected = from(e);
        return ok;
    }

    template <typename D>
    W fetch_add_with(const D& v, long long o) {
        if constexpr (std::is_pointer_v<native_type>) return from(m_value.fetch_add(delta_of(v), order_of(o)));
        else return from(m_value.fetch_add(to(v), order_of(o)));
    }

    template <typename D>
    W fetch_sub_with(const D& v, long long o) {
        if constexpr (std::is_pointer_v<native_type>) return from(m_value.fetch_sub(delta_of(v), order_of(o)));
        else return from(m_value.fetch_sub(to(v), order_of(o)));
    }

    template <typename D>
    W add_with(const D& v, long long o) {
        if constexpr (std::is_pointer_v<native_type>) { const std::ptrdiff_t d = delta_of(v); return from(m_value.fetch_add(d, order_of(o)) + d); }
        else return from(static_cast<native_type>(m_value.fetch_add(to(v), order_of(o)) + to(v)));
    }

    template <typename D>
    W sub_with(const D& v, long long o) {
        if constexpr (std::is_pointer_v<native_type>) { const std::ptrdiff_t d = delta_of(v); return from(m_value.fetch_sub(d, order_of(o)) - d); }
        else return from(static_cast<native_type>(m_value.fetch_sub(to(v), order_of(o)) - to(v)));
    }

    W and_with(const W& v, long long o) { return from(static_cast<native_type>(m_value.fetch_and(to(v), order_of(o)) & to(v))); }
    W or_with(const W& v, long long o) { return from(static_cast<native_type>(m_value.fetch_or(to(v), order_of(o)) | to(v))); }
    W xor_with(const W& v, long long o) { return from(static_cast<native_type>(m_value.fetch_xor(to(v), order_of(o)) ^ to(v))); }

    W fetch_and_with(const W& v, long long o) { return from(m_value.fetch_and(to(v), order_of(o))); }
    W fetch_or_with(const W& v, long long o) { return from(m_value.fetch_or(to(v), order_of(o))); }
    W fetch_xor_with(const W& v, long long o) { return from(m_value.fetch_xor(to(v), order_of(o))); }

    W fetch_min_with(const W& v, long long o) {
        const native_type want = to(v);
        native_type cur = m_value.load(std::memory_order_relaxed);
        while (want < cur && !m_value.compare_exchange_weak(cur, want, order_of(o), std::memory_order_relaxed)) {}
        return from(cur);
    }

    W fetch_max_with(const W& v, long long o) {
        const native_type want = to(v);
        native_type cur = m_value.load(std::memory_order_relaxed);
        while (cur < want && !m_value.compare_exchange_weak(cur, want, order_of(o), std::memory_order_relaxed)) {}
        return from(cur);
    }

    void wait_with(const W& old, long long o) const {
        const native_type n = to(old);
        if (m_value.load(load_order(o)) != n) return;
        park_while(this, [&]() { return m_value.load(load_order(o)) == n; });
    }

    void notify() const { unpark(this); }
    bool lock_free() const noexcept { return m_value.is_lock_free(); }

private:
    template <typename D>
    static std::ptrdiff_t delta_of(const D& v) {
        if constexpr (std::is_arithmetic_v<D>) return static_cast<std::ptrdiff_t>(v);
        else return static_cast<std::ptrdiff_t>(to_native<long long>(v));
    }

    static native_type to(const W& v) {
        if constexpr (std::is_same_v<native_type, W>) return v;
        else return to_native<native_type>(v);
    }

    static W from(native_type v) {
        if constexpr (std::is_same_v<native_type, W>) return v;
        else return from_native<W>(v);
    }

    std::atomic<native_type> m_value;
};

} // namespace threading
} // namespace walnut_rt

#endif // WALNUT_THREADING_HPP
