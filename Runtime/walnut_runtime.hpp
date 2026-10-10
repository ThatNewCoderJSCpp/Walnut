#ifndef WALNUT_RUNTIME_HPP
#define WALNUT_RUNTIME_HPP

#ifndef FIZMO
#define FIZMO
#endif
#include "fizmo/includes.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <optional>
#include <cstdlib>
#include <functional>
#include <limits>
#include <iostream>
#include <memory>
#include <new>
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

namespace walnut_rt {

namespace mp = fizmo::multiprecision;

using i8    = mp::int8;
using i16   = mp::int16;
using i32   = mp::int32;
using i64   = mp::int64;
using i128  = mp::int128;
using i256  = mp::int256;
using i512  = mp::int512;
using i1024 = mp::int1024;
using i2048 = mp::int2048;

using u8    = mp::uint8;
using u16   = mp::uint16;
using u32   = mp::uint32;
using u64   = mp::uint64;
using u128  = mp::uint128;
using u256  = mp::uint256;
using u512  = mp::uint512;
using u1024 = mp::uint1024;
using u2048 = mp::uint2048;

using f32   = mp::float32;
using f64   = mp::float64;
using f128  = mp::float128;
using f256  = mp::float256;
using f512  = mp::float512;
using f1024 = mp::float1024;
using f2048 = mp::float2048;

using string    = std::string;
using text      = std::string;
using character = char32_t;

struct runtime_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(const std::string& message) { throw runtime_error(message); }

template <typename T> struct int_info { static constexpr bool value = false; };

template <std::size_t B, mp::sign S>
struct int_info<mp::integer<B, S>> {
    static constexpr bool        value     = true;
    static constexpr std::size_t bits      = B;
    static constexpr bool        is_signed = (S == mp::sign::is_signed);
    using unsigned_type = mp::integer<B, mp::sign::is_unsigned>;
    using signed_type   = mp::integer<B, mp::sign::is_signed>;
};

template <typename T> struct float_info { static constexpr bool value = false; };
template <> struct float_info<f32>   { static constexpr bool value = true; static constexpr std::size_t bits = 32; };
template <> struct float_info<f64>   { static constexpr bool value = true; static constexpr std::size_t bits = 64; };
template <> struct float_info<f128>  { static constexpr bool value = true; static constexpr std::size_t bits = 128; };
template <> struct float_info<f256>  { static constexpr bool value = true; static constexpr std::size_t bits = 256; };
template <> struct float_info<f512>  { static constexpr bool value = true; static constexpr std::size_t bits = 512; };
template <> struct float_info<f1024> { static constexpr bool value = true; static constexpr std::size_t bits = 1024; };
template <> struct float_info<f2048> { static constexpr bool value = true; static constexpr std::size_t bits = 2048; };

template <typename T> inline constexpr bool is_int_v   = int_info<std::decay_t<T>>::value;
template <typename T> inline constexpr bool is_float_v = float_info<std::decay_t<T>>::value;

template <typename T>
using unsigned_of = typename int_info<T>::unsigned_type;

template <typename T>
inline T norm(const T& v) {
    if constexpr (is_int_v<T> && int_info<T>::is_signed) {
        using U = unsigned_of<T>;
        if (v.is_undefined()) return v;
        const auto& mag = v.magnitude();
        U u(mag);
        constexpr std::size_t B = int_info<T>::bits;
        const bool top = u.get_bit(B - 1);
        if (!v.is_negative()) {
            if (!top) return v;
            U neg_mag = U() - u;
            return T(neg_mag.magnitude(), true);
        }
        if (!top) return v;
        U rest = U() - u;
        if (rest.is_zero()) return v;
        if (rest.get_bit(B - 1)) return v;
        return T(rest.magnitude(), false);
    } else {
        return v;
    }
}

template <typename T>
inline unsigned_of<T> to_twos(const T& v) {
    using U = unsigned_of<T>;
    if constexpr (!int_info<T>::is_signed) {
        return v;
    } else {
        U u(v.magnitude());
        if (v.is_negative()) u = U() - u;
        return u;
    }
}

template <typename T>
inline T from_twos(const unsigned_of<T>& u) {
    if constexpr (!int_info<T>::is_signed) {
        return u;
    } else {
        constexpr std::size_t B = int_info<T>::bits;
        if (!u.get_bit(B - 1)) return T(u.magnitude(), false);
        unsigned_of<T> m = unsigned_of<T>() - u;
        return T(m.magnitude(), true);
    }
}

template <typename T>
inline long long to_ll(const T& v) {
    if constexpr (is_int_v<T>) {
        long long low = static_cast<long long>(v.get_lowest_bits() & 0x7fffffffffffffffULL);
        if constexpr (int_info<T>::is_signed) { if (v.is_negative()) return -low; }
        return low;
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
    const unsigned long long u = static_cast<unsigned long long>(v.get_lowest_bits());
    if (v.is_negative()) return u == (1ULL << 63) ? std::numeric_limits<long long>::min() : -static_cast<long long>(u);
    return static_cast<long long>(u);
}

template <typename To, typename From>
inline To int_to_int(const From& v) {
    if constexpr (std::is_same_v<To, From>) {
        return v;
    } else {
        constexpr std::size_t FB = int_info<From>::bits;
        constexpr std::size_t TB = int_info<To>::bits;
        constexpr bool        FS = int_info<From>::is_signed;
        constexpr bool        TS = int_info<To>::is_signed;

        if constexpr (FS == TS && TB >= FB) {
            return To(v);
        } else if constexpr (TB <= 64 && FB <= 64) {
            unsigned long long u = static_cast<unsigned long long>(to_twos(v).get_lowest_bits());
            if constexpr (FS && TB > FB && FB < 64) { if (v.is_negative()) u |= (~0ULL << FB); }
            if constexpr (TB < 64) u &= ((1ULL << TB) - 1ULL);

            if constexpr (TS) {
                long long sv = 0;
                if constexpr (TB < 64) sv = ((u >> (TB - 1)) & 1ULL) ? static_cast<long long>(u | (~0ULL << TB)) : static_cast<long long>(u);
                else sv = static_cast<long long>(u);
                if (sv == std::numeric_limits<long long>::min()) return To(i64(sv + 1)) - To(1LL);
                return To(sv);
            } else {
                return To(u);
            }
        } else {
            using W = mp::int4096;
            const W& modulus = power_of_two_modulus<TB>();
            W r = W(v) % modulus;
            if (r.is_negative()) r = r + modulus;
            if constexpr (TS) return from_twos<To>(unsigned_of<To>(r));
            else return To(r);
        }
    }
}

template <typename F>
inline f2048 widen_float(const F& f) {
    if constexpr (std::is_same_v<F, f2048>) return f;
    else return f2048(f);
}

template <typename T>
inline f2048 int_to_f2048(const T& v) {
    i2048 w = int_to_int<i2048>(v);
    if constexpr (int_info<T>::is_signed) {
        return f2048(f2048::sstore_t(w));
    } else {
        return f2048(f2048::sstore_t(w));
    }
}

template <typename To, typename F>
inline To float_to_int(const F& f) {
    f2048 w = widen_float(f);
    if (!w.is_finite()) fail("float value cannot be converted to an integer");
    auto ip = w.get_integer_part_as_int();
    i2048 mag = i2048(ip);
    i2048 v = w.is_negative() ? -mag : mag;
    return int_to_int<To>(v);
}

template <typename To, typename From>
inline To cast(const From& v) {
    using T = std::decay_t<From>;
    if constexpr (std::is_same_v<To, T>) {
        return v;
    } else if constexpr (is_int_v<To> && is_int_v<T>) {
        return int_to_int<To>(v);
    } else if constexpr (is_float_v<To> && is_float_v<T>) {
        return To(v);
    } else if constexpr (is_float_v<To> && is_int_v<T>) {
        if constexpr (int_info<T>::bits <= 64 && float_info<To>::bits == 32) {
            if constexpr (int_info<T>::is_signed) return To(static_cast<float>(to_ll_exact(v)));
            else return To(static_cast<float>(static_cast<unsigned long long>(v.get_lowest_bits())));
        } else if constexpr (int_info<T>::bits <= 64 && float_info<To>::bits == 64) {
            if constexpr (int_info<T>::is_signed) return To(static_cast<double>(to_ll_exact(v)));
            else return To(static_cast<double>(static_cast<unsigned long long>(v.get_lowest_bits())));
        } else if constexpr (std::is_same_v<To, f2048>) return int_to_f2048(v);
        else return To(int_to_f2048(v));
    } else if constexpr (is_int_v<To> && is_float_v<T>) {
        return float_to_int<To>(v);
    } else if constexpr (std::is_same_v<To, bool>) {
        if constexpr (is_int_v<T> || is_float_v<T>) return !v.is_zero();
        else return static_cast<bool>(v);
    } else if constexpr (is_int_v<To> && (std::is_same_v<T, bool> || std::is_same_v<T, char32_t> || std::is_integral_v<T>)) {
        return int_to_int<To>(i64(static_cast<long long>(v)));
    } else if constexpr (is_float_v<To> && (std::is_same_v<T, bool> || std::is_same_v<T, char32_t> || std::is_integral_v<T>)) {
        return cast<To>(i64(static_cast<long long>(v)));
    } else if constexpr (std::is_same_v<To, char32_t> && is_int_v<T>) {
        return static_cast<char32_t>(to_twos(v).get_lowest_bits() & 0xffffffffULL);
    } else {
        return static_cast<To>(v);
    }
}

template <typename T>
inline bool truthy(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v;
    else if constexpr (is_int_v<T> || is_float_v<T>) return !v.is_zero();
    else if constexpr (std::is_pointer_v<T> || std::is_null_pointer_v<T>) return v != nullptr;
    else if constexpr (std::is_same_v<T, char32_t>) return v != 0;
    else return static_cast<bool>(v);
}

template <typename T> inline T op_add(const T& a, const T& b) { if constexpr (is_int_v<T>) return norm(a + b); else return a + b; }
template <typename T> inline T op_sub(const T& a, const T& b) { if constexpr (is_int_v<T>) return norm(a - b); else return a - b; }
template <typename T> inline T op_mul(const T& a, const T& b) { if constexpr (is_int_v<T>) return norm(a * b); else return a * b; }
template <typename T> inline T op_neg(const T& a)             { if constexpr (is_int_v<T>) return norm(T() - a); else return -a; }
template <typename T> inline T op_pos(const T& a)             { return a; }

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
    if constexpr (is_int_v<T>) {
        if (b.is_zero()) fail("integer division by zero");
        return norm(a / b);
    } else {
        return a / b;
    }
}

template <typename T>
inline T op_mod(const T& a, const T& b) {
    if (b.is_zero()) fail("integer modulo by zero");
    return a % b;
}

template <typename T>
inline T op_pow(const T& a, const T& b) {
    if constexpr (is_int_v<T>) {
        if constexpr (int_info<T>::is_signed) {
            if (b.is_negative()) {
                if (a.is_zero()) fail("zero raised to a negative power");
                const T one(1);
                if (a == one) return one;
                if (a == -one) return to_twos(b).get_bit(0) ? -one : one;
                fail("integer raised to a negative power");
            }
            using U = unsigned_of<T>;
            U base(a.magnitude());
            U r(std::uint64_t(1));
            U e(b.magnitude());
            while (!e.is_zero()) {
                if (e.get_bit(0)) r = r * base;
                base = base * base;
                e = e >> 1;
            }
            const bool neg = a.is_negative() && b.magnitude().get_bit(0);
            T out = from_twos<T>(neg ? U() - r : r);
            return out;
        } else {
            T base = a, r = T(std::uint64_t(1)), e = b;
            while (!e.is_zero()) {
                if (e.get_bit(0)) r = r * base;
                base = base * base;
                e = e >> 1;
            }
            return r;
        }
    } else {
        return T(mp::math::pow(a, b));
    }
}

template <typename T>
inline T op_bitand(const T& a, const T& b) { return from_twos<T>(to_twos(a) & to_twos(b)); }
template <typename T>
inline T op_bitor(const T& a, const T& b)  { return from_twos<T>(to_twos(a) | to_twos(b)); }
template <typename T>
inline T op_bitxor(const T& a, const T& b) { return from_twos<T>(to_twos(a) ^ to_twos(b)); }
template <typename T>
inline T op_bitnot(const T& a)             { return from_twos<T>(~to_twos(a)); }

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
inline T op_shl(const T& a, const S& s) {
    const std::size_t n = shift_count<T>(s);
    return from_twos<T>(to_twos(a) << n);
}

template <typename T, typename S>
inline T op_shr(const T& a, const S& s) {
    const std::size_t n = shift_count<T>(s);
    if constexpr (int_info<T>::is_signed) {
        if (a.is_negative()) {
            using U = unsigned_of<T>;
            U u = to_twos(a);
            U shifted = u >> n;
            U fill = (~U()) << (int_info<T>::bits - n);
            if (n == 0) fill = U();
            return from_twos<T>(shifted | fill);
        }
    }
    return from_twos<T>(to_twos(a) >> n);
}

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

template <typename C, typename I>
inline decltype(auto) index(C& c, const I& i) {
    long long n = to_ll(i);
    if constexpr (std::is_pointer_v<std::decay_t<C>>) {
        return c[n];
    } else {
        if (n < 0 || static_cast<std::size_t>(n) >= c.size()) fail("array index " + std::to_string(n) + " out of range");
        return c[static_cast<std::size_t>(n)];
    }
}

inline std::string utf8(char32_t c) { std::string s; append_utf8(s, c); return s; }

class dynamic;
inline std::string to_display(const dynamic& d);

template <typename T>
inline std::string to_display(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
    else if constexpr (std::is_same_v<T, char32_t>) return utf8(v);
    else if constexpr (std::is_same_v<T, std::string>) return v;
    else if constexpr (std::is_convertible_v<T, const char*>) return std::string(v);
    else if constexpr (std::is_null_pointer_v<T>) return "nullptr";
    else if constexpr (std::is_pointer_v<T>) { std::ostringstream os; os << static_cast<const void*>(v); return os.str(); }
    else if constexpr (is_int_v<T>) return v.to_string();
    else if constexpr (is_float_v<T>) { std::ostringstream os; os << v; return os.str(); }
    else { std::ostringstream os; os << v; return os.str(); }
}

class dynamic {
public:
    using value_type = std::variant<std::monostate, bool, char32_t, i2048, f2048, std::string>;

