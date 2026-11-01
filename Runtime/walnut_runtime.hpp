#ifndef WALNUT_RUNTIME_HPP
#define WALNUT_RUNTIME_HPP

#ifndef FIZMO
#define FIZMO
#endif
#include "fizmo/includes.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cmath>
#include <limits>
#include <iostream>
#include <memory>
#include <new>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#define WALNUT_RT_COROUTINES 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define WALNUT_FORCE_INLINE [[gnu::always_inline]] inline
#elif defined(_MSC_VER)
#define WALNUT_FORCE_INLINE __forceinline
#else
#define WALNUT_FORCE_INLINE inline
#endif

namespace walnut_rt {

namespace mp = fizmo::multiprecision;

using f32   = mp::float32;
using f64   = mp::float64;
using f128  = mp::float128;
using f256  = mp::float256;
using f512  = mp::float512;
using f1024 = mp::float1024;
using f2048 = mp::float2048;

using uf32   = mp::ufloat32;
using uf64   = mp::ufloat64;
using uf128  = mp::ufloat128;
using uf256  = mp::ufloat256;
using uf512  = mp::ufloat512;
using uf1024 = mp::ufloat1024;
using uf2048 = mp::ufloat2048;

using string    = std::string;
using text      = std::string;
using character = char32_t;

template <class T>
inline T& volatile_access(T& object) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(&object) : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
    return object;
}

struct runtime_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace walnut_rt

#include "walnut_safety.hpp"

namespace walnut_rt {

#if defined(WALNUT_SAFETY)
[[noreturn]] inline void fail(const std::string& message) { throw runtime_error(safety::where_now() + message); }
#else
[[noreturn]] inline void fail(const std::string& message) { throw runtime_error(message); }
#endif

enum class special_kind : std::uint8_t { none = 0, pos_inf, neg_inf, nan, undefined, indeterminate };

enum class special_test : std::uint8_t { infinity = 0, nan, undefined, indeterminate };

inline constexpr bool unordered_kind(special_kind k) noexcept {
    return k == special_kind::nan || k == special_kind::undefined || k == special_kind::indeterminate;
}

inline constexpr bool infinite_kind(special_kind k) noexcept {
    return k == special_kind::pos_inf || k == special_kind::neg_inf;
}

inline constexpr special_kind inf_kind(bool negative) noexcept {
    return negative ? special_kind::neg_inf : special_kind::pos_inf;
}

inline constexpr special_kind unordered_of(special_kind a, special_kind b) noexcept {
    return (a == special_kind::undefined     || b == special_kind::undefined)     ? special_kind::undefined
         : (a == special_kind::indeterminate || b == special_kind::indeterminate) ? special_kind::indeterminate
         : special_kind::nan;
}

inline constexpr bool special_matches(special_kind k, special_test t) noexcept {
    switch (t) {
        case special_test::infinity:      return infinite_kind(k);
        case special_test::nan:           return k == special_kind::nan;
        case special_test::undefined:     return k == special_kind::undefined;
        case special_test::indeterminate: return k == special_kind::indeterminate;
    }
    return false;
}

inline const char* special_name(special_kind k) noexcept {
    switch (k) {
        case special_kind::pos_inf:       return "∞";
        case special_kind::neg_inf:       return "-∞";
        case special_kind::nan:           return "nan";
        case special_kind::undefined:     return "undefined";
        case special_kind::indeterminate: return "indeterminate";
        default:                          return "";
    }
}

struct special_value {
    special_kind kind = special_kind::undefined;

    constexpr special_value() noexcept = default;
    constexpr special_value(special_kind k) noexcept : kind(k) {}

    std::string to_string() const { return special_name(kind); }
    friend std::ostream& operator<<(std::ostream& os, const special_value& s) { return os << special_name(s.kind); }
};

inline special_value op_neg(special_value v) {
    if (v.kind == special_kind::pos_inf) return special_kind::neg_inf;
    if (v.kind == special_kind::neg_inf) return special_kind::pos_inf;
    return v;
}

inline special_value op_pos(special_value v) { return v; }

template <special_kind K> struct special_constant;

template <typename T> struct special_constant_info { static constexpr bool value = false; };
template <special_kind K> struct special_constant_info<special_constant<K>> { static constexpr bool value = true; static constexpr special_kind kind = K; };

template <typename T> inline constexpr bool is_special_constant_v = special_constant_info<std::decay_t<T>>::value;

template <typename T> inline constexpr bool is_hardware_float_v = std::is_floating_point_v<std::decay_t<T>>;

template <typename T> inline constexpr bool is_hardware_int_v =
    std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> && !std::is_same_v<std::decay_t<T>, char32_t> && !std::is_same_v<std::decay_t<T>, char>;

template <typename T> inline constexpr bool is_hardware_number_v = is_hardware_float_v<T> || is_hardware_int_v<T>;

template <typename T>
using hardware_wide_t = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;

template <typename F>
inline special_kind float_kind(const F& f) {
    if constexpr (is_hardware_float_v<F>) {
        if (std::isnan(f)) return special_kind::nan;
        if (std::isinf(f)) return inf_kind(std::signbit(f));
        return special_kind::none;
    } else {
        if (f.is_nan())           return special_kind::nan;
        if (f.is_undefined())     return special_kind::undefined;
        if (f.is_indeterminate()) return special_kind::indeterminate;
        if (f.is_infinite())      return inf_kind(f.is_negative());
        return special_kind::none;
    }
}

template <typename F>
inline F float_special(special_kind k) {
    if constexpr (is_hardware_float_v<F>) {
        switch (k) {
            case special_kind::pos_inf: return std::numeric_limits<F>::infinity();
            case special_kind::neg_inf: return -std::numeric_limits<F>::infinity();
            case special_kind::none:    return F(0);
            default:                    return std::numeric_limits<F>::quiet_NaN();
        }
    } else {
        switch (k) {
            case special_kind::pos_inf:       return F::infinity();
            case special_kind::neg_inf:       { const F r = F::infinity(true); return r.is_negative() ? r : F::zero(); }
            case special_kind::nan:           return F::nan();
            case special_kind::undefined:     return F::undefined();
            case special_kind::indeterminate: return F::indeterminate();
            default:                          return F();
        }
    }
}

template <typename T> struct raw_info { static constexpr bool value = false; };

template <std::size_t B, mp::sign S>
struct raw_info<mp::integer<B, S>> {
    static constexpr bool        value     = true;
    static constexpr std::size_t bits      = B;
    static constexpr bool        is_signed = (S == mp::sign::is_signed);
    using unsigned_type = mp::integer<B, mp::sign::is_unsigned>;
    using signed_type   = mp::integer<B, mp::sign::is_signed>;
};

[[noreturn]] inline void fail_clamp(bool is_signed, std::size_t bits, bool below_zero) {
    const std::string kind = std::string(is_signed ? "a signed " : "an unsigned ") + std::to_string(bits) + "-bit integer";
    if (below_zero) fail("integer overflow: a negative value was converted to " + kind + ", which can't hold it");
    fail("integer overflow: a value doesn't fit when converted to " + kind);
}

inline void clamped(bool is_signed, std::size_t bits, bool below_zero) {
#if defined(WALNUT_CHECK_OVERFLOW)
    fail_clamp(is_signed, bits, below_zero);
#else
    (void)is_signed; (void)bits; (void)below_zero;
#endif
}

template <std::size_t B, mp::sign S>
class wint {
public:
    using raw_type = mp::integer<B, S>;
    using mag_type = mp::umag<B>;
    static constexpr std::size_t bits           = B;
    static constexpr bool        is_signed_type = (S == mp::sign::is_signed);

private:
    template <typename A, typename... R>
    struct forwardable : std::integral_constant<bool,
        !std::is_same_v<std::decay_t<A>, wint> && !std::is_same_v<std::decay_t<A>, raw_type> && !std::is_same_v<std::decay_t<A>, special_value>
        && !is_special_constant_v<A> && std::is_constructible_v<raw_type, A&&, R&&...>> {};

    template <typename A, typename... R>
    struct implicit_from : std::integral_constant<bool, sizeof...(R) == 0 && std::is_convertible_v<A&&, raw_type>> {};

    static constexpr raw_type make_raw(const mag_type& m, bool neg) noexcept {
        if constexpr (is_signed_type) return raw_type::from_magnitude(m, neg);
        else { (void)neg; return raw_type(m); }
    }

    constexpr void set_parts(const mag_type& m, bool neg) noexcept {
        m_m   = m;
        m_neg = is_signed_type && neg && !m.is_zero();
    }

    void set_raw(const raw_type& r) noexcept {
        if (r.is_undefined()) { m_m = mag_type(); m_neg = false; m_k = special_kind::undefined; return; }
        set_parts(r.magnitude(), r.is_negative());
    }

    void set_saturated(std::uint64_t m, bool neg) noexcept {
        if (neg && !is_signed_type) { if (m) clamped(is_signed_type, B, true); set_parts(mag_type(), false); return; }
        if constexpr (B < 64) { if (m > ((std::uint64_t(1) << B) - 1u)) { clamped(is_signed_type, B, false); m = (std::uint64_t(1) << B) - 1u; } }
        set_parts(mag_type(m), neg);
    }

    template <typename A, typename... R>
    void assign_from(A&& a, R&&... r) {
        using D = std::decay_t<A>;
        if constexpr (sizeof...(R) == 0 && std::is_integral_v<D>) {
            if constexpr (std::is_signed_v<D>) {
                const bool neg = a < D(0);
                const std::uint64_t u = static_cast<std::uint64_t>(static_cast<long long>(a));
                set_saturated(neg ? std::uint64_t(0) - u : u, neg);
            } else {
                set_saturated(static_cast<std::uint64_t>(a), false);
            }
        } else if constexpr (sizeof...(R) == 0 && std::is_floating_point_v<D>) {
            if (std::isnan(a)) { m_k = special_kind::nan; return; }
            if (std::isinf(a)) { *this = from_kind(inf_kind(a < D(0))); return; }
            *this = wint(raw_type(a));
        } else {
            *this = wint(raw_type(std::forward<A>(a), std::forward<R>(r)...));
        }
    }

public:
    constexpr wint() noexcept = default;

    wint(const raw_type& r) noexcept { set_raw(r); }

    wint(special_value s) noexcept { *this = from_kind(s.kind); }

    template <special_kind K>
    constexpr wint(special_constant<K>) noexcept : m_k((K == special_kind::neg_inf && !is_signed_type) ? special_kind::none : K) {}

    template <typename A, typename... R, std::enable_if_t<forwardable<A, R...>::value && implicit_from<A, R...>::value, int> = 0>
    wint(A&& a, R&&... r) { assign_from(std::forward<A>(a), std::forward<R>(r)...); }

    template <typename A, typename... R, std::enable_if_t<forwardable<A, R...>::value && !implicit_from<A, R...>::value, int> = 0>
    explicit wint(A&& a, R&&... r) { assign_from(std::forward<A>(a), std::forward<R>(r)...); }

    static constexpr wint from_kind(special_kind k) noexcept {
        wint r;
        if (k == special_kind::neg_inf && !is_signed_type) return r;
        r.m_k = k;
        return r;
    }

    static constexpr wint from_magnitude(const mag_type& m, bool negative = false) noexcept {
        wint r;
        r.set_parts(m, negative);
        return r;
    }

    static constexpr wint max_value() noexcept { return from_magnitude(mag_type::max(), false); }
    static constexpr wint min_value() noexcept { return is_signed_type ? from_magnitude(mag_type::max(), true) : wint(); }

    static constexpr wint infinity(bool negative = false) noexcept { return from_kind(inf_kind(negative)); }
    static constexpr wint nan()           noexcept { return from_kind(special_kind::nan); }
    static constexpr wint undefined()     noexcept { return from_kind(special_kind::undefined); }
    static constexpr wint indeterminate() noexcept { return from_kind(special_kind::indeterminate); }

    constexpr special_kind    kind()             const noexcept { return m_k; }
    constexpr bool            is_special()       const noexcept { return m_k != special_kind::none; }
    constexpr bool            is_finite()        const noexcept { return m_k == special_kind::none; }
    constexpr bool            is_infinite()      const noexcept { return infinite_kind(m_k); }
    constexpr bool            is_nan()           const noexcept { return m_k == special_kind::nan; }
    constexpr bool            is_undefined()     const noexcept { return m_k == special_kind::undefined; }
    constexpr bool            is_indeterminate() const noexcept { return m_k == special_kind::indeterminate; }
    constexpr bool            is_unordered()     const noexcept { return unordered_kind(m_k); }
    constexpr raw_type        raw()              const noexcept { return make_raw(m_m, m_neg); }

    constexpr bool is_zero()     const noexcept { return m_k == special_kind::none && m_m.is_zero(); }
    constexpr bool is_negative() const noexcept { return m_k == special_kind::neg_inf || (m_k == special_kind::none && m_neg); }

