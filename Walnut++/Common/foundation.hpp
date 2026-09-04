#ifndef WALNUT_FOUNDATION_HPP
#define WALNUT_FOUNDATION_HPP

#define FIZMO
#include "fizmo/includes.hpp"

namespace walnut {

namespace mp = fizmo::multiprecision;

using WideInt   = mp::int256;
using WideUInt   = mp::uint256;
using WideFloat = mp::float1024;

inline constexpr unsigned int bit_width_of_rank(int rank) { return 8u << (rank + 2); }

inline WideFloat wf_parse(const std::string& s, bool& ok) {
    WideFloat f(s);                       
    ok = !f.is_undefined();
    return f;
}

inline WideFloat wf_from_int(const WideInt& v) {
    return WideFloat(WideFloat::sstore_t(v));
}

inline bool wf_trunc_to_int(const WideFloat& f, WideInt& out) {
    if (!f.is_finite()) return false;
    WideFloat::store_t mag = f.get_integer_part_as_int();      
    if (!(mag >> WideInt::bits).is_zero()) return false;        
    mp::int1024 wide(mag);                                      
    if (f.is_negative()) wide = -wide;
    out = wide.truncated<WideInt::bits>();                     
    return true;
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