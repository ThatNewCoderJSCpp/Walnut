#ifndef WALNUT_SEMA_TYPE_IMPL_HPP
#define WALNUT_SEMA_TYPE_IMPL_HPP

#include "type.hpp"
#include "substitution.hpp"

#include "symbol.hpp"
#include "../Parser/Types/type_info.hpp"
#include "../Parser/Types/typename.hpp"
#include "../Parser/Types/dependent_types.hpp"

#include <algorithm>

namespace walnut {
namespace semantics {

void RecordType::write_to(std::ostream& os) const {
    write_cv(os);
    os << (m_decl ? m_decl->name : std::string_view{"<record>"});

    if (!m_args.empty()) {
        os << '<';

        for (std::size_t i = 0; i < m_args.size(); ++i) {
            if (i) os << ", ";
            const TemplateArg& a = m_args[i];
            if      (a.is_type && a.type) a.type->write_to(os);
            else if (a.value)             os << *a.value;
            else if (a.fvalue)            os << *a.fvalue;
            else if (a.param)             os << a.param->name;
            else                          os << "<expr>";
        }

        os << '>';
    }
}

void EnumType::write_to(std::ostream& os) const {
    write_cv(os);
    os << (m_decl ? m_decl->name : std::string_view{"<enum>"});
}

void TypeParamType::write_to(std::ostream& os) const {
    os << (m_param ? m_param->name : std::string_view{"<typeparam>"});
}

inline Type* TypeContext::builtin_ranked(
    parser_types::PrimitiveType::BaseKind base,
    int w, bool is_unsigned, CV cv
) {
    BuiltinKey key{ std::uint8_t(base), std::uint8_t(std::int8_t(w)), std::uint8_t(is_unsigned), cv.bits() };
    if (auto it = m_builtins.find(key); it != m_builtins.end()) return it->second;
    Type* t = make_in<BuiltinType>(m_arena, base, w, is_unsigned, cv);
    m_builtins.emplace(key, t);
    return t;
}

inline Type* TypeContext::builtin(
    parser_types::PrimitiveType::BaseKind base,
    parser_types::LengthModifier length,
    bool is_unsigned, bool long_form, CV cv
) {
    return builtin_ranked(base, builtin_width(base, length, long_form), is_unsigned, cv);
}

Type* TypeContext::pointer(Type* pointee, CV cv) {
    UnaryKey key{ TypeKind::Pointer, pointee, cv.bits(), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<PointerType>(m_arena, pointee, cv);
    mark_dependent(t, pointee && pointee->is_dependent());
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::reference(Type* referent, RefQual rq, CV cv) {
    UnaryKey key{ TypeKind::Reference, referent, std::uint8_t(cv.bits() | (std::uint8_t(rq) << 3)), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<ReferenceType>(m_arena, referent, rq, cv);
    mark_dependent(t, referent && referent->is_dependent());
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::array(Type* element, std::optional<std::size_t> extent, CV cv) {
    UnaryKey key{ TypeKind::Array, element, cv.bits(), extent ? *extent : 0, extent.has_value() };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<ArrayType>(m_arena, element, extent, cv);
    mark_dependent(t, element && element->is_dependent());
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::dependent_array(Type* element, Symbol* extent_param, CV cv) {
    UnaryKey key{ TypeKind::Array, element, std::uint8_t(cv.bits() | 0x80u), static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(extent_param)), true };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<ArrayType>(m_arena, element, std::nullopt, cv, extent_param);
    mark_dependent(t, true);
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::closure(nodes::ASTNode* lambda) {
    UnaryKey key{ TypeKind::Closure, reinterpret_cast<Type*>(lambda), 0, 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<ClosureType>(m_arena, lambda);
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::coroutine(Type* value, bool is_generator, CV cv) {
    UnaryKey key{ TypeKind::Coroutine, value, std::uint8_t(cv.bits() | (is_generator ? 0x40u : 0u)), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<CoroutineType>(m_arena, value, is_generator, cv);
    mark_dependent(t, value && value->is_dependent());
    m_unary.emplace(key, t);
    return t;
}

void ArrayType::write_to(std::ostream& os) const {
    os << "array[";
    if (m_extent) os << *m_extent;
    else if (m_extent_param) os << m_extent_param->name;
    os << "] ";
    m_element->write_to(os);
}

Type* TypeContext::enum_(Symbol* decl, CV cv) {
    UnaryKey key{ TypeKind::Enum, reinterpret_cast<Type*>(decl), cv.bits(), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<EnumType>(m_arena, decl, cv);
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::record(Symbol* decl, TemplateArgs args, CV cv) {
    RecordKey key{ decl, args, cv.bits() };
    if (auto it = m_records.find(key); it != m_records.end()) return it->second;
    Type* t = make_in<RecordType>(m_arena, decl, std::move(args), cv);
    bool dep = false;

    for (const TemplateArg& a : static_cast<RecordType*>(t)->args()) {
        if (a.is_dependent_value() || (a.is_type && a.type && a.type->is_dependent())) { dep = true; break; }
    }

    mark_dependent(t, dep);
    m_records.emplace(std::move(key), t);
    return t;
}

Type* TypeContext::function(Type* ret, std::vector<Type*> params, bool is_const, RefQual rq, bool is_noexcept) {
    std::uint8_t flags = std::uint8_t(is_const) | (std::uint8_t(rq) << 1) | (std::uint8_t(is_noexcept) << 3);
    FuncKey key{ ret, params, flags };
    if (auto it = m_functions.find(key); it != m_functions.end()) return it->second;
    Type* t = make_in<FunctionType>(m_arena, ret, std::move(params), is_const, rq, is_noexcept);
    bool dep = (ret && ret->is_dependent()) || any_dependent(static_cast<FunctionType*>(t)->params());
    mark_dependent(t, dep);
    m_functions.emplace(std::move(key), t);
    return t;
}

Type* TypeContext::variant(std::vector<Type*> alternatives) {
    // small N (2-5 typical): insertion sort beats std::sort overhead; swap in
    // your faster sort at this call site when benchmarking. Comparator is a
    // single pointer compare, stable within the compilation.
    for (std::size_t i = 1; i < alternatives.size(); ++i) {
        Type* v = alternatives[i];
        std::size_t j = i;
        while (j > 0 && alternatives[j - 1] > v) { alternatives[j] = alternatives[j - 1]; --j; }
        alternatives[j] = v;
    }

    alternatives.erase(std::unique(alternatives.begin(), alternatives.end()), alternatives.end());
    SeqKey key{ alternatives };
    if (auto it = m_variants.find(key); it != m_variants.end()) return it->second;
    Type* t = make_in<VariantType>(m_arena, std::move(alternatives));
    mark_dependent(t, any_dependent(static_cast<VariantType*>(t)->alternatives()));
    m_variants.emplace(std::move(key), t);
    return t;
}

Type* TypeContext::type_param(Symbol* param, CV cv) {
    UnaryKey key{ TypeKind::TypeParam, reinterpret_cast<Type*>(param), cv.bits(), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<TypeParamType>(m_arena, param, cv);
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::dependent_name(Type* base, std::string_view name) {
    DepKey key{ base, name };
    if (auto it = m_dependents.find(key); it != m_dependents.end()) return it->second;
    Type* t = make_in<DependentType>(m_arena, base, name);
    m_dependents.emplace(key, t);
    return t;
}

Type* TypeContext::with_cv(Type* base, CV cv) {
    if (!base || base->cv() == cv) return base;

    switch (base->kind()) {
        case TypeKind::Builtin: {
            auto* b = static_cast<BuiltinType*>(base);
            return builtin_ranked(b->base(), b->width(), b->is_unsigned(), cv);
        }

        case TypeKind::Pointer: return pointer(static_cast<PointerType*>(base)->pointee(), cv);

        case TypeKind::Array: {
            auto* a = static_cast<ArrayType*>(base);
            if (a->extent_param()) return dependent_array(a->element(), a->extent_param(), cv);
            return array(a->element(), a->extent(), cv);
        }

        case TypeKind::Record: {
            auto* r = static_cast<RecordType*>(base);
            return record(r->decl(), r->args(), cv);
        }

        case TypeKind::Enum: return enum_(static_cast<EnumType*>(base)->decl(), cv);
        case TypeKind::TypeParam: return type_param(static_cast<TypeParamType*>(base)->param(), cv);
        case TypeKind::Coroutine: return coroutine(static_cast<CoroutineType*>(base)->value(), static_cast<CoroutineType*>(base)->is_generator(), cv);
            
        default:
            return base;
    }
}

Type* TypeContext::apply_subst(Type* t) {
    if (!t || !t->is_dependent() || m_subst.empty()) return t;
    const std::vector<const SubstEnv*> stack = m_subst;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) t = subst(t, **it, *this);
    return t;
}

Type* TypeContext::pack_expansion(Type* pattern) {
    UnaryKey key{ TypeKind::PackExpansion, pattern, 0, 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<PackExpansionType>(m_arena, pattern);
    m_unary.emplace(key, t);
    return t;
}

namespace {

CV cv_from_modifiers(const modifiers::RawModifiers& m) {
    CV cv;
    cv.is_const     = m.has(modifiers::RawModifiers::Const) || m.has(modifiers::RawModifiers::Constexpr);
    cv.is_volatile  = m.has(modifiers::RawModifiers::Volatile);
    cv.is_immutable = m.has(modifiers::RawModifiers::Immutable);
    return cv;
}

Symbol* strip_aliases(Symbol* s) {
    while (s) {
        if (s->is_imported && s->import_target) { s = s->import_target; continue; }

        if (s->kind == SymbolKind::TypeAlias && s->type && s->type->resolved) {
            s = s->type->resolved; continue;
        }

        break;
    }
    return s;
}

} // namespace

Type* TypeContext::canonicalize(const parser_types::TypeInfo& info) {
    if (info.canonical) return apply_subst(info.canonical);  
    const std::size_t dims_before = m_dim_evals;
    const CV base_cv = cv_from_modifiers(info.modifiers);
    Type* base = nullptr;
    Symbol* sym = info.resolved;
    while (sym && sym->is_imported && sym->import_target) sym = sym->import_target;

    if (sym && sym->kind == SymbolKind::TypeAlias && sym->type && sym->type != &info) {
        Type* target = canonicalize(*sym->type);
        if (target) {
            CV merged = target->cv();
            merged.is_const = merged.is_const || base_cv.is_const;
            merged.is_volatile = merged.is_volatile || base_cv.is_volatile;
            base = target->is_reference() ? target : with_cv(target, merged);
        } else {
            base = error_();
        }
        sym = nullptr;
    } else {
        sym = strip_aliases(info.resolved);
    }

    if (base) {
    } else if (sym) {
        switch (sym->kind) {
            case SymbolKind::Enum:
                base = enum_(sym, base_cv);
                break;
            case SymbolKind::TemplateParam:
                base = type_param(sym, base_cv);                     
                break;
            case SymbolKind::Type:
            default: {
                TemplateArgs args;
                args.reserve(info.template_args.size());
                bool bad_value = false;

                for (const parser_types::TemplateArgument* a : info.template_args) {
                    if (!a) continue;

                    if (a->is_type()) {
                        if (value_symbol_hook && !a->is_pack && a->type.indirection.empty() && a->type.template_args.empty()) {
                            TemplateArg v;
                            const int r = value_symbol_hook(a->type.resolved, v);
                            if (r < 0) { bad_value = true; break; }
                            if (r > 0) { args.push_back(v); continue; }
                        }

                        Type* t = canonicalize(a->type);
                        args.push_back(TemplateArg::of_type(a->is_pack ? pack_expansion(t) : t));
                    } else if (a->is_value()) {
                        TemplateArg v;
                        if (!a->value || !value_arg_hook || !value_arg_hook(a->value, v)) { bad_value = true; break; }
                        args.push_back(v);
                    }
                }

                if (!bad_value && args.empty() && info.has_angle_args && sym->template_decl && default_instance_hook) {
                    if (Type* inst = default_instance_hook(sym)) { base = with_cv(inst, base_cv); break; }
                }

                if (!bad_value && args.empty() && instance_type_hook) {
                    if (Type* inst = instance_type_hook(sym)) { base = with_cv(inst, base_cv); break; }
                }

                if (!bad_value && !args.empty() && !sym->template_decl && instance_primary_hook) sym = instance_primary_hook(sym);
                base = bad_value ? error_() : record(sym, std::move(args), base_cv);
                break;
            }
        }
    } else if (info.type && info.type->is_qualified() && static_cast<parser_types::QualifiedType*>(info.type)->has_prefix()) {
        auto* qt = static_cast<parser_types::QualifiedType*>(info.type);
        Type* prefix = canonicalize(qt->prefix());
        Type* target = nullptr;
        if (prefix && prefix->is_dependent()) target = dependent_name(strip_cv(prefix), qt->name());
        else if (prefix && strip_cv(prefix)->is_record() && member_type_hook) target = member_type_hook(strip_cv(prefix), qt->name());

        if (!target) {
            base = error_();
        } else {
            CV merged = target->cv();
            merged.is_const = merged.is_const || base_cv.is_const;
            merged.is_volatile = merged.is_volatile || base_cv.is_volatile;
            base = target->is_reference() ? target : with_cv(target, merged);
        }
    } else if (!sym && info.type && info.type->is_user_defined() && info.template_args.size() == 1 && info.template_args[0] && info.template_args[0]->is_type()
               && static_cast<parser_types::UserDefinedType*>(info.type)->parts().size() == 1
               && (static_cast<parser_types::UserDefinedType*>(info.type)->name() == "generator" || static_cast<parser_types::UserDefinedType*>(info.type)->name() == "task")) {
        Type* value = canonicalize(info.template_args[0]->type);
        base = coroutine(value, static_cast<parser_types::UserDefinedType*>(info.type)->name() == "generator", base_cv);
    } else if (info.type && info.type->is_primitive()) {
        auto* p = static_cast<parser_types::PrimitiveType*>(info.type);
        base = builtin(p->base_kind(), p->length_modifier(), p->is_unsigned(), p->used_long_form(), base_cv);
    } else if (info.type && info.type->is_array()) {
        auto* at = static_cast<parser_types::ArrayType*>(info.type);
        Type* el = canonicalize(at->element_type());
        std::optional<std::size_t> extent = at->dimension();

        Symbol* extent_param = nullptr;

        if (!extent && at->dimension_expr() && value_eval_hook) {
            ++m_dim_evals;
            TemplateArg v;
            if (value_eval_hook(const_cast<nodes::ASTNode*>(at->dimension_expr()), v) && v.value && !v.value->is_negative()) extent = static_cast<std::size_t>(v.value->get_lowest_bits());
            else if (extent_param_hook) extent_param = extent_param_hook(const_cast<nodes::ASTNode*>(at->dimension_expr()));
        }

        if (extent_param && !(el && el->is_reference())) {
            base = dependent_array(el, extent_param, base_cv);
        } else if (el && el->is_reference()) {
            auto* r = static_cast<ReferenceType*>(el);
            base = reference(array(r->referent(), extent, CV{}), r->ref_qual(), base_cv);
        } else {
            base = array(el, extent, base_cv);
        }
    } else if (info.type && info.type->is_function_pointer()) {
        auto* fp = static_cast<parser_types::FunctionPointerType*>(info.type);
        std::vector<Type*> params;
        params.reserve(fp->param_count());
        for (const parser_types::TypeInfo& pt : fp->param_types()) params.push_back(canonicalize(pt));
        base = function(canonicalize(fp->return_type()), std::move(params), false, RefQual::None, false);
    } else {
        base = dynamic_();   
    }

    Type* result = base;

    for (const parser_types::IndirectionQualifier& q : info.indirection) {
        CV qcv; qcv.is_const = q.is_const;

        if (q.is_pointer()) {
            result = pointer(result, qcv);
        } else {
            result = reference(result, q.is_rvalue_ref() ? RefQual::RValue : RefQual::LValue, qcv);
        }
    }

    if (m_dim_evals == dims_before) const_cast<parser_types::TypeInfo&>(info).canonical = result;    
    return apply_subst(result);   
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_IMPL_HPP