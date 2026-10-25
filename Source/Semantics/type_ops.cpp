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

bool record_has_virtual(const Scope* s, bool abstract_only, int depth) {
    if (!s || depth > 64) return false;
    const nodes::RecordDeclaration* rd = s->node && s->node->kind == nodes::ASTNode::Kind::RecordDeclaration ? static_cast<const nodes::RecordDeclaration*>(s->node) : nullptr;
    using FQ = modifiers::FunctionQualifiers;

    if (rd) {
        for (const auto& m : rd->get_members()) {
            if (!m.node) continue;
            const modifiers::FunctionQualifiers* q = nullptr;
            bool has_body = true;
            if (m.node->kind == nodes::ASTNode::Kind::FunctionDeclaration) { auto* f = static_cast<const nodes::FunctionDeclaration*>(m.node); q = &f->qualifiers(); has_body = f->has_body(); }
            else if (m.node->kind == nodes::ASTNode::Kind::OperatorFunctionDeclaration) { auto* f = static_cast<const nodes::OperatorFunctionDeclaration*>(m.node); q = &f->qualifiers(); has_body = f->has_body(); }
            else if (m.node->kind == nodes::ASTNode::Kind::DestructorDeclaration) { if (!abstract_only && static_cast<const nodes::DestructorDeclaration*>(m.node)->m_qualifiers.has(FQ::Virtual)) return true; continue; }
            if (!q) continue;
            if (abstract_only) { if (q->has(FQ::Abstract) || (q->has(FQ::Virtual) && !has_body)) return true; }
            else if (q->has_any(FQ::Virtual | FQ::Override | FQ::Abstract)) return true;
        }
    }

    if (abstract_only) return false;
    for (const Scope* b : s->bases) if (record_has_virtual(b, false, depth + 1)) return true;
    return false;
}

bool record_is_empty(TypeContext& ctx, const Scope* s, int depth) {
    if (!s || depth > 64) return true;
    const nodes::RecordDeclaration* rd = s->node && s->node->kind == nodes::ASTNode::Kind::RecordDeclaration ? static_cast<const nodes::RecordDeclaration*>(s->node) : nullptr;
    if (rd) {
        if (rd->m_native) return false;
        for (const auto& m : rd->get_members()) {
            if (!m.node) continue;
            if (m.node->kind == nodes::ASTNode::Kind::VariableDeclaration && !static_cast<const nodes::VariableDeclaration*>(m.node)->get_type_info().modifiers.has(modifiers::RawModifiers::Static)) return false;
            if (m.node->kind == nodes::ASTNode::Kind::ArrayDeclaration && !static_cast<const nodes::ArrayDeclaration*>(m.node)->get_array_modifiers().has(modifiers::RawModifiers::Static)) return false;
        }
    }
    if (record_has_virtual(s, false, 0)) return false;
    for (const Scope* b : s->bases) if (!record_is_empty(ctx, b, depth + 1)) return false;
    return true;
}

std::int64_t int_bits_of(BuiltinType* b) { return static_cast<std::int64_t>(bit_width_of_rank(b->width())); }

std::int64_t float_bits_of(BuiltinType* b) { return static_cast<std::int64_t>(bit_width_of_rank(b->width())); }

