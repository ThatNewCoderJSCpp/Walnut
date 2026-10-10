#ifndef WALNUT_FOUNDATION_HPP
#define WALNUT_FOUNDATION_HPP

#define FIZMO
#include "fizmo/includes.hpp"

namespace walnut {

namespace mp = fizmo::multiprecision;

using WideInt   = mp::int2048;
using WideUInt  = mp::uint2048;
using WideFloat = mp::float2048;

inline constexpr unsigned int kMaxDeclaredIntBits   = 2048;
inline constexpr unsigned int kMaxDeclaredFloatBits = 2048;

inline constexpr unsigned int bit_width_of_rank(int rank) {
    return (rank < -2 || rank > 6) ? 0u : (8u << (rank + 2));
}

inline WideFloat wf_parse(const std::string& s, bool& ok) {
    WideFloat f(s);                       
    ok = !f.is_undefined();
    return f;
}

inline WideFloat wf_from_int(const WideInt& v) {
    return WideFloat(WideFloat::sstore_t(v));
}

bool wf_from_int_exact(const WideInt& v, WideFloat& out);

bool wf_trunc_to_int(const WideFloat& f, WideInt& out);

#define WALNUT_FLOAT_WIDTHS(X) X(32) X(64) X(128) X(256) X(512) X(1024) X(2048)

bool wf_round_to_width(const WideFloat& v, unsigned bits, WideFloat& out);

unsigned float_digits_of_bits(unsigned bits);

inline bool wf_same_bits(const WideFloat& a, const WideFloat& b) {
    return a.is_negative() == b.is_negative() && a.get_bits() == b.get_bits();
}

std::size_t wf_hash_bits(const WideFloat& f);

} // namespace walnut

#endif // WALNUT_FOUNDATION_HPP