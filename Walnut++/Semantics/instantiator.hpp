#ifndef WALNUT_SEMANTICS_INSTANTIATOR_HPP
#define WALNUT_SEMANTICS_INSTANTIATOR_HPP

#include "substitution.hpp"
#include "type.hpp"
#include "scope.hpp"
#include "symbol.hpp"
#include "overload.hpp"
#include "semantic_error.hpp"
#include "semantic_warning.hpp"
#include "const_value.hpp"
#include "const_evaluator.hpp"
#include "../Parser/nodes.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

namespace walnut {
namespace semantics {

class Instantiator {
public:
    struct Arg {
        Type*            type    = nullptr;
        const WideInt*   value   = nullptr;   
        const WideFloat* fvalue  = nullptr;   
        bool             is_type = true;

        bool operator==(const Arg& o) const {
            return is_type == o.is_type && type == o.type && value == o.value && fvalue == o.fvalue;
        }
    };

    struct Instantiation {
        nodes::TemplateDeclaration* source = nullptr;
        nodes::ASTNode*             decl   = nullptr;  // deep clone (starts untyped)
        Symbol*                     sym    = nullptr;  // clone's entity symbol
        Scope*                      scope  = nullptr;  // private scope wrapping the clone
        SubstEnv                    env;               // ORIGINAL param symbols -> args
        std::vector<Arg>            args;              // post-default, canonical, packs flattened
        Type*                       type = nullptr;    // RecordType / FunctionType
        std::vector<ParamShape>     fn_shapes;
        bool                        typed = false;     
    };

    enum class ArgCoerce : std::uint8_t {
        Ok = 0,
        Overflow,        // magnitude exceeds the declared width
        Underflow,       // nonzero value rounds to zero at the declared width
        WrongForm,       // float argument for an integer parameter, or vice versa
        NotConstant      // no usable value at all
    };

    struct CoerceResult {
        ArgCoerce status  = ArgCoerce::Ok;
        bool      inexact = false;      // rounded, but still a usable argument
        bool ok() const { return status == ArgCoerce::Ok; }

        static CoerceResult fail(ArgCoerce s) { CoerceResult r; r.status = s; return r; }
    };

    struct SpecMatch {
        nodes::TemplateDeclaration* tmpl = nullptr;
        SubstEnv                    env;
        bool                        failed = false;  
    };

public:
    Instantiator(Arena& arena, TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings)
        : m_arena(arena), m_types(types), m_reporter(reporter), m_warnings(warnings), m_eval(types) {}

    std::function<void(nodes::ASTNode*, Scope*)> build_scopes;

    Instantiation* instantiate(Symbol* generic, std::vector<Arg> args, nodes::ASTNode* site) {
        return instantiate_impl(generic, std::move(args), site, false);
    }

    template <typename Fn>
    void for_each(Fn&& fn) const { for (const auto& kv : m_memo) fn(*kv.second); }

    Instantiation* for_record(RecordType* rt, nodes::ASTNode* site) {
        if (!rt || !rt->is_instantiation()) return nullptr;
        std::vector<Arg> args;
        args.reserve(rt->args().size());
        for (Type* t : rt->args()) { Arg a; a.type = t; args.push_back(a); }
        return instantiate_impl(rt->decl(), std::move(args), site, false);
    }

