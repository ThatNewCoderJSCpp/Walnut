#ifndef WALNUT_LAZY_HPP
#define WALNUT_LAZY_HPP

#include "walnut_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace lazy_detail {

inline std::vector<const void*>& computing() {
    thread_local std::vector<const void*> cells;
    return cells;
}

inline bool is_computing(const void* cell) {
    const std::vector<const void*>& cells = computing();
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

struct computing_guard {
    const void* cell;
    explicit computing_guard(const void* c) : cell(c) { computing().push_back(c); }
    ~computing_guard() {
        std::vector<const void*>& cells = computing();
        auto it = std::find(cells.begin(), cells.end(), cell);
        if (it != cells.end()) cells.erase(it);
    }
    computing_guard(const computing_guard&) = delete;
    computing_guard& operator=(const computing_guard&) = delete;
};

[[noreturn]] inline void while_computing(const char* name, const char* action) {
    fail(std::string("the lazy field '") + name + "' was " + action + " while its own value was being computed");
}

} // namespace lazy_detail

template <typename T, bool ThreadSafe>
class lazy_cell {
    static constexpr unsigned char empty_state = 0;
    static constexpr unsigned char ready_state = 1;
    static constexpr unsigned char busy_state  = 2;

    using state_type = std::conditional_t<ThreadSafe, std::atomic<unsigned char>, unsigned char>;

    alignas(T) unsigned char m_buf[sizeof(T)];
    state_type m_state{empty_state};

    T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(m_buf)); }
    const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(m_buf)); }

    unsigned char load() const noexcept {
        if constexpr (ThreadSafe) return m_state.load(std::memory_order_acquire);
        else return m_state;
    }

    void store(unsigned char s) noexcept {
        if constexpr (ThreadSafe) m_state.store(s, std::memory_order_release);
        else m_state = s;
    }

    unsigned char acquire(const char* name, const char* action) {
        if constexpr (ThreadSafe) {
            if (lazy_detail::is_computing(this)) {
                if (name) lazy_detail::while_computing(name, action);
                return busy_state;
            }

            for (;;) {
                unsigned char s = m_state.load(std::memory_order_acquire);
                if (s != busy_state && m_state.compare_exchange_weak(s, busy_state, std::memory_order_acquire)) return s;
                std::this_thread::yield();
            }
        } else {
            if (m_state == busy_state) {
                if (name) lazy_detail::while_computing(name, action);
                return busy_state;
            }

            const unsigned char s = m_state;
            m_state = busy_state;
            return s;
        }
    }

    void take(lazy_cell& from, bool move) {
        const unsigned char s = from.acquire(nullptr, nullptr);
        if (s == busy_state) return;

        if (s == ready_state) {
            try {
                if (move) ::new (static_cast<void*>(m_buf)) T(std::move(*from.ptr()));
                else      ::new (static_cast<void*>(m_buf)) T(*from.ptr());
            } catch (...) {
                from.store(s);
                throw;
            }

            store(ready_state);
            if (move) { from.ptr()->~T(); from.store(empty_state); return; }
        }

        from.store(s);
    }

    void replace_with(lazy_cell&& other) {
        const unsigned char s = acquire(nullptr, nullptr);
        if (s == busy_state) return;
        if (s == ready_state) ptr()->~T();

        if (other.load() == ready_state) {
            try {
                ::new (static_cast<void*>(m_buf)) T(std::move(*other.ptr()));
            } catch (...) {
                store(empty_state);
                throw;
            }
            store(ready_state);
            return;
        }

        store(empty_state);
    }

public:
    lazy_cell() noexcept {}

    lazy_cell(const T& value) {
        ::new (static_cast<void*>(m_buf)) T(value);
        store(ready_state);
    }

    lazy_cell(T&& value) {
        ::new (static_cast<void*>(m_buf)) T(std::move(value));
        store(ready_state);
    }

    lazy_cell(const lazy_cell& other) { take(const_cast<lazy_cell&>(other), false); }

    lazy_cell(lazy_cell&& other) { take(other, true); }

    lazy_cell& operator=(const lazy_cell& other) {
        if (this != &other) {
            lazy_cell copy(other);
            replace_with(std::move(copy));
        }
        return *this;
    }

    lazy_cell& operator=(lazy_cell&& other) {
        if (this != &other) {
            lazy_cell moved(std::move(other));
            replace_with(std::move(moved));
        }
        return *this;
    }

    ~lazy_cell() {
        if (load() == ready_state) ptr()->~T();
    }

    bool ready() const noexcept { return load() == ready_state; }

    template <typename F>
    T& get(F&& make, const char* name) {
        if (load() == ready_state) return *ptr();

        if constexpr (ThreadSafe) {
            if (lazy_detail::is_computing(this)) lazy_detail::while_computing(name, "read");

            for (;;) {
                unsigned char s = m_state.load(std::memory_order_acquire);
                if (s == ready_state) return *ptr();

                if (s == empty_state && m_state.compare_exchange_weak(s, busy_state, std::memory_order_acquire)) {
                    lazy_detail::computing_guard guard(this);
                    try {
                        ::new (static_cast<void*>(m_buf)) T(make());
                    } catch (...) {
                        store(empty_state);
                        throw;
                    }
                    store(ready_state);
                    return *ptr();
                }

                std::this_thread::yield();
            }
        } else {
            if (m_state == busy_state) lazy_detail::while_computing(name, "read");
            m_state = busy_state;

            try {
                ::new (static_cast<void*>(m_buf)) T(make());
            } catch (...) {
                m_state = empty_state;
                throw;
            }

            m_state = ready_state;
            return *ptr();
        }
    }

    T& set(T value, const char* name) {
        const unsigned char s = acquire(name, "assigned");
        if (s == ready_state) ptr()->~T();

        try {
            ::new (static_cast<void*>(m_buf)) T(std::move(value));
        } catch (...) {
            store(empty_state);
            throw;
        }

        store(ready_state);
        return *ptr();
    }

    void reset(const char* name) {
        const unsigned char s = acquire(name, "invalidated");
        if (s == ready_state) ptr()->~T();
        store(empty_state);
    }
};

} // namespace walnut_rt

#endif // WALNUT_LAZY_HPP
