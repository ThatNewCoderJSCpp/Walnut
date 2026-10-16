#ifndef WALNUT_OPTIONAL_HPP
#define WALNUT_OPTIONAL_HPP

#include "walnut_runtime.hpp"

#include <optional>
#include <utility>

namespace walnut_rt {
namespace opt {

template <typename T>
using optional = std::optional<T>;

template <typename T, typename N>
inline std::optional<T> from_none(const N&) { return std::nullopt; }

template <typename T>
inline T& get(std::optional<T>& o) { return *o; }

template <typename T>
inline const T& get(const std::optional<T>& o) { return *o; }

template <typename T>
inline T* pointer(std::optional<T>& o) { return o ? &*o : nullptr; }

template <typename T>
inline const T* pointer(const std::optional<T>& o) { return o ? &*o : nullptr; }

template <typename T, typename V>
inline void assign(std::optional<T>& o, V&& value) { o = std::forward<V>(value); }

template <typename T>
inline T take(std::optional<T>& o) {
    T out = std::move(*o);
    o.reset();
    return out;
}

} // namespace opt
} // namespace walnut_rt

#endif // WALNUT_OPTIONAL_HPP