    Instantiation* instantiate_for_call(
        Symbol* generic,
        const std::vector<parser_types::TemplateArgument*>& explicit_args,
        const std::vector<Type*>& call_args,
        nodes::ASTNode* site
    ) {
        nodes::TemplateDeclaration* tmpl = generic ? generic->template_decl : nullptr;
        if (!tmpl || !tmpl->m_declaration || tmpl->m_declaration->kind != nodes::ASTNode::Kind::FunctionDeclaration) return nullptr;
        const auto& params = tmpl->params();
        if (has_nontrailing_pack(params)) return nullptr;
        nodes::TemplateParameter* tpack = trailing_pack(params);
        const std::size_t fixed = params.size() - (tpack ? 1 : 0);
        SubstEnv env;
        if (!tpack && explicit_args.size() > params.size()) return nullptr;
        const std::size_t nfix_explicit = std::min(explicit_args.size(), fixed);
        for (std::size_t i = 0; i < nfix_explicit; ++i) if (!bind_written(params[i], explicit_args[i], env)) return nullptr;

        if (tpack && explicit_args.size() > fixed) {
            if (!tpack->is_type_param() || !tpack->symbol) return nullptr;
            std::vector<Type*>& pk = env.packs[tpack->symbol];

            for (std::size_t i = fixed; i < explicit_args.size(); ++i) {
                const parser_types::TemplateArgument* w = explicit_args[i];
                if (!w || !w->is_type()) return nullptr;
                pk.push_back(m_types.canonicalize(w->type));
            }
        }

        auto* fd = static_cast<nodes::FunctionDeclaration*>(tmpl->m_declaration);

        if (const nodes::FunctionParameters* ps = fd->get_parameters()) {
            std::size_t nfix_fn = ps->size();
            Type* last_pattern = nullptr;
            std::vector<Symbol*> last_pks;

            if (!ps->m_params.empty() && ps->m_params.back()->is_variadic()) {
                last_pattern = m_types.canonicalize(ps->m_params.back()->get_type());
                collect_pack_params(last_pattern, last_pks);
                if (!last_pks.empty()) --nfix_fn;  
            }

            const std::size_t n = std::min(nfix_fn, call_args.size());

            for (std::size_t i = 0; i < n; ++i) {
                Type* pt = m_types.canonicalize(ps->m_params[i]->get_type());
                if (!deduce(pt, call_args[i], env, m_types)) return nullptr;
            }

            if (!last_pks.empty()) {
                const bool already = last_pks.size() == 1 && env.lookup_pack(last_pks[0]) != nullptr;

                if (!already) {
                    std::vector<Type*> rest;
                    for (std::size_t i = nfix_fn; i < call_args.size(); ++i) rest.push_back(call_args[i]);
                    if (!deduce_pack(last_pattern, rest, env, m_types)) return nullptr;
                }
            }
        }

        std::vector<Arg> args;
        if (!assemble_args(tmpl, env, args, site, true)) return nullptr;
        return instantiate_impl(generic, std::move(args), site, true);
    }

    std::vector<Arg> canon_args(const std::vector<parser_types::TemplateArgument*>& written, bool& ok) {
        std::vector<Arg> out;
        out.reserve(written.size());
        ok = true;

        for (const parser_types::TemplateArgument* w : written) {
            Arg a;

            if (w && w->is_type()) {
                Type* t = m_types.canonicalize(w->type);
                a.type = w->is_pack ? m_types.pack_expansion(t) : t;
            } else if (w && w->is_value()) {
                if (!eval_value_arg(w->value, a)) { ok = false; continue; }
            }

            out.push_back(a);
        }

        return out;
    }

    void refresh_function_type(Instantiation* I) {
        if (!I || !I->decl || I->decl->kind != nodes::ASTNode::Kind::FunctionDeclaration) return;
        finish_function_type(I, static_cast<nodes::FunctionDeclaration*>(I->decl));
    }

    Instantiation* owning(Symbol* s) const {
        for (Scope* sc = s ? s->owner : nullptr; sc; sc = sc->parent) {
            if (auto it = m_scope_owner.find(sc); it != m_scope_owner.end()) return it->second;
        }
        return nullptr;
    }

private:
    struct Key {
        Symbol* generic;
        std::vector<Arg> args;
        bool operator==(const Key& o) const { return generic == o.generic && args == o.args; }
    };

    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            std::size_t h = std::hash<const void*>{}(k.generic);

            for (const Arg& a : k.args) {
                h = h * 1000003u ^ std::hash<const void*>{}(a.type);
                h = h * 1000003u ^ (std::hash<const void*>{}(a.value) + std::size_t(a.is_type));
                h = h * 1000003u ^ std::hash<const void*>{}(a.fvalue);
            }

            return h;
        }
    };

