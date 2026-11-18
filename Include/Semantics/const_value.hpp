#ifndef WALNUT_SEMANTICS_CONST_VALUE_HPP
#define WALNUT_SEMANTICS_CONST_VALUE_HPP

#include "../Common/foundation.hpp"
#include "../Common/arena_allocator.hpp"
#include <unordered_set>
#include <charconv>
#include <system_error>

namespace walnut {
namespace semantics {

class ConstTable {
public:
    explicit ConstTable(Arena& arena) : m_arena(arena) {}

    const WideInt* intern(const WideInt& v);

    const WideInt* zero() { return intern(WideInt()); }

    template <typename T>
    const WideInt* from(T v) { return intern(WideInt(v)); }

    const WideInt* parse(const std::string& text, int base = 10) {
        if (!text.empty() && base >= 2 && base <= 36) {
            unsigned long long small = 0;
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), small, base);
            if (ec == std::errc() && end == text.data() + text.size()) return intern(WideInt(small));
        }
        WideInt v(text, base);
        return v.is_undefined() ? nullptr : intern(v);
    }

    static bool fits(const WideInt& v, unsigned int bit_width, bool is_signed);
    static bool fits_twos(const WideInt& v, unsigned int bit_width, bool is_signed);
    static bool fits(const WideInt& v, unsigned int bit_width, bool is_signed, bool is_hardware);

private:
    struct Hash {
        std::size_t operator()(const WideInt& v) const;
        std::size_t operator()(const WideInt* v) const { return (*this)(*v); }
    };

    struct Eq {
        bool operator()(const WideInt& a, const WideInt& b) const { return a == b; }
        bool operator()(const WideInt* a, const WideInt* b) const { return *a == *b; }
    };

    Arena& m_arena;
    std::unordered_set<const WideInt*, Hash, Eq> m_interned;
};

class FloatTable {
public:
    explicit FloatTable(Arena& arena) : m_arena(arena) {}

    const WideFloat* intern(const WideFloat& v);

    const WideFloat* parse(const std::string& text) {
        if (auto it = m_parsed.find(text); it != m_parsed.end()) return it->second;
        bool ok = false;
        WideFloat v = wf_parse(text, ok);
        const WideFloat* out = ok ? intern(v) : nullptr;
        m_parsed.emplace(text, out);
        return out;
    }

    const WideFloat* from_int(const WideInt& v) { return intern(wf_from_int(v)); }

private:
    struct Hash { std::size_t operator()(const WideFloat& v) const { return wf_hash_bits(v); } };
    struct Eq   { bool operator()(const WideFloat& a, const WideFloat& b) const { return wf_same_bits(a, b); } };

    Arena& m_arena;
    std::unordered_map<WideFloat, const WideFloat*, Hash, Eq> m_interned;
    std::unordered_map<std::string, const WideFloat*> m_parsed;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_CONST_VALUE_HPP