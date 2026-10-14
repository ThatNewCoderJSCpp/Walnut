#include <algorithm>

#include "Semantics/type_ops.hpp"
#include "Semantics/conversion.hpp"
#include "Parser/nodes.hpp"

namespace walnut {
namespace semantics {

namespace {

using BK = parser_types::PrimitiveType::BaseKind;
using Op = parser_types::TypeOp;
using Trait = parser_types::TypeTrait;

Type* unref(Type* t) {
    return t && t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t;
}

BuiltinType* as_builtin(Type* t) {
    return t && t->is_builtin() ? static_cast<BuiltinType*>(t) : nullptr;
}

bool is_base(Type* t, BK base) {
    BuiltinType* b = as_builtin(t);
    return b && b->base() == base;
}

Type* edit_cv(TypeContext& ctx, Type* t, int set_const, int set_volatile) {
    if (!t) return t;

    if (t->is_reference()) {
        auto* r = static_cast<ReferenceType*>(t);
        return ctx.reference(edit_cv(ctx, r->referent(), set_const, set_volatile), r->ref_qual(), r->cv());
    }

    CV c = t->cv();
    if (set_const >= 0) c.is_const = set_const == 1;
    if (set_volatile >= 0) c.is_volatile = set_volatile == 1;
    return ctx.with_cv(t, c);
}

Type* decayed(TypeContext& ctx, Type* t) {
    return ctx.strip_cv(unref(t));
}

std::string text(Type* t) {
    return t ? type_str_of(t) : std::string("<unknown>");
}

Scope* record_scope(TypeContext& ctx, Type* t) {
    if (!t || !t->is_record()) return nullptr;
    auto* r = static_cast<RecordType*>(t);
    if (ctx.record_scope_hook) if (Scope* s = ctx.record_scope_hook(r)) return s;
    return r->decl() ? r->decl()->inner_scope : nullptr;
}

bool derives_from(Scope* derived, Scope* base, int depth) {
    if (!derived || !base || depth > 64) return false;
    if (derived == base) return true;
    for (Scope* b : derived->bases) if (derives_from(b, base, depth + 1)) return true;
    return false;
}

constexpr int kMinIntWidth   = -2;
constexpr int kMaxIntWidth   = 6;
constexpr int kMinFloatWidth = 0;
constexpr int kMaxFloatWidth = 6;
constexpr int kFloatRankBase = 2;

bool is_numeric(BuiltinType* b) {
    return b && (b->base() == BK::Int || b->base() == BK::Float);
}

nodes::EnumDeclaration* enum_decl_of(Type* v) {
    if (!v || !v->is_enum()) return nullptr;
    Symbol* e = static_cast<EnumType*>(v)->decl();
    if (!e || !e->decl || e->decl->kind != nodes::ASTNode::Kind::EnumDeclaration) return nullptr;
    return static_cast<nodes::EnumDeclaration*>(e->decl);
}

TypeQueryOverloadResult overload_of(TypeContext& ctx, Type* t, std::string_view name, const std::vector<Type*>& others) {
    Type* v = ctx.strip_cv(unref(t));
    if (!v || !v->is_record() || !ctx.type_query_overload_hook) return {};
    if (parser_types::type_query_overload_policy(name) == parser_types::OverloadPolicy::Never) return {};
    return ctx.type_query_overload_hook(static_cast<RecordType*>(v), name, others);
}

void load_conversions(TypeContext& ctx, Type* t) {
    Type* v = ctx.strip_cv(unref(t));
    if (v && v->is_record() && ctx.conversions_hook) ctx.conversions_hook(v);
}

Type* common_of(TypeContext& ctx, Type* a, Type* b, std::string& error) {
    a = decayed(ctx, a);
    b = decayed(ctx, b);
    if (a == b) return a;

    TypeQueryOverloadResult custom = overload_of(ctx, a, "common_type", { b });
    if (!custom.found) custom = overload_of(ctx, b, "common_type", { a });

    if (custom.found) {
        if (!custom.error.empty()) { error = custom.error; return nullptr; }
        return decayed(ctx, custom.type);
    }

    load_conversions(ctx, a);
    load_conversions(ctx, b);
    if (is_base(a, BK::Dynamic) || is_base(b, BK::Dynamic)) return ctx.dynamic_();
    if (rank_conversion(b, a, ctx) != ConversionRank::None) return a;
    if (rank_conversion(a, b, ctx) != ConversionRank::None) return b;
    error = "'" + text(a) + "' and '" + text(b) + "' have no common type";
    return nullptr;
}

}

std::optional<std::size_t> enumerator_count(Type* t) {
    nodes::EnumDeclaration* d = enum_decl_of(t);
    if (!d) return std::nullopt;
    return d->values.size();
}

Type* apply_type_op(TypeContext& ctx, Op op, const std::vector<Type*>& args, std::string& error) {
    for (Type* a : args) {
        if (!a || a->is_error()) return ctx.error_();
    }

    for (Type* a : args) {
        if (a->is_dependent()) return ctx.dependent_name(ctx.strip_cv(unref(a)), parser_types::type_op_name(op));
    }

    Type* t = args.empty() ? nullptr : args.front();
    const std::string name(parser_types::type_op_name(op));

    if (op != Op::CommonType && args.size() == 1) {
        TypeQueryOverloadResult custom = overload_of(ctx, t, name, {});

        if (custom.found) {
            if (!custom.error.empty()) { error = custom.error; return nullptr; }
            return custom.type;
        }
    }

    switch (op) {
        case Op::Typeof:
        case Op::Decltype:
        case Op::Conditional:
            return t;

        case Op::DiscardConst:    return edit_cv(ctx, t, 0, -1);
        case Op::DiscardVolatile: return edit_cv(ctx, t, -1, 0);
        case Op::DiscardCv:       return edit_cv(ctx, t, 0, 0);
        case Op::AddConst:        return edit_cv(ctx, t, 1, -1);
        case Op::AddVolatile:     return edit_cv(ctx, t, -1, 1);
        case Op::DiscardRef:      return unref(t);
        case Op::Decay:           return decayed(ctx, t);

        case Op::DiscardPointer: {
            Type* v = unref(t);
            return v->is_pointer() ? static_cast<PointerType*>(v)->pointee() : v;
        }

        case Op::AddRef:
            if (is_base(t, BK::Void)) { error = "add_ref cannot make a reference to void"; return nullptr; }
            if (t->is_reference()) return ctx.reference(unref(t), RefQual::LValue, t->cv());
            return ctx.reference(t, RefQual::LValue, CV{});

        case Op::AddRvalueRef:
            if (is_base(t, BK::Void)) { error = "add_rvalue_ref cannot make a reference to void"; return nullptr; }
            if (t->is_reference()) return t;
            return ctx.reference(t, RefQual::RValue, CV{});

        case Op::AddPointer:
            return ctx.pointer(unref(t), CV{});

        case Op::MakeSigned:
        case Op::MakeUnsigned: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!is_numeric(b)) { error = name + " needs an integer or float type, not '" + text(v) + "'"; return nullptr; }
            return ctx.builtin_ranked(b->base(), b->width(), op == Op::MakeUnsigned, v->cv());
        }