    constexpr const mag_type& magnitude() const noexcept { return m_m; }
    constexpr std::uint64_t get_lowest_bits() const noexcept { return m_m.get_lowest_bits(); }
    constexpr bool          get_bit(std::size_t i) const noexcept { return m_m.get_bit(i); }
    long long               highest_bit()     const noexcept { return static_cast<long long>(m_m.highest_bit()); }

    std::string to_string(long long base = 10) const {
        if (is_special()) return special_name(m_k);
        if constexpr (B <= 64) {
            if (base == 10) {
                const std::string s = std::to_string(static_cast<unsigned long long>(m_m.get_lowest_bits()));
                return m_neg ? "-" + s : s;
            }
        }
        return raw().to_string(base);
    }

    double to_double() const noexcept {
        if (m_k == special_kind::pos_inf) return std::numeric_limits<double>::infinity();
        if (m_k == special_kind::neg_inf) return -std::numeric_limits<double>::infinity();
        if (is_unordered()) return std::numeric_limits<double>::quiet_NaN();
        const double d = m_m.to_double();
        return m_neg ? -d : d;
    }

    friend std::ostream& operator<<(std::ostream& os, const wint& x) { return os << x.to_string(); }

    friend constexpr bool operator==(const wint& a, const wint& b) noexcept {
        if (a.m_k == special_kind::none && b.m_k == special_kind::none) return a.m_neg == b.m_neg && a.m_m == b.m_m;
        if (a.is_unordered() || b.is_unordered()) return false;
        return a.m_k == b.m_k;
    }

    friend constexpr bool operator<(const wint& a, const wint& b) noexcept {
        if (a.m_k == special_kind::none && b.m_k == special_kind::none) {
            if (a.m_neg != b.m_neg) return a.m_neg;
            return a.m_neg ? (b.m_m < a.m_m) : (a.m_m < b.m_m);
        }
        if (a.is_unordered() || b.is_unordered()) return false;
        if (a.m_k == special_kind::neg_inf) return b.m_k != special_kind::neg_inf;
        if (b.m_k == special_kind::pos_inf) return a.m_k != special_kind::pos_inf;
        return false;
    }

    friend constexpr bool operator!=(const wint& a, const wint& b) noexcept { return !(a == b); }
    friend constexpr bool operator> (const wint& a, const wint& b) noexcept { return b < a; }
    friend constexpr bool operator<=(const wint& a, const wint& b) noexcept { return !a.is_unordered() && !b.is_unordered() && !(b < a); }
    friend constexpr bool operator>=(const wint& a, const wint& b) noexcept { return !a.is_unordered() && !b.is_unordered() && !(a < b); }

private:
    mag_type     m_m{};
    bool         m_neg = false;
    special_kind m_k   = special_kind::none;
};

using i8    = wint<8,    mp::sign::is_signed>;
using i16   = wint<16,   mp::sign::is_signed>;
using i32   = wint<32,   mp::sign::is_signed>;
using i64   = wint<64,   mp::sign::is_signed>;
using i128  = wint<128,  mp::sign::is_signed>;
using i256  = wint<256,  mp::sign::is_signed>;
using i512  = wint<512,  mp::sign::is_signed>;
using i1024 = wint<1024, mp::sign::is_signed>;
using i2048 = wint<2048, mp::sign::is_signed>;

using u8    = wint<8,    mp::sign::is_unsigned>;
using u16   = wint<16,   mp::sign::is_unsigned>;
using u32   = wint<32,   mp::sign::is_unsigned>;
using u64   = wint<64,   mp::sign::is_unsigned>;
using u128  = wint<128,  mp::sign::is_unsigned>;
using u256  = wint<256,  mp::sign::is_unsigned>;
using u512  = wint<512,  mp::sign::is_unsigned>;
using u1024 = wint<1024, mp::sign::is_unsigned>;
using u2048 = wint<2048, mp::sign::is_unsigned>;

template <typename T> struct int_info { static constexpr bool value = false; };

template <std::size_t B, mp::sign S>
struct int_info<wint<B, S>> {
    static constexpr bool        value     = true;
    static constexpr std::size_t bits      = B;
    static constexpr bool        is_signed = (S == mp::sign::is_signed);
    using raw_type      = mp::integer<B, S>;
    using unsigned_type = wint<B, mp::sign::is_unsigned>;
    using signed_type   = wint<B, mp::sign::is_signed>;
};

template <typename T> struct float_info { static constexpr bool value = false; };
template <> struct float_info<f32>   { static constexpr bool value = true; static constexpr std::size_t bits = 32; };
template <> struct float_info<f64>   { static constexpr bool value = true; static constexpr std::size_t bits = 64; };
template <> struct float_info<f128>  { static constexpr bool value = true; static constexpr std::size_t bits = 128; };
template <> struct float_info<f256>  { static constexpr bool value = true; static constexpr std::size_t bits = 256; };
template <> struct float_info<f512>  { static constexpr bool value = true; static constexpr std::size_t bits = 512; };
template <> struct float_info<f1024> { static constexpr bool value = true; static constexpr std::size_t bits = 1024; };
template <> struct float_info<f2048> { static constexpr bool value = true; static constexpr std::size_t bits = 2048; };
template <> struct float_info<uf32>   { static constexpr bool value = true; static constexpr std::size_t bits = 32; };
template <> struct float_info<uf64>   { static constexpr bool value = true; static constexpr std::size_t bits = 64; };
template <> struct float_info<uf128>  { static constexpr bool value = true; static constexpr std::size_t bits = 128; };
template <> struct float_info<uf256>  { static constexpr bool value = true; static constexpr std::size_t bits = 256; };
template <> struct float_info<uf512>  { static constexpr bool value = true; static constexpr std::size_t bits = 512; };
template <> struct float_info<uf1024> { static constexpr bool value = true; static constexpr std::size_t bits = 1024; };
template <> struct float_info<uf2048> { static constexpr bool value = true; static constexpr std::size_t bits = 2048; };

template <typename T> inline constexpr bool is_int_v   = int_info<std::decay_t<T>>::value;
template <typename T> inline constexpr bool is_float_v = float_info<std::decay_t<T>>::value;

template <special_kind K>
struct special_constant {
    static constexpr special_kind kind = K;

    constexpr special_constant() noexcept = default;

    constexpr operator special_value() const noexcept { return special_value(K); }

    template <typename F, std::enable_if_t<is_float_v<F>, int> = 0>
    operator F() const { return float_special<F>(K); }

    template <typename F, std::enable_if_t<is_hardware_float_v<F>, int> = 0>
    constexpr operator F() const noexcept {
        return K == special_kind::pos_inf ? std::numeric_limits<F>::infinity()
             : K == special_kind::neg_inf ? -std::numeric_limits<F>::infinity()
             : std::numeric_limits<F>::quiet_NaN();
    }

