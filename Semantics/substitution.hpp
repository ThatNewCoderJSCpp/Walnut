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

    bool empty() const { return types.empty() && values.empty() && fvalues.empty() && packs.empty(); }
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

inline Type* subst(Type* t, const SubstEnv& env, TypeContext& ctx);

inline bool deduce(Type* param, Type* arg, SubstEnv& env, TypeContext& ctx);

inline bool deduce_template_arg(const TemplateArg& p, const TemplateArg& a, SubstEnv& env, TypeContext& ctx) {
    if (p.is_type != a.is_type) return false;
    if (p.is_type) return deduce(p.type, a.type, env, ctx);
    if (p.is_value()) return p.value == a.value && p.fvalue == a.fvalue;
    if (!a.is_value()) return p == a;
    if (!p.param) return true;

    if (a.value) {
        if (const WideInt* prior = env.lookup_value(p.param); prior && prior != a.value) return false;
        env.values[p.param] = a.value;
        return true;
    }

    if (const WideFloat* prior = env.lookup_fvalue(p.param); prior && prior != a.fvalue) return false;
    env.fvalues[p.param] = a.fvalue;
    return true;
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

            if (Symbol* p = pa->extent_param()) {
                if (!aa->extent()) return false;
                const WideInt* v = ctx.consts().from(static_cast<long long>(*aa->extent()));
                if (const WideInt* prior = env.lookup_value(p); prior && prior != v) return false;
                env.values[p] = v;
            }

            return deduce(pa->element(), aa->element(), env, ctx);
        }

        case TypeKind::Record: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (!a || !a->is_record()) return false;
            auto* pr = static_cast<RecordType*>(param);
            auto* ar = static_cast<RecordType*>(a);
            if (pr->decl() != ar->decl()) return false;
            if (pr->args().size() != ar->args().size()) return false;
            for (std::size_t i = 0; i < pr->args().size(); ++i) if (!deduce_template_arg(pr->args()[i], ar->args()[i], env, ctx)) return false;
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

        case TypeKind::Coroutine: {
            Type* a = ctx.strip_cv(strip_ref(arg));
            if (!a || a->kind() != TypeKind::Coroutine) return false;
            auto* pc = static_cast<CoroutineType*>(param);
            auto* ac = static_cast<CoroutineType*>(a);
            if (pc->is_generator() != ac->is_generator()) return false;
            return deduce(pc->value(), ac->value(), env, ctx);
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
            for (const TemplateArg& a : static_cast<RecordType*>(t)->args()) if (a.is_type && contains_error(a.type)) return true;
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
        case TypeKind::Record:    for (const TemplateArg& a : static_cast<RecordType*>(t)->args()) if (a.is_type) collect_pack_params(a.type, out); return;
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

inline bool subst_value_arg(const TemplateArg& a, const SubstEnv& env, TypeContext& ctx, TemplateArg& out) {
    if (a.param) {
        if (const WideInt*   v = env.lookup_value(a.param))  { out = TemplateArg::of_int(v);   return true; }
        if (const WideFloat* f = env.lookup_fvalue(a.param)) { out = TemplateArg::of_float(f); return true; }
        if (!a.expr) { out = a; return true; }
    }

    if (!a.expr || !ctx.value_eval_hook) { out = a; return true; }
    ctx.push_subst(&env);
    TemplateArg v;
    const bool ok = ctx.value_eval_hook(a.expr, v);
    ctx.pop_subst();
    out = ok ? v : a;
    return true;
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
            Type* el = subst(a->element(), env, ctx);

            if (Symbol* p = a->extent_param()) {
                const WideInt* v = env.lookup_value(p);
                if (!v) return ctx.dependent_array(el, p, t->cv());
                if (v->is_negative()) return ctx.error_();
                return ctx.array(el, static_cast<std::size_t>(v->get_lowest_bits()), t->cv());
            }

            return ctx.array(el, a->extent(), t->cv());
        }

        case TypeKind::Record: {
            auto* r = static_cast<RecordType*>(t);
            TemplateArgs args;
            args.reserve(r->args().size());

            for (const TemplateArg& a : r->args()) {
                if (a.is_type) {
                    std::vector<Type*> expanded;
                    if (!expand_into(a.type, env, ctx, expanded)) return ctx.error_();
                    for (Type* e : expanded) args.push_back(TemplateArg::of_type(e));
                } else if (a.is_dependent_value()) {
                    TemplateArg v;
                    if (!subst_value_arg(a, env, ctx, v)) return ctx.error_();
                    args.push_back(v);
                } else {
                    args.push_back(a);
                }
            }

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

            if (s && s->is_record() && ctx.member_type_hook) {
                if (Type* m = ctx.member_type_hook(s, d->name())) return m;
            }

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

        case TypeKind::Coroutine: {
            auto* c = static_cast<CoroutineType*>(t);
            return ctx.coroutine(subst(c->value(), env, ctx), c->is_generator(), t->cv());
        }

        default: return t;
    }
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SUBSTITUTION_HPP