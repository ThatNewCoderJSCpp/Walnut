#ifndef WALNUT_SEMANTICS_CONST_VALUE_HPP
#define WALNUT_SEMANTICS_CONST_VALUE_HPP

#include "../Common/foundation.hpp"
#include "../Common/arena_allocator.hpp"

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
        WideInt v(text, base);
        return v.is_undefined() ? nullptr : intern(v);
    }

    static bool fits(const WideInt& v, unsigned int bit_width, bool is_signed);

private:
    struct Hash {
        std::size_t operator()(const WideInt& v) const;
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

    const WideFloat* intern(const WideFloat& v);

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