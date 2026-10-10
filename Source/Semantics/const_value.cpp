#include "Semantics/const_value.hpp"

namespace walnut {
namespace semantics {

const WideInt* ConstTable::intern(const WideInt& v) {
    if (auto it = m_interned.find(v); it != m_interned.end()) return it->second;
    WideInt* stored = make_in<WideInt>(m_arena);
    *stored = v;
    m_interned.emplace(v, stored);
    return stored;
}

bool ConstTable::fits(const WideInt& v, unsigned int bit_width, bool is_signed) {
    using Mag = WideInt::mag_t;
    constexpr std::size_t max_bits = WideInt::bits;
    if (v.is_undefined()) return false;
    const Mag& m = v.magnitude();

    if (!is_signed) {
        if (v.is_negative()) return false;
        return bit_width >= max_bits || (m >> bit_width).is_zero();     
    }

    if (bit_width > max_bits) bit_width = max_bits;
    const Mag limit = Mag(std::uint64_t(1)) << (bit_width - 1);     
    return v.is_negative() ? (m <= limit) : (m < limit);                         
}

std::size_t ConstTable::Hash::operator()(const WideInt& v) const {
    const auto& m = v.magnitude();
    std::size_t h = v.is_negative() ? 0x9e3779b9u : 0;          
    for (std::size_t i = 0; i < WideInt::bits / 64; ++i) h = h * 1000003u ^ static_cast<std::size_t>((m >> (64 * i)).get_lowest_bits());
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
