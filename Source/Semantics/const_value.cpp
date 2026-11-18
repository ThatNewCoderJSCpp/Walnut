#include "Semantics/const_value.hpp"

namespace walnut {
namespace semantics {

const WideInt* ConstTable::intern(const WideInt& v) {
    if (auto it = m_interned.find(&v); it != m_interned.end()) return *it;
    WideInt* stored = make_in<WideInt>(m_arena);
    *stored = v;
    m_interned.insert(stored);
    return stored;
}

bool ConstTable::fits(const WideInt& v, unsigned int bit_width, bool is_signed) {
    constexpr std::size_t max_bits = WideInt::bits;
    if (v.is_undefined()) return false;
    if (!is_signed && v.is_negative()) return false;
    return bit_width >= max_bits || v.magnitude().highest_bit() < static_cast<long long>(bit_width);
}

bool ConstTable::fits(const WideInt& v, unsigned int bit_width, bool is_signed, bool is_hardware) {
    return is_hardware ? fits_twos(v, bit_width, is_signed) : fits(v, bit_width, is_signed);
}

bool ConstTable::fits_twos(const WideInt& v, unsigned int bit_width, bool is_signed) {
    using Mag = WideInt::mag_t;
    constexpr std::size_t max_bits = WideInt::bits;
    if (v.is_undefined()) return false;
    const Mag& m = v.magnitude();

    if (!is_signed) {
        if (v.is_negative()) return false;
        return bit_width >= max_bits || m.highest_bit() < static_cast<long long>(bit_width);
    }

    if (bit_width > max_bits) bit_width = max_bits;
    const long long top = m.highest_bit();
    const long long last = static_cast<long long>(bit_width) - 1;
    if (top < last) return true;
    if (top > last) return false;
    if (!v.is_negative()) return false;
    const Mag limit = Mag(std::uint64_t(1)) << (bit_width - 1);
    return m == limit;
}

std::size_t ConstTable::Hash::operator()(const WideInt& v) const {
    std::uint64_t limbs[fizmo::multiprecision::umag_limbs<WideInt::bits>::count];
    fizmo::multiprecision::umag_limbs<WideInt::bits>::store(v.magnitude(), limbs);
    std::size_t h = v.is_negative() ? 0x9e3779b9u : 0;
    for (std::uint64_t limb : limbs) h = h * 1000003u ^ static_cast<std::size_t>(limb ^ (limb >> 29));
    return h;
}

const WideFloat* FloatTable::intern(const WideFloat& v) {
    if (auto it = m_interned.find(v); it != m_interned.end()) return it->second;
    WideFloat* stored = make_in<WideFloat>(m_arena);
    *stored = v;
    m_interned.emplace(v, stored);
    return stored;
}

} // namespace semantics
} // namespace walnut
