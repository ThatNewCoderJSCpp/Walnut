#ifndef WALNUT_RANDOM_HPP
#define WALNUT_RANDOM_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../../Source/Random/random_generator.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>
#include <vector>

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

template <typename F>
inline F decimal_step(long long k, const F& scale) {
    return F(k) / scale;
}

template <typename F>
struct float_is_signed : std::true_type {};

template <std::size_t TB, std::size_t MB>
struct float_is_signed<mp::floatmp<TB, MB, mp::sign::is_unsigned>> : std::false_type {};

template <typename F>
struct decimal_grid {
    static constexpr std::size_t mantissa = static_cast<std::size_t>(F::mantissa_bits);
    static constexpr std::size_t bits = mantissa + 6 <= 64 ? 64 : mantissa + 6 <= 128 ? 128 : mantissa + 6 <= 256 ? 256 : mantissa + 6 <= 512 ? 512 : mantissa + 6 <= 1024 ? 1024 : 2048;
    static_assert(mantissa + 6 <= bits, "random_float: no integer type is wide enough for this float type");
    using K = wint<bits, mp::sign::is_signed>;
    using R = typename K::raw_type;

    static constexpr unsigned long long all_decimals = (1ULL << (static_cast<std::size_t>(F::exponent_bits) - 1)) + static_cast<unsigned long long>(mantissa) + 2ULL;

    std::vector<F> factors;
    F limit;

    static F power_of_ten(unsigned long long n) {
        F result(1);
        F base(10);
        while (n != 0) {
            if ((n & 1ULL) != 0) result = result * base;
            n >>= 1;
            if (n != 0) base = base * base;
            if (!result.is_finite() || !base.is_finite()) return F::infinity();
        }
        return result;
    }

    explicit decimal_grid(unsigned long long digits) : limit(1) {
        for (std::size_t i = 0; i + 3 < bits; ++i) limit = limit * F(2);
        unsigned long long chunk = 1;
        while (chunk < (1ULL << 62) && power_of_ten(chunk * 2).is_finite()) chunk *= 2;
        const F big = power_of_ten(chunk);
        for (unsigned long long left = digits; left != 0;) {
            const unsigned long long step = left < chunk ? left : chunk;
            factors.push_back(step == chunk ? big : power_of_ten(step));
            left -= step;
        }
    }

    bool fits(const F& v) const {
        if (!v.is_finite() || !(v < limit)) return false;
        if constexpr (float_is_signed<F>::value) return -limit < v;
        else return true;
    }

    bool scaled(const F& x, F& out) const {
        out = x;
        for (const F& f : factors) {
            out = out * f;
            if (!fits(out)) return false;
        }
        return true;
    }

    F value(const R& k) const {
        F out = int_to_float<F>(K(k));
        for (const F& f : factors) out = out / f;
        return out;
    }

    R nearest(const F& s) const {
        R k = float_to_int<K>(s).raw();
        const F rest = s - int_to_float<F>(K(k));
        if (!(rest < F(0.5))) k = k + R(1);
        if constexpr (float_is_signed<F>::value) {
            if (!(-F(0.5) < rest)) k = k - R(1);
        }
        return k;
    }

    R first_at_least(const F& lo, R first, R last) const {
        while (first < last) {
            const R mid = first + (last - first) / R(2);
            if (value(mid) < lo) first = mid + R(1);
            else last = mid;
        }
        return first;
    }

    R last_at_most(const F& hi, R first, R last) const {
        while (first < last) {
            const R mid = first + (last - first + R(1)) / R(2);
            if (hi < value(mid)) last = mid - R(1);
            else first = mid;
        }
        return first;
    }

    R guess(const F& v, const R& floor_k, const R& ceil_k, const R& shift) const {
        F s;
        if (!scaled(v, s)) return floor_k;
        R k = nearest(s) + shift;
        if (k < floor_k) k = floor_k;
        if (ceil_k < k) k = ceil_k;
        return k;
    }

    R low_index(const F& lo, const R& floor_k, const R& ceil_k) const {
        const R a = guess(lo, floor_k, ceil_k, R(-8));
        const R b = guess(lo, floor_k, ceil_k, R(8));
        if ((a == floor_k || value(a) < lo) && !(value(b) < lo)) return first_at_least(lo, a, b);
        return first_at_least(lo, floor_k, ceil_k);
    }

    R high_index(const F& hi, const R& floor_k, const R& ceil_k) const {
        const R a = guess(hi, floor_k, ceil_k, R(-8));
        const R b = guess(hi, floor_k, ceil_k, R(8));
        if (!(hi < value(a)) && (b == ceil_k || hi < value(b))) return last_at_most(hi, a, b);
        return last_at_most(hi, floor_k, ceil_k);
    }
};

template <typename F, typename PickInt, typename PickReal>
inline F float_decimals_impl(const F& a, const F& b, unsigned long long digits, PickInt&& pick_int, PickReal&& pick_real) {
    check_bounds(a, b, "random_float");
    const F lo = b < a ? b : a;
    const F hi = b < a ? a : b;
    using G = decimal_grid<F>;
    using R = typename G::R;
    if (digits >= G::all_decimals) return pick_real(lo, hi);
    const G grid(digits);
    F slo, shi;
    if (grid.scaled(lo, slo) && grid.scaled(hi, shi)) {
        const R top = float_to_int<typename G::K>(grid.limit).raw();
        const R zero(0);
        R floor_k = zero - top;
        R ceil_k = top;
        if (!(lo < F(0))) floor_k = zero;
        if (!(F(0) < hi)) ceil_k = zero;
        const R low_k = grid.low_index(lo, floor_k, ceil_k);
        const R high_k = grid.high_index(hi, floor_k, ceil_k);
        if (high_k < low_k || grid.value(low_k) < lo || hi < grid.value(low_k)) fail("random_float: there is no number with " + std::to_string(digits) + " decimal places between the bounds");
        return grid.value(pick_int(low_k, high_k));
    }
    for (int attempt = 0; attempt < 64; ++attempt) {
        const F x = pick_real(lo, hi);
        if (x.is_zero()) return x;
        F s;
        if (!grid.scaled(x, s)) return x;
        const F r = grid.value(grid.nearest(s));
        if (!(r < lo) && !(hi < r)) return r;
    }
    return pick_real(lo, hi);
}

template <typename F>
inline F float_decimals(const F& a, const F& b, unsigned long long digits) {
    return float_decimals_impl(a, b, digits, [](const auto& l, const auto& h) { return fizmo::random_int(l, h); }, [](const F& l, const F& h) { return guarded([&] { return fizmo::random_real<F>(l, h); }); });
}

template <typename F>
inline F float_decimals_with(fizmo::RandomGenerator& g, const F& a, const F& b, unsigned long long digits) {
    return float_decimals_impl(a, b, digits, [&](const auto& l, const auto& h) { return g.random_int(l, h); }, [&](const F& l, const F& h) { return guarded([&] { return g.random_real(l, h); }); });
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
