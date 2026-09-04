#ifndef WALNUT_SEMANTICS_SUBSTITUTION_HPP
#define WALNUT_SEMANTICS_SUBSTITUTION_HPP

#include "type.hpp"
#include "symbol.hpp"
#include "scope.hpp"

#include <cstdint>
#include <unordered_map>

#include "const_value.hpp" 

namespace walnut {
namespace semantics {

struct SubstEnv {
    std::unordered_map<Symbol*, Type*>              types;   
    std::unordered_map<Symbol*, const WideInt*>     values;  
    std::unordered_map<Symbol*, const WideFloat*>   fvalues;
    std::unordered_map<Symbol*, std::vector<Type*>> packs;  

    Type* lookup_type(Symbol* p) const {
        auto it = types.find(p);
        return it == types.end() ? nullptr : it->second;
    }

    const WideInt* lookup_value(Symbol* p) const {
        auto it = values.find(p);
        return it == values.end() ? nullptr : it->second;
    }

    const WideFloat* lookup_fvalue(Symbol* p) const {
        auto it = fvalues.find(p);
        return it == fvalues.end() ? nullptr : it->second;
    }

    const std::vector<Type*>* lookup_pack(Symbol* p) const {
        auto it = packs.find(p);
        return it == packs.end() ? nullptr : &it->second;
    }

    bool empty() const { return types.empty() && values.empty() && packs.empty(); }
};

inline CV cv_union(CV a, CV b) {
    CV r;
    r.is_const     = a.is_const     || b.is_const;
    r.is_volatile  = a.is_volatile  || b.is_volatile;
    r.is_immutable = a.is_immutable || b.is_immutable;
    return r;
}

inline Type* strip_ref(Type* t) {
    return (t && t->is_reference()) ? static_cast<ReferenceType*>(t)->referent() : t;
}

inline Type* subst(Type* t, const SubstEnv& env, TypeContext& ctx) {
    if (!t || !t->is_dependent() || env.empty()) return t;

    switch (t->kind()) {
        case TypeKind::TypeParam: {
            auto* p = static_cast<TypeParamType*>(t);
            Type* bound = env.lookup_type(p->param());
            if (!bound) return t;                                       
            return ctx.with_cv(bound, cv_union(bound->cv(), t->cv()));  
        }

        case TypeKind::Pointer: {
            auto* p = static_cast<PointerType*>(t);
            return ctx.pointer(subst(p->pointee(), env, ctx), t->cv());
        }

        case TypeKind::Reference: {
            auto* r = static_cast<ReferenceType*>(t);
            return ctx.reference(subst(r->referent(), env, ctx), r->ref_qual(), t->cv());
        }

        case TypeKind::Array: {
            auto* a = static_cast<ArrayType*>(t);
            return ctx.array(subst(a->element(), env, ctx), a->extent(), t->cv());
        }

        case TypeKind::Record: {
            auto* r = static_cast<RecordType*>(t);
            std::vector<Type*> args;
            for (Type* a : r->args()) if (!expand_into(a, env, ctx, args)) return ctx.error_();
            return ctx.record(r->decl(), std::move(args), t->cv());
        }

        case TypeKind::Function: {
            auto* f = static_cast<FunctionType*>(t);
            std::vector<Type*> params;
            params.reserve(f->params().size());
            for (Type* p : f->params()) params.push_back(subst(p, env, ctx));
            return ctx.function(subst(f->ret(), env, ctx), std::move(params), f->is_const(), f->ref_qual(), f->is_noexcept());
        }

        case TypeKind::Variant: {
            auto* v = static_cast<VariantType*>(t);
            std::vector<Type*> alts;
            alts.reserve(v->alternatives().size());
            for (Type* a : v->alternatives()) alts.push_back(subst(a, env, ctx));
            return ctx.variant(std::move(alts));    
        }

        case TypeKind::Dependent: {
            auto* d = static_cast<DependentType*>(t);
            Type* base = subst(d->base(), env, ctx);

            if (base->is_dependent()) {
                return (base == d->base()) ? t : ctx.dependent_name(base, d->name());
            }

            Type* s = ctx.strip_cv(base);

            if (s && s->is_record()) {
                Symbol* rec = static_cast<RecordType*>(s)->decl();

                if (rec && rec->inner_scope) {
                    if (Symbol* m = rec->inner_scope->find_member(d->name())) {
                        switch (m->kind) {
                            case SymbolKind::TypeAlias:
                                if (m->type) return subst(ctx.canonicalize(*m->type), env, ctx);
                                break;
                            case SymbolKind::Type: return ctx.record(m, {}, CV{});
                            case SymbolKind::Enum: return ctx.enum_(m, CV{});
                            default: break;
                        }
                    }
                }
            }

            return ctx.error_();   
        }

        case TypeKind::PackExpansion:
            return ctx.pack_expansion(subst(static_cast<PackExpansionType*>(t)->pattern(), env, ctx));

        default: return t;
    }
}

inline bool deduce(Type* param, Type* arg, SubstEnv& env, TypeContext& ctx) {
    if (!param) return false;
    if (!param->is_dependent()) return true; 
    if (!arg) return false;

    switch (param->kind()) {
        case TypeKind::TypeParam: {
            Symbol* p = static_cast<TypeParamType*>(param)->param();
            Type* want = ctx.strip_cv(strip_ref(arg));          
            if (Type* prior = env.lookup_type(p); prior && prior != want) return false;
            env.types[p] = want;
            return true;
        }

        case TypeKind::Reference: return deduce(static_cast<ReferenceType*>(param)->referent(), strip_ref(arg), env, ctx);

        case TypeKind::Pointer: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (!a || !a->is_pointer()) return false;
            return deduce(static_cast<PointerType*>(param)->pointee(), static_cast<PointerType*>(a)->pointee(), env, ctx);
        }

        case TypeKind::Array: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (!a || !a->is_array()) return false;
            auto* pa = static_cast<ArrayType*>(param);
            auto* aa = static_cast<ArrayType*>(a);
            if (pa->extent() && aa->extent() && *pa->extent() != *aa->extent()) return false;
            return deduce(pa->element(), aa->element(), env, ctx);
        }

