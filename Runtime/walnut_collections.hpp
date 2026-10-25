#ifndef WALNUT_COLLECTIONS_HPP
#define WALNUT_COLLECTIONS_HPP

#include "walnut_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace coll {

struct bool_cell {
    bool value = false;
    bool_cell() = default;
    bool_cell(bool b) : value(b) {}
    operator bool() const { return value; }
};

template <typename T> struct cell_of { using type = T; };
template <> struct cell_of<bool> { using type = bool_cell; };

template <typename T>
using vector = std::vector<typename cell_of<T>::type>;

template <typename E> struct element_of { using type = E; };
template <> struct element_of<bool_cell> { using type = bool; };

template <typename C>
using element_t = typename element_of<typename C::value_type>::type;

template <typename C>
inline auto data(C& c) {
    using E = element_t<std::remove_const_t<C>>;
    if constexpr (std::is_const_v<C>) return reinterpret_cast<const E*>(c.data());
    else return reinterpret_cast<E*>(c.data());
}

template <typename C>
inline std::int64_t hardware_size(const C& c) { return static_cast<std::int64_t>(c.size()); }

template <typename C>
inline auto begin(C& c) { return coll::data(c); }

template <typename C>
inline auto end(C& c) { return coll::data(c) + c.size(); }

template <typename T>
inline vector<T> from_items(std::vector<T> items) {
    if constexpr (std::is_same_v<T, bool>) return vector<T>(items.begin(), items.end());
    else return items;
}

template <typename C>
inline element_t<C> take_back(C& c) {
    element_t<C> out = std::move(coll::data(c)[c.size() - 1]);
    c.pop_back();
    return out;
}

template <typename C, typename V>
inline void insert_at(C& c, long long index, const V& value) {
    c.insert(c.begin() + index, value);
}

template <typename C>
inline element_t<C> erase_at(C& c, long long index) {
    element_t<C> out = std::move(coll::data(c)[index]);
    c.erase(c.begin() + index);
    return out;
}

template <typename C>
inline void truncate(C& c, long long count) {
    c.erase(c.begin() + count, c.end());
}

template <typename C>
inline void append(C& c, const C& other) {
    if (&c == &other) {
        const C copy = other;
        c.insert(c.end(), copy.begin(), copy.end());
        return;
    }
    c.insert(c.end(), other.begin(), other.end());
}

template <typename C>
inline void reverse(C& c) {
    std::reverse(coll::data(c), coll::data(c) + c.size());
}

template <typename C>
inline void sort(C& c) {
    std::sort(coll::data(c), coll::data(c) + c.size());
}

template <typename C>
inline void resize_fill(C& c, long long count, const element_t<C>& value) {
    c.resize(static_cast<std::size_t>(count), value);
}

} // namespace coll
} // namespace walnut_rt

#endif // WALNUT_COLLECTIONS_HPP
