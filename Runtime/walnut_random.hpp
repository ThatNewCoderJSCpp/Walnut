#ifndef WALNUT_RANDOM_HPP
#define WALNUT_RANDOM_HPP

#include "walnut_runtime.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>

namespace walnut_rt {
namespace random {

template <typename T>
struct ops {};

struct api {};

template <typename Fn>
inline auto guarded(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const runtime_error&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

template <typename W>
inline void check_bounds(const W& a, const W& b, const char* what) {
    if constexpr (is_int_v<W>) {
        if (a.is_special() || b.is_special()) fail(std::string(what) + ": the bounds must be ordinary numbers, not special values");
    } else if constexpr (is_float_v<W>) {
        if (!a.is_finite() || !b.is_finite()) fail(std::string(what) + ": the bounds must be finite numbers");
    }
}

template <typename W>
inline W int_between(const W& a, const W& b) {
    check_bounds(a, b, "random_int");
    if constexpr (is_int_v<W>) return W(guarded([&] { return fizmo::random_int(a.raw(), b.raw()); }));
    else return guarded([&] { return fizmo::random_int(a, b); });
}

template <typename W>
inline W int_any() {
    if constexpr (is_int_v<W>) return W(guarded([&] { return fizmo::random_int(W::raw_type::min(), W::raw_type::max()); }));
    else return fizmo::random_int<W>();
}

template <typename W>
inline W int_between_with(fizmo::RandomGenerator& g, const W& a, const W& b) {
    check_bounds(a, b, "random_int");
    if constexpr (is_int_v<W>) return W(g.random_int(a.raw(), b.raw()));
    else return g.random_int(a, b);
}

template <typename W>
inline W int_any_with(fizmo::RandomGenerator& g) {
    if constexpr (is_int_v<W>) return W(g.random_int(W::raw_type::min(), W::raw_type::max()));
    else return g.random_int<W>();
}

template <typename F>
inline F float_between(const F& a, const F& b) {
    check_bounds(a, b, "random_float");
    return guarded([&] { return fizmo::random_real<F>(a, b); });
}

template <typename F>
inline F float_unit() {
    return guarded([&] { return fizmo::random_real<F>(); });
}

template <typename F>
inline F float_between_with(fizmo::RandomGenerator& g, const F& a, const F& b) {
    check_bounds(a, b, "random_float");
    return guarded([&] { return g.random_real(a, b); });
}

template <typename F>
inline F float_unit_with(fizmo::RandomGenerator& g) {
    return guarded([&] { return g.random_unit<F>(); });
}

inline long long checked_length(long long length) {
    if (length < 0) fail("random_string: the length " + std::to_string(length) + " is negative");
    return length;
}

inline bool bool_any() { return fizmo::random_bool(); }
inline bool bool_chance(double p) { return fizmo::random_bool(p); }
inline char32_t char_any() { return fizmo::random_char(); }
inline char32_t char_between(char32_t lo, char32_t hi) { return guarded([&] { return fizmo::random_char(lo, hi); }); }
inline char32_t char_from(const std::string& alphabet) { return guarded([&] { return fizmo::random_char(alphabet); }); }
inline std::string string_of(long long length) { return fizmo::random_string(static_cast<std::size_t>(checked_length(length))); }
inline std::string string_from(long long length, const std::string& alphabet) { return guarded([&] { return fizmo::random_string(static_cast<std::size_t>(checked_length(length)), alphabet); }); }

inline bool bool_any_with(fizmo::RandomGenerator& g) { return g.random_bool(); }
inline bool bool_chance_with(fizmo::RandomGenerator& g, double p) { return g.random_bool(p); }
inline char32_t char_any_with(fizmo::RandomGenerator& g) { return g.random_char(); }
inline char32_t char_between_with(fizmo::RandomGenerator& g, char32_t lo, char32_t hi) { return guarded([&] { return g.random_char(lo, hi); }); }
inline char32_t char_from_with(fizmo::RandomGenerator& g, const std::string& alphabet) { return guarded([&] { return g.random_char(alphabet); }); }
inline std::string string_of_with(fizmo::RandomGenerator& g, long long length) { return g.random_string(static_cast<std::size_t>(checked_length(length))); }
inline std::string string_from_with(fizmo::RandomGenerator& g, long long length, const std::string& alphabet) { return guarded([&] { return g.random_string(static_cast<std::size_t>(checked_length(length)), alphabet); }); }

inline long long index_below(long long n) { return static_cast<long long>(fizmo::random_int<std::uint64_t>(0, static_cast<std::uint64_t>(n - 1))); }
inline long long index_below_with(fizmo::RandomGenerator& g, long long n) { return static_cast<long long>(g.below(static_cast<std::uint64_t>(n))); }

} // namespace random
} // namespace walnut_rt

#endif // WALNUT_RANDOM_HPP
