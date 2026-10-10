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

CV cv_union(CV a, CV b);

inline Type* strip_ref(Type* t) {
    return (t && t->is_reference()) ? static_cast<ReferenceType*>(t)->referent() : t;
}

Type* subst(Type* t, const SubstEnv& env, TypeContext& ctx);

bool deduce(Type* param, Type* arg, SubstEnv& env, TypeContext& ctx);

bool deduce_template_arg(const TemplateArg& p, const TemplateArg& a, SubstEnv& env, TypeContext& ctx);

bool deduce(Type* param, Type* arg, SubstEnv& env, TypeContext& ctx);

bool contains_error(Type* t);

bool is_pack_param(Symbol* s);

void collect_pack_params(Type* t, std::vector<Symbol*>& out);

bool deduce_pack(Type* pattern, const std::vector<Type*>& rest, SubstEnv& env, TypeContext& ctx);

bool expand_into(Type* elem, const SubstEnv& env, TypeContext& ctx, std::vector<Type*>& out);

bool subst_value_arg(const TemplateArg& a, const SubstEnv& env, TypeContext& ctx, TemplateArg& out);

Type* subst(Type* t, const SubstEnv& env, TypeContext& ctx);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SUBSTITUTION_HPP