        case Op::Promote:
        case Op::Demote: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!is_numeric(b)) { error = name + " needs an integer or float type, not '" + text(v) + "'"; return nullptr; }

            const bool is_int = b->base() == BK::Int;
            const int  w      = b->width() + (op == Op::Promote ? 1 : -1);
            const int  lo     = is_int ? kMinIntWidth : kMinFloatWidth;
            const int  hi     = is_int ? kMaxIntWidth : kMaxFloatWidth;

            if (w < lo || w > hi) {
                error = "'" + text(ctx.strip_cv(v)) + "' is already the " + (op == Op::Promote ? "widest " : "narrowest ") + (is_int ? "integer" : "float") + " type, so it cannot be " + (op == Op::Promote ? "promoted" : "demoted");
                return nullptr;
            }

            return ctx.builtin_ranked(b->base(), w, b->is_unsigned(), v->cv());
        }

        case Op::IntFromFloat: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!b || b->base() != BK::Float) { error = "int_from_float needs a float type, not '" + text(v) + "'"; return nullptr; }
            return ctx.builtin_ranked(BK::Int, b->width(), b->is_unsigned(), v->cv());
        }

        case Op::FloatFromInt: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!b || b->base() != BK::Int) { error = "float_from_int needs an integer type, not '" + text(v) + "'"; return nullptr; }
            return ctx.builtin_ranked(BK::Float, std::max(b->width(), kMinFloatWidth), b->is_unsigned(), v->cv());
        }

        case Op::ElementType: {
            Type* v = ctx.strip_cv(unref(t));
            if (v->is_array()) return static_cast<ArrayType*>(v)->element();
            if (is_base(v, BK::String) || is_base(v, BK::Text)) return ctx.builtin(BK::Char);
            error = "element_type needs an array or string type, not '" + text(v) + "'";
            return nullptr;
        }

        case Op::UnderlyingType: {
            Type* v = ctx.strip_cv(unref(t));

            if (!v->is_enum()) {
                error = "underlying_type needs an enum type, not '" + text(v) + "'";
                return nullptr;
            }

            if (nodes::EnumDeclaration* d = enum_decl_of(v)) {
                if (d->has_underlying_type) return ctx.canonicalize(d->underlying_type);
            }

            return ctx.builtin(BK::Int);
        }

        case Op::ReturnType: {
            Type* v = ctx.strip_cv(unref(t));
            if (v->is_pointer()) v = ctx.strip_cv(static_cast<PointerType*>(v)->pointee());
            if (v->is_function()) return static_cast<FunctionType*>(v)->ret();
            error = "return_type needs a function type, not '" + text(v) + "'";
            return nullptr;
        }

        case Op::CommonType: {
            Type* result = decayed(ctx, t);

            for (std::size_t i = 1; i < args.size() && result; ++i) {
                result = common_of(ctx, result, args[i], error);
            }

            return result;
        }
    }

    return t;
}

