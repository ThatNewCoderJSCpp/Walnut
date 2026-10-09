#ifndef WALNUT_FOUNDATION_HPP
#define WALNUT_FOUNDATION_HPP

#define FIZMO
#include "fizmo/includes.hpp"

namespace walnut {

namespace mp = fizmo::multiprecision;

using WideInt   = mp::int2048;
using WideUInt  = mp::uint2048;
using WideFloat = mp::float2048;

inline constexpr unsigned int kMaxDeclaredIntBits   = 1024;
inline constexpr unsigned int kMaxDeclaredFloatBits = 1024;

inline constexpr unsigned int bit_width_of_rank(int rank) {
    return (rank < -2 || rank > 5) ? 0u : (8u << (rank + 2));
}

inline WideFloat wf_parse(const std::string& s, bool& ok) {
    WideFloat f(s);                       
    ok = !f.is_undefined();
    return f;
}

inline WideFloat wf_from_int(const WideInt& v) {
    return WideFloat(WideFloat::sstore_t(v));
}

inline bool wf_from_int_exact(const WideInt& v, WideFloat& out) {
    out = wf_from_int(v);
    return v.highest_bit() < static_cast<long long>(WideFloat::mantissa_bits);
}

inline bool wf_trunc_to_int(const WideFloat& f, WideInt& out) {
    if (!f.is_finite()) return false;
    if (f.is_zero()) { out = WideInt(); return true; }
    using S = WideFloat::sstore_t;
    if (!(f.exponent_base2() < S(static_cast<long long>(WideInt::bits) - 1))) return false;
    WideInt wide(f.get_integer_part_as_int());
    out = f.is_negative() ? -wide : wide;
    return true;
}

#define WALNUT_FLOAT_WIDTHS(X) X(32) X(64) X(128) X(256) X(512) X(1024)

inline bool wf_round_to_width(const WideFloat& v, unsigned bits, WideFloat& out) {
    switch (bits) {
        #define WALNUT_X(B) case B: out = WideFloat(mp::StandardFloat<B>(v)); return true;
        WALNUT_FLOAT_WIDTHS(WALNUT_X)
        #undef WALNUT_X
        default: return false;
    }
}

inline unsigned float_digits_of_bits(unsigned bits) {
    switch (bits) {
        #define WALNUT_X(B) case B: return unsigned(fizmo::fizmo_float_traits<mp::StandardFloat<B>>::digits);
        WALNUT_FLOAT_WIDTHS(WALNUT_X)
        #undef WALNUT_X
        default: return 0;
    }
}

inline bool wf_same_bits(const WideFloat& a, const WideFloat& b) {
    return a.is_negative() == b.is_negative() && a.get_bits() == b.get_bits();
}

inline std::size_t wf_hash_bits(const WideFloat& f) {
    const WideFloat::store_t& bits = f.get_bits();
    std::size_t h = f.is_negative() ? 0x9e3779b9u : 0;
    for (std::size_t i = 0; i < WideFloat::math_bits / 64; ++i) h = h * 1000003u ^ static_cast<std::size_t>((bits >> (64 * i)).get_lowest_bits());
    return h;
}

} // namespace walnut

#endif // WALNUT_FOUNDATION_HPP