#ifndef WALNUT_ARENA_VECTOR_HPP
#define WALNUT_ARENA_VECTOR_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "arena_allocator.hpp"

namespace walnut {

template <typename T>
class Span {
    T* m_data = nullptr;
    std::size_t m_size = 0;

public:
    using value_type = std::remove_cv_t<T>;
    using iterator = T*;
    using const_iterator = T*;

    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : m_data(data), m_size(size) {}

    template <typename C, typename = std::enable_if_t<std::is_convertible_v<decltype(std::declval<C&>().data()), T*>>>
    Span(C& c) noexcept : m_data(c.data()), m_size(c.size()) {}

    template <typename C, typename = std::enable_if_t<std::is_convertible_v<decltype(std::declval<const C&>().data()), T*>>, typename = void>
    Span(const C& c) noexcept : m_data(c.data()), m_size(c.size()) {}

    Span(std::initializer_list<std::remove_const_t<T>> init) noexcept : m_data(init.begin()), m_size(init.size()) {}

    constexpr T* data() const noexcept { return m_data; }
    constexpr std::size_t size() const noexcept { return m_size; }
    constexpr bool empty() const noexcept { return m_size == 0; }
    constexpr T* begin() const noexcept { return m_data; }
    constexpr T* end() const noexcept { return m_data + m_size; }
    constexpr T& operator[](std::size_t i) const noexcept { return m_data[i]; }
    constexpr T& front() const noexcept { return m_data[0]; }
    constexpr T& back() const noexcept { return m_data[m_size - 1]; }
    std::vector<value_type> to_vector() const { return std::vector<value_type>(begin(), end()); }

    friend bool operator==(Span a, Span b) { return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()); }
    friend bool operator!=(Span a, Span b) { return !(a == b); }
};

namespace detail {

template <typename T, bool Trivial = std::is_trivially_destructible_v<T>>
struct ArenaVectorStore {
    T* m_data = nullptr;
    std::uint32_t m_size = 0;
    std::uint32_t m_capacity = 0;

    void destroy_elements() noexcept {}
};

template <typename T>
struct ArenaVectorStore<T, false> {
    T* m_data = nullptr;
    std::uint32_t m_size = 0;
    std::uint32_t m_capacity = 0;

    void destroy_elements() noexcept {
        for (std::uint32_t i = 0; i < m_size; ++i) m_data[i].~T();
    }

    ArenaVectorStore() = default;
    ArenaVectorStore(const ArenaVectorStore&) = default;
    ArenaVectorStore& operator=(const ArenaVectorStore&) = default;
    ~ArenaVectorStore() { destroy_elements(); }
};

} // namespace detail

template <typename T>
class ArenaVector : private detail::ArenaVectorStore<T> {
    using Store = detail::ArenaVectorStore<T>;
    using Store::m_data;
    using Store::m_size;
    using Store::m_capacity;

    void grow_to(std::size_t want) {
        if (want <= m_capacity) return;
        std::size_t cap = m_capacity ? static_cast<std::size_t>(m_capacity) * 2 : 4;
        if (cap < want) cap = want;
        Arena& arena = Arena::current();
        if (m_data && arena.try_extend_last(m_data, sizeof(T) * m_capacity, sizeof(T) * cap)) {
            m_capacity = static_cast<std::uint32_t>(cap);
            return;
        }
        T* fresh = arena.template alloc_array_uninit<T>(cap);
        for (std::uint32_t i = 0; i < m_size; ++i) {
            ::new (fresh + i) T(std::move(m_data[i]));
            m_data[i].~T();
        }
        m_data = fresh;
        m_capacity = static_cast<std::uint32_t>(cap);
    }

    template <typename It>
    void append_range(It first, It last) {
        const std::size_t n = static_cast<std::size_t>(std::distance(first, last));
        grow_to(m_size + n);
        for (; first != last; ++first) ::new (m_data + m_size++) T(*first);
    }