    dynamic() = default;
    dynamic(bool b) : m_v(b) {}
    dynamic(char32_t c) : m_v(c) {}
    dynamic(const std::string& s) : m_v(s) {}
    dynamic(std::string&& s) : m_v(std::move(s)) {}
    dynamic(const char* s) : m_v(std::string(s)) {}
    dynamic(std::nullptr_t) : m_v(std::monostate{}) {}

    template <typename T, typename = std::enable_if_t<is_int_v<T>>>
    dynamic(const T& v) : m_v(cast<i2048>(v)) {}

    template <typename T, typename = std::enable_if_t<is_float_v<T>>, typename = void>
    dynamic(const T& v) : m_v(cast<f2048>(v)) {}

    bool is_none()   const { return std::holds_alternative<std::monostate>(m_v); }
    bool is_bool()   const { return std::holds_alternative<bool>(m_v); }
    bool is_char()   const { return std::holds_alternative<char32_t>(m_v); }
    bool is_int()    const { return std::holds_alternative<i2048>(m_v); }
    bool is_float()  const { return std::holds_alternative<f2048>(m_v); }
    bool is_string() const { return std::holds_alternative<std::string>(m_v); }
    bool is_number() const { return is_int() || is_float(); }

    const value_type& raw() const { return m_v; }