    Instantiation* instantiate_impl(Symbol* generic, std::vector<Arg> args, nodes::ASTNode* site, bool quiet) {
        using K = nodes::ASTNode::Kind;
        nodes::TemplateDeclaration* tmpl = generic ? generic->template_decl : nullptr;

        if (!tmpl) {
            if (!quiet) SemanticError::not_a_template(m_reporter, file_of(site), line_of(site), generic ? generic->name : "<name>");
            return nullptr;
        }

        if (has_nontrailing_pack(tmpl->params())) {
            if (!quiet) unsupported(site, "non-trailing parameter packs");
            return nullptr;
        }

        if (!flatten_packs(args)) {
            if (!quiet) unsupported(site, "unexpanded or mismatched pack arguments");
            return nullptr;
        }

        if (!complete_arguments(tmpl, args, site, quiet)) return nullptr;
        Key key{ generic, args };
        if (auto it = m_memo.find(key); it != m_memo.end()) return it->second;

        if (m_depth >= kMaxDepth) {
            SemanticError::template_depth_exceeded(m_reporter, file_of(site), line_of(site), generic->name);
            return nullptr;
        }

        SpecMatch spec = select_specialization(generic, args, site, quiet);
        if (spec.failed) return nullptr;
        nodes::TemplateDeclaration* chosen = spec.tmpl ? spec.tmpl : tmpl;
        auto* I   = make_in<Instantiation>(m_arena);
        I->source = chosen;
        I->args   = args;
        m_memo.emplace(std::move(key), I);

        if (spec.tmpl) {
            I->env = std::move(spec.env);
        } else {
            const auto& ps = tmpl->params();
            nodes::TemplateParameter* tpack = trailing_pack(ps);
            const std::size_t fixed = ps.size() - (tpack ? 1 : 0);

            for (std::size_t i = 0; i < fixed; ++i) {
                if (!ps[i]->symbol) continue;
                if      (args[i].is_type) I->env.types  [ps[i]->symbol] = args[i].type;
                else if (args[i].value)   I->env.values [ps[i]->symbol] = args[i].value;
                else if (args[i].fvalue)  I->env.fvalues[ps[i]->symbol] = args[i].fvalue;
            }

            if (tpack && tpack->symbol) {
                std::vector<Type*>& pk = I->env.packs[tpack->symbol];
                for (std::size_t i = fixed; i < args.size(); ++i) pk.push_back(args[i].type);
            }
        }

        ++m_depth;
        I->decl = chosen->m_declaration ? chosen->m_declaration->clone_into(m_arena) : nullptr;
        Scope* parent = chosen->scope ? chosen->scope->parent : nullptr;
        I->scope = make_in<Scope>(m_arena, Scope::Kind::Template, parent);
        if (I->decl && build_scopes) build_scopes(I->decl, I->scope);
        I->sym = entity_symbol(I->decl);
        m_scope_owner.emplace(I->scope, I);
        if (I->sym && I->sym->inner_scope) m_scope_owner.emplace(I->sym->inner_scope, I);

        if (I->decl && I->decl->kind == K::RecordDeclaration) {
            std::vector<Type*> targs;
            for (const Arg& a : args) if (a.is_type) targs.push_back(a.type);
            I->type = m_types.record(generic, std::move(targs), CV{});
        } else if (I->decl && I->decl->kind == K::FunctionDeclaration) {
            auto* fd = static_cast<nodes::FunctionDeclaration*>(I->decl);
            DiagnosticTrap trap(m_reporter);
            finish_function_type(I, fd);
            const bool trapped  = quiet ? trap.discard() : trap.commit();
            const bool poisoned = trapped || contains_error(I->type);

            if (poisoned) {
                m_memo.erase(Key{ generic, I->args });
                m_scope_owner.erase(I->scope);
                if (I->sym && I->sym->inner_scope) m_scope_owner.erase(I->sym->inner_scope);
                --m_depth;
                return nullptr;
            }
        }

        --m_depth;
        return I;
    }