    std::string to_string() const { return special_name(K); }
    friend std::ostream& operator<<(std::ostream& os, special_constant) { return os << special_name(K); }
};

using infinity_t      = special_constant<special_kind::pos_inf>;
using neg_infinity_t  = special_constant<special_kind::neg_inf>;
using nan_t           = special_constant<special_kind::nan>;
using undefined_t     = special_constant<special_kind::undefined>;
using indeterminate_t = special_constant<special_kind::indeterminate>;

inline constexpr infinity_t      infinity_v{};
inline constexpr neg_infinity_t  neg_infinity_v{};
inline constexpr nan_t           nan_v{};
inline constexpr undefined_t     undefined_v{};
inline constexpr indeterminate_t indeterminate_v{};

template <special_kind K> constexpr bool operator==(special_constant<K> a, special_constant<K> b) noexcept { (void)a; (void)b; return !unordered_kind(K); }
template <special_kind K> constexpr bool operator!=(special_constant<K> a, special_constant<K> b) noexcept { return !(a == b); }

template <special_kind K>
constexpr auto op_neg(special_constant<K>) noexcept {
    if constexpr (K == special_kind::pos_inf) return neg_infinity_v;
    else if constexpr (K == special_kind::neg_inf) return infinity_v;
    else return special_constant<K>{};
}

template <special_kind K> constexpr special_constant<K> op_pos(special_constant<K> v) noexcept { return v; }

template <typename T>
using unsigned_of = typename int_info<T>::unsigned_type;

template <typename R>
inline R raw_norm(const R& v) {
    if constexpr (raw_info<R>::is_signed) {
        using U = typename raw_info<R>::unsigned_type;
        if (v.is_undefined()) return v;
        U u(v.magnitude());
        constexpr std::size_t B = raw_info<R>::bits;
        const bool top = u.get_bit(B - 1);
        if (!v.is_negative()) {
            if (!top) return v;
            U neg_mag = U() - u;
            return R(neg_mag.magnitude(), true);
        }
        if (!top) return v;
        U rest = U() - u;
        if (rest.is_zero()) return v;
        if (rest.get_bit(B - 1)) return v;
        return R(rest.magnitude(), false);
    } else {
        return v;
    }
}

template <typename R>
inline typename raw_info<R>::unsigned_type raw_to_twos(const R& v) {
    using U = typename raw_info<R>::unsigned_type;
    if constexpr (!raw_info<R>::is_signed) {
        return v;
    } else {
        U u(v.magnitude());
        if (v.is_negative()) u = U() - u;
        return u;
    }
}

template <typename R>
inline R raw_from_twos(const typename raw_info<R>::unsigned_type& u) {
    if constexpr (!raw_info<R>::is_signed) {
        return u;
    } else {
        constexpr std::size_t B = raw_info<R>::bits;
        if (!u.get_bit(B - 1)) return R(u.magnitude(), false);
        typename raw_info<R>::unsigned_type m = typename raw_info<R>::unsigned_type() - u;
        return R(m.magnitude(), true);
    }
}

template <typename T>
inline T norm(const T& v) { return v; }

template <typename T>
inline unsigned_of<T> to_twos(const T& v) { return unsigned_of<T>(raw_to_twos(v.raw())); }

template <typename T>
inline T from_twos(const unsigned_of<T>& u) { return T(raw_from_twos<typename int_info<T>::raw_type>(u.raw())); }

[[noreturn]] inline void fail_special(special_kind k, const char* where) {
    fail(std::string("an integer holding ") + special_name(k) + " can't be used " + where);
}

template <typename T>
inline long long saturated_ll(const T& v) noexcept {
    const bool neg = v.is_negative();
    const std::uint64_t m = v.get_lowest_bits();
    bool big = m > (neg ? (1ULL << 63) : 0x7fffffffffffffffULL);
    if constexpr (int_info<T>::bits > 64) big = big || v.highest_bit() >= 64;
    if (big) return neg ? std::numeric_limits<long long>::min() : std::numeric_limits<long long>::max();
    if (neg) return m == (1ULL << 63) ? std::numeric_limits<long long>::min() : -static_cast<long long>(m);
    return static_cast<long long>(m);
}

template <typename T>
inline long long to_ll(const T& v) {
    if constexpr (is_int_v<T>) {
        if (v.is_special()) fail_special(v.kind(), "as an index, count or size");
        return saturated_ll(v);
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? 1 : 0;
    } else if constexpr (std::is_same_v<T, char32_t>) {
        return static_cast<long long>(v);
    } else {
        return static_cast<long long>(v);
    }
}

template <std::size_t Bits>
inline const mp::int4096& power_of_two_modulus() {
    static const mp::int4096 m = [] {
        mp::int4096 r = mp::int4096(1);
        for (std::size_t i = 0; i < Bits; ++i) r = r + r;
        return r;
    }();
    return m;
}

template <typename T>
inline long long to_ll_exact(const T& v) {
    if constexpr (std::is_arithmetic_v<T>) {
        return static_cast<long long>(v);
    } else {
        if constexpr (is_int_v<T>) {
            if (v.is_special()) fail_special(v.kind(), "as a C or C++ integer");
            return saturated_ll(v);
        } else {
            const unsigned long long u = static_cast<unsigned long long>(v.get_lowest_bits());
            if (v.is_negative()) return u == (1ULL << 63) ? std::numeric_limits<long long>::min() : -static_cast<long long>(u);
            return static_cast<long long>(u);
        }
    }
}

template <typename To, typename From>
inline To raw_int_to_int(const From& v) {
    if constexpr (std::is_same_v<To, From>) {
        return v;
    } else {
        constexpr std::size_t FB = raw_info<From>::bits;
        constexpr std::size_t TB = raw_info<To>::bits;
        constexpr bool        FS = raw_info<From>::is_signed;
        constexpr bool        TS = raw_info<To>::is_signed;

        if constexpr (FS == TS && TB >= FB) {
            return To(v);
        } else if constexpr (TB <= 64 && FB <= 64) {
            unsigned long long u = static_cast<unsigned long long>(raw_to_twos(v).get_lowest_bits());
            if constexpr (FS && TB > FB && FB < 64) { if (v.is_negative()) u |= (~0ULL << FB); }
            if constexpr (TB < 64) u &= ((1ULL << TB) - 1ULL);

            if constexpr (TS) {
                long long sv = 0;
                if constexpr (TB < 64) sv = ((u >> (TB - 1)) & 1ULL) ? static_cast<long long>(u | (~0ULL << TB)) : static_cast<long long>(u);
                else sv = static_cast<long long>(u);
                if (sv == std::numeric_limits<long long>::min()) return To(mp::int64(sv + 1)) - To(1LL);
                return To(sv);
            } else {
                return To(u);
            }
        } else {
            using W = mp::int4096;
            const W& modulus = power_of_two_modulus<TB>();
            W r = W(v) % modulus;
            if (r.is_negative()) r = r + modulus;
            if constexpr (TS) return raw_from_twos<To>(typename raw_info<To>::unsigned_type(r));
            else return To(r);
        }
    }
}

template <typename To, typename From>
inline To int_to_int(const From& v) {
    if constexpr (std::is_same_v<To, From>) {
        return v;
    } else {
        if (v.is_special()) return To::from_kind(v.kind());
        const bool neg = v.is_negative();
        if constexpr (!int_info<To>::is_signed) { if (neg) { clamped(false, int_info<To>::bits, true); return To(); } }
        constexpr std::size_t TB = int_info<To>::bits;
        if constexpr (TB < int_info<From>::bits) {
            if (v.highest_bit() >= static_cast<long long>(TB)) { clamped(int_info<To>::is_signed, TB, false); return To::from_magnitude(To::mag_type::max(), neg); }
        }
        return To::from_magnitude(mp::resize_mag<TB>(v.magnitude()), neg);
    }
}

template <typename F>
inline f2048 widen_float(const F& f) {
    if constexpr (std::is_same_v<F, f2048>) return f;
    else return f2048(f);
}

template <typename T>
inline f2048 int_to_f2048(const T& v) {
    if (v.is_special()) return float_special<f2048>(v.kind());
    const mp::int2048 w = int_to_int<i2048>(v).raw();
    return f2048(f2048::sstore_t(w));
}

template <typename S>
inline long long exponent_value(const S& e) noexcept {
    const long long m = static_cast<long long>(e.get_lowest_bits() & 0x3fffffffffffffffULL);
    return e.is_negative() ? -m : m;
}

template <typename F>
using float_core_t = std::decay_t<decltype(std::declval<const F&>().as_signed_core())>;

template <typename To, typename F>
inline To float_to_int(const F& f) {
    if (!f.is_finite()) return To::from_kind(float_kind(f));
    if (f.is_zero()) return To();
    const bool neg = f.is_negative();
    if constexpr (!int_info<To>::is_signed) { if (neg) { if (!(f.as_signed_core() > float_core_t<F>(-1.0))) clamped(false, int_info<To>::bits, true); return To(); } }
    using C = float_core_t<F>;
    constexpr std::size_t TB = int_info<To>::bits;
    constexpr long long   MB = static_cast<long long>(C::mantissa_bits);
    const auto parts = f.as_signed_core().frexp();
    const long long e = exponent_value(parts.second);
    if (e < 0) return To();
    if (e >= static_cast<long long>(TB)) { clamped(int_info<To>::is_signed, TB, false); return To::from_magnitude(To::mag_type::max(), neg); }
    const auto& mant = parts.first.magnitude();
    const long long shift = e - MB;
    if (shift < 0) return To::from_magnitude(mp::resize_mag<TB>(mant >> static_cast<std::size_t>(-shift)), neg);
    return To::from_magnitude(mp::resize_mag<TB>(mant) << static_cast<std::size_t>(shift), neg);
}

template <typename F, typename T>
inline F int_to_float(const T& v) {
    if (v.is_special()) return float_special<F>(v.kind());
    using C  = float_core_t<F>;
    using SS = typename C::sstore_t;
    constexpr std::size_t FB = C::math_bits;
    constexpr std::size_t WB = FB * 2;
    constexpr std::size_t IB = int_info<T>::bits;
    const bool  neg = v.is_negative();
    const auto& m   = v.magnitude();
    if (m.is_zero()) return F();
    if constexpr (IB <= FB) {
        return F(C(typename C::store_t(mp::resize_mag<FB>(m)), neg));
    } else if constexpr (IB <= WB) {
        return F(C::from_wide(neg, typename C::wide_t(mp::resize_mag<WB>(m)), SS(0), false));
    } else {
        const long long hb = v.highest_bit();
        if (hb < static_cast<long long>(WB)) return F(C::from_wide(neg, typename C::wide_t(mp::resize_mag<WB>(m)), SS(0), false));
        const std::size_t shift = static_cast<std::size_t>(hb - static_cast<long long>(WB - 1));
        return F(C::from_wide(neg, typename C::wide_t(mp::resize_mag<WB>(m >> shift)), SS(static_cast<long long>(shift)), m.any_bit_below(shift)));
    }
}

template <typename To, typename T>
inline To int_to_hardware_float(const T& v) {
    if (v.is_special()) return float_special<To>(v.kind());
    const bool neg = v.is_negative();
    To r;
    if constexpr (int_info<T>::bits <= 64) {
        r = static_cast<To>(v.get_lowest_bits());
    } else {
        const long long hb = v.highest_bit();
        if (hb < 64) {
            r = static_cast<To>(v.get_lowest_bits());
        } else {
            const std::size_t shift = static_cast<std::size_t>(hb - 63);
            std::uint64_t top = (v.magnitude() >> shift).get_lowest_bits();
            if (v.magnitude().any_bit_below(shift)) top |= 1u;
            r = std::ldexp(static_cast<To>(top), static_cast<int>(shift));
        }
    }
    return neg ? -r : r;
}

template <typename To, typename T>
inline To int_to_hardware_int(const T& v) {
    if (v.is_special()) fail_special(v.kind(), "as a hardware integer");
    const bool neg = v.is_negative();
    constexpr std::uint64_t max = static_cast<std::uint64_t>(std::numeric_limits<To>::max());
    const std::uint64_t m = v.get_lowest_bits();
    bool big = false;
    if constexpr (int_info<T>::bits > 64) big = v.highest_bit() >= 64;
    constexpr std::size_t bits = sizeof(To) * 8;
    if constexpr (std::is_unsigned_v<To>) {
        if (neg) { clamped(false, bits, true); return To(0); }
        if (big || m > max) { clamped(false, bits, false); return std::numeric_limits<To>::max(); }
        return static_cast<To>(m);
    } else {
        if (neg) {
            if (big || m > max + 1u) { clamped(true, bits, false); return std::numeric_limits<To>::min(); }
            return static_cast<To>(-static_cast<long long>(m - 1u) - 1);
        }
        if (big || m > max) { clamped(true, bits, false); return std::numeric_limits<To>::max(); }
        return static_cast<To>(m);
    }
}

template <typename To, typename From>
inline To cast(const From& v);

template <typename F>
inline double float_to_double(const F& v);

template <typename To, typename F>
inline To hardware_int_from_float(F d) {
    if (!std::isfinite(d)) fail(std::string("a float holding ") + (std::isnan(d) ? "nan" : "infinity") + " can't be converted to a hardware integer");
    const long double t = std::trunc(static_cast<long double>(d));
    if constexpr (std::is_signed_v<To>) {
        if (t < -9223372036854775808.0L || t >= 9223372036854775808.0L) fail("this float is too large for a hardware integer");
        return static_cast<To>(static_cast<unsigned long long>(static_cast<long long>(t)));
    } else {
        if (t <= -1.0L || t >= 18446744073709551616.0L) fail("this float doesn't fit in an unsigned hardware integer");
        return static_cast<To>(static_cast<unsigned long long>(t));
    }
}

template <typename To, typename From>
inline To hardware_cast(const From& v) {
    using T = std::decay_t<From>;
    if constexpr (is_hardware_int_v<To>) {
        if constexpr (is_hardware_int_v<T> || std::is_same_v<T, bool> || std::is_same_v<T, char32_t>) {
            return static_cast<To>(static_cast<hardware_wide_t<To>>(v));
        } else if constexpr (is_hardware_float_v<T>) {
            return hardware_int_from_float<To>(v);
        } else if constexpr (std::is_same_v<T, special_value>) {
            fail_special(v.kind, "as a hardware integer");
        } else if constexpr (is_int_v<T>) {
            return int_to_hardware_int<To>(v);
        } else if constexpr (is_float_v<T>) {
            return hardware_int_from_float<To>(static_cast<long double>(float_to_double(v)));
        } else {
            return static_cast<To>(v);
        }
    } else if constexpr (is_hardware_int_v<T>) {
        if constexpr (std::is_same_v<To, bool>) return v != T(0);
        else if constexpr (std::is_same_v<To, char32_t> || is_hardware_float_v<To>) return static_cast<To>(v);
        else if constexpr (is_int_v<To> && std::is_signed_v<T>) return int_to_int<To>(i64(static_cast<long long>(v)));
        else if constexpr (is_int_v<To>) return int_to_int<To>(u64(static_cast<unsigned long long>(v)));
        else if constexpr (is_float_v<To> && std::is_signed_v<T>) return cast<To>(i64(static_cast<long long>(v)));
        else if constexpr (is_float_v<To>) return cast<To>(u64(static_cast<unsigned long long>(v)));
        else return static_cast<To>(v);
    } else if constexpr (is_hardware_float_v<To>) {
        if constexpr (is_hardware_float_v<T> || std::is_integral_v<T> || std::is_same_v<T, char32_t>) {
            return static_cast<To>(v);
        } else if constexpr (std::is_same_v<T, special_value>) {
            return float_special<To>(v.kind);
        } else if constexpr (is_int_v<T>) {
            return int_to_hardware_float<To>(v);
        } else if constexpr (is_float_v<T>) {
            return static_cast<To>(float_to_double(v));
        } else {
            return static_cast<To>(v);
        }
    } else {
        const special_kind k = float_kind(v);
        if constexpr (std::is_same_v<To, bool>) {
            return v != T(0);
        } else if constexpr (is_float_v<To>) {
            if (k != special_kind::none) return float_special<To>(k);
            return To(static_cast<double>(v));
        } else if constexpr (is_int_v<To>) {
            if (k != special_kind::none) return To::from_kind(k);
            return float_to_int<To>(f64(static_cast<double>(v)));
        } else {
            return static_cast<To>(v);
        }
    }
}

template <typename To, typename From>
inline To cast(const From& v) {
    using T = std::decay_t<From>;
    if constexpr (std::is_same_v<To, T>) {
        return v;
    } else if constexpr (is_special_constant_v<T>) {
        if constexpr (is_int_v<To>) return To(v);
        else if constexpr (is_float_v<To> || is_hardware_float_v<To>) return static_cast<To>(v);
        else if constexpr (std::is_same_v<To, bool>) return true;
        else return cast<To>(special_value(v));
    } else if constexpr (is_hardware_number_v<To> || is_hardware_number_v<T>) {
        return hardware_cast<To>(v);
    } else if constexpr (std::is_same_v<T, special_value>) {
        if constexpr (is_int_v<To>) return To::from_kind(v.kind);
        else if constexpr (is_float_v<To>) return float_special<To>(v.kind);
        else if constexpr (std::is_same_v<To, bool>) return true;
        else return static_cast<To>(v);
    } else if constexpr (is_int_v<To> && is_int_v<T>) {
        return int_to_int<To>(v);
    } else if constexpr (is_float_v<To> && is_float_v<T>) {
        return To(v);
    } else if constexpr (is_float_v<To> && is_int_v<T>) {
        return int_to_float<To>(v);
    } else if constexpr (is_int_v<To> && is_float_v<T>) {
        return float_to_int<To>(v);
    } else if constexpr (std::is_same_v<To, bool>) {
        if constexpr (is_int_v<T> || is_float_v<T>) return !v.is_zero();
        else return static_cast<bool>(v);
    } else if constexpr (is_int_v<To> && (std::is_same_v<T, bool> || std::is_same_v<T, char32_t> || std::is_integral_v<T>)) {
        return To(static_cast<std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>>(v));
    } else if constexpr (is_float_v<To> && (std::is_same_v<T, bool> || std::is_same_v<T, char32_t> || std::is_integral_v<T>)) {
        if constexpr (std::is_signed_v<T>) return int_to_float<To>(i64(static_cast<long long>(v)));
        else return int_to_float<To>(u64(static_cast<unsigned long long>(v)));
    } else if constexpr (std::is_same_v<To, char32_t> && is_int_v<T>) {
        if (v.is_special()) fail_special(v.kind(), "as a character");
        return static_cast<char32_t>(int_to_hardware_int<std::uint32_t>(v));
    } else {
        return static_cast<To>(v);
    }
}

template <typename F>
inline double float_to_double(const F& v) {
    if (v.is_unordered()) return std::numeric_limits<double>::quiet_NaN();
    if (v.is_infinite()) return v.is_negative() ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (v.is_zero()) return v.is_negative() ? -0.0 : 0.0;
    const auto parts = v.frexp();
    constexpr std::size_t wide = static_cast<std::size_t>(F::mantissa_bits) + 1;
    constexpr std::size_t shift = wide > 64 ? wide - 64 : 0;
    const auto top = shift ? (parts.first >> shift) : parts.first;
    const double significand = static_cast<double>(static_cast<unsigned long long>(top.get_lowest_bits()));
    const long long exponent = static_cast<long long>(parts.second.get_lowest_bits());
    const double magnitude = std::ldexp(significand, static_cast<int>(parts.second.is_negative() ? -exponent : exponent) - static_cast<int>(F::mantissa_bits) + static_cast<int>(shift));
    return v.is_negative() ? -magnitude : magnitude;
}

template <typename Native, typename W>
inline Native to_native(const W& v) {
    if constexpr (std::is_same_v<Native, const char*>) {
        return v.c_str();
    } else if constexpr (std::is_same_v<Native, W>) {
        return v;
    } else if constexpr (is_hardware_number_v<W>) {
        return static_cast<Native>(v);
    } else if constexpr (is_int_v<W> && std::is_floating_point_v<Native>) {
        return int_to_hardware_float<Native>(v);
    } else if constexpr (is_int_v<W> && std::is_integral_v<Native> && !std::is_same_v<Native, bool>) {
        if (v.is_special()) fail_special(v.kind(), "as a C or C++ integer");
        return int_to_hardware_int<Native>(v);
    } else if constexpr (std::is_floating_point_v<Native>) {
        return static_cast<Native>(float_to_double(v));
    } else if constexpr (std::is_signed_v<Native>) {
        return static_cast<Native>(to_ll_exact(v));
    } else {
        if constexpr (is_int_v<W>) { if (v.is_special()) fail_special(v.kind(), "as a C or C++ integer"); }
        return static_cast<Native>(v.get_lowest_bits());
    }
}

template <typename W, typename Native>
inline W from_native(const Native& n) {
    if constexpr (std::is_same_v<Native, const char*> || std::is_same_v<Native, char*>) {
        return W(n ? n : "");
    } else if constexpr (std::is_same_v<Native, W>) {
        return n;
    } else if constexpr (std::is_floating_point_v<Native>) {
        return W(static_cast<double>(n));
    } else if constexpr (std::is_signed_v<Native>) {
        return W(static_cast<long long>(n));
    } else {
        return W(static_cast<unsigned long long>(n));
    }
}

template <typename T>
inline bool truthy(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v;
    else if constexpr (is_hardware_number_v<T>) return v != T(0);
    else if constexpr (is_int_v<T> || is_float_v<T>) return !v.is_zero();
    else if constexpr (std::is_same_v<T, special_value> || is_special_constant_v<T>) return true;
    else if constexpr (std::is_pointer_v<T> || std::is_null_pointer_v<T>) return v != nullptr;
    else if constexpr (std::is_same_v<T, char32_t>) return v != 0;
    else return static_cast<bool>(v);
}

template <typename T>
inline T special_add(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (a.is_infinite() && b.is_infinite()) return a.kind() == b.kind() ? a : T::indeterminate();
    return a.is_infinite() ? a : b;
}

template <typename T>
inline T special_sub(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (a.is_infinite() && b.is_infinite()) return a.kind() == b.kind() ? T::indeterminate() : a;
    if (a.is_infinite()) return a;
    return T::from_kind(inf_kind(b.kind() == special_kind::pos_inf));
}

template <typename T>
inline T special_mul(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (a.is_zero() || b.is_zero()) return T::indeterminate();
    return T::infinity(a.is_negative() != b.is_negative());
}

template <typename T>
inline T special_div(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (b.is_zero()) return a.is_zero() ? T::indeterminate() : T::undefined();
    if (a.is_infinite() && b.is_infinite()) return T::indeterminate();
    if (a.is_infinite()) return T::infinity(a.is_negative() != b.is_negative());
    return T();
}

template <typename T>
inline T special_mod(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (b.is_zero()) return a.is_zero() ? T::indeterminate() : T::undefined();
    if (a.is_infinite()) return T::undefined();
    return a;
}

template <typename T>
inline T special_bits(const T& a, const T& b) {
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    return T::undefined();
}

template <typename T, typename R>
inline R checked_norm(const R& v, const char* op) {
#if defined(WALNUT_CHECK_OVERFLOW)
    const R n = raw_norm(v);
    if (!(n == v)) {
        fail(std::string("integer overflow: the result of ") + op + " doesn't fit in " + (int_info<T>::is_signed ? "a signed " : "an unsigned ") + std::to_string(int_info<T>::bits) + "-bit integer");
    }
    return n;
#else
    (void)op;
    return raw_norm(v);
#endif
}

enum class int_overflow : std::uint8_t { none = 0, above, below };

enum class overflow_mode : std::uint8_t { to_infinity = 0, fail, fail_safe };

#if defined(WALNUT_CHECK_OVERFLOW)
inline constexpr overflow_mode default_overflow_mode = overflow_mode::fail;
#else
inline constexpr overflow_mode default_overflow_mode = overflow_mode::to_infinity;
#endif

template <typename T>
struct int_outcome {
    typename T::mag_type mag{};
    bool                 neg  = false;
    int_overflow         over = int_overflow::none;
};

template <std::size_t B>
inline bool mag_add_carry(const mp::umag<B>& a, const mp::umag<B>& b, mp::umag<B>& r) noexcept {
    const mp::umag<B> s = a + b;
    const bool carry = s < a;
    r = s;
    return carry;
}

template <std::size_t B>
inline bool mag_mul_overflow(const mp::umag<B>& a, const mp::umag<B>& b, mp::umag<B>& r) noexcept {
    if constexpr (B <= 32) {
        const std::uint64_t p = a.to_u64() * b.to_u64();
        r = mp::umag<B>(p);
        return (p >> B) != 0;
    } else if constexpr (B == 64) {
        mp::umag<64> hi;
        mp::wide_mul<64>::mul(a, b, hi, r);
        return !hi.is_zero();
    } else {
        const long long ha = a.highest_bit(), hb = b.highest_bit();
        if (ha < 0 || hb < 0) { r = mp::umag<B>(); return false; }
        if (ha + hb + 2 <= static_cast<long long>(B)) { r = a * b; return false; }
        if (ha + hb >= static_cast<long long>(B)) { r = a * b; return true; }
        mp::umag<B> hi, lo;
        mp::wide_mul<B>::mul(a, b, hi, lo);
        r = lo;
        return !hi.is_zero();
    }
}

template <typename T>
inline int_outcome<T> int_add_parts(const T& a, const T& b, bool negate_b = false) noexcept {
    int_outcome<T> o;
    const auto& ma = a.magnitude();
    const auto& mb = b.magnitude();
    if constexpr (int_info<T>::is_signed) {
        const bool na = a.is_negative();
        const bool nb = b.is_negative() != (negate_b && !mb.is_zero());
        if (na == nb) {
            if (mag_add_carry(ma, mb, o.mag)) o.over = int_overflow::above;
            o.neg = na;
        } else if (mb <= ma) {
            o.mag = ma - mb;
            o.neg = na;
        } else {
            o.mag = mb - ma;
            o.neg = nb;
        }
    } else if (negate_b) {
        if (ma < mb) o.over = int_overflow::below;
        else o.mag = ma - mb;
    } else {
        if (mag_add_carry(ma, mb, o.mag)) o.over = int_overflow::above;
    }
    return o;
}

template <typename T>
inline int_outcome<T> int_mul_parts(const T& a, const T& b) noexcept {
    int_outcome<T> o;
    if (mag_mul_overflow(a.magnitude(), b.magnitude(), o.mag)) o.over = int_overflow::above;
    o.neg = a.is_negative() != b.is_negative();
    return o;
}

template <typename T>
inline int_outcome<T> int_neg_parts(const T& a) noexcept {
    int_outcome<T> o;
    if constexpr (int_info<T>::is_signed) {
        o.mag = a.magnitude();
        o.neg = !a.is_negative();
    } else if (!a.is_zero()) {
        o.over = int_overflow::below;
    }
    return o;
}

template <typename T>
inline int_outcome<T> int_shl_parts(const T& a, std::size_t n) noexcept {
    int_outcome<T> o;
    const long long h = a.highest_bit();
    if (h >= 0 && h + static_cast<long long>(n) >= static_cast<long long>(int_info<T>::bits)) o.over = int_overflow::above;
    o.mag = a.magnitude() << n;
    o.neg = a.is_negative();
    return o;
}

template <typename T>
[[noreturn]] inline void fail_int_overflow(int_overflow over, const char* op, bool safe) {
    const std::string kind = std::string(int_info<T>::is_signed ? "a signed " : "an unsigned ") + std::to_string(int_info<T>::bits) + "-bit integer";
    const std::string head = safe ? "integer overflow in safe code: the result of " : "integer overflow: the result of ";
    if (over == int_overflow::below) fail(head + op + " is below zero, which " + kind + " can't hold");
    fail(head + op + " doesn't fit in " + kind);
}

template <typename T, overflow_mode M = default_overflow_mode>
inline T int_finish(const int_outcome<T>& o, const char* op) {
    if (o.over == int_overflow::none) return T::from_magnitude(o.mag, o.neg);
    if constexpr (M == overflow_mode::to_infinity) {
        (void)op;
        if (o.over == int_overflow::below) return T();
        return T::infinity(o.neg);
    } else {
        fail_int_overflow<T>(o.over, op, M == overflow_mode::fail_safe);
    }
}

template <typename T, overflow_mode M = default_overflow_mode>
inline T int_pow(const T& a, const T& b) {
    using Mag = typename T::mag_type;
    const bool neg = a.is_negative() && b.get_bit(0);
    Mag base = a.magnitude(), r(std::uint64_t(1));
    const Mag& e = b.magnitude();
    const long long top = e.highest_bit();
    int_outcome<T> o;
    o.neg = neg;
    for (long long i = 0; i <= top; ++i) {
        if (e.get_bit(static_cast<std::size_t>(i))) {
            Mag t;
            if (mag_mul_overflow(r, base, t)) { o.over = int_overflow::above; break; }
            r = t;
        }
        if (i < top) {
            Mag t;
            if (mag_mul_overflow(base, base, t)) { o.over = int_overflow::above; break; }
            base = t;
        }
    }
    o.mag = r;
    return int_finish<T, M>(o, "**");
}

template <typename T>
inline T hw_wrap(unsigned long long v) { return static_cast<T>(static_cast<std::make_unsigned_t<T>>(v)); }

template <typename T> inline T op_add(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) return hw_wrap<T>(static_cast<unsigned long long>(a) + static_cast<unsigned long long>(b));
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_add(a, b); return int_finish<T>(int_add_parts(a, b), "+"); }
    else return a + b;
}

