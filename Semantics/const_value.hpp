#ifndef WALNUT_SEMANTICS_CONST_VALUE_HPP
#define WALNUT_SEMANTICS_CONST_VALUE_HPP

#include "../Common/foundation.hpp"
#include "../Common/arena_allocator.hpp"

namespace walnut {
namespace semantics {

class ConstTable {
public:
    explicit ConstTable(Arena& arena) : m_arena(arena) {}

    const WideInt* intern(const WideInt& v) {
        if (auto it = m_interned.find(v); it != m_interned.end()) return it->second;
        WideInt* stored = make_in<WideInt>(m_arena);
        *stored = v;
        m_interned.emplace(v, stored);
        return stored;
    }

    const WideInt* zero() { return intern(WideInt()); }

    template <typename T>
    const WideInt* from(T v) { return intern(WideInt(v)); }

    const WideInt* parse(const std::string& text, int base = 10) {
        WideInt v(text, base);
        return v.is_undefined() ? nullptr : intern(v);
    }

    static bool fits(const WideInt& v, unsigned int bit_width, bool is_signed) {
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

private:
    struct Hash {
        std::size_t operator()(const WideInt& v) const {
            const auto& m = v.magnitude();
            std::size_t h = v.is_negative() ? 0x9e3779b9u : 0;          
            for (std::size_t i = 0; i < WideInt::bits / 64; ++i) h = h * 1000003u ^ static_cast<std::size_t>((m >> (64 * i)).get_lowest_bits());
            return h;
        }
    };

    struct Eq {
        bool operator()(const WideInt& a, const WideInt& b) const { return a == b; }
    };

    Arena& m_arena;
    std::unordered_map<WideInt, const WideInt*, Hash, Eq> m_interned;
};

class FloatTable {
public:
    explicit FloatTable(Arena& arena) : m_arena(arena) {}

    const WideFloat* intern(const WideFloat& v) {
        if (auto it = m_interned.find(v); it != m_interned.end()) return it->second;
        WideFloat* stored = make_in<WideFloat>(m_arena);
        *stored = v;
        m_interned.emplace(v, stored);
        return stored;
    }

    const WideFloat* parse(const std::string& text) {
        bool ok = false;
        WideFloat v = wf_parse(text, ok);
        return ok ? intern(v) : nullptr;
    }

    const WideFloat* from_int(const WideInt& v) { return intern(wf_from_int(v)); }

private:
    struct Hash { std::size_t operator()(const WideFloat& v) const { return wf_hash_bits(v); } };
    struct Eq   { bool operator()(const WideFloat& a, const WideFloat& b) const { return wf_same_bits(a, b); } };

    Arena& m_arena;
    std::unordered_map<WideFloat, const WideFloat*, Hash, Eq> m_interned;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_CONST_VALUE_HPP