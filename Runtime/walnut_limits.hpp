#ifndef WALNUT_LIMITS_HPP
#define WALNUT_LIMITS_HPP

#include "walnut_runtime.hpp"

#include <limits>

namespace walnut_rt {

namespace num {

template <typename... T>
struct tag {};

template <typename T> inline T epsilon() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::epsilon(); else return T::epsilon(); }
template <typename T> inline T smallest() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::min(); else return T::min(); }
template <typename T> inline T largest() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::max(); else return T::max(); }
template <typename T> inline T lowest() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::lowest(); else return T::lowest(); }
template <typename T> inline T subnormal_min() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::denorm_min(); else return T::subnormal_min(); }
template <typename T> inline T infinity_of() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::infinity(); else return T::positive_infinity(); }
template <typename T> inline T nan_of() { if constexpr (is_hardware_float_v<T>) return std::numeric_limits<T>::quiet_NaN(); else return T::nan(); }

} // namespace num

} // namespace walnut_rt

#endif // WALNUT_LIMITS_HPP
