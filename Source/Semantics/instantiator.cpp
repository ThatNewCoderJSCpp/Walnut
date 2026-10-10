#include "Semantics/instantiator.hpp"

namespace walnut {
namespace semantics {

Instantiator::Instantiator(Arena& arena, TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings) : m_arena(arena), m_types(types), m_reporter(reporter), m_warnings(warnings), m_eval(types)
{
    m_types.value_arg_hook  = [this](nodes::ASTNode* e, TemplateArg& out) { return classify_value_arg(e, out); };
    m_types.value_eval_hook = [this](nodes::ASTNode* e, TemplateArg& out) { return eval_value_arg(e, out); };
    m_types.value_symbol_hook = [this](Symbol* s, TemplateArg& out) { return classify_value_symbol(s, out); };
    m_types.member_type_hook = [this](Type* rec, std::string_view name) -> Type* { return member_type(rec, name); };
    m_types.callable_hook = [this](Type* rec, Type* sig) -> Symbol* { return call_operator_for(rec, sig); };
    m_types.extent_param_hook = [](nodes::ASTNode* e) -> Symbol* {
        if (!e || e->kind != nodes::ASTNode::Kind::Identifier) return nullptr;
        Symbol* s = static_cast<nodes::Identifier*>(e)->resolved;
        return is_value_param(s) ? s : nullptr;
    };
    m_types.instance_primary_hook = [this](Symbol* s) -> Symbol* { return primary_of_instance(s); };
    m_types.default_instance_hook = [this](Symbol* s) -> Type* {
        Instantiation* I = instantiate_impl(s, {}, nullptr, false);
        return (I && I->type) ? I->type : nullptr;
    };
    m_types.instance_type_hook = [this](Symbol* s) -> Type* {
        Instantiation* I = owning(s);
        return (I && I->sym == s && I->type && I->type->is_record()) ? I->type : nullptr;
    };
    m_types.record_scope_hook = [this](RecordType* rt) -> Scope* {
        Instantiation* I = for_record(rt, nullptr);
        return (I && I->sym) ? I->sym->inner_scope : nullptr;
    };
}

auto Instantiator::for_record(RecordType* rt, nodes::ASTNode* site) -> Instantiation* {
    if (!rt || !rt->is_instantiation()) return nullptr;
    std::vector<Arg> args;
    args.reserve(rt->args().size());

    for (const TemplateArg& a : rt->args()) {
        if (a.is_dependent_value()) return nullptr;
        args.push_back(a);
    }

    return instantiate_impl(rt->decl(), std::move(args), site, false);
}

auto Instantiator::instantiate_for_call(
    Symbol* generic,
    const std::vector<parser_types::TemplateArgument*>& explicit_args,
    const std::vector<Type*>& call_args,
    nodes::ASTNode* site
) -> Instantiation* {
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

    struct SuspendGuard {
        TypeContext& t; std::vector<const SubstEnv*> saved;
        explicit SuspendGuard(TypeContext& tc) : t(tc), saved(tc.suspend_subst()) {}
        ~SuspendGuard() { t.resume_subst(std::move(saved)); }
    };

    if (const nodes::FunctionParameters* ps = fd->get_parameters()) {
        SuspendGuard suspend(m_types);
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

auto Instantiator::canon_args(const std::vector<parser_types::TemplateArgument*>& written, bool& ok) -> std::vector<Arg> {
    std::vector<Arg> out;
    out.reserve(written.size());
    ok = true;

    for (const parser_types::TemplateArgument* w : written) {
        Arg a;

        if (w && w->is_type() && !w->is_pack && is_value_symbol(w->type.resolved)
            && w->type.indirection.empty() && w->type.template_args.empty()) {
            if (!const_to_arg(m_eval.value_of(w->type.resolved), a)) { ok = false; continue; }
        } else if (w && w->is_type()) {
            Type* t = m_types.canonicalize(w->type);
            a.type = w->is_pack ? m_types.pack_expansion(t) : t;
        } else if (w && w->is_value()) {
            if (!eval_value_arg(w->value, a)) { ok = false; continue; }
        }

        out.push_back(a);
    }

    return out;
}

void Instantiator::refresh_function_type(Instantiation* I) {
    if (!I || !I->decl || I->decl->kind != nodes::ASTNode::Kind::FunctionDeclaration) return;
    finish_function_type(I, static_cast<nodes::FunctionDeclaration*>(I->decl));
}

auto Instantiator::owning(Symbol* s) const -> Instantiation* {
    for (Scope* sc = s ? s->owner : nullptr; sc; sc = sc->parent) {
        if (auto it = m_scope_owner.find(sc); it != m_scope_owner.end()) return it->second;
    }
    return nullptr;
}

std::size_t Instantiator::KeyHash::operator()(const Key& k) const {
    std::size_t h = std::hash<const void*>{}(k.generic);

    for (const Arg& a : k.args) {
        h = h * 1000003u ^ std::hash<const void*>{}(a.type);
        h = h * 1000003u ^ (std::hash<const void*>{}(a.value) + std::size_t(a.is_type));
        h = h * 1000003u ^ std::hash<const void*>{}(a.fvalue);
    }

    return h;
}

Symbol* Instantiator::primary_of_instance(Symbol* s) const {
    Instantiation* I = owning(s);
    if (!I || I->sym != s || !I->source || !I->source->m_declaration) return s;
    nodes::ASTNode* d = I->source->m_declaration;
    if (d->kind == nodes::ASTNode::Kind::RecordDeclaration) { if (Symbol* p = static_cast<nodes::RecordDeclaration*>(d)->symbol) return p; }
    if (d->kind == nodes::ASTNode::Kind::FunctionDeclaration) { if (Symbol* p = static_cast<nodes::FunctionDeclaration*>(d)->symbol) return p; }
    return s;
}

auto Instantiator::instantiate_impl(Symbol* generic, std::vector<Arg> args, nodes::ASTNode* site, bool quiet) -> Instantiation* {
    using K = nodes::ASTNode::Kind;
    if (generic && !generic->template_decl) generic = primary_of_instance(generic);
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

    if (Instantiation* outer = owning(generic); outer && outer != I) {
        for (const auto& kv : outer->env.types)   I->env.types.emplace(kv.first, kv.second);
        for (const auto& kv : outer->env.values)  I->env.values.emplace(kv.first, kv.second);
        for (const auto& kv : outer->env.fvalues) I->env.fvalues.emplace(kv.first, kv.second);
        for (const auto& kv : outer->env.packs)   I->env.packs.emplace(kv.first, kv.second);
    }

    if (check_constraints && !check_constraints(chosen, I->env, site, quiet)) {
        m_memo.erase(Key{ generic, I->args });
        ++constraint_failures;
        return nullptr;
    }

    ++m_depth;
    I->decl = chosen->m_declaration ? chosen->m_declaration->clone_into(m_arena) : nullptr;
    Scope* parent = chosen->scope;
    I->scope = make_in<Scope>(m_arena, Scope::Kind::Template, parent);
    if (I->decl && build_scopes) build_scopes(I->decl, I->scope);
    if (I->decl && resolve_names) resolve_names(I->decl, I->scope);
    I->sym = entity_symbol(I->decl);
    m_scope_owner.emplace(I->scope, I);
    if (I->sym && I->sym->inner_scope) m_scope_owner.emplace(I->sym->inner_scope, I);

    if (I->decl && I->decl->kind == K::RecordDeclaration) {
        I->type = m_types.record(generic, TemplateArgs(args.begin(), args.end()), CV{});
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

auto Instantiator::select_specialization(Symbol* generic, const std::vector<Arg>& args, nodes::ASTNode* site, bool quiet) -> SpecMatch {
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
            if (!w) { ok = false; break; }

            if (w->is_type() && !(args[i].is_type == false && is_value_symbol(w->type.resolved))) {
                if (!args[i].is_type) { ok = false; break; }
                ok = deduce(m_types.canonicalize(w->type), args[i].type, env, m_types);
            } else if (w->is_type()) {
                TemplateArg pattern;
                if (classify_value_symbol(w->type.resolved, pattern) <= 0) { ok = false; break; }
                ok = deduce_template_arg(pattern, args[i], env, m_types);
            } else {
                TemplateArg pattern;
                if (!w->value || !classify_value_arg(w->value, pattern)) { ok = false; break; }
                ok = deduce_template_arg(pattern, args[i], env, m_types);
            }
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
        if (!pattern_matches(*written, fixed, args, env)) continue;

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

void Instantiator::finish_function_type(Instantiation* I, nodes::FunctionDeclaration* fd) {
    using FQ = modifiers::FunctionQualifiers;
    m_types.push_subst(&I->env);
    I->fn_shapes.clear();
    std::vector<Type*> param_types;

    if (const nodes::FunctionParameters* ps = fd->get_parameters()) {
        I->fn_shapes.reserve(ps->size());

        for (nodes::FunctionParameter* p : ps->m_params) {
            Type* et = m_types.canonicalize(p->get_type());
            std::vector<Symbol*> pks;
            collect_pack_params(et, pks);

            bool bound = !pks.empty();
            for (Symbol* s : pks) bound = bound && (I->env.lookup_pack(s) != nullptr);

            if (p->is_variadic() && bound) {
                std::vector<Type*> elems;
                if (!expand_into(m_types.pack_expansion(et), I->env, m_types, elems)) elems.assign(1, m_types.error_());
                p->expanded_pack = true;
                p->pack_symbols.clear();

                for (Type* t : elems) {
                    ParamShape s{};
                    s.element = t;
                    I->fn_shapes.push_back(s);
                    param_types.push_back(t);
                    m_pack_names.push_back(std::string(p->get_name()) + "#" + std::to_string(p->pack_symbols.size()));
                    auto* es = make_in<Symbol>(m_arena, std::string_view(m_pack_names.back()), SymbolKind::Parameter, p);
                    es->owner = p->symbol ? p->symbol->owner : nullptr;
                    es->bound_type = t;
                    p->pack_symbols.push_back(es);
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

bool Instantiator::flatten_packs(std::vector<Arg>& args) {
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

Symbol* Instantiator::template_param_of(nodes::ASTNode* e) {
    if (!e) return nullptr;
    Symbol* s = nullptr;
    if      (e->kind == nodes::ASTNode::Kind::Identifier)          s = static_cast<nodes::Identifier*>(e)->resolved;
    else if (e->kind == nodes::ASTNode::Kind::QualifiedIdentifier) s = static_cast<nodes::QualifiedIdentifier*>(e)->resolved;
    return (s && s->kind == SymbolKind::TemplateParam) ? s : nullptr;
}

bool Instantiator::refers_to_template_param(nodes::ASTNode* e) {
    using NK = nodes::ASTNode::Kind;
    if (!e) return false;
    if (template_param_of(e)) return true;

    switch (e->kind) {
        case NK::UnaryExpression:      return refers_to_template_param(static_cast<nodes::UnaryExpression*>(e)->operand);
        case NK::BitwiseNotExpression: return refers_to_template_param(static_cast<nodes::BitwiseNotExpression*>(e)->operand);
        case NK::CastExpression:       return refers_to_template_param(static_cast<nodes::CastExpression*>(e)->operand);

        case NK::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);
            return refers_to_template_param(b->left) || refers_to_template_param(b->right);
        }

        case NK::TernaryExpression: {
            auto* t = static_cast<nodes::TernaryExpression*>(e);
            return refers_to_template_param(t->condition) || refers_to_template_param(t->true_branch) || refers_to_template_param(t->false_branch);
        }

        default: return false;
    }
}

bool Instantiator::classify_value_arg(nodes::ASTNode* e, TemplateArg& out) {
    if (Symbol* p = template_param_of(e)) { out = TemplateArg::dependent(p, e); return true; }
    if (refers_to_template_param(e))      { out = TemplateArg::dependent(nullptr, e); return true; }
    return eval_value_arg(e, out);
}

Symbol* Instantiator::call_operator_for(Type* rec, Type* sig) {
    rec = m_types.strip_cv(rec);
    if (!rec || !rec->is_record() || !sig || !sig->is_function()) return nullptr;
    auto* rt = static_cast<RecordType*>(rec);
    auto* ft = static_cast<FunctionType*>(sig);
    Instantiation* I = rt->is_instantiation() ? for_record(rt, nullptr) : nullptr;
    Symbol* owner = I ? I->sym : rt->decl();
    Scope* sc = owner ? owner->inner_scope : nullptr;
    if (!sc) return nullptr;
    if (m_call_probe.count(rec)) return nullptr;
    m_call_probe.insert(rec);
    struct Done { std::unordered_set<Type*>& s; Type* t; ~Done() { s.erase(t); } } done{ m_call_probe, rec };

    for (Symbol* o = sc->find_member("operator"); o; o = o->next_overload) {
        if (!o->decl || o->decl->kind != nodes::ASTNode::Kind::OperatorFunctionDeclaration) continue;
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(o->decl);
        if (od->get_overload() != nodes::OverloadableOperator::Call || od->is_conversion()) continue;
        const nodes::FunctionParameters* ps = od->get_parameters();
        const std::size_t np = ps ? ps->m_params.size() : 0;
        if (np != ft->params().size()) continue;
        Instantiation* oi = owning(o);
        if (oi) m_types.push_subst(&oi->env);
        bool ok = true;

        for (std::size_t i = 0; i < np && ok; ++i) {
            if (ps->m_params[i]->is_variadic()) { ok = false; break; }
            ok = rank_conversion(ft->params()[i], m_types.canonicalize(ps->m_params[i]->get_type()), m_types) != ConversionRank::None;
        }

        Type* ret = m_types.canonicalize(od->get_return_type());
        if (oi) m_types.pop_subst();
        Type* want = ft->ret();
        const bool void_want = want && want->is_builtin() && static_cast<BuiltinType*>(want)->is_void();
        if (ok && !void_want) ok = ret && rank_conversion(ret, want, m_types) != ConversionRank::None;
        if (ok) return o;
    }

    return nullptr;
}

Type* Instantiator::member_type(Type* rec, std::string_view name) {
    rec = m_types.strip_cv(rec);
    if (!rec || !rec->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(rec);
    Instantiation* I = rt->is_instantiation() ? for_record(rt, nullptr) : nullptr;
    Symbol* owner = I ? I->sym : rt->decl();
    Scope* sc = owner ? owner->inner_scope : nullptr;
    Symbol* m = sc ? sc->find_member(name) : nullptr;
    if (!m) return nullptr;

    switch (m->kind) {
        case SymbolKind::TypeAlias: {
            if (!m->type) return nullptr;
            if (I) m_types.push_subst(&I->env);
            Type* t = m_types.canonicalize(*m->type);
            if (I) m_types.pop_subst();
            return t;
        }
        case SymbolKind::Type:
            if (m->template_decl) return nullptr;
            if (m_types.instance_type_hook) if (Type* t = m_types.instance_type_hook(m)) return t;
            return m_types.record(m, {}, CV{});
        case SymbolKind::Enum:
            return m_types.enum_(m, CV{});
        default:
            return nullptr;
    }
}

bool Instantiator::is_value_param(Symbol* s) {
    if (!s || s->kind != SymbolKind::TemplateParam || !s->decl || s->decl->kind != nodes::ASTNode::Kind::TemplateParameter) return false;
    return static_cast<nodes::TemplateParameter*>(s->decl)->is_non_type_param();
}

bool Instantiator::is_value_symbol(Symbol* s) {
    return s && (is_value_param(s) || s->kind == SymbolKind::Variable || s->kind == SymbolKind::EnumConstant);
}

int Instantiator::classify_value_symbol(Symbol* s, TemplateArg& out) {
    if (!is_value_symbol(s)) return 0;
    if (is_value_param(s)) { out = TemplateArg::dependent(s, nullptr); return 1; }
    return const_to_arg(m_eval.value_of(s), out) ? 1 : -1;
}

bool Instantiator::const_to_arg(const ConstValue& v, Arg& a) {
    switch (v.kind) {
        case ConstValue::Kind::Int:   a.is_type = false; a.value  = v.i; return true;
        case ConstValue::Kind::Float: a.is_type = false; a.fvalue = v.f; return true;
        case ConstValue::Kind::Bool:  a.is_type = false; a.value  = m_types.consts().from(std::uint64_t(v.b ? 1 : 0)); return true;
        default: return false;
    }
}

auto Instantiator::coerce_value_arg(nodes::TemplateParameter* p, Arg& a) -> CoerceResult {
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

bool Instantiator::complete_arguments(nodes::TemplateDeclaration* tmpl, std::vector<Arg>& args, nodes::ASTNode* site, bool quiet) {
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

    return true;
}

bool Instantiator::assemble_args(nodes::TemplateDeclaration* tmpl, const SubstEnv& env, std::vector<Arg>& out, nodes::ASTNode* site, bool quiet) {
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

bool Instantiator::bind_written(nodes::TemplateParameter* p, const parser_types::TemplateArgument* w, SubstEnv& env) {
    if (!p || !w || !p->symbol) return false;

    if (p->is_type_param()) {
        if (!w->is_type()) return false;
        env.types[p->symbol] = m_types.canonicalize(w->type);
        return true;
    }

    Arg a;

    if (w->is_type()) {
        if (w->is_pack || !is_value_symbol(w->type.resolved) || !w->type.indirection.empty() || !w->type.template_args.empty()) return false;
        if (!const_to_arg(m_eval.value_of(w->type.resolved), a)) return false;
    } else {
        if (!w->is_value()) return false;
        if (!eval_value_arg(w->value, a)) return false;
    }

    if (!coerce_value_arg(p, a).ok()) return false;
    if (a.value)  env.values [p->symbol] = a.value;
    if (a.fvalue) env.fvalues[p->symbol] = a.fvalue;
    return true;
}

Symbol* Instantiator::entity_symbol(nodes::ASTNode* d) {
    using K = nodes::ASTNode::Kind;
    if (!d) return nullptr;

    switch (d->kind) {
        case K::RecordDeclaration:   return static_cast<nodes::RecordDeclaration*>(d)->symbol;
        case K::FunctionDeclaration: return static_cast<nodes::FunctionDeclaration*>(d)->symbol;
        default: return nullptr;
    }
}

const std::vector<parser_types::TemplateArgument*>* Instantiator::spec_args_of(nodes::TemplateDeclaration* t) {
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

const parser_types::TemplateArgument* Instantiator::trailing_targ_pack(
    const std::vector<parser_types::TemplateArgument*>& written, std::size_t& fixed_out
) {
    fixed_out = written.size();
    if (written.empty()) return nullptr;
    const parser_types::TemplateArgument* last = written.back();
    if (!last || !last->is_pack) return nullptr;
    fixed_out = written.size() - 1;
    return last;
}

bool Instantiator::pattern_matches(const std::vector<parser_types::TemplateArgument*>& written, std::size_t fixed, const std::vector<Arg>& args, const SubstEnv& env) {
    for (std::size_t i = 0; i < fixed && i < args.size(); ++i) {
        const parser_types::TemplateArgument* w = written[i];
        if (!w) return false;
        TemplateArg pattern;

        if (w->is_type() && !is_value_symbol(w->type.resolved)) {
            pattern = TemplateArg::of_type(m_types.canonicalize(w->type));
        } else if (w->is_type()) {
            if (classify_value_symbol(w->type.resolved, pattern) <= 0) return false;
        } else if (!w->value || !classify_value_arg(w->value, pattern)) {
            return false;
        }

        if (pattern.is_type) {
            if (subst(pattern.type, env, m_types) != args[i].type) return false;
            continue;
        }

        TemplateArg resolved;
        if (pattern.is_dependent_value()) { subst_value_arg(pattern, env, m_types, resolved); }
        else                              { resolved = pattern; }
        if (resolved.is_dependent_value() || resolved.value != args[i].value || resolved.fvalue != args[i].fvalue) return false;
    }

    return true;
}

bool Instantiator::bindings_complete(nodes::TemplateDeclaration* st, const SubstEnv& env) {
    for (nodes::TemplateParameter* p : st->params()) {
        if (!p->symbol) return false;
        if (p->m_is_pack)            { if (!env.lookup_pack(p->symbol)) return false; }
        else if (p->is_type_param()) { if (!env.lookup_type(p->symbol)) return false; }
        else if (!env.lookup_value(p->symbol) && !env.lookup_fvalue(p->symbol)) { return false; }
    }
    return true;
}

} // namespace semantics
} // namespace walnut