    SpecMatch select_specialization(Symbol* generic, const std::vector<Arg>& args, nodes::ASTNode* site, bool quiet) {
        SpecMatch full, partial;
        bool partial_ambiguous = false;

        for (nodes::TemplateDeclaration* st : generic->specializations) {
            if (has_nontrailing_pack(st->params())) continue;          // unusable pattern, never matches
            const auto* written = spec_args_of(st);
            if (!written) continue;
            std::size_t fixed = 0;
            const parser_types::TemplateArgument* tpack = trailing_targ_pack(*written, fixed);
            if (tpack ? args.size() < fixed : args.size() != written->size()) continue;
            SubstEnv env;
            bool ok = true;

            for (std::size_t i = 0; i < fixed && ok; ++i) {
                const parser_types::TemplateArgument* w = (*written)[i];
                if (!w || !w->is_type() || !args[i].is_type) { ok = false; break; }
                ok = deduce(m_types.canonicalize(w->type), args[i].type, env, m_types);
            }

            if (ok && tpack) {
                if (!tpack->is_type()) { ok = false; }
                else {
                    std::vector<Type*> rest;
                    rest.reserve(args.size() - fixed);

                    for (std::size_t i = fixed; i < args.size(); ++i) {
                        if (!args[i].is_type) { ok = false; break; }
                        rest.push_back(args[i].type);
                    }

                    if (ok) {
                        Type* pattern = m_types.pack_expansion(m_types.canonicalize(tpack->type));
                        ok = deduce_pack(pattern, rest, env, m_types);
                    }
                }
            }

            if (!ok || !bindings_complete(st, env)) continue;

            if (st->params().empty()) {          // full specialization / exact match
                full.tmpl = st;
                full.env  = std::move(env);
                return full;
            }

            if (partial.tmpl) { partial_ambiguous = true; }
            else { partial.tmpl = st; partial.env = std::move(env); }
        }

        if (partial_ambiguous) {
            if (!quiet) SemanticError::ambiguous_specialization(m_reporter, file_of(site), line_of(site), generic->name);
            SpecMatch bad; bad.failed = true;
            return bad;
        }

        return partial;
    }

    void finish_function_type(Instantiation* I, nodes::FunctionDeclaration* fd) {
        using FQ = modifiers::FunctionQualifiers;
        m_types.push_subst(&I->env);
        I->fn_shapes.clear();
        std::vector<Type*> param_types;

        if (const nodes::FunctionParameters* ps = fd->get_parameters()) {
            I->fn_shapes.reserve(ps->size());

            for (const nodes::FunctionParameter* p : ps->m_params) {
                Type* et = m_types.canonicalize(p->get_type());
                std::vector<Symbol*> pks;
                collect_pack_params(et, pks);

                bool bound = !pks.empty();
                for (Symbol* s : pks) bound = bound && (I->env.lookup_pack(s) != nullptr);

                if (p->is_variadic() && bound) {
                    std::vector<Type*> elems;
                    if (!expand_into(m_types.pack_expansion(et), I->env, m_types, elems)) elems.assign(1, m_types.error_());

                    for (Type* t : elems) {
                        ParamShape s{};
                        s.element = t;
                        I->fn_shapes.push_back(s);
                        param_types.push_back(t);
                    }
                } else {
                    I->fn_shapes.push_back(shape_of(p, m_types));
                    param_types.push_back(I->fn_shapes.back().element);
                }
            }
        }

        Type* ret = m_types.canonicalize(fd->get_return_type());
        modifiers::FunctionQualifiers q = fd->qualifiers();
        RefQual rq = q.has(FQ::RValueRef) ? RefQual::RValue : q.has(FQ::LValueRef) ? RefQual::LValue : RefQual::None;
        I->type = m_types.function(ret, std::move(param_types), q.has(FQ::Const), rq, q.has(FQ::Noexcept));
        m_types.pop_subst();
    }

    bool flatten_packs(std::vector<Arg>& args) {
        bool any = false;
        for (const Arg& a : args) if (a.is_type && a.type && a.type->kind() == TypeKind::PackExpansion) { any = true; break; }
        if (!any) return true;
        const SubstEnv* env = m_types.active_subst();
        if (!env) return false;   
        std::vector<Arg> out;
        out.reserve(args.size());

        for (const Arg& a : args) {
            if (!a.is_type || !a.type || a.type->kind() != TypeKind::PackExpansion) { out.push_back(a); continue; }
            std::vector<Type*> elems;
            if (!expand_into(a.type, *env, m_types, elems)) return false;

            for (Type* t : elems) {
                if (t && t->kind() == TypeKind::PackExpansion) return false; 
                Arg e; e.type = t;
                out.push_back(e);
            }
        }

        args = std::move(out);
        return true;
    }