template <typename T> inline T op_sub(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) return hw_wrap<T>(static_cast<unsigned long long>(a) - static_cast<unsigned long long>(b));
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_sub(a, b); return int_finish<T>(int_add_parts(a, b, true), "-"); }
    else return a - b;
}

template <typename T> inline T op_mul(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) return hw_wrap<T>(static_cast<unsigned long long>(a) * static_cast<unsigned long long>(b));
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_mul(a, b); return int_finish<T>(int_mul_parts(a, b), "*"); }
    else return a * b;
}

template <typename T> inline T op_neg(const T& a) {
    if constexpr (is_hardware_int_v<T>) {
        return hw_wrap<T>(0ULL - static_cast<unsigned long long>(a));
    } else if constexpr (is_int_v<T>) {
        if (a.is_unordered()) return a;
        if (a.is_infinite()) return T::from_kind(inf_kind(a.kind() == special_kind::pos_inf));
        return int_finish<T>(int_neg_parts(a), "negating");
    } else {
        return -a;
    }
}

template <typename T> inline T op_pos(const T& a) { return a; }

inline std::string op_add(const std::string& a, const std::string& b) { return a + b; }

inline void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) { out += static_cast<char>(c); }
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
}

inline std::string op_add(const std::string& a, char32_t c) { std::string r = a; append_utf8(r, c); return r; }
inline std::string op_add(char32_t c, const std::string& b) { std::string r; append_utf8(r, c); return r + b; }