std::optional<std::int64_t> eval_type_trait(TypeContext& ctx, Trait trait, const std::vector<Type*>& args, std::string& error) {
    for (Type* a : args) {
        if (!a || a->is_error() || a->is_dependent()) return std::nullopt;
    }

    if (args.size() == 1) {
        TypeQueryOverloadResult custom = overload_of(ctx, args.front(), parser_types::type_trait_name(trait), {});

        if (custom.found) {
            if (!custom.error.empty()) error = custom.error;
            return custom.value;
        }
    }

    Type* raw = args.empty() ? nullptr : args.front();
    Type* t = unref(raw);
    Type* v = ctx.strip_cv(t);
    BuiltinType* b = as_builtin(v);

    switch (trait) {
        case Trait::IsSame: {
            Type* x = args[0];
            Type* y = args[1];
            if (x == y) return 1;
            return text(x) == text(y) ? 1 : 0;
        }

        case Trait::IsConst:       return t->cv().is_const ? 1 : 0;
        case Trait::IsVolatile:    return t->cv().is_volatile ? 1 : 0;
        case Trait::IsRef:         return raw->is_reference() ? 1 : 0;
        case Trait::IsLvalueRef:   return raw->is_reference() && static_cast<ReferenceType*>(raw)->ref_qual() == RefQual::LValue ? 1 : 0;
        case Trait::IsRvalueRef:   return raw->is_reference() && static_cast<ReferenceType*>(raw)->ref_qual() == RefQual::RValue ? 1 : 0;
        case Trait::IsPointer:     return v->is_pointer() ? 1 : 0;
        case Trait::IsArray:       return v->is_array() ? 1 : 0;
        case Trait::IsIntegral:    return b && b->base() == BK::Int ? 1 : 0;
        case Trait::IsFloating:    return b && b->base() == BK::Float ? 1 : 0;
        case Trait::IsArithmetic:  return b && (b->base() == BK::Int || b->base() == BK::Float) ? 1 : 0;
        case Trait::IsSigned:      return is_numeric(b) && !b->is_unsigned() ? 1 : 0;
        case Trait::IsUnsigned:    return is_numeric(b) && b->is_unsigned() ? 1 : 0;
        case Trait::IsBool:        return b && b->base() == BK::Bool ? 1 : 0;
        case Trait::IsChar:        return b && b->base() == BK::Char ? 1 : 0;
        case Trait::IsString:      return b && (b->base() == BK::String || b->base() == BK::Text) ? 1 : 0;
        case Trait::IsEnum:        return v->is_enum() ? 1 : 0;
        case Trait::IsClass:       return v->is_record() ? 1 : 0;
        case Trait::IsVoid:        return b && b->base() == BK::Void ? 1 : 0;
        case Trait::IsFunction:    return v->is_function() ? 1 : 0;
        case Trait::IsDynamic:     return b && b->base() == BK::Dynamic ? 1 : 0;
        case Trait::IsConvertible:
            load_conversions(ctx, args[0]);
            return rank_conversion(args[0], args[1], ctx) != ConversionRank::None ? 1 : 0;

        case Trait::IsBaseOf: {
            Scope* base = record_scope(ctx, ctx.strip_cv(unref(args[0])));
            Scope* derived = record_scope(ctx, ctx.strip_cv(unref(args[1])));
            if (!base || !derived) return 0;
            return derives_from(derived, base, 0) ? 1 : 0;
        }

        case Trait::IntegerRank:
            if (!b || b->base() != BK::Int) { error = "integer_rank needs an integer type, not '" + text(v) + "'"; return std::nullopt; }
            return b->width();

        case Trait::FloatRank:
            if (!b || b->base() != BK::Float) { error = "float_rank needs a float type, not '" + text(v) + "'"; return std::nullopt; }
            return b->width() - kFloatRankBase;
    }

    return std::nullopt;
}

} // namespace semantics
} // namespace walnut