    bool eval_value_arg(nodes::ASTNode* e, Arg& a) {
        ConstValue v = m_eval.eval(e);
        switch (v.kind) {
            case ConstValue::Kind::Int:   a.is_type = false; a.value  = v.i; return true;
            case ConstValue::Kind::Float: a.is_type = false; a.fvalue = v.f; return true;
            case ConstValue::Kind::Bool:  a.is_type = false; a.value  = m_types.consts().from(std::uint64_t(v.b ? 1 : 0)); return true;
            default: return false;
        }
    }

    CoerceResult coerce_value_arg(nodes::TemplateParameter* p, Arg& a) {
        using BK = parser_types::PrimitiveType::BaseKind;
        Type* pt = m_types.strip_cv(m_types.canonicalize(p->m_type));
        if (!pt || !pt->is_builtin()) return {};                 
        auto* b = static_cast<BuiltinType*>(pt);

        if (b->base() == BK::Float) {
            const unsigned bits = bit_width_of_rank(b->width());

            WideFloat src;
            if      (a.fvalue) src = *a.fvalue;
            else if (a.value)  src = wf_from_int(*a.value);
            else               return CoerceResult::fail(ArgCoerce::NotConstant);

            WideFloat out;
            if (!wf_round_to_width(src, bits, out))      return CoerceResult::fail(ArgCoerce::NotConstant);
            if (!src.is_infinite() && out.is_infinite()) return CoerceResult::fail(ArgCoerce::Overflow);
            if (!src.is_zero()     && out.is_zero())     return CoerceResult::fail(ArgCoerce::Underflow);
            CoerceResult r;
            r.inexact = !wf_same_bits(out, src);
            a.fvalue = m_types.floats().intern(out);
            a.value  = nullptr;
            return r;
        }

        if (a.fvalue) return CoerceResult::fail(ArgCoerce::WrongForm);
        if (!a.value) return CoerceResult::fail(ArgCoerce::NotConstant);

        return ConstTable::fits(*a.value, bit_width_of_rank(b->width()), !b->is_unsigned())
             ? CoerceResult{}
             : CoerceResult::fail(ArgCoerce::Overflow);
    }

    bool complete_arguments(nodes::TemplateDeclaration* tmpl, std::vector<Arg>& args, nodes::ASTNode* site, bool quiet) {
        const auto& params = tmpl->params();
        nodes::TemplateParameter* tpack = trailing_pack(params);
        const std::size_t fixed = params.size() - (tpack ? 1 : 0);

        if (tpack && !tpack->is_type_param()) {
            if (!quiet) unsupported(site, "non-type parameter packs");
            return false;
        }

        if (!tpack && args.size() > params.size()) {
            if (!quiet) SemanticError::template_arity(m_reporter, file_of(site), line_of(site), params.size(), args.size());
            return false;
        }

        // defaults fill the FIXED prefix only; the pack tail defaults to empty
        // earlier bindings (types AND values) are visible to later defaults
        if (args.size() < fixed) {
            SubstEnv partial;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (!params[i]->symbol) continue;
                if      (args[i].is_type) partial.types  [params[i]->symbol] = args[i].type;
                else if (args[i].value)   partial.values [params[i]->symbol] = args[i].value;
                else if (args[i].fvalue)  partial.fvalues[params[i]->symbol] = args[i].fvalue;
            }

            m_types.push_subst(&partial);

            for (std::size_t i = args.size(); i < fixed; ++i) {
                nodes::TemplateParameter* p = params[i];
                Arg a;

                if (p->is_type_param() && p->m_has_default_type) {
                    a.type = m_types.canonicalize(p->m_default_type);
                    if (p->symbol) partial.types[p->symbol] = a.type;
                } else if (p->is_non_type_param() && p->m_default_value) {
                    if (!eval_value_arg(p->m_default_value, a)) {
                        m_types.pop_subst();
                        if (!quiet) unsupported(site, "non-constant default arguments");
                        return false;
                    }
                    if (p->symbol) {
                        if (a.value)  partial.values [p->symbol] = a.value;
                        if (a.fvalue) partial.fvalues[p->symbol] = a.fvalue;
                    }
                } else {
                    m_types.pop_subst();
                    if (!quiet) SemanticError::template_arity(m_reporter, file_of(site), line_of(site), params.size(), args.size());
                    return false;
                }

                args.push_back(a);
            }

            m_types.pop_subst();
        }