    std::string type_name() const {
        if (is_none())   return "none";
        if (is_bool())   return "bool";
        if (is_char())   return "char";
        if (is_int())    return "int";
        if (is_float())  return "float";
        return "string";
    }

    std::string to_string() const {
        if (is_none())   return "none";
        if (is_bool())   return std::get<bool>(m_v) ? "true" : "false";
        if (is_char())   return utf8(std::get<char32_t>(m_v));
        if (is_int())    return std::get<i2048>(m_v).to_string();
        if (is_float())  { std::ostringstream os; os << std::get<f2048>(m_v); return os.str(); }
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
        } else if constexpr (is_int_v<T>) {
            if (is_int())  return cast<T>(std::get<i2048>(m_v));
            if (is_char()) return cast<T>(std::get<char32_t>(m_v));
            fail("cannot convert dynamic value of type '" + type_name() + "' to an integer");
        } else if constexpr (is_float_v<T>) {
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
        if (a.is_number() && b.is_number()) return compare(a, b) == 0;
        if (a.m_v.index() != b.m_v.index()) return false;
        if (a.is_none())   return true;
        if (a.is_bool())   return std::get<bool>(a.m_v) == std::get<bool>(b.m_v);
        if (a.is_char())   return std::get<char32_t>(a.m_v) == std::get<char32_t>(b.m_v);
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

inline i64 parse_int(const dynamic& d) { return parse_int(d.to_string()); }
inline f128 parse_float(const dynamic& d) { return parse_float(d.to_string()); }

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

    iterator begin() {
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

inline void write_all(std::ostream&) {}

template <typename A, typename... R>
inline void write_all(std::ostream& os, const A& a, const R&... rest) {
    os << to_display(a);
    if constexpr (sizeof...(rest) > 0) { os << ' '; write_all(os, rest...); }
}

template <typename... A>
inline void print(const A&... args) { write_all(std::cout, args...); std::cout.flush(); }

template <typename... A>
inline void println(const A&... args) { write_all(std::cout, args...); std::cout << '\n'; std::cout.flush(); }

inline std::string input() {
    std::string line;
    if (!std::getline(std::cin, line)) return std::string();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

inline i64 parse_int(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { negative = s[i] == '-'; ++i; }
    const std::size_t digits_start = i;
    i2048 value = i2048(0LL);
    const i2048 ten = i2048(10LL);
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { value = value * ten + i2048(static_cast<long long>(s[i] - '0')); ++i; }
    const std::size_t digits_end = i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (digits_start == digits_end || i != s.size()) fail("cannot parse '" + s + "' as an integer");
    if (negative) value = -value;
    if (value < i2048(std::numeric_limits<long long>::min()) || i2048(std::numeric_limits<long long>::max()) < value) fail("'" + s + "' does not fit in a long int");
    return cast<i64>(value);
}

inline f128 parse_float(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    const std::string t = s.substr(b, e - b);
    bool digit = false, ok = !t.empty();
    for (std::size_t k = 0; k < t.size() && ok; ++k) {
        const char ch = t[k];
        if (ch >= '0' && ch <= '9') digit = true;
        else if (!(ch == '.' || ch == 'e' || ch == 'E' || ((ch == '+' || ch == '-') && (k == 0 || t[k - 1] == 'e' || t[k - 1] == 'E')))) ok = false;
    }
    if (!ok || !digit) fail("cannot parse '" + s + "' as a number");
    f128 v = f128(t);
    if (v.is_undefined()) fail("cannot parse '" + s + "' as a number");
    return v;
}

inline i64 parse_int(const dynamic& d);
inline f128 parse_float(const dynamic& d);

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

template <typename F>
inline int run_program(F&& body) {
    try {
        return body();
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

#endif // WALNUT_RUNTIME_HPP