        case TypeKind::Record: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (!a || !a->is_record()) return false;
            auto* pr = static_cast<RecordType*>(param);
            auto* ar = static_cast<RecordType*>(a);
            if (pr->decl() != ar->decl()) return false;
            if (pr->args().size() != ar->args().size()) return false;
            for (std::size_t i = 0; i < pr->args().size(); ++i) if (!deduce(pr->args()[i], ar->args()[i], env, ctx)) return false;
            return true;
        }

        case TypeKind::Function: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (a && a->is_pointer()) a = ctx.strip_cv(static_cast<PointerType*>(a)->pointee());
            if (!a || !a->is_function()) return false;
            auto* pf = static_cast<FunctionType*>(param);
            auto* af = static_cast<FunctionType*>(a);
            if (pf->params().size() != af->params().size()) return false;
            if (!deduce(pf->ret(), af->ret(), env, ctx)) return false;
            for (std::size_t i = 0; i < pf->params().size(); ++i) if (!deduce(pf->params()[i], af->params()[i], env, ctx)) return false;
            return true;
        }

        case TypeKind::Variant:      
        case TypeKind::Dependent:
        default:
            return true;
    }
}

inline bool contains_error(Type* t) {
    if (!t) return false;
    switch (t->kind()) {
        case TypeKind::Error:     return true;
        case TypeKind::Pointer:   return contains_error(static_cast<PointerType*>(t)->pointee());
        case TypeKind::Reference: return contains_error(static_cast<ReferenceType*>(t)->referent());
        case TypeKind::Array:     return contains_error(static_cast<ArrayType*>(t)->element());
        case TypeKind::Record:
            for (Type* a : static_cast<RecordType*>(t)->args()) if (contains_error(a)) return true;
            return false;
        case TypeKind::Function: {
            auto* f = static_cast<FunctionType*>(t);
            if (contains_error(f->ret())) return true;
            for (Type* p : f->params()) if (contains_error(p)) return true;
            return false;
        }
        case TypeKind::Variant:
            for (Type* a : static_cast<VariantType*>(t)->alternatives()) if (contains_error(a)) return true;
            return false;
        default: return false;
    }
}