        for (std::size_t i = 0; i < fixed; ++i) {
            if (params[i]->is_type_param() != args[i].is_type) {
                if (!quiet) SemanticError::template_arg_form(m_reporter, file_of(site), line_of(site), i);
                return false;
            }

            if (!args[i].is_type) {
                const CoerceResult r = coerce_value_arg(params[i], args[i]);

                if (!r.ok()) {
                    if (!quiet) {
                        const std::string shown =
                            args[i].value  ? args[i].value->to_string()
                          : args[i].fvalue ? args[i].fvalue->to_string()
                          :                  "<value>";

                        const std::string pt = type_str_of(m_types.strip_cv(m_types.canonicalize(params[i]->m_type)));

                        switch (r.status) {
                            case ArgCoerce::Overflow:
                                SemanticError::template_arg_overflow(m_reporter, file_of(site), line_of(site), i, shown, pt);
                                break;
                            case ArgCoerce::Underflow:
                                SemanticError::template_arg_underflow(m_reporter, file_of(site), line_of(site), i, shown, pt);
                                break;
                            default:
                                SemanticError::template_arg_out_of_range(
                                    m_reporter, file_of(site), line_of(site), i, shown);
                                break;
                        }
                    }

                    return false;
                }

                if (r.inexact && !quiet) {
                    std::stringstream ss;
                    ss << "template argument " << i << " is not representable in '"
                       << type_str_of(m_types.strip_cv(m_types.canonicalize(params[i]->m_type)))
                       << "' and was rounded to " << args[i].fvalue->to_string();

                    SemanticWarning::emit(m_warnings, file_of(site), line_of(site), "Template", ss.str());
                }
            }
        }

        for (std::size_t i = fixed; i < args.size(); ++i) {
            if (!args[i].is_type) {
                if (!quiet) SemanticError::template_arg_form(m_reporter, file_of(site), line_of(site), i);
                return false;
            }
        }

        if (tmpl->m_declaration && tmpl->m_declaration->kind == nodes::ASTNode::Kind::RecordDeclaration) {
            for (const Arg& a : args) {
                if (!a.is_type) { if (!quiet) unsupported(site, "non-type arguments on record templates"); return false; }
            }
        }