template <typename T>
inline T op_div(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) {
        if (b == T(0)) fail("hardware integer division by zero");
        if constexpr (std::is_signed_v<T>) { if (b == T(-1)) return op_neg(a); }
        return static_cast<T>(a / b);
    } else if constexpr (is_int_v<T>) {
        if (a.is_special() || b.is_special() || b.is_zero()) return special_div(a, b);
        return T::from_magnitude(a.magnitude() / b.magnitude(), a.is_negative() != b.is_negative());
    } else {
        return a / b;
    }
}

template <typename T>
inline T op_mod(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) {
        if (b == T(0)) fail("hardware integer remainder by zero");
        if constexpr (std::is_signed_v<T>) { if (b == T(-1)) return T(0); }
        return static_cast<T>(a % b);
    } else {
        if (a.is_special() || b.is_special() || b.is_zero()) return special_mod(a, b);
        return T::from_magnitude(a.magnitude() % b.magnitude(), a.is_negative());
    }
}

template <typename T>
inline T special_pow(const T& a, const T& b) {
    const T one(1);
    if (a.is_unordered() || b.is_unordered()) return T::from_kind(unordered_of(a.kind(), b.kind()));
    if (b.is_zero()) return (a.is_zero() || a.is_infinite()) ? T::indeterminate() : one;
    if (a == one) return b.is_infinite() ? T::indeterminate() : one;

    if (b.is_infinite()) {
        if (a.is_zero()) return b.is_negative() ? T::undefined() : T();
        if (b.is_negative()) return T();
        if (a.is_negative()) return (a.is_finite() && a == op_neg(one)) ? T::indeterminate() : T::undefined();
        return T::infinity();
    }

    if (b.is_negative()) return T();
    return T::infinity(a.is_negative() && b.raw().get_bit(0));
}

template <typename T>
inline T op_pow(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) {
        if constexpr (std::is_signed_v<T>) {
            if (b < T(0)) {
                if (a == T(1)) return T(1);
                if (a == T(-1)) return (static_cast<long long>(b) & 1) ? T(-1) : T(1);
                fail("integer raised to a negative power");
            }
        }
        unsigned long long base = static_cast<unsigned long long>(a), r = 1ULL, e = static_cast<unsigned long long>(b);
        while (e) {
            if (e & 1ULL) r *= base;
            base *= base;
            e >>= 1;
        }
        return hw_wrap<T>(r);
    } else if constexpr (is_int_v<T>) {
        if (a.is_special() || b.is_special() || b.is_zero()) return special_pow(a, b);
        if (a.is_zero() && b.is_negative()) return T::undefined();
        if constexpr (int_info<T>::is_signed) {
            if (b.is_negative()) {
                if (a.magnitude() == typename T::mag_type(std::uint64_t(1))) return T::from_magnitude(a.magnitude(), a.is_negative() && b.get_bit(0));
                fail("integer raised to a negative power");
            }
        }
        return int_pow<T>(a, b);
    } else if constexpr (is_hardware_float_v<T>) {
        return static_cast<T>(std::pow(a, b));
    } else {
        return T(mp::math::pow(a, b));
    }
}

template <typename T, std::enable_if_t<is_hardware_int_v<T>, int> = 0>
inline T op_bitand(T a, T b) { return static_cast<T>(a & b); }
template <typename T, std::enable_if_t<is_hardware_int_v<T>, int> = 0>
inline T op_bitor(T a, T b)  { return static_cast<T>(a | b); }
template <typename T, std::enable_if_t<is_hardware_int_v<T>, int> = 0>
inline T op_bitxor(T a, T b) { return static_cast<T>(a ^ b); }
template <typename T, std::enable_if_t<is_hardware_int_v<T>, int> = 0>
inline T op_bitnot(T a)      { return static_cast<T>(~a); }

template <typename T, std::enable_if_t<!std::is_enum_v<T> && !is_hardware_int_v<T>, int> = 0>
inline T op_bitand(const T& a, const T& b) { if (a.is_special() || b.is_special()) return special_bits(a, b); return T::from_magnitude(a.magnitude() & b.magnitude(), a.is_negative() && b.is_negative()); }
template <typename T, std::enable_if_t<!std::is_enum_v<T> && !is_hardware_int_v<T>, int> = 0>
inline T op_bitor(const T& a, const T& b)  { if (a.is_special() || b.is_special()) return special_bits(a, b); return T::from_magnitude(a.magnitude() | b.magnitude(), a.is_negative() || b.is_negative()); }
template <typename T, std::enable_if_t<!std::is_enum_v<T> && !is_hardware_int_v<T>, int> = 0>
inline T op_bitxor(const T& a, const T& b) { if (a.is_special() || b.is_special()) return special_bits(a, b); return T::from_magnitude(a.magnitude() ^ b.magnitude(), a.is_negative() != b.is_negative()); }
template <typename T, std::enable_if_t<!std::is_enum_v<T> && !is_hardware_int_v<T>, int> = 0>
inline T op_bitnot(const T& a)             { if (a.is_special()) return special_bits(a, a); return T::from_magnitude(~a.magnitude(), a.is_negative()); }

template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E op_bitand(E a, E b) { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) & static_cast<std::underlying_type_t<E>>(b)); }
template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E op_bitor(E a, E b)  { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) | static_cast<std::underlying_type_t<E>>(b)); }
template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E op_bitxor(E a, E b) { return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) ^ static_cast<std::underlying_type_t<E>>(b)); }
template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
inline E op_bitnot(E a)      { return static_cast<E>(~static_cast<std::underlying_type_t<E>>(a)); }

inline bool op_bitand(bool a, bool b) { return a && b; }
inline bool op_bitor(bool a, bool b)  { return a || b; }
inline bool op_bitxor(bool a, bool b) { return a != b; }

template <typename T, typename S>
inline std::size_t shift_count(const S& s) {
    long long n = to_ll(s);
    if (n < 0 || n >= static_cast<long long>(int_info<T>::bits)) fail("shift count out of range");
    return static_cast<std::size_t>(n);
}

template <typename T, typename S>
inline std::size_t hardware_shift_count(const S& s) {
    if constexpr (std::is_same_v<S, special_value>) {
        fail_special(s.kind, "as a shift count");
    } else {
        if constexpr (is_int_v<S>) { if (s.is_special()) fail_special(s.kind(), "as a shift count"); }
        const long long n = to_ll(s);
        if (n < 0 || n >= static_cast<long long>(sizeof(T) * 8)) fail("shift count out of range");
        return static_cast<std::size_t>(n);
    }
}

template <typename T, typename S>
inline T op_shl(const T& a, const S& s) {
    if constexpr (is_hardware_int_v<T>) {
        return hw_wrap<T>(static_cast<unsigned long long>(a) << hardware_shift_count<T>(s));
    } else if constexpr (std::is_same_v<S, special_value>) {
        return a.is_unordered() ? a : T::undefined();
    } else {
        if constexpr (is_int_v<S>) { if (s.is_special()) return a.is_unordered() ? a : T::undefined(); }
        if (a.is_special()) return a.is_unordered() ? a : T::undefined();
        const std::size_t n = shift_count<T>(s);
        return int_finish<T>(int_shl_parts(a, n), "<<");
    }
}

template <typename T, typename S>
inline T op_shr(const T& a, const S& s) {
    if constexpr (is_hardware_int_v<T>) {
        return static_cast<T>(a >> hardware_shift_count<T>(s));
    } else if constexpr (std::is_same_v<S, special_value>) {
        return a.is_unordered() ? a : T::undefined();
    } else {
        if constexpr (is_int_v<S>) { if (s.is_special()) return a.is_unordered() ? a : T::undefined(); }
        if (a.is_special()) return a.is_unordered() ? a : T::undefined();
        const std::size_t n = shift_count<T>(s);
        return T::from_magnitude(a.magnitude() >> n, a.is_negative());
    }
}

template <typename T>
inline bool is_special(const T& v, special_test t) {
    if constexpr (is_int_v<T>) return special_matches(v.kind(), t);
    else if constexpr (is_float_v<T> || is_hardware_float_v<T>) return special_matches(float_kind(v), t);
    else if constexpr (std::is_same_v<T, special_value>) return special_matches(v.kind, t);
    else if constexpr (is_special_constant_v<T>) return special_matches(special_constant_info<T>::kind, t);
    else return false;
}

template <typename T>
inline bool is_special(const std::variant<T, special_value>& v, special_test t) {
    return std::holds_alternative<special_value>(v) && special_matches(std::get<special_value>(v).kind, t);
}

template <typename T>
inline T& special_as(std::variant<T, special_value>& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this special(" + name + ") holds " + special_name(std::get<special_value>(v).kind) + ", not a value of type '" + name + "'");
    return std::get<T>(v);
}

template <typename T>
inline const T& special_as(const std::variant<T, special_value>& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this special(" + name + ") holds " + special_name(std::get<special_value>(v).kind) + ", not a value of type '" + name + "'");
    return std::get<T>(v);
}

template <typename T>
inline T special_as(std::variant<T, special_value>&& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this special(" + name + ") holds " + special_name(std::get<special_value>(v).kind) + ", not a value of type '" + name + "'");
    return std::get<T>(std::move(v));
}

constexpr int special_rank(special_kind k) noexcept { return k == special_kind::neg_inf ? -1 : k == special_kind::pos_inf ? 1 : 0; }
constexpr bool special_ordered(const special_value& a, const special_value& b) noexcept { return !unordered_kind(a.kind) && !unordered_kind(b.kind); }

constexpr bool operator==(const special_value& a, const special_value& b) noexcept { return special_ordered(a, b) && special_rank(a.kind) == special_rank(b.kind); }
constexpr bool operator!=(const special_value& a, const special_value& b) noexcept { return !(a == b); }
constexpr bool operator< (const special_value& a, const special_value& b) noexcept { return special_ordered(a, b) && special_rank(a.kind) <  special_rank(b.kind); }
constexpr bool operator> (const special_value& a, const special_value& b) noexcept { return special_ordered(a, b) && special_rank(a.kind) >  special_rank(b.kind); }
constexpr bool operator<=(const special_value& a, const special_value& b) noexcept { return special_ordered(a, b) && special_rank(a.kind) <= special_rank(b.kind); }
constexpr bool operator>=(const special_value& a, const special_value& b) noexcept { return special_ordered(a, b) && special_rank(a.kind) >= special_rank(b.kind); }

template <typename T> inline T& pre_inc(T& x)  { x = op_add(x, T(1)); return x; }
template <typename T> inline T& pre_dec(T& x)  { x = op_sub(x, T(1)); return x; }
template <typename T> inline T  post_inc(T& x) { T old = x; x = op_add(x, T(1)); return old; }
template <typename T> inline T  post_dec(T& x) { T old = x; x = op_sub(x, T(1)); return old; }

inline char32_t& pre_inc(char32_t& x)  { ++x; return x; }
inline char32_t& pre_dec(char32_t& x)  { --x; return x; }
inline char32_t  post_inc(char32_t& x) { return x++; }
inline char32_t  post_dec(char32_t& x) { return x--; }

template <typename T> inline T* pre_inc(T*& p)  { return ++p; }
template <typename T> inline T* pre_dec(T*& p)  { return --p; }
template <typename T> inline T* post_inc(T*& p) { return p++; }
template <typename T> inline T* post_dec(T*& p) { return p--; }

namespace checked {

template <typename T, typename R>
inline R norm(const R& v, const char* op) {
    const R n = raw_norm(v);
    if (!(n == v)) {
        fail(std::string("integer overflow in safe code: the result of ") + op + " doesn't fit in " + (int_info<T>::is_signed ? "a signed " : "an unsigned ") + std::to_string(int_info<T>::bits) + "-bit integer");
    }
    return n;
}

template <typename T>
inline T hardware_overflow(const char* op) {
    fail(std::string("integer overflow in safe code: the result of ") + op + " doesn't fit in " + (std::is_signed_v<T> ? "a signed " : "an unsigned ") + std::to_string(sizeof(T) * 8) + "-bit hardware integer");
}

template <typename T> inline T op_add(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) { T r; if (__builtin_add_overflow(a, b, &r)) return hardware_overflow<T>("+"); return r; }
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_add(a, b); return int_finish<T, overflow_mode::fail_safe>(int_add_parts(a, b), "+"); }
    else return walnut_rt::op_add(a, b);
}

template <typename T> inline T op_sub(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) { T r; if (__builtin_sub_overflow(a, b, &r)) return hardware_overflow<T>("-"); return r; }
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_sub(a, b); return int_finish<T, overflow_mode::fail_safe>(int_add_parts(a, b, true), "-"); }
    else return walnut_rt::op_sub(a, b);
}

template <typename T> inline T op_mul(const T& a, const T& b) {
    if constexpr (is_hardware_int_v<T>) { T r; if (__builtin_mul_overflow(a, b, &r)) return hardware_overflow<T>("*"); return r; }
    else if constexpr (is_int_v<T>) { if (a.is_special() || b.is_special()) return special_mul(a, b); return int_finish<T, overflow_mode::fail_safe>(int_mul_parts(a, b), "*"); }
    else return walnut_rt::op_mul(a, b);
}

