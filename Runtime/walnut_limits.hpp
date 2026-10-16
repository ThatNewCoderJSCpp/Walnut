#ifndef WALNUT_LIMITS_HPP
#define WALNUT_LIMITS_HPP

#include "walnut_runtime.hpp"

namespace walnut_rt {

namespace num {

template <typename... T>
struct tag {};

template <typename T> inline T epsilon() { return T::epsilon(); }
template <typename T> inline T smallest() { return T::min(); }
template <typename T> inline T largest() { return T::max(); }
template <typename T> inline T lowest() { return T::lowest(); }
template <typename T> inline T subnormal_min() { return T::subnormal_min(); }
template <typename T> inline T infinity_of() { return T::positive_infinity(); }
template <typename T> inline T nan_of() { return T::nan(); }

} // namespace num

} // namespace walnut_rt

#endif // WALNUT_LIMITS_HPP