        return true;
    }

    bool assemble_args(nodes::TemplateDeclaration* tmpl, const SubstEnv& env, std::vector<Arg>& out, nodes::ASTNode* site, bool quiet) {
        const auto& params = tmpl->params();
        nodes::TemplateParameter* tpack = trailing_pack(params);
        const std::size_t fixed = params.size() - (tpack ? 1 : 0);

        for (std::size_t i = 0; i < fixed; ++i) {
            nodes::TemplateParameter* p = params[i];
            Arg a;

            if (p->is_type_param()) {
                Type* t = p->symbol ? env.lookup_type(p->symbol) : nullptr;
                if (!t) break;
                a.type = t;
            } else {
                if (const WideInt* v = p->symbol ? env.lookup_value(p->symbol) : nullptr) {
                    a.is_type = false; a.value = v;
                } else if (const WideFloat* f = p->symbol ? env.lookup_fvalue(p->symbol) : nullptr) {
                    a.is_type = false; a.fvalue = f;
                } else break;
            }

            out.push_back(a);
        }

        if (out.size() == fixed && tpack && tpack->symbol) {
            if (const std::vector<Type*>* pk = env.lookup_pack(tpack->symbol)) {
                for (Type* t : *pk) { Arg a; a.type = t; out.push_back(a); }
            }
        }

        return complete_arguments(tmpl, out, site, quiet);
    }

    bool bind_written(nodes::TemplateParameter* p, const parser_types::TemplateArgument* w, SubstEnv& env) {
        if (!p || !w || !p->symbol) return false;

        if (p->is_type_param()) {
            if (!w->is_type()) return false;
            env.types[p->symbol] = m_types.canonicalize(w->type);
            return true;
        }

        if (!w->is_value()) return false;
        Arg a;
        if (!eval_value_arg(w->value, a)) return false;
        if (!coerce_value_arg(p, a).ok()) return false;
        if (a.value)  env.values [p->symbol] = a.value;
        if (a.fvalue) env.fvalues[p->symbol] = a.fvalue;
        return true;
    }

    static nodes::TemplateParameter* trailing_pack(const std::vector<nodes::TemplateParameter*>& ps) {
        return (!ps.empty() && ps.back()->is_pack()) ? ps.back() : nullptr;
    }

    static bool has_nontrailing_pack(const std::vector<nodes::TemplateParameter*>& ps) {
        for (std::size_t i = 0; i + 1 < ps.size(); ++i) if (ps[i]->is_pack()) return true;
        return false;
    }

    static Symbol* entity_symbol(nodes::ASTNode* d) {
        using K = nodes::ASTNode::Kind;
        if (!d) return nullptr;

        switch (d->kind) {
            case K::RecordDeclaration:   return static_cast<nodes::RecordDeclaration*>(d)->symbol;
            case K::FunctionDeclaration: return static_cast<nodes::FunctionDeclaration*>(d)->symbol;
            default: return nullptr;
        }
    }

    static const std::vector<parser_types::TemplateArgument*>* spec_args_of(nodes::TemplateDeclaration* t) {
        nodes::ASTNode* d = t->m_declaration;
        if (!d) return nullptr;

        if (d->kind == nodes::ASTNode::Kind::RecordDeclaration) {
            auto* r = static_cast<nodes::RecordDeclaration*>(d);
            return r->is_specialization() ? &r->get_spec_args() : nullptr;
        }

        if (d->kind == nodes::ASTNode::Kind::FunctionDeclaration) {
            auto* f = static_cast<nodes::FunctionDeclaration*>(d);
            return f->is_specialization() ? &f->get_spec_args() : nullptr;
        }

        return nullptr;
    }

    static const parser_types::TemplateArgument* trailing_targ_pack(
        const std::vector<parser_types::TemplateArgument*>& written, std::size_t& fixed_out
    ) {
        fixed_out = written.size();
        if (written.empty()) return nullptr;
        const parser_types::TemplateArgument* last = written.back();
        if (!last || !last->is_pack) return nullptr;
        fixed_out = written.size() - 1;
        return last;
    }

    bool bindings_complete(nodes::TemplateDeclaration* st, const SubstEnv& env) {
        for (nodes::TemplateParameter* p : st->params()) {
            if (!p->symbol) return false;
            if (p->m_is_pack)            { if (!env.lookup_pack(p->symbol)) return false; }
            else if (p->is_type_param()) { if (!env.lookup_type(p->symbol)) return false; }
            else                         { return false; }   // non-type params
        }
        return true;
    }

    void unsupported(nodes::ASTNode* site, const char* what) {
        SemanticError::template_unsupported(m_reporter, file_of(site), line_of(site), what);
    }

    static FileId        file_of(const nodes::ASTNode* n) { return n ? n->file_id : FileId{}; }
    static std::uint32_t line_of(const nodes::ASTNode* n) { return n ? n->line : 0; }

private:
    Arena&         m_arena;
    TypeContext&   m_types;
    ErrorReporter& m_reporter;
    WarningReporter& m_warnings;
    ConstEvaluator m_eval;

    std::unordered_map<Key, Instantiation*, KeyHash> m_memo;
    std::unordered_map<Scope*, Instantiation*>       m_scope_owner;
    int m_depth = 0;
    static constexpr int kMaxDepth = 256;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_INSTANTIATOR_HPP