template <typename T> inline T op_neg(const T& a) {
    if constexpr (is_hardware_int_v<T>) {
        T r;
        if (__builtin_sub_overflow(T(0), a, &r)) return hardware_overflow<T>("negating");
        return r;
    } else if constexpr (is_int_v<T>) {
        if (a.is_unordered() || a.is_infinite()) return walnut_rt::op_neg(a);
        return int_finish<T, overflow_mode::fail_safe>(int_neg_parts(a), "negating");
    } else {
        return walnut_rt::op_neg(a);
    }
}

template <typename T> inline decltype(auto) pre_inc(T& x) {
    if constexpr (is_int_v<T> || is_hardware_int_v<T>) { x = checked::op_add(x, T(1)); return (x); }
    else return walnut_rt::pre_inc(x);
}

template <typename T> inline decltype(auto) pre_dec(T& x) {
    if constexpr (is_int_v<T> || is_hardware_int_v<T>) { x = checked::op_sub(x, T(1)); return (x); }
    else return walnut_rt::pre_dec(x);
}

template <typename T> inline decltype(auto) post_inc(T& x) {
    if constexpr (is_int_v<T> || is_hardware_int_v<T>) { T old = x; x = checked::op_add(x, T(1)); return old; }
    else return walnut_rt::post_inc(x);
}

template <typename T> inline decltype(auto) post_dec(T& x) {
    if constexpr (is_int_v<T> || is_hardware_int_v<T>) { T old = x; x = checked::op_sub(x, T(1)); return old; }
    else return walnut_rt::post_dec(x);
}

} // namespace checked

template <typename T, typename I> inline T* ptr_add(T* p, const I& i) { return p + to_ll(i); }
template <typename T, typename I> inline T* ptr_sub(T* p, const I& i) { return p - to_ll(i); }
template <typename R, typename T> inline R ptr_diff(T* a, T* b) { return cast<R>(i64(static_cast<long long>(a - b))); }

template <typename T>
class array_ref {
public:
    array_ref() = default;
    template <std::size_t N> array_ref(std::array<T, N>& a) : m_data(a.data()), m_size(N) {}
    template <std::size_t N> array_ref(const std::array<T, N>& a) : m_data(const_cast<T*>(a.data())), m_size(N) {}
    array_ref(std::vector<T>& v) : m_data(v.data()), m_size(v.size()) {}
    array_ref(const std::vector<T>& v) : m_data(const_cast<T*>(v.data())), m_size(v.size()) {}

    std::size_t size() const { return m_size; }
    T& operator[](std::size_t i) const { return m_data[i]; }
    T* begin() const { return m_data; }
    T* end() const { return m_data + m_size; }
    T* data() const { return m_data; }

private:
    T*          m_data = nullptr;
    std::size_t m_size = 0;
};

template <typename C>
inline auto to_vector(const C& c) {
    using T = std::decay_t<decltype(*std::begin(c))>;
    return std::vector<T>(std::begin(c), std::end(c));
}

template <typename T, typename I>
inline std::vector<T> sized_vector(const I& n) {
    const long long k = to_ll(n);
    if (k < 0) fail("array size " + std::to_string(k) + " is negative");
    return std::vector<T>(static_cast<std::size_t>(k));
}

template <typename T, typename I>
inline T& index(const array_ref<T>& c, const I& i) {
    long long n = to_ll(i);
    if (n < 0 || static_cast<std::size_t>(n) >= c.size()) fail("array index " + std::to_string(n) + " out of range");
    return c[static_cast<std::size_t>(n)];
}

template <typename T>
inline std::vector<T> fill_from(std::vector<T> target, const std::vector<T>& values) {
    if (values.size() > target.size()) fail("too many initializers for an array of size " + std::to_string(target.size()));
    for (std::size_t i = 0; i < values.size(); ++i) target[i] = values[i];
    return target;
}

template <typename T, typename I>
inline T& index(T* p, const I& i) {
#if defined(WALNUT_CHECK_BOUNDS) || defined(WALNUT_CHECK_NULL) || defined(WALNUT_CHECK_DANGLING)
    safety::check_index(p, to_ll(i));
#endif
    return p[to_ll(i)];
}

struct native_tag {};

template <typename P, typename C, typename R>
inline P native_ptr(C& c, R&& r) {
    if constexpr (std::is_pointer_v<std::decay_t<R>>) return r;
    else return c.data() + (r - c.begin());
}

template <typename C>
struct spread_part {
    const C& items;
};

template <typename C>
inline spread_part<C> spread(const C& items) {
    return spread_part<C>{ items };
}

template <typename T>
struct is_spread_part : std::false_type {};

template <typename C>
struct is_spread_part<spread_part<C>> : std::true_type {};

template <typename V, typename P>
inline void gather_into(V& out, P&& part) {
    if constexpr (is_spread_part<std::decay_t<P>>::value) {
        for (const auto& item : part.items) out.push_back(static_cast<typename V::value_type>(item));
    } else {
        out.push_back(static_cast<typename V::value_type>(std::forward<P>(part)));
    }
}

template <typename V, typename... Parts>
inline V gather(Parts&&... parts) {
    V out;
    (gather_into(out, std::forward<Parts>(parts)), ...);
    return out;
}

template <typename C, typename I>
inline decltype(auto) index(C& c, const I& i) {
    long long n = to_ll(i);
    if constexpr (std::is_pointer_v<std::decay_t<C>>) {
#if defined(WALNUT_CHECK_BOUNDS) || defined(WALNUT_CHECK_NULL) || defined(WALNUT_CHECK_DANGLING)
        safety::check_index(c, n);
#endif
        return c[n];
    } else {
        if (n < 0 || static_cast<std::size_t>(n) >= c.size()) fail("array index " + std::to_string(n) + " out of range");
        return c[static_cast<std::size_t>(n)];
    }
}

inline std::string utf8(char32_t c) { std::string s; append_utf8(s, c); return s; }

class dynamic;
inline std::string to_display(const dynamic& d);

struct dynamic_function {
    std::size_t arity = 0;
    std::function<dynamic(std::vector<dynamic>&)> body;
};

template <typename F>
inline std::string float_display(const F& f, std::int64_t precision = 0) {
    if (!f.is_finite()) return f.to_string();
    if (f.is_zero()) return f.is_negative() ? "-0" : "0";
    const std::string sci = f.to_scientific_string(precision > 0 ? precision : 0);
    const std::size_t ep = sci.find('e');
    if (ep == std::string::npos) return f.to_string(precision);
    const std::int64_t e10 = std::stoll(sci.substr(ep + 1));
    std::string mant = sci.substr(0, ep);
    const bool neg = !mant.empty() && mant[0] == '-';
    if (neg) mant.erase(0, 1);
    std::string digits;
    for (char c : mant) if (c != '.') digits += c;
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const bool scientific = precision > 0 ? (e10 < -4 || e10 >= precision) : (e10 < -7 || e10 >= 21);
    std::string out;

    if (scientific) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += e10 < 0 ? "e-" : "e+";
        out += std::to_string(e10 < 0 ? -e10 : e10);
    } else if (e10 < 0) {
        out = "0." + std::string(static_cast<std::size_t>(-e10 - 1), '0') + digits;
    } else {
        const std::size_t whole = static_cast<std::size_t>(e10 + 1);
        if (digits.size() <= whole) out = digits + std::string(whole - digits.size(), '0');
        else out = digits.substr(0, whole) + "." + digits.substr(whole);
    }

    return neg ? "-" + out : out;
}

template <typename F>
inline std::string hardware_display(F v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-\u221e" : "\u221e";
    if (v == F(0)) return std::signbit(v) ? "-0" : "0";
    char buf[64];
    for (int digits = 1; digits <= std::numeric_limits<F>::max_digits10; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, static_cast<double>(v));
        if (static_cast<F>(std::strtod(buf, nullptr)) == v) break;
    }
    std::string out = buf;
    const std::size_t e = out.find('e');
    if (e != std::string::npos) {
        std::string mant = out.substr(0, e);
        std::string exp = out.substr(e + 1);
        const bool neg = !exp.empty() && exp[0] == '-';
        if (!exp.empty() && (exp[0] == '+' || exp[0] == '-')) exp.erase(0, 1);
        while (exp.size() > 1 && exp[0] == '0') exp.erase(0, 1);
        out = mant + "e" + (neg ? "-" : "+") + exp;
    }
    return out;
}

template <typename T>
inline std::string to_display(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
    else if constexpr (std::is_same_v<T, char32_t>) return utf8(v);
    else if constexpr (std::is_same_v<T, std::string>) return v;
    else if constexpr (std::is_convertible_v<T, const char*>) return std::string(v);
    else if constexpr (std::is_null_pointer_v<T>) return "nullptr";
    else if constexpr (std::is_pointer_v<T>) { std::ostringstream os; os << static_cast<const void*>(v); return os.str(); }
    else if constexpr (is_int_v<T>) return v.to_string();
    else if constexpr (is_float_v<T>) return float_display(v);
    else if constexpr (is_hardware_float_v<T>) return hardware_display(v);
    else if constexpr (is_hardware_int_v<T>) return std::to_string(static_cast<hardware_wide_t<T>>(v));
    else if constexpr (std::is_enum_v<T>) return std::to_string(static_cast<long long>(v));
    else { std::ostringstream os; os << v; return os.str(); }
}

template <typename T>
inline std::string to_display(const std::variant<T, special_value>& v) {
    if (std::holds_alternative<special_value>(v)) return special_name(std::get<special_value>(v).kind);
    return to_display(std::get<T>(v));
}

class dynamic {
public:
    using value_type = std::variant<std::monostate, bool, char32_t, i2048, f2048, std::string, std::shared_ptr<dynamic_function>>;

    dynamic() = default;
    dynamic(bool b) : m_v(b) {}
    dynamic(char32_t c) : m_v(c) {}
    dynamic(const std::string& s) : m_v(s) {}
    dynamic(std::string&& s) : m_v(std::move(s)) {}
    dynamic(const char* s) : m_v(std::string(s)) {}
    dynamic(std::nullptr_t) : m_v(std::monostate{}) {}
    dynamic(special_value s) : m_v(float_special<f2048>(s.kind)) {}
    dynamic(std::shared_ptr<dynamic_function> f) : m_v(std::move(f)) {}

    template <typename T, typename = std::enable_if_t<is_int_v<T>>>
    dynamic(const T& v) : m_v(cast<i2048>(v)) {}

    template <typename T, typename = std::enable_if_t<is_float_v<T>>, typename = void>
    dynamic(const T& v) : m_v(cast<f2048>(v)) {}

    template <typename T, typename = std::enable_if_t<is_hardware_float_v<T>>, typename = void, typename = void>
    dynamic(const T& v) : m_v(cast<f2048>(v)) {}

    template <typename T, typename = std::enable_if_t<is_hardware_int_v<T>>, typename = void, typename = void, typename = void>
    dynamic(const T& v) : m_v(cast<i2048>(v)) {}

    bool is_none()   const { return std::holds_alternative<std::monostate>(m_v); }
    bool is_bool()   const { return std::holds_alternative<bool>(m_v); }
    bool is_char()   const { return std::holds_alternative<char32_t>(m_v); }
    bool is_int()    const { return std::holds_alternative<i2048>(m_v); }
    bool is_float()  const { return std::holds_alternative<f2048>(m_v); }
    bool is_string() const { return std::holds_alternative<std::string>(m_v); }
    bool is_function() const { return std::holds_alternative<std::shared_ptr<dynamic_function>>(m_v); }
    bool is_number() const { return is_int() || is_float(); }

    bool is_unordered_number() const {
        if (is_int())   return std::get<i2048>(m_v).is_unordered();
        if (is_float()) return std::get<f2048>(m_v).is_unordered();
        return false;
    }

    bool matches_special(special_test t) const {
        if (is_int())   return walnut_rt::special_matches(std::get<i2048>(m_v).kind(), t);
        if (is_float()) return walnut_rt::special_matches(float_kind(std::get<f2048>(m_v)), t);
        return false;
    }

    const value_type& raw() const { return m_v; }

    std::string type_name() const {
        if (is_none())   return "none";
        if (is_bool())   return "bool";
        if (is_char())   return "char";
        if (is_int())    return "int";
        if (is_float())  return "float";
        if (is_function()) return "function";
        return "string";
    }

    dynamic call(std::vector<dynamic> args) const {
        if (!is_function()) fail("a dynamic value of type '" + type_name() + "' cannot be called");
        const dynamic_function& f = *std::get<std::shared_ptr<dynamic_function>>(m_v);
        if (args.size() != f.arity) fail("this function takes " + std::to_string(f.arity) + " argument" + (f.arity == 1 ? "" : "s") + " but was called with " + std::to_string(args.size()));
        return f.body(args);
    }