inline bool is_pack_param(Symbol* s) {
    return s && s->kind == SymbolKind::TemplateParam
        && s->decl && s->decl->kind == nodes::ASTNode::Kind::TemplateParameter
        && static_cast<nodes::TemplateParameter*>(s->decl)->is_pack();
}

inline void collect_pack_params(Type* t, std::vector<Symbol*>& out) {
    if (!t || !t->is_dependent()) return;

    switch (t->kind()) {
        case TypeKind::TypeParam: {
            Symbol* p = static_cast<TypeParamType*>(t)->param();
            if (is_pack_param(p) && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
            return;
        }

        case TypeKind::Pointer:   collect_pack_params(static_cast<PointerType*>(t)->pointee(), out);   return;
        case TypeKind::Reference: collect_pack_params(static_cast<ReferenceType*>(t)->referent(), out); return;
        case TypeKind::Array:     collect_pack_params(static_cast<ArrayType*>(t)->element(), out);      return;
        case TypeKind::Record:    for (Type* a : static_cast<RecordType*>(t)->args())          collect_pack_params(a, out); return;
        case TypeKind::Variant:   for (Type* a : static_cast<VariantType*>(t)->alternatives()) collect_pack_params(a, out); return;

        case TypeKind::Function: {
            auto* f = static_cast<FunctionType*>(t);
            collect_pack_params(f->ret(), out);
            for (Type* p : f->params()) collect_pack_params(p, out);
            return;
        }

        case TypeKind::PackExpansion: collect_pack_params(static_cast<PackExpansionType*>(t)->pattern(), out); return;
        case TypeKind::Dependent:     collect_pack_params(static_cast<DependentType*>(t)->base(), out);        return;
        default: return;
    }
}

inline bool expand_into(Type* elem, const SubstEnv& env, TypeContext& ctx, std::vector<Type*>& out) {
    if (!elem || elem->kind() != TypeKind::PackExpansion) {
        out.push_back(subst(elem, env, ctx));
        return true;
    }

    Type* pattern = static_cast<PackExpansionType*>(elem)->pattern();
    std::vector<Symbol*> pks;
    collect_pack_params(pattern, pks);
    if (pks.empty()) return false;                       
    std::size_t n = std::size_t(-1);

    for (Symbol* p : pks) {
        const std::vector<Type*>* v = env.lookup_pack(p);
        if (!v) { out.push_back(ctx.pack_expansion(subst(pattern, env, ctx))); return true; }  
        if (n == std::size_t(-1)) n = v->size();
        else if (n != v->size())  return false;          
    }

    for (std::size_t i = 0; i < n; ++i) {
        SubstEnv per = env;                              
        for (Symbol* p : pks) per.types[p] = (*env.lookup_pack(p))[i];
        out.push_back(subst(pattern, per, ctx));
    }

    return true;
}

inline bool deduce_pack(Type* pattern, const std::vector<Type*>& rest, SubstEnv& env, TypeContext& ctx) {
    std::vector<Symbol*> pks;
    collect_pack_params(pattern, pks);
    if (pks.size() != 1) return false;
    Symbol* P = pks[0];
    std::vector<Type*> elems;
    elems.reserve(rest.size());

    for (Type* a : rest) {
        SubstEnv probe;
        probe.types = env.types;                         
        if (!deduce(pattern, a, probe, ctx)) return false;
        Type* got = probe.lookup_type(P);
        if (!got) return false;                         
        elems.push_back(got);

        for (auto& kv : probe.types) {                  
            if (kv.first == P) continue;
            if (Type* prior = env.lookup_type(kv.first); prior && prior != kv.second) return false;
            env.types[kv.first] = kv.second;
        }
    }

    if (const std::vector<Type*>* prior = env.lookup_pack(P); prior && *prior != elems) return false;
    env.packs[P] = std::move(elems);
    return true;
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SUBSTITUTION_HPP