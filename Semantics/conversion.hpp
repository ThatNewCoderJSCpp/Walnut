#ifndef WALNUT_SEMA_CONVERT_HPP
#define WALNUT_SEMA_CONVERT_HPP

#include <optional>
#include "type_impl.hpp"
#include "scope.hpp"

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
    using F = NumericProfile::Family;

    if (s.family == F::Int && d.family == F::Int) {
        if (s.is_unsigned != d.is_unsigned) return ConversionRank::None;
        return d.width >= s.width ? ConversionRank::Promotion : ConversionRank::None;
    }

    if (s.family == F::Float && d.family == F::Float) return d.width >= s.width ? ConversionRank::Promotion : ConversionRank::None;

    if (s.family == F::Int && d.family == F::Float) {
        const unsigned src_bits = bit_width_of_rank(s.width);
        const unsigned need     = s.is_unsigned ? src_bits : src_bits - 1;
        return float_digits_of_bits(bit_width_of_rank(d.width)) >= need ? ConversionRank::Conversion : ConversionRank::None;
    }

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

inline bool cv_at_least(const CV& have, const CV& want) {
    return (!have.is_const || want.is_const) && (!have.is_volatile || want.is_volatile) && (!have.is_immutable || want.is_immutable);
}

inline ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx);

inline Scope* record_scope_of(Type* t, TypeContext& ctx) {
    t = ctx.strip_cv(t);
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);
    if (rt->is_instantiation() && ctx.record_scope_hook) return ctx.record_scope_hook(rt);
    return rt->decl() ? rt->decl()->inner_scope : nullptr;
}

inline bool scope_derives_from(const Scope* s, const Scope* base, int depth = 0) {
    if (!s || !base || depth > 64) return false;
    for (Scope* b : s->bases) if (b == base || scope_derives_from(b, base, depth + 1)) return true;
    return false;
}

inline bool is_derived_record(Type* derived, Type* base, TypeContext& ctx) {
    Scope* d = record_scope_of(derived, ctx);
    Scope* b = record_scope_of(base, ctx);
    return d && b && d != b && scope_derives_from(d, b);
}

inline bool unsized_array_accepts(Type* src, Type* dst, TypeContext& ctx) {
    src = ctx.strip_cv(src);
    dst = ctx.strip_cv(dst);
    if (!src || !dst || !src->is_array() || !dst->is_array()) return false;
    auto* sa = static_cast<ArrayType*>(src);
    auto* da = static_cast<ArrayType*>(dst);
    if (da->extent()) return false;
    return ctx.strip_cv(sa->element()) == ctx.strip_cv(da->element());
}

inline ConversionRank rank_reference_binding(Type* src, ReferenceType* dst, TypeContext& ctx) {
    Type* referent = dst->referent();
    if (referent->is_error()) return ConversionRank::Exact;
    const bool same = ctx.strip_cv(src) == ctx.strip_cv(referent);

    if (!same && is_derived_record(src, referent, ctx)) return cv_at_least(src->cv(), referent->cv()) ? ConversionRank::Conversion : ConversionRank::None;

    if (!same && unsized_array_accepts(src, referent, ctx)) return ConversionRank::Conversion;

    if (dst->ref_qual() == RefQual::LValue && !referent->cv().is_const) {
        return (same && cv_at_least(src->cv(), referent->cv())) ? ConversionRank::Exact : ConversionRank::None;
    }

    if (same) return cv_at_least(src->cv(), referent->cv()) ? ConversionRank::Exact : ConversionRank::None;
    return rank_conversion(ctx.strip_cv(src), ctx.strip_cv(referent), ctx);
}

inline ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx) {
    if (!src || !dst) return ConversionRank::None;
    if (src->is_error() || dst->is_error()) return ConversionRank::Exact;
    if (src == dst) return ConversionRank::Exact;
    if (src->is_reference()) return rank_conversion(static_cast<ReferenceType*>(src)->referent(), dst, ctx);
    if (dst->is_reference()) return rank_reference_binding(src, static_cast<ReferenceType*>(dst), ctx);
    if (ctx.strip_cv(src) == ctx.strip_cv(dst)) return ConversionRank::Exact;
    if (src->is_null() && dst->is_pointer()) return ConversionRank::Conversion;  

    if (dst->is_builtin() && static_cast<BuiltinType*>(dst)->is_dynamic()) {
        Type* sv = ctx.strip_cv(src);
        if (sv->is_builtin() && !static_cast<BuiltinType*>(sv)->is_void()) return ConversionRank::Conversion;
    }

    if (src->is_builtin() && static_cast<BuiltinType*>(src)->is_dynamic()) {
        Type* dv = ctx.strip_cv(dst);
        if (dv->is_builtin() && !static_cast<BuiltinType*>(dv)->is_void()) return ConversionRank::UserDefined;
    }

    if (src->is_pointer() && dst->is_pointer()) {
        Type* sp = static_cast<PointerType*>(src)->pointee();
        Type* dp = static_cast<PointerType*>(dst)->pointee();
        if (ctx.strip_cv(sp) == ctx.strip_cv(dp)) return cv_at_least(sp->cv(), dp->cv()) ? ConversionRank::Conversion : ConversionRank::None;
        if (is_derived_record(sp, dp, ctx) && cv_at_least(sp->cv(), dp->cv())) return ConversionRank::Conversion;
    }

    if (is_derived_record(src, dst, ctx)) return ConversionRank::Conversion;
    if (unsized_array_accepts(src, dst, ctx)) return ConversionRank::Conversion;

    if (src->is_function() && dst->is_function()) {
        auto* sf = static_cast<FunctionType*>(src);
        auto* df = static_cast<FunctionType*>(dst);
        if (sf->ret() == df->ret() && sf->params() == df->params()) return ConversionRank::Conversion;
    }

    if (dst->is_function() && ctx.strip_cv(src)->is_record() && ctx.callable_hook && ctx.callable_hook(ctx.strip_cv(src), dst)) return ConversionRank::UserDefined;
    if (dst->is_function() && src->kind() == TypeKind::Closure && ctx.closure_hook && ctx.closure_hook(src, dst)) return ConversionRank::UserDefined;
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

    using BK = parser_types::PrimitiveType::BaseKind;
    Type* s = ctx.strip_cv(src);
    Type* d = ctx.strip_cv(dst);
    if (s->is_enum() && d->is_builtin() && static_cast<BuiltinType*>(d)->base() == BK::Int) return true;
    if (d->is_enum() && s->is_builtin() && static_cast<BuiltinType*>(s)->base() == BK::Int) return true;
    if (s->is_enum() && d->is_enum()) return true;

    if (s->is_pointer() && d->is_pointer()) {
        Type* sp = static_cast<PointerType*>(s)->pointee();
        Type* dp = static_cast<PointerType*>(d)->pointee();
        if (is_derived_record(dp, sp, ctx) && cv_at_least(sp->cv(), dp->cv())) return true;
    }

    return false;
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_CONVERSION_HPP