    std::string to_string() const {
        if (is_none())   return "none";
        if (is_bool())   return std::get<bool>(m_v) ? "true" : "false";
        if (is_char())   return utf8(std::get<char32_t>(m_v));
        if (is_int())    return std::get<i2048>(m_v).to_string();
        if (is_float())  return float_display(std::get<f2048>(m_v));
        if (is_function()) return "function";
        return std::get<std::string>(m_v);
    }

    f2048 as_float() const {
        if (is_float()) return std::get<f2048>(m_v);
        if (is_int())   return cast<f2048>(std::get<i2048>(m_v));
        fail("dynamic value of type '" + type_name() + "' is not a number");
    }

    const i2048& as_int() const {
        if (!is_int()) fail("dynamic value of type '" + type_name() + "' is not an integer");
        return std::get<i2048>(m_v);
    }

    template <typename T>
    T as() const {
        if constexpr (std::is_same_v<T, dynamic>) {
            return *this;
        } else if constexpr (is_int_v<T> || is_hardware_int_v<T>) {
            if (is_int())  return cast<T>(std::get<i2048>(m_v));
            if (is_char()) return cast<T>(std::get<char32_t>(m_v));
            fail("cannot convert dynamic value of type '" + type_name() + "' to an integer");
        } else if constexpr (is_float_v<T> || is_hardware_float_v<T>) {
            return cast<T>(as_float());
        } else if constexpr (std::is_same_v<T, bool>) {
            if (is_bool()) return std::get<bool>(m_v);
            fail("cannot convert dynamic value of type '" + type_name() + "' to bool");
        } else if constexpr (std::is_same_v<T, char32_t>) {
            if (is_char()) return std::get<char32_t>(m_v);
            fail("cannot convert dynamic value of type '" + type_name() + "' to char");
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (is_string()) return std::get<std::string>(m_v);
            fail("cannot convert dynamic value of type '" + type_name() + "' to string");
        } else {
            static_assert(sizeof(T) == 0, "unsupported dynamic conversion");
        }
    }

    bool truthy() const {
        if (is_none())   return false;
        if (is_bool())   return std::get<bool>(m_v);
        if (is_char())   return std::get<char32_t>(m_v) != 0;
        if (is_int())    return !std::get<i2048>(m_v).is_zero();
        if (is_float())  return !std::get<f2048>(m_v).is_zero();
        if (is_function()) return true;
        return !std::get<std::string>(m_v).empty();
    }

    friend dynamic arith(const dynamic& a, const dynamic& b, char op) {
        if (op == '+' && a.is_string() && b.is_string()) return dynamic(std::get<std::string>(a.m_v) + std::get<std::string>(b.m_v));
        if (op == '+' && a.is_string() && b.is_char())   return dynamic(op_add(std::get<std::string>(a.m_v), std::get<char32_t>(b.m_v)));
        if (op == '+' && a.is_char() && b.is_string())   return dynamic(op_add(std::get<char32_t>(a.m_v), std::get<std::string>(b.m_v)));
        if (!a.is_number() || !b.is_number()) fail(std::string("operator '") + op + "' cannot be applied to dynamic values of type '" + a.type_name() + "' and '" + b.type_name() + "'");

        if (a.is_int() && b.is_int()) {
            const i2048& x = std::get<i2048>(a.m_v);
            const i2048& y = std::get<i2048>(b.m_v);
            switch (op) {
                case '+': return dynamic(op_add(x, y));
                case '-': return dynamic(op_sub(x, y));
                case '*': return dynamic(op_mul(x, y));
                case '/': return dynamic(op_div(x, y));
                case '%': return dynamic(op_mod(x, y));
                case '^': return dynamic(op_pow(x, y));
                default:  break;
            }
        } else {
            const f2048 x = a.as_float(), y = b.as_float();
            switch (op) {
                case '+': return dynamic(x + y);
                case '-': return dynamic(x - y);
                case '*': return dynamic(x * y);
                case '/': return dynamic(x / y);
                case '^': return dynamic(op_pow(x, y));
                case '%': fail("operator '%' cannot be applied to floating-point dynamic values");
                default:  break;
            }
        }
        fail("unsupported dynamic operation");
    }

    friend dynamic operator+(const dynamic& a, const dynamic& b) { return arith(a, b, '+'); }
    friend dynamic operator-(const dynamic& a, const dynamic& b) { return arith(a, b, '-'); }
    friend dynamic operator*(const dynamic& a, const dynamic& b) { return arith(a, b, '*'); }
    friend dynamic operator/(const dynamic& a, const dynamic& b) { return arith(a, b, '/'); }
    friend dynamic operator%(const dynamic& a, const dynamic& b) { return arith(a, b, '%'); }

    friend dynamic operator-(const dynamic& a) {
        if (a.is_int())   return dynamic(op_neg(std::get<i2048>(a.m_v)));
        if (a.is_float()) return dynamic(-std::get<f2048>(a.m_v));
        fail("unary '-' cannot be applied to a dynamic value of type '" + a.type_name() + "'");
    }

