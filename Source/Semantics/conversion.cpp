#include "Semantics/conversion.hpp"
#include "Parser/nodes.hpp"

namespace walnut {
namespace semantics {

CastMode base_cast_mode(parser_types::PrimitiveType::BaseKind f, parser_types::PrimitiveType::BaseKind t) {
    for (const auto& r : kBaseCastRules) if (r.from == f && r.to == t) return r.mode;
    return CastMode::Forbidden;
}

std::optional<NumericProfile> numeric_profile(const BuiltinType* b) {
    using BK = parser_types::PrimitiveType::BaseKind;

    switch (b->base()) {
        case BK::Int:   return NumericProfile{ NumericProfile::Family::Int,   b->is_unsigned(), b->width(), b->is_hardware() };
        case BK::Float: return NumericProfile{ NumericProfile::Family::Float, b->is_unsigned(), b->width(), b->is_hardware() };
        default:        return std::nullopt;   
    }
}

ConversionRank rank_numeric_within(const NumericProfile& s, const NumericProfile& d);

ConversionRank rank_numeric(const NumericProfile& s, const NumericProfile& d) {
    const ConversionRank r = rank_numeric_within(s, d);
    if (r == ConversionRank::Promotion && s.hardware != d.hardware) return ConversionRank::Conversion;
    return r;
}

ConversionRank rank_numeric_within(const NumericProfile& s, const NumericProfile& d) {
    using F = NumericProfile::Family;

    if (s.family == F::Int && d.family == F::Int) {
        if (s.is_unsigned != d.is_unsigned) return ConversionRank::None;
        if (d.hardware && !s.hardware) return d.width > s.width ? ConversionRank::Promotion : ConversionRank::None;
        return d.width >= s.width ? ConversionRank::Promotion : ConversionRank::None;
    }

    if (s.family == F::Float && d.family == F::Float) {
        if (!s.is_unsigned && d.is_unsigned) return ConversionRank::None;
        if (d.hardware && !s.hardware) return d.width > s.width ? ConversionRank::Promotion : ConversionRank::None;
        return d.width >= s.width ? ConversionRank::Promotion : ConversionRank::None;
    }

    if (s.family == F::Int && d.family == F::Float) {
        if (!s.is_unsigned && d.is_unsigned) return ConversionRank::None;
        const unsigned src_bits = bit_width_of_rank(s.width);
        const unsigned need     = s.is_unsigned ? src_bits : src_bits - 1;
        const unsigned digits = d.hardware ? (bit_width_of_rank(d.width) == 32 ? 24u : 53u) : float_digits_of_bits(bit_width_of_rank(d.width));
        return digits >= need ? ConversionRank::Conversion : ConversionRank::None;
    }

    return ConversionRank::None;
}

ConversionRank rank_builtin(Type* src, Type* dst) {
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

bool rvalue_only_conversion(const Symbol* op) {
    if (!op || !op->decl) return false;
    using FQ = modifiers::FunctionQualifiers;
    const nodes::ASTNode* d = op->decl;
    if (d->kind == nodes::ASTNode::Kind::TemplateDeclaration) d = static_cast<const nodes::TemplateDeclaration*>(d)->m_declaration;
    if (!d) return false;
    if (d->kind == nodes::ASTNode::Kind::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(d)->qualifiers().has(FQ::RValueRef);
    if (d->kind == nodes::ASTNode::Kind::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(d)->qualifiers().has(FQ::RValueRef);
    return false;
}

ConversionRank rank_user_defined(
    Type* src, Type* dst, TypeContext& ctx,
    bool allow_explicit, Symbol** chosen 
) {
    if (chosen) *chosen = nullptr;
    if (!src || !dst) return ConversionRank::None;
    const auto* set = ctx.user_conversions().find(ctx.strip_cv(src));
    if (!set) return ConversionRank::None;
    ConversionRank best = ConversionRank::None;

    for (const UserConversion& uc : *set) {
        if (uc.explicit_ && !allow_explicit) continue;  
        if (uc.generic) continue;
        if (ctx.conversion_source_lvalue && rvalue_only_conversion(uc.op)) continue;
        ConversionRank here = ConversionRank::None;
        if (uc.dst == dst)                                          here = ConversionRank::UserDefined; 
        else if (rank_builtin(uc.dst, dst) != ConversionRank::None) here = ConversionRank::UserDefined;

        if (here != ConversionRank::None && (best == ConversionRank::None || uc.dst == dst)) {
            best = here;
            if (chosen) *chosen = uc.op;
            if (uc.dst == dst) break;                    
        }
    }

    const bool exact = chosen && *chosen && best != ConversionRank::None;

    if (ctx.template_conversion_hook && !(exact && best == ConversionRank::UserDefined && std::any_of(set->begin(), set->end(), [&](const UserConversion& u) { return !u.generic && u.dst == dst; }))) {
        Type* want = dst;
        if (want->is_reference()) want = static_cast<ReferenceType*>(want)->referent();
        want = ctx.strip_cv(want);

        UserConversion made;

        for (const UserConversion& uc : *set) {
            if (!uc.generic || (uc.explicit_ && !allow_explicit)) continue;
            if (ctx.conversion_source_lvalue && rvalue_only_conversion(uc.generic)) continue;
            if (Symbol* inst = ctx.template_conversion_hook(uc.generic, want)) {
                best = ConversionRank::UserDefined;
                if (chosen) *chosen = inst;
                made.dst = want;
                made.op = inst;
                made.explicit_ = uc.explicit_;
                break;
            }
        }

        if (made.op) ctx.user_conversions().add(ctx.strip_cv(src), made);
    }

    return best;
}

bool cv_at_least(const CV& have, const CV& want) {
    return (!have.is_const || want.is_const) && (!have.is_volatile || want.is_volatile);
}

Scope* record_scope_of(Type* t, TypeContext& ctx) {
    t = ctx.strip_cv(t);
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);
    if (rt->is_instantiation() && ctx.record_scope_hook) return ctx.record_scope_hook(rt);
    return rt->decl() ? rt->decl()->inner_scope : nullptr;
}

bool scope_derives_from(const Scope* s, const Scope* base, int depth) {
    if (!s || !base || depth > 64) return false;
    for (Scope* b : s->bases) if (b == base || scope_derives_from(b, base, depth + 1)) return true;
    return false;
}

bool is_derived_record(Type* derived, Type* base, TypeContext& ctx) {
    Scope* d = record_scope_of(derived, ctx);
    Scope* b = record_scope_of(base, ctx);
    return d && b && d != b && scope_derives_from(d, b);
}

bool dynamic_value_type(Type* t, TypeContext& ctx, bool allow_void) {
    if (!t) return false;
    if (t->is_reference()) {
        auto* r = static_cast<ReferenceType*>(t);
        if (r->ref_qual() == RefQual::LValue && !r->referent()->cv().is_const) return false;
        t = r->referent();
    }
    t = ctx.strip_cv(t);
    if (!t || !t->is_builtin()) return false;
    auto* b = static_cast<BuiltinType*>(t);
    if (b->is_void()) return allow_void;
    return b->base() != parser_types::PrimitiveType::BaseKind::Auto;
}

int variant_alternative_for(Type* src, VariantType* v, TypeContext& ctx) {
    if (!src || !v) return -1;
    Type* s = ctx.strip_cv(src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src);
    const std::vector<Type*>& alts = v->alternatives();
    for (std::size_t i = 0; i < alts.size(); ++i) if (ctx.strip_cv(alts[i]) == s) return static_cast<int>(i);
    int found = -1;

    for (std::size_t i = 0; i < alts.size(); ++i) {
        if (rank_conversion(s, alts[i], ctx) == ConversionRank::None) continue;
        if (found >= 0) return -2;
        found = static_cast<int>(i);
    }

    return found;
}

static bool passes_to(Type* given, Type* takes, TypeContext& ctx) {
    if (given == takes) return true;
    if (!given || !takes) return false;
    Type* gv = given->is_reference() ? static_cast<ReferenceType*>(given)->referent() : given;
    Type* tv = takes->is_reference() ? static_cast<ReferenceType*>(takes)->referent() : takes;
    if (!gv || !tv || ctx.strip_cv(gv) != ctx.strip_cv(tv)) return false;
    if (!takes->is_reference()) return true;
    auto* tr = static_cast<ReferenceType*>(takes);

    if (tr->ref_qual() == RefQual::LValue) {
        if (tv->cv().is_const) return true;
        return given->is_reference() && static_cast<ReferenceType*>(given)->ref_qual() == RefQual::LValue && !gv->cv().is_const;
    }

    return !given->is_reference() || static_cast<ReferenceType*>(given)->ref_qual() == RefQual::RValue;
}

bool callable_as(FunctionType* sf, FunctionType* df, TypeContext& ctx) {
    if (!sf || !df || sf->params().size() != df->params().size()) return false;
    for (std::size_t i = 0; i < sf->params().size(); ++i) if (!passes_to(df->params()[i], sf->params()[i], ctx)) return false;
    Type* sr = sf->ret();
    Type* dr = df->ret();
    if (sr == dr) return true;
    if (!sr || !dr) return false;
    if (dr->is_builtin() && static_cast<BuiltinType*>(dr)->is_void()) return true;
    if (dr->is_reference()) return sr->is_reference() && passes_to(sr, dr, ctx) && static_cast<ReferenceType*>(sr)->ref_qual() == static_cast<ReferenceType*>(dr)->ref_qual();
    Type* sv = ctx.strip_cv(sr->is_reference() ? static_cast<ReferenceType*>(sr)->referent() : sr);
    Type* dv = ctx.strip_cv(dr);
    if (sv == dv) return true;
    const ConversionRank r = rank_conversion(sv, dv, ctx);
    return r == ConversionRank::Exact || r == ConversionRank::Promotion;
}

bool dynamic_callable(FunctionType* f, TypeContext& ctx) {
    if (!f || !dynamic_value_type(f->ret(), ctx, true)) return false;
    for (Type* p : f->params()) if (!dynamic_value_type(p, ctx, false)) return false;
    return true;
}

bool unsized_array_accepts(Type* src, Type* dst, TypeContext& ctx) {
    src = ctx.strip_cv(src);
    dst = ctx.strip_cv(dst);
    if (!src || !dst || !src->is_array() || !dst->is_array()) return false;
    auto* sa = static_cast<ArrayType*>(src);
    auto* da = static_cast<ArrayType*>(dst);
    if (da->extent()) return false;
    return ctx.strip_cv(sa->element()) == ctx.strip_cv(da->element());
}

ConversionRank rank_reference_binding(Type* src, ReferenceType* dst, TypeContext& ctx) {
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

ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx) {
    if (!src || !dst) return ConversionRank::None;
    if (src->is_error() || dst->is_error()) return ConversionRank::Exact;
    if (src == dst) return ConversionRank::Exact;
    if (src->is_reference()) return rank_conversion(static_cast<ReferenceType*>(src)->referent(), dst, ctx);
    if (dst->is_reference()) return rank_reference_binding(src, static_cast<ReferenceType*>(dst), ctx);
    if (ctx.strip_cv(src) == ctx.strip_cv(dst)) return ConversionRank::Exact;
    if (src->is_null() && dst->is_pointer()) return ConversionRank::Conversion;  

    if (ctx.strip_cv(src)->is_special_value() && ctx.strip_cv(dst)->is_builtin()) {
        auto* db = static_cast<BuiltinType*>(ctx.strip_cv(dst));
        if (db->is_hardware() && db->base() == parser_types::PrimitiveType::BaseKind::Int) return ConversionRank::None;
        if (auto p = numeric_profile(db); p) return ConversionRank::Conversion;
    }

    if (dst->is_builtin() && static_cast<BuiltinType*>(dst)->is_dynamic()) {
        Type* sv = ctx.strip_cv(src);
        if (sv->is_builtin() && !static_cast<BuiltinType*>(sv)->is_void()) return ConversionRank::Conversion;
        if (sv->is_null() || sv->is_special_value()) return ConversionRank::Conversion;
        if (sv->is_function() && dynamic_callable(static_cast<FunctionType*>(sv), ctx)) return ConversionRank::Conversion;
    }

    if (Type* dvt = ctx.strip_cv(dst); dvt && dvt->kind() == TypeKind::Variant && ctx.strip_cv(src)->kind() != TypeKind::Variant) {
        if (variant_alternative_for(src, static_cast<VariantType*>(dvt), ctx) >= 0) return ConversionRank::Conversion;
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
        if (callable_as(sf, df, ctx)) return ConversionRank::Conversion;
    }

    if (dst->is_function() && ctx.strip_cv(src)->is_record() && ctx.callable_hook && ctx.callable_hook(ctx.strip_cv(src), dst)) return ConversionRank::UserDefined;
    if (dst->is_function() && src->kind() == TypeKind::Closure && ctx.closure_hook && ctx.closure_hook(src, dst)) return ConversionRank::UserDefined;
    ConversionRank r = rank_builtin(src, dst);
    if (r != ConversionRank::None) return r;
    return rank_user_defined(src, dst, ctx);
}

bool is_static_castable(Type* src, Type* dst, TypeContext& ctx) {
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