std::int64_t float_exponent_bits_for(std::int64_t bits) {
    if (bits <= 8)  return 3;
    if (bits <= 16) return 5;
    if (bits <= 32) return 8;
    if (bits <= 64) return 11;
    std::int64_t lg = 0;
    while ((std::int64_t(1) << (lg + 1)) <= bits) ++lg;
    return 16 + 2 * (lg - 7);
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
            if (b->is_hardware() && b->base() == BK::Float) {
                if (op == Op::MakeSigned) return v;
                error = "'" + text(ctx.strip_cv(v)) + "' is C++'s own float type, which has no unsigned version";
                return nullptr;
            }
            return ctx.builtin_ranked(b->base(), b->width(), op == Op::MakeUnsigned, v->cv(), b->is_hardware());
        }

        case Op::Promote:
        case Op::Demote: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!is_numeric(b)) { error = name + " needs an integer or float type, not '" + text(v) + "'"; return nullptr; }

            const bool is_int = b->base() == BK::Int;
            const int  w      = b->width() + (op == Op::Promote ? 1 : -1);
            const bool hw     = b->is_hardware();
            const int  lo     = hw ? (is_int ? kHardwareMinIntWidth : kHardwareFloatWidth) : is_int ? kMinIntWidth : kMinFloatWidth;
            const int  hi     = hw ? (is_int ? kHardwareMaxIntWidth : kHardwareDoubleWidth) : is_int ? kMaxIntWidth : kMaxFloatWidth;

            if (w < lo || w > hi) {
                error = "'" + text(ctx.strip_cv(v)) + "' is already the " + (op == Op::Promote ? "widest " : "narrowest ") + (hw ? "hardware " : "") + (is_int ? "integer" : "float") + " type, so it cannot be " + (op == Op::Promote ? "promoted" : "demoted");
                return nullptr;
            }

            return ctx.builtin_ranked(b->base(), w, b->is_unsigned(), v->cv(), hw);
        }

        case Op::IntFromFloat: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!b || b->base() != BK::Float) { error = "int_from_float needs a float type, not '" + text(v) + "'"; return nullptr; }
            return ctx.builtin_ranked(BK::Int, b->width(), b->is_unsigned(), v->cv(), b->is_hardware());
        }

        case Op::FloatFromInt: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (!b || b->base() != BK::Int) { error = "float_from_int needs an integer type, not '" + text(v) + "'"; return nullptr; }
            if (b->is_hardware()) return ctx.hardware_float(std::max(b->width(), kHardwareFloatWidth), v->cv());
            return ctx.builtin_ranked(BK::Float, std::max(b->width(), kMinFloatWidth), b->is_unsigned(), v->cv());
        }

        case Op::Hardware: {
            Type* v = unref(t);
            BuiltinType* b = as_builtin(v);
            if (b && b->is_hardware()) return v;
            if (b && b->base() == BK::Int && b->width() >= kHardwareMinIntWidth && b->width() <= kHardwareMaxIntWidth) return ctx.hardware_int(b->width(), b->is_unsigned(), v->cv());
            if (!b || b->base() != BK::Float || b->is_unsigned() || (b->width() != kHardwareFloatWidth && b->width() != kHardwareDoubleWidth)) {
                error = "hardware needs an integer from 'short short int' to 'long int' (signed or unsigned, for C++'s int8_t to int64_t), 'short short float' (for C++'s float) or 'short float' (for C++'s double), not '" + text(v) + "'";
                return nullptr;
            }
            return ctx.hardware_float(b->width(), v->cv());
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

bool trait_has_pack(const std::vector<parser_types::TemplateArgument*>& extra) {
    for (const parser_types::TemplateArgument* x : extra) if (x && x->type.pack_expansion) return true;
    return false;
}

std::vector<Type*> trait_operands(TypeContext& ctx, const parser_types::TemplateArgument* first, const std::vector<parser_types::TemplateArgument*>& extra) {
    std::vector<Type*> out;
    out.push_back(first ? ctx.canonicalize(first->type) : nullptr);
    for (const parser_types::TemplateArgument* x : extra) {
        if (!x) { out.push_back(nullptr); continue; }
        if (x->type.pack_expansion) {
            if (const std::vector<Type*>* pack = ctx.find_pack(x->type.resolved)) {
                out.insert(out.end(), pack->begin(), pack->end());
                continue;
            }
            out.push_back(nullptr);
            continue;
        }
        out.push_back(ctx.canonicalize(x->type));
    }
    return out;
}

std::optional<std::int64_t> eval_type_trait(TypeContext& ctx, Trait trait, const std::vector<Type*>& args, std::string& error) {
    for (Type* a : args) {
        if (!a || a->is_error() || a->is_dependent()) return std::nullopt;
    }

    if (parser_types::is_lifecycle_trait(trait)) {
        const std::pair<int, std::vector<Type*>> key{ static_cast<int>(trait), args };
        if (const auto it = ctx.lifecycle_trait_memo.find(key); it != ctx.lifecycle_trait_memo.end()) return it->second;
        if (!ctx.lifecycle_trait_hook) return std::nullopt;
        const std::optional<std::int64_t> r = ctx.lifecycle_trait_hook(static_cast<int>(trait), args);
        if (r) ctx.lifecycle_trait_memo.emplace(key, *r);
        return r;
    }

    if (args.size() == 1) {
        TypeQueryOverloadResult custom = overload_of(ctx, args.front(), parser_types::type_trait_name(trait), {});
        if (!custom.found && trait == Trait::IsFloating) custom = overload_of(ctx, args.front(), "is_floating", {});

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
        case Trait::IsInteger:     return b && b->base() == BK::Int ? 1 : 0;
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

        case Trait::IsText:        return b && b->base() == BK::Text ? 1 : 0;
        case Trait::IsStr:         return b && b->base() == BK::String ? 1 : 0;
        case Trait::IsNullPointer: return v->is_null() ? 1 : 0;
        case Trait::IsSpecial:     return v->is_special_value() ? 1 : 0;
        case Trait::IsVariant:     return v->kind() == TypeKind::Variant ? 1 : 0;
        case Trait::IsClosure:     return v->kind() == TypeKind::Closure ? 1 : 0;
        case Trait::IsFixedArray:  return v->is_array() && static_cast<ArrayType*>(v)->extent().has_value() ? 1 : 0;
        case Trait::IsRuntimeArray: return v->is_array() && !static_cast<ArrayType*>(v)->extent().has_value() ? 1 : 0;

        case Trait::ArrayExtent:
            if (!v->is_array() || !static_cast<ArrayType*>(v)->extent()) { error = "array_extent needs a fixed-size array type, not '" + text(v) + "'"; return std::nullopt; }
            return static_cast<std::int64_t>(*static_cast<ArrayType*>(v)->extent());

        case Trait::IsUnion:
        case Trait::IsStruct:
        case Trait::IsAbstract:
        case Trait::IsPolymorphic:
        case Trait::IsEmpty:
        case Trait::IsNative: {
            Scope* s = record_scope(ctx, v);
            const nodes::RecordDeclaration* rd = s && s->node && s->node->kind == nodes::ASTNode::Kind::RecordDeclaration ? static_cast<const nodes::RecordDeclaration*>(s->node) : nullptr;
            if (!rd) return 0;
            if (trait == Trait::IsUnion)  return rd->is_union() ? 1 : 0;
            if (trait == Trait::IsStruct) return rd->is_struct() ? 1 : 0;
            if (trait == Trait::IsNative) return rd->m_native ? 1 : 0;
            if (trait == Trait::IsEmpty)  return record_is_empty(ctx, s, 0) ? 1 : 0;
            return record_has_virtual(s, trait == Trait::IsAbstract, 0) ? 1 : 0;
        }

        case Trait::IntegerBits:
            if (!b || b->base() != BK::Int) { error = "integer_bits needs an integer type, not '" + text(v) + "'"; return std::nullopt; }
            return int_bits_of(b);

        case Trait::FloatBits:
        case Trait::FloatMantissaBits:
        case Trait::FloatExponentBits: {
            if (!b || b->base() != BK::Float) { error = std::string(parser_types::type_trait_name(trait)) + " needs a float type, not '" + text(v) + "'"; return std::nullopt; }
            const std::int64_t bits = float_bits_of(b);
            const std::int64_t exp = float_exponent_bits_for(bits);
            if (trait == Trait::FloatBits) return bits;
            if (trait == Trait::FloatExponentBits) return exp;
            if (b->is_hardware()) return bits - exp - 1;
            return bits - exp;
        }

        case Trait::IsHardware:    return b && b->is_hardware() ? 1 : 0;

        default:
            break;
    }

    return std::nullopt;
}

} // namespace semantics
} // namespace walnut
