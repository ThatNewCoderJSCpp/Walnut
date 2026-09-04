#ifndef WALNUT_SEMA_CONVERT_HPP
#define WALNUT_SEMA_CONVERT_HPP

#include <optional>
#include "type_impl.hpp"

namespace walnut {
namespace semantics {

enum class ConversionRank : std::uint8_t {
    Exact = 0,    // identical canonical type
    Promotion,    // same-family same-sign widening
    Conversion,   // int->float widening, pointer->bool, table Implicit
    UserDefined,  // operator T()  
    None          // not implicitly convertible 
};


#define WALNUT_BUILTIN_CAST_TABLE(X) \
    X(Char, Int,  Explicit) \
    X(Int,  Char, Forbidden)

enum class CastMode : std::uint8_t { Forbidden = 0, Explicit, Implicit };

struct BaseCastRule { parser_types::PrimitiveType::BaseKind from, to; CastMode mode; };

inline constexpr BaseCastRule kBaseCastRules[] = {
#define WALNUT_X(F,T,M) { parser_types::PrimitiveType::BaseKind::F, parser_types::PrimitiveType::BaseKind::T, CastMode::M },
    WALNUT_BUILTIN_CAST_TABLE(WALNUT_X)
#undef WALNUT_X
};

inline CastMode base_cast_mode(parser_types::PrimitiveType::BaseKind f, parser_types::PrimitiveType::BaseKind t) {
    for (const auto& r : kBaseCastRules) if (r.from == f && r.to == t) return r.mode;
    return CastMode::Forbidden;
}

struct NumericProfile {
    enum class Family : std::uint8_t { Int = 0, Float } family;
    bool is_unsigned;
    int  width;
};

inline std::optional<NumericProfile> numeric_profile(const BuiltinType* b) {
    using BK = parser_types::PrimitiveType::BaseKind;

    switch (b->base()) {
        case BK::Int:   return NumericProfile{ NumericProfile::Family::Int,   b->is_unsigned(), b->width() };
        case BK::Float: return NumericProfile{ NumericProfile::Family::Float, b->is_unsigned(), b->width() };
        default:        return std::nullopt;   
    }
}

inline ConversionRank rank_numeric(const NumericProfile& s, const NumericProfile& d) {
    if (s.is_unsigned != d.is_unsigned) return ConversionRank::None;   
    using F = NumericProfile::Family;
    if (s.family == F::Int   && d.family == F::Int)   return d.width >= s.width ? ConversionRank::Promotion  : ConversionRank::None;
    if (s.family == F::Float && d.family == F::Float) return d.width >= s.width ? ConversionRank::Promotion  : ConversionRank::None;
    if (s.family == F::Int   && d.family == F::Float) return d.width >= s.width ? ConversionRank::Conversion : ConversionRank::None;
    return ConversionRank::None;   
}

inline ConversionRank rank_builtin(Type* src, Type* dst) {
    if (src == dst) return ConversionRank::Exact;
    if (src->is_pointer() && dst->is_builtin() && static_cast<BuiltinType*>(dst)->is_bool()) return ConversionRank::Conversion;
    if (!src->is_builtin() || !dst->is_builtin()) return ConversionRank::None;
    auto* sb = static_cast<BuiltinType*>(src);
    auto* db = static_cast<BuiltinType*>(dst);

    if (auto sp = numeric_profile(sb); sp) {
        if (auto dp = numeric_profile(db); dp) {
            ConversionRank r = rank_numeric(*sp, *dp);
            if (r != ConversionRank::None) return r;
        }
    }
    
    if (base_cast_mode(sb->base(), db->base()) == CastMode::Implicit) return ConversionRank::Conversion;
    return ConversionRank::None;
}

inline ConversionRank rank_user_defined(
    Type* src, Type* dst, TypeContext& ctx,
    bool allow_explicit = false, Symbol** chosen = nullptr
) {
    if (chosen) *chosen = nullptr;
    if (!src || !dst) return ConversionRank::None;
    const auto* set = ctx.user_conversions().find(ctx.strip_cv(src));
    if (!set) return ConversionRank::None;
    ConversionRank best = ConversionRank::None;

    for (const UserConversion& uc : *set) {
        if (uc.explicit_ && !allow_explicit) continue;  
        ConversionRank here = ConversionRank::None;
        if (uc.dst == dst)                                          here = ConversionRank::UserDefined; 
        else if (rank_builtin(uc.dst, dst) != ConversionRank::None) here = ConversionRank::UserDefined;

        if (here != ConversionRank::None && best == ConversionRank::None) {
            best = here;
            if (chosen) *chosen = uc.op;
            if (uc.dst == dst) break;                    
        }
    }

    return best;
}

inline ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx) {
    if (!src || !dst) return ConversionRank::None;
    if (src->is_error() || dst->is_error()) return ConversionRank::Exact;  
    if (src == dst) return ConversionRank::Exact;
    if (src->is_null() && dst->is_pointer()) return ConversionRank::Conversion;  
    ConversionRank r = rank_builtin(src, dst);
    if (r != ConversionRank::None) return r;
    return rank_user_defined(src, dst, ctx);
}

inline bool is_static_castable(Type* src, Type* dst, TypeContext& ctx) {
    if (rank_conversion(src, dst, ctx) != ConversionRank::None) return true;          
    if (rank_user_defined(src, dst, ctx, true, nullptr) != ConversionRank::None) return true;                                    

    if (src->is_builtin() && dst->is_builtin()) {
        auto* sb = static_cast<BuiltinType*>(src);
        auto* db = static_cast<BuiltinType*>(dst);
        if (numeric_profile(sb) && numeric_profile(db)) return true;
        if (base_cast_mode(sb->base(), db->base()) == CastMode::Explicit) return true;
    }
    
    return false;
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_CONVERSION_HPP