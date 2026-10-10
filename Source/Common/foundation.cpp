#include "Common/foundation.hpp"

namespace walnut {

bool wf_from_int_exact(const WideInt& v, WideFloat& out) {
    out = wf_from_int(v);
    return v.highest_bit() < static_cast<long long>(WideFloat::mantissa_bits);
}

bool wf_trunc_to_int(const WideFloat& f, WideInt& out) {
    if (!f.is_finite()) return false;
    if (f.is_zero()) { out = WideInt(); return true; }
    using S = WideFloat::sstore_t;
    if (!(f.exponent_base2() < S(static_cast<long long>(WideInt::bits) - 1))) return false;
    WideInt wide(f.get_integer_part_as_int());
    out = f.is_negative() ? -wide : wide;
    return true;
}

bool wf_round_to_width(const WideFloat& v, unsigned bits, WideFloat& out) {
    switch (bits) {
        #define WALNUT_X(B) case B: out = WideFloat(mp::StandardFloat<B>(v)); return true;
        WALNUT_FLOAT_WIDTHS(WALNUT_X)
        #undef WALNUT_X
        default: return false;
    }
}

unsigned float_digits_of_bits(unsigned bits) {
    switch (bits) {
        #define WALNUT_X(B) case B: return unsigned(fizmo::fizmo_float_traits<mp::StandardFloat<B>>::digits);
        WALNUT_FLOAT_WIDTHS(WALNUT_X)
        #undef WALNUT_X
        default: return 0;
    }
}

std::size_t wf_hash_bits(const WideFloat& f) {
    const WideFloat::store_t& bits = f.get_bits();
    std::size_t h = f.is_negative() ? 0x9e3779b9u : 0;
    for (std::size_t i = 0; i < WideFloat::math_bits / 64; ++i) h = h * 1000003u ^ static_cast<std::size_t>((bits >> (64 * i)).get_lowest_bits());
    return h;
}

} // namespace walnut
