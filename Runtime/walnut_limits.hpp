#ifndef WALNUT_LIMITS_HPP
#define WALNUT_LIMITS_HPP

#include "walnut_runtime.hpp"

#include <limits>
#include <type_traits>

namespace walnut_rt {

namespace num {

template <typename... T>
struct tag {};

template <typename T> inline T epsilon() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::epsilon(); else return T::epsilon(); }
template <typename T> inline T smallest() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::min(); else return T::min(); }
template <typename T> inline T largest() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::max(); else return T::max(); }
template <typename T, typename = void>
struct has_own_lowest : std::false_type {};

template <typename T>
struct has_own_lowest<T, std::void_t<decltype(T::lowest())>> : std::true_type {};

template <typename T, typename = void>
struct has_own_subnormal_min : std::false_type {};

template <typename T>
struct has_own_subnormal_min<T, std::void_t<decltype(T::subnormal_min())>> : std::true_type {};

template <typename T> inline T lowest() {
    if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::lowest();
    else if constexpr (has_own_lowest<T>::value) return T::lowest();
    else return T::zero();
}
template <typename T> inline T subnormal_min() {
    if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::denorm_min();
    else if constexpr (has_own_subnormal_min<T>::value) return T::subnormal_min();
    else return T::min() * T::epsilon();
}
template <typename T> inline T infinity_of() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::infinity(); else return T::positive_infinity(); }
template <typename T> inline T nan_of() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::quiet_NaN(); else return T::nan(); }

} // namespace num

} // namespace walnut_rt

#endif // WALNUT_LIMITS_HPP
