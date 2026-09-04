#ifndef WALNUT_SEMA_TYPE_IMPL_HPP
#define WALNUT_SEMA_TYPE_IMPL_HPP

#include "type.hpp"
#include "substitution.hpp"

#include "symbol.hpp"
#include "../Parser/Types/type_info.hpp"
#include "../Parser/Types/typename.hpp"

#include <algorithm>

namespace walnut {
namespace semantics {

void RecordType::write_to(std::ostream& os) const {
    write_cv(os);
    os << (m_decl ? m_decl->name : std::string_view{"<record>"});

    if (!m_args.empty()) {
        os << '<';
        for (std::size_t i = 0; i < m_args.size(); ++i) { if (i) os << ", "; m_args[i]->write_to(os); }
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

inline Type* TypeContext::builtin(
    parser_types::PrimitiveType::BaseKind base,
    parser_types::LengthModifier length,
    bool is_unsigned, bool long_form, CV cv
) {
    int w = builtin_width(length, long_form);
    BuiltinKey key{ std::uint8_t(base), std::uint8_t(std::int8_t(w)), std::uint8_t(is_unsigned), cv.bits() };
    if (auto it = m_builtins.find(key); it != m_builtins.end()) return it->second;
    Type* t = make_in<BuiltinType>(m_arena, base, w, is_unsigned, cv);
    m_builtins.emplace(key, t);
    return t;
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

Type* TypeContext::enum_(Symbol* decl, CV cv) {
    UnaryKey key{ TypeKind::Enum, reinterpret_cast<Type*>(decl), cv.bits(), 0, false };
    if (auto it = m_unary.find(key); it != m_unary.end()) return it->second;
    Type* t = make_in<EnumType>(m_arena, decl, cv);
    m_unary.emplace(key, t);
    return t;
}

Type* TypeContext::record(Symbol* decl, std::vector<Type*> args, CV cv) {
    RecordKey key{ decl, args, cv.bits() };
    if (auto it = m_records.find(key); it != m_records.end()) return it->second;
    Type* t = make_in<RecordType>(m_arena, decl, std::move(args), cv);
    mark_dependent(t, any_dependent(static_cast<RecordType*>(t)->args()));
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
            return builtin(b->base(), b->length(), b->is_unsigned(), b->is_long_form(), cv);
        }

        case TypeKind::Pointer: return pointer(static_cast<PointerType*>(base)->pointee(), cv);

        case TypeKind::Array: {
            auto* a = static_cast<ArrayType*>(base);
            return array(a->element(), a->extent(), cv);
        }

        case TypeKind::Record: {
            auto* r = static_cast<RecordType*>(base);
            return record(r->decl(), r->args(), cv);
        }

        case TypeKind::Enum: return enum_(static_cast<EnumType*>(base)->decl(), cv);
        case TypeKind::TypeParam: return type_param(static_cast<TypeParamType*>(base)->param(), cv);
            
        default:
            return base;
    }
}

Type* TypeContext::apply_subst(Type* t) {
    if (!t || !t->is_dependent() || m_subst.empty()) return t;
    for (auto it = m_subst.rbegin(); it != m_subst.rend(); ++it) t = subst(t, **it, *this);
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
    const CV base_cv = cv_from_modifiers(info.modifiers);
    Type* base = nullptr;
    Symbol* sym = strip_aliases(info.resolved);

    if (sym) {
        switch (sym->kind) {
            case SymbolKind::Enum:
                base = enum_(sym, base_cv);
                break;
            case SymbolKind::TemplateParam:
                base = type_param(sym, base_cv);                     
                break;
            case SymbolKind::Type:
            default: {
                std::vector<Type*> args;
                args.reserve(info.template_args.size());

                for (const parser_types::TemplateArgument* a : info.template_args) {
                    if (a && a->is_type()) {
                        Type* t = canonicalize(a->type);
                        args.push_back(a->is_pack ? pack_expansion(t) : t);
                    }
                }

                base = record(sym, std::move(args), base_cv);
                break;
            }
        }
    } else if (info.type && info.type->is_primitive()) {
        auto* p = static_cast<parser_types::PrimitiveType*>(info.type);
        base = builtin(p->base_kind(), p->length_modifier(), p->is_unsigned(), p->used_long_form(), base_cv);
    } else {
        base = dynamic_();   
    }

    Type* result = base;

    for (const parser_types::IndirectionQualifier& q : info.indirection) {
        CV qcv; qcv.is_const = q.is_const;

        if (q.is_pointer()) {
            result = pointer(result, qcv);
        } else {
            result = reference(result, RefQual::LValue, qcv);
        }
    }

    const_cast<parser_types::TypeInfo&>(info).canonical = result;    
    return apply_subst(result);   
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_IMPL_HPP