    void take(ArenaVector& o) noexcept {
        m_data = o.m_data;
        m_size = o.m_size;
        m_capacity = o.m_capacity;
        o.m_data = nullptr;
        o.m_size = 0;
        o.m_capacity = 0;
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    ArenaVector() noexcept = default;
    ArenaVector(std::initializer_list<T> init) { append_range(init.begin(), init.end()); }
    ArenaVector(const std::vector<T>& v) { append_range(v.begin(), v.end()); }
    ArenaVector(std::vector<T>&& v) {
        grow_to(v.size());
        for (T& x : v) ::new (m_data + m_size++) T(std::move(x));
    }
    template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
    ArenaVector(It first, It last) { append_range(first, last); }
    explicit ArenaVector(std::size_t n, const T& value = T()) { assign(n, value); }

    ArenaVector(const ArenaVector& o) : Store() { append_range(o.begin(), o.end()); }
    ArenaVector(ArenaVector&& o) noexcept : Store() { take(o); }

    ArenaVector& operator=(const ArenaVector& o) {
        if (this != &o) { clear(); append_range(o.begin(), o.end()); }
        return *this;
    }
    ArenaVector& operator=(ArenaVector&& o) noexcept {
        if (this != &o) { this->destroy_elements(); take(o); }
        return *this;
    }
    ArenaVector& operator=(const std::vector<T>& v) {
        clear();
        append_range(v.begin(), v.end());
        return *this;
    }
    ArenaVector& operator=(std::vector<T>&& v) {
        clear();
        grow_to(v.size());
        for (T& x : v) ::new (m_data + m_size++) T(std::move(x));
        return *this;
    }
    ArenaVector& operator=(std::initializer_list<T> init) {
        clear();
        append_range(init.begin(), init.end());
        return *this;
    }

    T* data() noexcept { return m_data; }
    const T* data() const noexcept { return m_data; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t capacity() const noexcept { return m_capacity; }
    bool empty() const noexcept { return m_size == 0; }

    iterator begin() noexcept { return m_data; }
    iterator end() noexcept { return m_data + m_size; }
    const_iterator begin() const noexcept { return m_data; }
    const_iterator end() const noexcept { return m_data + m_size; }
    const_iterator cbegin() const noexcept { return m_data; }
    const_iterator cend() const noexcept { return m_data + m_size; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    T& operator[](std::size_t i) noexcept { return m_data[i]; }
    const T& operator[](std::size_t i) const noexcept { return m_data[i]; }
    T& at(std::size_t i) { return m_data[i]; }
    const T& at(std::size_t i) const { return m_data[i]; }
    T& front() noexcept { return m_data[0]; }
    const T& front() const noexcept { return m_data[0]; }
    T& back() noexcept { return m_data[m_size - 1]; }
    const T& back() const noexcept { return m_data[m_size - 1]; }

    void reserve(std::size_t n) { grow_to(n); }
    void shrink_to_fit() noexcept {}

    void clear() noexcept {
        this->destroy_elements();
        m_size = 0;
    }

    void push_back(const T& v) {
        if (m_size == m_capacity) {
            T copy(v);
            grow_to(m_size + 1);
            ::new (m_data + m_size++) T(std::move(copy));
            return;
        }
        ::new (m_data + m_size++) T(v);
    }
    void push_back(T&& v) {
        if (m_size == m_capacity) {
            T moved(std::move(v));
            grow_to(m_size + 1);
            ::new (m_data + m_size++) T(std::move(moved));
            return;
        }
        ::new (m_data + m_size++) T(std::move(v));
    }
    template <typename... Args>
    T& emplace_back(Args&&... args) {
        if (m_size == m_capacity) {
            T made(std::forward<Args>(args)...);
            grow_to(m_size + 1);
            return *::new (m_data + m_size++) T(std::move(made));
        }
        return *::new (m_data + m_size++) T(std::forward<Args>(args)...);
    }
    void pop_back() noexcept { m_data[--m_size].~T(); }

    void resize(std::size_t n) {
        if (n < m_size) {
            for (std::size_t i = n; i < m_size; ++i) m_data[i].~T();
            m_size = static_cast<std::uint32_t>(n);
            return;
        }
        grow_to(n);
        while (m_size < n) ::new (m_data + m_size++) T();
    }
    void resize(std::size_t n, const T& value) {
        if (n < m_size) { resize(n); return; }
        T copy(value);
        grow_to(n);
        while (m_size < n) ::new (m_data + m_size++) T(copy);
    }

    void assign(std::size_t n, const T& value) {
        T copy(value);
        clear();
        grow_to(n);
        while (m_size < n) ::new (m_data + m_size++) T(copy);
    }
    template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
    void assign(It first, It last) {
        std::vector<T> tmp(first, last);
        clear();
        grow_to(tmp.size());
        for (T& x : tmp) ::new (m_data + m_size++) T(std::move(x));
    }

    iterator insert(const_iterator pos, T value) {
        const std::size_t at = static_cast<std::size_t>(pos - m_data);
        grow_to(m_size + 1);
        if (at == m_size) {
            ::new (m_data + m_size++) T(std::move(value));
            return m_data + at;
        }
        ::new (m_data + m_size) T(std::move(m_data[m_size - 1]));
        for (std::size_t i = m_size - 1; i > at; --i) m_data[i] = std::move(m_data[i - 1]);
        m_data[at] = std::move(value);
        ++m_size;
        return m_data + at;
    }
    template <typename It, typename = std::enable_if_t<!std::is_integral_v<It>>>
    iterator insert(const_iterator pos, It first, It last) {
        const std::size_t at = static_cast<std::size_t>(pos - m_data);
        std::vector<T> tmp(first, last);
        if (tmp.empty()) return m_data + at;
        std::vector<T> tail(std::make_move_iterator(m_data + at), std::make_move_iterator(m_data + m_size));
        resize(at);
        grow_to(at + tmp.size() + tail.size());
        for (T& x : tmp) ::new (m_data + m_size++) T(std::move(x));
        for (T& x : tail) ::new (m_data + m_size++) T(std::move(x));
        return m_data + at;
    }
    iterator insert(const_iterator pos, std::initializer_list<T> init) { return insert(pos, init.begin(), init.end()); }

    iterator erase(const_iterator pos) { return erase(pos, pos + 1); }
    iterator erase(const_iterator first, const_iterator last) {
        T* f = m_data + (first - m_data);
        T* l = m_data + (last - m_data);
        if (f == l) return f;
        T* new_end = std::move(l, m_data + m_size, f);
        for (T* p = new_end; p != m_data + m_size; ++p) p->~T();
        m_size = static_cast<std::uint32_t>(new_end - m_data);
        return f;
    }

    std::vector<T> to_vector() const { return std::vector<T>(begin(), end()); }

    friend bool operator==(const ArenaVector& a, const ArenaVector& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
    friend bool operator!=(const ArenaVector& a, const ArenaVector& b) { return !(a == b); }
    friend bool operator==(const ArenaVector& a, const std::vector<T>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
    friend bool operator!=(const ArenaVector& a, const std::vector<T>& b) { return !(a == b); }
};

} // namespace walnut

#endif // WALNUT_ARENA_VECTOR_HPP