    friend int compare(const dynamic& a, const dynamic& b) {
        if (a.is_number() && b.is_number()) {
            if (a.is_int() && b.is_int()) {
                const i2048& x = std::get<i2048>(a.m_v);
                const i2048& y = std::get<i2048>(b.m_v);
                return x < y ? -1 : (y < x ? 1 : 0);
            }
            const f2048 x = a.as_float(), y = b.as_float();
            return x < y ? -1 : (y < x ? 1 : 0);
        }
        if (a.is_string() && b.is_string()) {
            const int c = std::get<std::string>(a.m_v).compare(std::get<std::string>(b.m_v));
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        if (a.is_char() && b.is_char()) {
            const char32_t x = std::get<char32_t>(a.m_v), y = std::get<char32_t>(b.m_v);
            return x < y ? -1 : (y < x ? 1 : 0);
        }
        fail("dynamic values of type '" + a.type_name() + "' and '" + b.type_name() + "' cannot be ordered");
    }

    friend bool operator==(const dynamic& a, const dynamic& b) {
        if (a.is_number() && b.is_number()) return !a.is_unordered_number() && !b.is_unordered_number() && compare(a, b) == 0;
        if (a.m_v.index() != b.m_v.index()) return false;
        if (a.is_none())   return true;
        if (a.is_bool())   return std::get<bool>(a.m_v) == std::get<bool>(b.m_v);
        if (a.is_char())   return std::get<char32_t>(a.m_v) == std::get<char32_t>(b.m_v);
        if (a.is_function()) return std::get<std::shared_ptr<dynamic_function>>(a.m_v) == std::get<std::shared_ptr<dynamic_function>>(b.m_v);
        return std::get<std::string>(a.m_v) == std::get<std::string>(b.m_v);
    }

    friend bool operator!=(const dynamic& a, const dynamic& b) { return !(a == b); }
    friend bool operator< (const dynamic& a, const dynamic& b) { return compare(a, b) <  0; }
    friend bool operator> (const dynamic& a, const dynamic& b) { return compare(a, b) >  0; }
    friend bool operator<=(const dynamic& a, const dynamic& b) { return compare(a, b) <= 0; }
    friend bool operator>=(const dynamic& a, const dynamic& b) { return compare(a, b) >= 0; }

private:
    value_type m_v;
};

template <typename T, typename Alloc, typename... A>
inline T* custom_new(Alloc alloc, A&&... args) {
    void* memory = alloc(static_cast<long long>(sizeof(T)));
    if (!memory) fail("operator(new) returned nullptr");
    return ::new (memory) T(std::forward<A>(args)...);
}

inline constexpr std::size_t array_header_size = alignof(std::max_align_t) > sizeof(long long) ? alignof(std::max_align_t) : sizeof(long long);

template <typename T, typename Alloc>
inline T* custom_new_array(long long count, Alloc alloc) {
    if (count < 0) fail("array size " + std::to_string(count) + " is negative");
    auto* memory = static_cast<unsigned char*>(alloc(static_cast<long long>(array_header_size + sizeof(T) * static_cast<std::size_t>(count))));
    if (!memory) fail("operator(new[]) returned nullptr");
    *reinterpret_cast<long long*>(memory) = count;
    T* first = reinterpret_cast<T*>(memory + array_header_size);
    for (long long i = 0; i < count; ++i) ::new (static_cast<void*>(first + i)) T();
    return first;
}

template <typename T, typename Release>
inline void custom_delete(T* p, Release release) {
    if (!p) return;
    p->~T();
    release(static_cast<void*>(p));
}

template <typename T, typename Release>
inline void custom_delete_array(T* p, Release release) {
    if (!p) return;
    unsigned char* memory = reinterpret_cast<unsigned char*>(p) - array_header_size;
    const long long count = *reinterpret_cast<long long*>(memory);
    for (long long i = count; i-- > 0;) p[i].~T();
    release(static_cast<void*>(memory));
}

inline bool is_special(const dynamic& d, special_test t) { return d.matches_special(t); }

template <typename T>
inline bool dynamic_is(const dynamic& d) {
    if constexpr (std::is_same_v<T, bool>) return d.is_bool();
    else if constexpr (std::is_same_v<T, char32_t>) return d.is_char();
    else if constexpr (std::is_same_v<T, std::string>) return d.is_string();
    else if constexpr (is_int_v<T>) return d.is_int();
    else if constexpr (is_float_v<T>) return d.is_float();
    else return false;
}

template <typename T>
inline T dynamic_as(const dynamic& d, const std::string& name) {
    if (!dynamic_is<T>(d)) fail("this var holds a value of type '" + d.type_name() + "', not '" + name + "'");
    return d.template as<T>();
}

template <typename T, typename... A>
inline T& variant_as(std::variant<A...>& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this variant does not hold a value of type '" + name + "'");
    return std::get<T>(v);
}

template <typename T, typename... A>
inline const T& variant_as(const std::variant<A...>& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this variant does not hold a value of type '" + name + "'");
    return std::get<T>(v);
}

template <typename T, typename... A>
inline T variant_as(std::variant<A...>&& v, const std::string& name) {
    if (!std::holds_alternative<T>(v)) fail("this variant does not hold a value of type '" + name + "'");
    return std::get<T>(std::move(v));
}

template <typename T, typename U>
inline bool object_is(const U& u) { return dynamic_cast<const T*>(&u) != nullptr; }

template <typename T, typename U>
inline T& object_as(U& u, const std::string& name) {
    T* p = dynamic_cast<T*>(&u);
    if (!p) fail("this object's real type is not '" + name + "'");
    return *p;
}

template <typename T, typename U>
inline const T& object_as(const U& u, const std::string& name) {
    const T* p = dynamic_cast<const T*>(&u);
    if (!p) fail("this object's real type is not '" + name + "'");
    return *p;
}

template <typename T, typename U>
inline bool pointer_is(U* u) { return u && dynamic_cast<const T*>(u) != nullptr; }

template <typename T, typename U>
inline auto pointer_as(U* u, const std::string& name) -> std::conditional_t<std::is_const_v<U>, const T*, T*> {
    if (!u) return nullptr;
    auto* p = dynamic_cast<std::conditional_t<std::is_const_v<U>, const T*, T*>>(u);
    if (!p) fail("this object's real type is not '" + name + "'");
    return p;
}

template <typename R, typename... Ps, typename F, std::size_t... I>
inline dynamic invoke_dynamic(F& f, std::vector<dynamic>& a, std::index_sequence<I...>) {
    if constexpr (std::is_void_v<R>) { f(a[I].template as<std::decay_t<Ps>>()...); return dynamic(); }
    else return dynamic(f(a[I].template as<std::decay_t<Ps>>()...));
}

template <typename R, typename... Ps, typename F>
inline dynamic make_dynamic_function(F f) {
    auto fn = std::make_shared<dynamic_function>();
    fn->arity = sizeof...(Ps);
    fn->body = [f](std::vector<dynamic>& a) mutable -> dynamic { return invoke_dynamic<R, Ps...>(f, a, std::index_sequence_for<Ps...>{}); };
    return dynamic(std::move(fn));
}

inline dynamic op_add(const dynamic& a, const dynamic& b) { return a + b; }
inline dynamic op_sub(const dynamic& a, const dynamic& b) { return a - b; }
inline dynamic op_mul(const dynamic& a, const dynamic& b) { return a * b; }
inline dynamic op_div(const dynamic& a, const dynamic& b) { return a / b; }
inline dynamic op_mod(const dynamic& a, const dynamic& b) { return a % b; }
inline dynamic op_pow(const dynamic& a, const dynamic& b) { return arith(a, b, '^'); }
inline dynamic op_neg(const dynamic& a)                   { return -a; }
inline dynamic op_pos(const dynamic& a)                   { return a; }

inline bool truthy(const dynamic& d) { return d.truthy(); }
inline std::string to_display(const dynamic& d) { return d.to_string(); }

inline dynamic& pre_inc(dynamic& x)  { x = x + dynamic(i2048(1)); return x; }
inline dynamic& pre_dec(dynamic& x)  { x = x - dynamic(i2048(1)); return x; }
inline dynamic  post_inc(dynamic& x) { dynamic o = x; x = x + dynamic(i2048(1)); return o; }
inline dynamic  post_dec(dynamic& x) { dynamic o = x; x = x - dynamic(i2048(1)); return o; }

template <typename To>
inline To from_dynamic(const dynamic& d) { return d.template as<To>(); }

template <typename T, typename F>
inline T& compound(T& target, F&& op) {
    target = op(static_cast<const T&>(target));
    return target;
}

inline std::vector<char32_t> decode_utf8(const std::string& s) {
    std::vector<char32_t> out;
    std::size_t i = 0;

    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        std::size_t extra = 0;
        if (c >= 0xF0)      { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (std::size_t k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }

    return out;
}

inline std::vector<dynamic> dynamic_items(const dynamic& d) {
    if (!d.is_string()) fail("a for-each loop can't go through a var holding a value of type '" + d.type_name() + "'; only strings can be looped over");
    std::vector<dynamic> out;
    for (char32_t c : decode_utf8(d.as<std::string>())) out.emplace_back(c);
    return out;
}

template <typename... Fs>
struct overloaded : Fs... { using Fs::operator()...; };

template <typename... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

template <typename T>
class task {
    struct state {
        std::function<T()>  body;
        std::optional<T>    value;
        std::exception_ptr  error;
        bool                done = false;
        bool                running = false;
    };

public:
    task() = default;
    explicit task(std::function<T()> body) : m_state(std::make_shared<state>()) { m_state->body = std::move(body); }

    T get() {
        if (!m_state) fail("awaited an empty task");
        state& s = *m_state;

        if (!s.done) {
            if (s.running) fail("a task awaited itself");
            s.running = true;
            try { s.value.emplace(s.body()); } catch (...) { s.error = std::current_exception(); }
            s.running = false;
            s.done = true;
            s.body = nullptr;
        }

        if (s.error) std::rethrow_exception(s.error);
        return *s.value;
    }

private:
    std::shared_ptr<state> m_state;
};

template <>
class task<void> {
    struct state {
        std::function<void()> body;
        std::exception_ptr     error;
        bool                   done = false;
        bool                   running = false;
    };

public:
    task() = default;
    explicit task(std::function<void()> body) : m_state(std::make_shared<state>()) { m_state->body = std::move(body); }

    void get() {
        if (!m_state) fail("awaited an empty task");
        state& s = *m_state;

        if (!s.done) {
            if (s.running) fail("a task awaited itself");
            s.running = true;
            try { s.body(); } catch (...) { s.error = std::current_exception(); }
            s.running = false;
            s.done = true;
            s.body = nullptr;
        }

        if (s.error) std::rethrow_exception(s.error);
    }

private:
    std::shared_ptr<state> m_state;
};

#ifdef WALNUT_RT_COROUTINES
template <typename T>
class generator {
public:
    struct promise_type {
        std::optional<T>   current;
        std::exception_ptr error;

        generator get_return_object() { return generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v) { current.emplace(std::move(v)); return {}; }
        void return_void() {}
        void unhandled_exception() { error = std::current_exception(); }
    };

    using handle = std::coroutine_handle<promise_type>;

    struct sentinel {};

    class iterator {
    public:
        explicit iterator(handle h) : m_h(h) {}
        T& operator*() const { return *m_h.promise().current; }
        iterator& operator++() { generator::step(m_h); return *this; }
        bool operator!=(sentinel) const { return !m_h.done(); }
        bool operator==(sentinel) const { return m_h.done(); }

    private:
        handle m_h;
    };

    generator() = default;

    iterator begin() const {
        if (!m_frame) fail("iterated an empty generator");
        if (m_frame->started) fail("a generator can only be iterated once");
        m_frame->started = true;
        step(m_frame->h);
        return iterator(m_frame->h);
    }

    sentinel end() const { return {}; }

private:
    struct frame {
        handle h;
        bool   started = false;
        explicit frame(handle hh) : h(hh) {}
        ~frame() { if (h) h.destroy(); }
        frame(const frame&) = delete;
        frame& operator=(const frame&) = delete;
    };

    explicit generator(handle h) : m_frame(std::make_shared<frame>(h)) {}

    static void step(handle h) {
        h.promise().current.reset();
        h.resume();
        if (h.promise().error) {
            std::exception_ptr e = h.promise().error;
            h.promise().error = nullptr;
            std::rethrow_exception(e);
        }
    }

    std::shared_ptr<frame> m_frame;
};
#endif

template <typename C>
inline void check_binding_count(const C& c, std::size_t n) {
    if (c.size() != n) fail("structured binding expects " + std::to_string(n) + " elements but the array has " + std::to_string(c.size()));
}

inline i32 count_of(const std::string& s) { return i32(static_cast<long long>(decode_utf8(s).size())); }

template <typename C>
inline i32 count_of(const C& c) { return i32(static_cast<long long>(c.size())); }

template <typename I>
inline char32_t char_at(const std::string& s, const I& i) {
    const long long n = to_ll(i);
    const std::vector<char32_t> cps = decode_utf8(s);
    if (n < 0 || static_cast<std::size_t>(n) >= cps.size()) fail("string index " + std::to_string(n) + " out of range");
    return cps[static_cast<std::size_t>(n)];
}

template <typename I>
inline char32_t set_char(std::string& s, const I& i, char32_t c) {
    const long long n = to_ll(i);
    std::vector<char32_t> cps = decode_utf8(s);
    if (n < 0 || static_cast<std::size_t>(n) >= cps.size()) fail("string index " + std::to_string(n) + " out of range");
    cps[static_cast<std::size_t>(n)] = c;
    std::string out;
    for (char32_t cp : cps) append_utf8(out, cp);
    s = std::move(out);
    return c;
}

namespace bm {

inline std::string needle_of(const std::string& s) { return s; }
inline std::string needle_of(char32_t c) { return utf8(c); }

inline i32 cp_index(const std::string& s, std::size_t byte) {
    long long n = 0;
    for (std::size_t i = 0; i < byte && i < s.size(); ++i) if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return i32(n);
}

inline std::size_t cp_offset(const std::string& s, long long n) {
    std::size_t i = 0;
    while (i < s.size() && n > 0) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        --n;
    }
    return i;
}

inline i32 length(const std::string& s) { return count_of(s); }
template <typename C> inline i32 length(const C& c) { return count_of(c); }
inline i32 size(const std::string& s) { return i32(static_cast<long long>(s.size())); }
template <typename C> inline i32 size(const C& c) { return count_of(c); }
inline i32 countof(const std::string& s) { return count_of(s); }
template <typename C> inline i32 countof(const C& c) { return count_of(c); }
template <typename C> inline bool empty(const C& c) { return c.empty(); }

inline i32 byte_size(const std::string& s) { return i32(static_cast<long long>(s.size())); }
template <typename C> inline i32 byte_size(const C& c, long long element) { return i32(static_cast<long long>(c.size()) * element); }

template <typename I>
inline std::string substr(const std::string& s, const I& start) {
    const long long b = to_ll(start);
    const long long total = to_ll(count_of(s));
    if (b < 0 || b > total) fail("substr start " + std::to_string(b) + " out of range for a string of length " + std::to_string(total));
    return s.substr(cp_offset(s, b));
}

template <typename I, typename J>
inline std::string substr(const std::string& s, const I& start, const J& count) {
    const long long b = to_ll(start);
    const long long n = to_ll(count);
    const long long total = to_ll(count_of(s));
    if (b < 0 || b > total) fail("substr start " + std::to_string(b) + " out of range for a string of length " + std::to_string(total));
    if (n < 0) fail("substr count " + std::to_string(n) + " is negative");
    const std::size_t from = cp_offset(s, b);
    const std::size_t to = from + cp_offset(s.substr(from), n);
    return s.substr(from, to - from);
}

template <typename N>
inline i32 find(const std::string& s, const N& needle) {
    const std::size_t at = s.find(needle_of(needle));
    if (at == std::string::npos) return i32::from_kind(special_kind::undefined);
    return cp_index(s, at);
}

template <typename N>
inline bool contains(const std::string& s, const N& needle) { return s.find(needle_of(needle)) != std::string::npos; }

template <typename N>
inline bool starts_with(const std::string& s, const N& needle) {
    const std::string n = needle_of(needle);
    return s.size() >= n.size() && s.compare(0, n.size(), n) == 0;
}

template <typename N>
inline bool ends_with(const std::string& s, const N& needle) {
    const std::string n = needle_of(needle);
    return s.size() >= n.size() && s.compare(s.size() - n.size(), n.size(), n) == 0;
}

inline char32_t upper_cp(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

inline char32_t lower_cp(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

inline std::string to_upper(const std::string& s) {
    std::string out;
    for (char32_t c : decode_utf8(s)) append_utf8(out, upper_cp(c));
    return out;
}

inline std::string to_lower(const std::string& s) {
    std::string out;
    for (char32_t c : decode_utf8(s)) append_utf8(out, lower_cp(c));
    return out;
}

inline std::string trim(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

template <typename C, typename V>
inline i32 find(const C& c, const V& v) {
    long long i = 0;
    for (const auto& e : c) { if (e == v) return i32(i); ++i; }
    return i32::from_kind(special_kind::undefined);
}

template <typename C, typename V>
inline bool contains(const C& c, const V& v) {
    for (const auto& e : c) if (e == v) return true;
    return false;
}

} // namespace bm

template <typename T>
class late {
public:
    late() = default;
    late(const late&) = delete;
    late& operator=(const late&) = delete;
    ~late() { if (m_live) get().~T(); }

    template <typename... A>
    void init(A&&... args) {
        if (m_live) { get().~T(); m_live = false; }
        ::new (static_cast<void*>(m_buf)) T(std::forward<A>(args)...);
        m_live = true;
    }

    T& get() {
        if (!m_live) fail("global variable used before it was initialized");
        return *std::launder(reinterpret_cast<T*>(m_buf));
    }

private:
    alignas(T) unsigned char m_buf[sizeof(T)];
    bool m_live = false;
};

inline std::vector<std::string> program_args(int argc, char** argv) {
    std::vector<std::string> out;
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

template <typename T>
inline int exit_code(const T& v) {
    if constexpr (is_int_v<T>) return static_cast<int>(to_ll(v));
    else return 0;
}

template <typename T>
struct init_list_store {
    std::vector<T> items;
    explicit init_list_store(std::vector<T> v) : items(std::move(v)) {}
    const T* data() const noexcept { return items.data(); }
    std::size_t size() const noexcept { return items.size(); }
};

template <>
struct init_list_store<bool> {
    std::unique_ptr<bool[]> items;
    std::size_t n = 0;
    explicit init_list_store(const std::vector<bool>& v) : items(new bool[v.size() ? v.size() : 1]), n(v.size()) {
        for (std::size_t i = 0; i < n; ++i) items[i] = v[i];
    }
    const bool* data() const noexcept { return items.get(); }
    std::size_t size() const noexcept { return n; }
};

template <typename T>
class init_list {
public:
    init_list() = default;
    explicit init_list(std::vector<T> items) : m_store(std::make_shared<const init_list_store<T>>(std::move(items))) {}

    long long size() const noexcept { return m_store ? static_cast<long long>(m_store->size()) : 0; }
    const T* begin() const noexcept { return m_store ? m_store->data() : nullptr; }
    const T* end() const noexcept { return m_store ? m_store->data() + m_store->size() : nullptr; }

    const T& at(long long i) const {
        if (i < 0 || i >= size()) fail("initializer_list index " + std::to_string(i) + " is out of range for size " + std::to_string(size()));
        return m_store->data()[i];
    }

private:
    std::shared_ptr<const init_list_store<T>> m_store;
};

template <typename F>
inline int run_program(F&& body) {
    volatile char stack_anchor = 0;
    safety::start(const_cast<char*>(&stack_anchor));
    try {
        return safety::finish(body());
    } catch (const runtime_error& e) {
        std::cout.flush();
        std::cerr << "walnut runtime error: " << e.what() << "\n";
        return 70;
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << "walnut: uncaught exception: " << e.what() << "\n";
        return 71;
    } catch (...) {
        std::cout.flush();
        std::cerr << "walnut: uncaught exception\n";
        return 71;
    }
}

} // namespace walnut_rt

#include "walnut_io.hpp"
#include "walnut_lazy.hpp"

#endif // WALNUT_RUNTIME_HPP
