#ifndef WALNUT_SMALL_VECTOR_HPP
#define WALNUT_SMALL_VECTOR_HPP

#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>
#include <type_traits>
#include <initializer_list>

namespace walnut {

template <typename T, std::size_t N>
class SmallVector {
    static_assert(N > 0, "SmallVector inline capacity must be >= 1");

public:
    using value_type      = T;
    using size_type       = std::size_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator        = T*;
    using const_iterator  = const T*;

    SmallVector() noexcept : m_data(inline_ptr()), m_size(0), m_capacity(N) {}

    SmallVector(std::initializer_list<T> il) : SmallVector() {
        reserve(il.size());
        for (const T& v : il) push_back(v);
    }

    template <typename It>
    SmallVector(It first, It last) : SmallVector() { assign(first, last); }

    SmallVector(const SmallVector& o) : SmallVector() {
        reserve(o.m_size);
        for (size_type i = 0; i < o.m_size; ++i) construct(m_data + i, o.m_data[i]);
        m_size = o.m_size;
    }

    SmallVector(SmallVector&& o)
        noexcept(std::is_nothrow_move_constructible_v<T>) : SmallVector() {
        move_from(std::move(o));
    }

    SmallVector& operator=(const SmallVector& o) {
        if (this == &o) return *this;
        clear();
        reserve(o.m_size);
        for (size_type i = 0; i < o.m_size; ++i) construct(m_data + i, o.m_data[i]);
        m_size = o.m_size;
        return *this;
    }

    SmallVector& operator=(SmallVector&& o)
        noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this == &o) return *this;
        clear();
        if (!is_inline()) std::free(m_data);
        m_data = inline_ptr();
        m_capacity = N;
        m_size = 0;
        move_from(std::move(o));
        return *this;
    }

    ~SmallVector() {
        clear();
        if (!is_inline()) std::free(m_data);
    }

    reference       operator[](size_type i) noexcept       { return m_data[i]; }
    const_reference operator[](size_type i) const noexcept { return m_data[i]; }
    reference       back()  noexcept       { return m_data[m_size - 1]; }
    const_reference back()  const noexcept { return m_data[m_size - 1]; }
    reference       front() noexcept       { return m_data[0]; }
    const_reference front() const noexcept { return m_data[0]; }
    pointer         data()  noexcept       { return m_data; }
    const_pointer   data()  const noexcept { return m_data; }

    iterator       begin()  noexcept       { return m_data; }
    const_iterator begin()  const noexcept { return m_data; }
    iterator       end()    noexcept       { return m_data + m_size; }
    const_iterator end()    const noexcept { return m_data + m_size; }
    const_iterator cbegin() const noexcept { return m_data; }
    const_iterator cend()   const noexcept { return m_data + m_size; }

    bool      empty()    const noexcept { return m_size == 0; }
    size_type size()     const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void reserve(size_type cap) { if (cap > m_capacity) grow_to(cap); }

    void clear() noexcept {
        for (size_type i = 0; i < m_size; ++i) m_data[i].~T();
        m_size = 0;
    }

    void push_back(const T& v) {
        if (m_size == m_capacity) grow_to(m_capacity * 2);
        construct(m_data + m_size, v);
        ++m_size;
    }

    void push_back(T&& v) {
        if (m_size == m_capacity) grow_to(m_capacity * 2);
        construct(m_data + m_size, std::move(v));
        ++m_size;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if (m_size == m_capacity) grow_to(m_capacity * 2);
        construct(m_data + m_size, std::forward<Args>(args)...);
        return m_data[m_size++];
    }

    void pop_back() noexcept {
        --m_size;
        m_data[m_size].~T();
    }

    void resize(size_type n) {
        if (n < m_size) {
            for (size_type i = n; i < m_size; ++i) m_data[i].~T();
        } else if (n > m_size) {
            reserve(n);
            for (size_type i = m_size; i < n; ++i) construct(m_data + i);
        }

        m_size = n;
    }

    template <typename It>
    void assign(It first, It last) {
        clear();
        for (; first != last; ++first) push_back(*first);
    }

private:
    pointer       inline_ptr() noexcept       { return reinterpret_cast<pointer>(m_inline); }
    const_pointer inline_ptr() const noexcept { return reinterpret_cast<const_pointer>(m_inline); }

    bool is_inline() const noexcept {
        return static_cast<const void*>(m_data) == static_cast<const void*>(m_inline);
    }

    template <typename... Args>
    static void construct(pointer p, Args&&... args) {
        ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
    }

    void grow_to(size_type new_cap) {
        if (new_cap < N) new_cap = N;
        pointer new_data = static_cast<pointer>(std::malloc(new_cap * sizeof(T)));
        if (!new_data) throw std::bad_alloc();

        for (size_type i = 0; i < m_size; ++i) {
            construct(new_data + i, std::move_if_noexcept(m_data[i]));
            m_data[i].~T();
        }

        if (!is_inline()) std::free(m_data);
        m_data = new_data;
        m_capacity = new_cap;
    }

    void move_from(SmallVector&& o) {
        if (o.is_inline()) {
            for (size_type i = 0; i < o.m_size; ++i) construct(m_data + i, std::move(o.m_data[i]));
            m_size = o.m_size;       
            o.clear();
        } else {
            m_data     = o.m_data;   
            m_capacity = o.m_capacity;
            m_size     = o.m_size;
            o.m_data     = o.inline_ptr();
            o.m_capacity = N;
            o.m_size     = 0;
        }
    }

    pointer   m_data;
    size_type m_size;
    size_type m_capacity;
    alignas(T) std::byte m_inline[N * sizeof(T)];
};

} // namespace walnut

#endif // WALNUT_SMALL_VECTOR_HPP