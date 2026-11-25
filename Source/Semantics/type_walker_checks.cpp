#include "Semantics/type_walker.hpp"
#include "Semantics/type_ops.hpp"
#include "Semantics/substitution.hpp"
#include "Common/arena_allocator.hpp"
#include "Parser/Nodes/node_children.hpp"
#include <unordered_set>
#include <algorithm>
#include <deque>
#include "Lexer/numeric_literal.hpp"

namespace walnut {
namespace semantics {

bool TypeWalker::binds_nonconst_lvalue_ref_to_rvalue(nodes::ASTNode* expr, Type* from, Type* to) {
    if (!to || !to->is_reference() || !expr || !from || is_dynamic_t(from) || from->is_error()) return false;
    auto* rt = static_cast<ReferenceType*>(to);
    return rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const && !is_lvalue(expr);
}

bool TypeWalker::is_plain_literal(const nodes::ASTNode* n) {
    if (!n) return false;
    if (n->kind == K::UnaryExpression) {
        auto* u = static_cast<const nodes::UnaryExpression*>(n);
        if (u->op != tokenizing::Token::Kind::Minus || u->resolved) return false;
        n = u->operand;
    }
    if (!n || n->kind != K::Literal) return false;
    const auto k = static_cast<const nodes::Literal*>(n)->get_kind();
    return k == tokenizing::Token::Kind::Integer || k == tokenizing::Token::Kind::Float;
}

Type* TypeWalker::adapt_literal(nodes::ASTNode* lit, Type* lit_type, Type* other) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!is_plain_literal(lit)) return nullptr;
    Type* lt = m_types.strip_cv(lit_type);
    Type* ot = m_types.strip_cv(other && other->is_reference() ? static_cast<ReferenceType*>(other)->referent() : other);
    if (!lt || !ot || lt == ot || !lt->is_builtin() || !ot->is_builtin()) return nullptr;
    const BK lb = static_cast<BuiltinType*>(lt)->base();
    const BK ob = static_cast<BuiltinType*>(ot)->base();
    if (!((lb == BK::Int && (ob == BK::Int || ob == BK::Float)) || (lb == BK::Float && ob == BK::Float))) return nullptr;
    ConstValue v = m_eval.eval(lit);
    if (!v.ok()) return nullptr;
    ConstValue fitted = m_eval.coerce(v, ot);
    if (!fitted.ok() || fitted.fail == ConstValue::Fail::Overflow || fitted.fail == ConstValue::Fail::Underflow) return nullptr;
    return ot;
}

bool TypeWalker::constant_fits(nodes::ASTNode* expr, Type* from, Type* to) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!expr || !from || !to || to->is_reference()) return false;
    Type* f = m_types.strip_cv(from);
    Type* t = m_types.strip_cv(to);
    if (!f->is_builtin() || !t->is_builtin()) return false;
    const BK fb = static_cast<BuiltinType*>(f)->base();
    const BK tb = static_cast<BuiltinType*>(t)->base();
    const bool same_family = (fb == tb) && (fb == BK::Int || fb == BK::Float);
    const bool int_to_float = fb == BK::Int && tb == BK::Float;
    if (!same_family && !int_to_float) return false;
    if (rank_conversion(from, to, m_types) != ConversionRank::None) return false;
    ConstValue v = m_eval.eval(expr);
    if (!v.ok()) return false;
    ConstValue fitted = m_eval.coerce(v, t);
    return fitted.ok() && fitted.fail != ConstValue::Fail::Overflow && fitted.fail != ConstValue::Fail::Underflow;
}

bool TypeWalker::is_string_view_t(Type* t) {
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == parser_types::PrimitiveType::BaseKind::StringView;
}

Type* TypeWalker::owning_string(Type* t) {
    if (is_string_view_t(t)) return m_types.builtin(parser_types::PrimitiveType::BaseKind::String);
    return t;
}

void TypeWalker::check_view_source(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site, bool returning) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!expr || !from || !to || to->is_reference() || !is_string_view_t(to)) return;
    if (is_dynamic_t(from)) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "a var can't be turned into a string_view, because the string it holds could change or go away; convert it to a string first");
        return;
    }
    if (from->is_reference()) return;
    Type* fv = m_types.strip_cv(from);
    if (!fv || !fv->is_builtin()) return;
    const BK fb = static_cast<BuiltinType*>(fv)->base();
    if (fb != BK::String && fb != BK::Text) return;
    if (expr->kind == nodes::ASTNode::Kind::Literal) return;
    if (!is_lvalue(expr)) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "this string_view would view a temporary string that is destroyed at the end of the statement; store the string in a variable first, or make this a string");
        return;
    }
    if (!returning || expr->kind != nodes::ASTNode::Kind::Identifier) return;
    const Symbol* s = static_cast<nodes::Identifier*>(expr)->resolved;
    if (!s || !s->owner) return;
    const bool local = s->kind == SymbolKind::Parameter
        || (s->kind == SymbolKind::Variable && (s->owner->kind == Scope::Kind::Function || s->owner->kind == Scope::Kind::Block || s->owner->kind == Scope::Kind::Lambda));
    if (local) SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "this returns a string_view of '" + std::string(s->name) + "', which is destroyed when the function returns; return a string instead");
}

void TypeWalker::check_binding(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site, bool check_rvalue_refs) {
    if (!from || !to) return;
    if (constant_fits(expr, from, to)) return;

    if (is_dynamic_t(to) && !to->is_reference() && !from->is_error() && !from->is_dependent() && rank_conversion(from, to, m_types) == ConversionRank::None) {
        report_not_dynamic(from, site);
        return;
    }

    if (to->is_reference() && expr && !is_dynamic_t(from) && !from->is_error()) {
        auto* rt = static_cast<ReferenceType*>(to);

        if (binds_nonconst_lvalue_ref_to_rvalue(expr, from, to)) {
            SemanticError::bad_reference_binding(m_reporter, file_of(site), site->line, type_str(to), true);
            return;
        }

        if (check_rvalue_refs && rt->ref_qual() == RefQual::RValue && is_lvalue(expr)
            && m_types.strip_cv(from) == m_types.strip_cv(rt->referent())) {
            SemanticError::bad_reference_binding(m_reporter, file_of(site), site->line, type_str(to), false);
            return;
        }
    }

    struct SourceGuard {
        TypeContext& t; bool saved;
        SourceGuard(TypeContext& c, bool lv) : t(c), saved(c.conversion_source_lvalue) { c.conversion_source_lvalue = lv; }
        ~SourceGuard() { t.conversion_source_lvalue = saved; }
    } source_guard(m_types, expr && is_lvalue(expr));

    if (m_types.conversion_source_lvalue && !from->is_error() && !to->is_error() && rank_conversion(from, to, m_types) == ConversionRank::None) {
        m_types.conversion_source_lvalue = false;
        Symbol* via = nullptr;
        const bool moved_ok = rank_user_defined(from, to, m_types, false, &via) != ConversionRank::None && rvalue_only_conversion(via);
        m_types.conversion_source_lvalue = true;
        if (moved_ok) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "'" + type_str(from) + "' can only be converted to '" + type_str(to) + "' by moving it, because the conversion takes the value over; write 'std::move(...)' around it");
            return;
        }
    }

    check_copy_or_move(expr, from, to, site);

    check_convertible(from, to, site);
}

std::string_view TypeWalker::template_name_of(nodes::TemplateDeclaration* tmpl) {
    nodes::ASTNode* d = tmpl ? tmpl->m_declaration : nullptr;
    if (!d) return "<template>";
    switch (d->kind) {
        case K::FunctionDeclaration: return static_cast<nodes::FunctionDeclaration*>(d)->get_name();
        case K::RecordDeclaration:   return static_cast<nodes::RecordDeclaration*>(d)->get_name();
        case K::ConceptDeclaration:  return static_cast<nodes::ConceptDeclaration*>(d)->get_name();
        default:                     return "<template>";
    }
}

bool TypeWalker::concept_satisfied(Symbol* cept, const TemplateArgs& args) {
    if (!cept || cept->kind != SymbolKind::Concept || !cept->decl || cept->decl->kind != K::ConceptDeclaration) return false;
    nodes::TemplateDeclaration* tmpl = cept->template_decl;
    if (!tmpl || m_constraint_depth > 64) return false;
    const auto& ps = tmpl->params();
    const bool has_pack = !ps.empty() && ps.back()->m_is_pack;
    if (!has_pack && args.size() > ps.size()) return false;
    SubstEnv env;

    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (!ps[i]->symbol) return false;
        if (has_pack && i + 1 == ps.size()) {
            std::vector<Type*> rest;
            for (std::size_t k = i; k < args.size(); ++k) {
                if (!args[k].is_type || !args[k].type) return false;
                rest.push_back(args[k].type);
            }
            env.packs[ps[i]->symbol] = std::move(rest);
            break;
        }
        if (i >= args.size()) {
            if (!ps[i]->m_has_default_type) return false;
            m_types.push_subst(&env);
            Type* d = m_types.canonicalize(ps[i]->m_default_type);
            m_types.pop_subst();
            if (!d || d->is_error()) return false;
            env.types[ps[i]->symbol] = d;
            continue;
        }
        const TemplateArg& a = args[i];
        if      (a.is_type)  env.types  [ps[i]->symbol] = a.type;
        else if (a.value)    env.values [ps[i]->symbol] = a.value;
        else if (a.fvalue)   env.fvalues[ps[i]->symbol] = a.fvalue;
        else if (a.svalue)   env.svalues[ps[i]->symbol] = a.svalue;
        else return false;
    }

    ++m_constraint_depth;
    m_types.push_subst(&env);
    Scope* saved = m_current;
    if (tmpl->scope) m_current = tmpl->scope;
    std::string ignored;
    const bool ok = constraint_holds(static_cast<nodes::ConceptDeclaration*>(cept->decl)->constraint, ignored);
    m_current = saved;
    m_types.pop_subst();
    --m_constraint_depth;
    return ok;
}

bool TypeWalker::concept_args(nodes::TemplateInstantiation* t, Symbol*& cept, TemplateArgs& args) {
    cept = callee_symbol(t->m_template);
    if (cept && cept->kind == SymbolKind::Import && cept->import_target) cept = cept->import_target;
    if (!cept || cept->kind != SymbolKind::Concept || !m_inst) return false;
    bool ok = true;
    std::vector<Instantiator::Arg> raw = m_inst->canon_args(t->m_args, ok);
    if (!ok) return false;
    args.assign(raw.begin(), raw.end());

    for (const TemplateArg& a : args) {
        if (a.is_type && (!a.type || a.type->is_dependent())) return false;
        if (a.is_dependent_value()) return false;
    }

    return true;
}

void TypeWalker::rebind_inherited(nodes::QualifiedIdentifier* q) {
    Symbol* s = q->resolved;
    if (!s || !s->owner || s->owner->kind != Scope::Kind::Record) return;
    const auto& parts = q->parts();
    if (parts.size() < 2) return;
    std::vector<std::string_view> prefix(parts.begin(), parts.end() - 1);
    Symbol* rec = resolve_name(m_current, m_root, prefix, q->is_global(), q);
    if (rec && rec->kind == SymbolKind::Import && rec->import_target) rec = rec->import_target;
    if (!rec || rec->kind != SymbolKind::Type || !rec->inner_scope || rec->inner_scope == s->owner) return;
    if (rec->decl && rec->decl->kind == K::RecordDeclaration) rebase_on_instance(static_cast<nodes::RecordDeclaration*>(rec->decl));
    if (Symbol* found = rec->inner_scope->find_member(parts.back()); found && found != s) q->resolved = found;
}

bool TypeWalker::member_value(Type* rec, std::string_view name, TemplateArg& out) {
    if (!rec || !rec->is_record() || !m_inst) return false;
    auto* rt = static_cast<RecordType*>(rec);
    Scope* sc = nullptr;
    if (rt->is_instantiation()) {
        auto* I = m_inst->for_record(rt, nullptr);
        if (!I || !I->sym) return false;
        ensure_typed(I);
        sc = I->sym->inner_scope;
    } else if (rt->decl()) {
        if (rt->decl()->decl && rt->decl()->decl->kind == K::RecordDeclaration) rebase_on_instance(static_cast<nodes::RecordDeclaration*>(rt->decl()->decl));
        sc = rt->decl()->inner_scope;
    }
    Symbol* found = sc ? sc->find_member(name) : nullptr;
    if (!found) return false;
    auto* owner = m_inst->owning(found);
    if (owner) m_types.push_subst(&owner->env);
    ConstValue v = m_eval.eval_named(found);
    if (owner) m_types.pop_subst();

    switch (v.kind) {
        case ConstValue::Kind::Int:    out = TemplateArg::of_int(v.i); return true;
        case ConstValue::Kind::Float:  out = TemplateArg::of_float(v.f); return true;
        case ConstValue::Kind::Bool:   out = TemplateArg::of_int(m_types.consts().from(std::uint64_t(v.b ? 1 : 0))); return true;
        case ConstValue::Kind::String: out = TemplateArg::of_string(v.s); return true;
        default: return false;
    }
}

ConstValue TypeWalker::scoped_member_value(nodes::MemberAccessExpression* m) {
    if (!m_inst || !m->m_object || m->m_object->kind != K::TemplateInstantiation) return {};
    auto* t = static_cast<nodes::TemplateInstantiation*>(m->m_object);
    Symbol* g = callee_symbol(t->m_template);
    if (g && g->kind == SymbolKind::Import && g->import_target) g = g->import_target;
    if (!g || g->kind != SymbolKind::Type || !g->template_decl) return {};
    bool ok = true;
    std::vector<Instantiator::Arg> args = m_inst->canon_args(t->m_args, ok);
    if (!ok) return {};

    for (const Instantiator::Arg& a : args) {
        if (a.is_type && (!a.type || a.type->is_dependent())) return {};
        if (a.is_dependent_value()) return {};
    }

    auto* I = m_inst->instantiate(g, std::move(args), m);
    if (!I || !I->sym || !I->sym->inner_scope) return {};
    ensure_typed(I);
    Symbol* found = I->sym->inner_scope->find_member(m->get_member());
    if (!found) return {};
    auto* owner = m_inst->owning(found);
    if (owner) m_types.push_subst(&owner->env);
    ConstValue v = m_eval.eval_named(found);
    if (owner) m_types.pop_subst();
    return v;
}

bool TypeWalker::is_variable_template(Symbol* g) {
    if (!g || !g->decl || g->decl->kind != K::FunctionDeclaration) return false;
    return static_cast<nodes::FunctionDeclaration*>(g->decl)->variable_template;
}

ConstValue TypeWalker::variable_template_value(nodes::TemplateInstantiation* t) {
    Symbol* g = callee_symbol(t->m_template);
    if (!is_variable_template(g) || !m_inst) return {};
    bool ok = true;
    std::vector<Instantiator::Arg> args = m_inst->canon_args(t->m_args, ok);
    if (!ok) return {};

    for (const Instantiator::Arg& a : args) {
        if (a.is_type && (!a.type || a.type->is_dependent())) return {};
        if (a.is_dependent_value()) return {};
    }

    auto* I = m_inst->instantiate(g, std::move(args), t);
    if (!I || !I->sym) return {};
    ensure_typed(I);
    return m_eval.eval_value_function(I->sym);
}

int TypeWalker::concept_id_value(nodes::TemplateInstantiation* t) {
    Symbol* cept = nullptr;
    TemplateArgs args;
    if (!concept_args(t, cept, args)) return -1;
    return concept_satisfied(cept, args) ? 1 : 0;
}

bool TypeWalker::requires_satisfied(nodes::RequiresExpression* r) {
    using Form = nodes::RequiresExpression::Requirement::Form;
    DiagnosticTrap trap(m_reporter);
    Scope* saved = m_current;
    if (r->scope) m_current = r->scope;
    bool ok = true;

    struct SavedPack {
        nodes::FunctionParameter* param;
        bool expanded;
        std::vector<Symbol*> symbols;
    };
    std::vector<SavedPack> saved_packs;
    bool unbound_pack = false;
    const SubstEnv* active = m_types.active_subst();

    if (r->m_parameters && m_inst) {
        for (nodes::FunctionParameter* p : *r->m_parameters) {
            if (!p->is_variadic() || !p->symbol) continue;
            Type* et = m_types.canonicalize(p->get_type());
            std::vector<Symbol*> pks;
            collect_pack_params(et, pks);
            if (pks.empty()) continue;
            bool bound = active != nullptr;
            for (Symbol* s : pks) bound = bound && active->lookup_pack(s) != nullptr;
            if (!bound) { unbound_pack = true; continue; }
            std::vector<Type*> elems;
            Type* pattern = et && et->kind() == TypeKind::PackExpansion ? et : m_types.pack_expansion(et);
            if (!expand_into(pattern, *active, m_types, elems)) { ok = false; continue; }
            saved_packs.push_back(SavedPack{ p, p->expanded_pack, std::vector<Symbol*>(p->pack_symbols.begin(), p->pack_symbols.end()) });
            ArenaScope arena(m_inst->arena());
            p->expanded_pack = true;
            p->pack_symbols.clear();
            for (std::size_t k = 0; k < elems.size(); ++k) {
                Symbol* es = synthetic_symbol(std::string(p->get_name()) + "#" + std::to_string(k), elems[k], p);
                es->kind = SymbolKind::Parameter;
                es->owner = p->symbol->owner;
                p->pack_symbols.push_back(es);
            }
        }
    }

    auto requirement_expr = [&](nodes::ASTNode* e) -> nodes::ASTNode* {
        if (saved_packs.empty() || !e) return e;
        ArenaScope arena(m_inst->arena());
        return clone_resolved(e);
    };

    for (auto& req : r->m_requirements) {
        if (!ok) break;

        switch (req.form) {
            case Form::Simple:
                walk_expr(requirement_expr(req.expr));
                break;

            case Form::Compound: {
                nodes::ASTNode* expr = requirement_expr(req.expr);
                walk_expr(expr);
                Type* t = type_of(expr);
                if (!req.has_type_constraint || !t) break;
                Symbol* cs = req.type.resolved;

                if (cs && cs->kind == SymbolKind::Concept) {
                    TemplateArgs args;
                    args.push_back(TemplateArg::of_type(m_types.strip_cv(t)));
                    bool aok = true;
                    std::vector<Instantiator::Arg> extra = m_inst ? m_inst->canon_args(req.type.template_args, aok) : std::vector<Instantiator::Arg>{};
                    args.insert(args.end(), extra.begin(), extra.end());
                    ok = aok && concept_satisfied(cs, args);
                } else {
                    Type* want = m_types.canonicalize(req.type);
                    ok = want && !want->is_error() && rank_conversion(t, want, m_types) != ConversionRank::None;
                }
                break;
            }

            case Form::Type: {
                Type* t = m_types.canonicalize(req.type);
                if (t && !t->is_error()) t = m_types.apply_subst(t);
                ok = t && !t->is_error() && !contains_error(t);
                if (ok && t->kind() == TypeKind::Dependent) {
                    const Type* base = static_cast<const DependentType*>(t)->base();
                    if (base && (base->kind() == TypeKind::Record || base->kind() == TypeKind::Builtin || base->kind() == TypeKind::Enum || base->kind() == TypeKind::Pointer)) ok = false;
                }
                break;
            }

            case Form::Nested: {
                std::string ignored;
                ok = constraint_holds(req.expr, ignored);
                break;
            }
        }
    }

    for (const SavedPack& sp : saved_packs) {
        ArenaScope arena(m_inst->arena());
        sp.param->expanded_pack = sp.expanded;
        sp.param->pack_symbols.clear();
        for (Symbol* s : sp.symbols) sp.param->pack_symbols.push_back(s);
    }

    m_current = saved;
    const bool had_errors = trap.discard();
    return ok && !had_errors && !unbound_pack;
}

bool TypeWalker::constraint_holds(nodes::ASTNode* e, std::string& failed) {
    if (!e) return true;
    using TK = tokenizing::Token::Kind;

    switch (e->kind) {
        case K::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);
            if (b->op == TK::LogicAnd) return constraint_holds(b->left, failed) && constraint_holds(b->right, failed);
            if (b->op == TK::LogicOr) {
                std::string l, r;
                if (constraint_holds(b->left, l) || constraint_holds(b->right, r)) return true;
                failed = l.empty() ? r : l;
                return false;
            }
            break;
        }

        case K::UnaryExpression: {
            auto* u = static_cast<nodes::UnaryExpression*>(e);
            if (u->op == TK::ExclamationMark) { std::string ignored; return !constraint_holds(u->operand, ignored); }
            break;
        }

        case K::TemplateInstantiation: {
            auto* t = static_cast<nodes::TemplateInstantiation*>(e);
            Symbol* cept = nullptr;
            TemplateArgs args;

            if (concept_args(t, cept, args)) {
                if (concept_satisfied(cept, args)) return true;
                std::ostringstream os;
                os << cept->name << '<';
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i) os << ", ";
                    if (args[i].is_type && args[i].type) args[i].type->write_to(os);
                    else if (args[i].value)  os << *args[i].value;
                    else if (args[i].fvalue) os << *args[i].fvalue;
                    else if (args[i].svalue) os << '"' << *args[i].svalue << '"';
                }
                os << '>';
                failed = os.str();
                return false;
            }

            if (is_variable_template(callee_symbol(t->m_template)) && m_inst) {
                ConstValue v = variable_template_value(t);
                bool ok = false;
                const bool holds = ConstEvaluator::truthy(v, ok);
                if (ok && holds) return true;
                std::ostringstream os;
                if (t->m_template->kind == K::QualifiedIdentifier) {
                    const auto& parts = static_cast<nodes::QualifiedIdentifier*>(t->m_template)->parts();
                    for (std::size_t i = 0; i < parts.size(); ++i) os << (i ? "::" : "") << parts[i];
                } else if (Symbol* g = callee_symbol(t->m_template)) {
                    os << g->name;
                }
                bool aok = true;
                std::vector<Instantiator::Arg> raw = m_inst->canon_args(t->m_args, aok);
                os << '<';
                for (std::size_t i = 0; aok && i < raw.size(); ++i) {
                    if (i) os << ", ";
                    if (raw[i].is_type && raw[i].type) raw[i].type->write_to(os);
                    else if (raw[i].value)  os << *raw[i].value;
                    else if (raw[i].fvalue) os << *raw[i].fvalue;
                    else if (raw[i].svalue) os << '"' << *raw[i].svalue << '"';
                }
                os << '>';
                failed = os.str();
                return false;
            }
            break;
        }

        case K::RequiresExpression:
            if (requires_satisfied(static_cast<nodes::RequiresExpression*>(e))) return true;
            failed = "requires-expression";
            return false;

        default: break;
    }

    ConstValue v = m_eval.eval(e);
    bool ok = false;
    const bool t = ConstEvaluator::truthy(v, ok);
    if (ok && t) return true;
    failed = ok ? "constraint expression" : "non-constant constraint expression";

    if (ok && e->kind == K::TypeQuery && static_cast<nodes::TypeQueryExpression*>(e)->op == nodes::TypeQueryExpression::Op::Trait) {
        auto* q = static_cast<nodes::TypeQueryExpression*>(e);
        std::string text = std::string(parser_types::type_trait_name(q->trait)) + "(";
        Type* first = q->operand ? m_types.canonicalize(q->operand->type) : nullptr;
        text += first ? type_str(first) : "?";
        for (parser_types::TemplateArgument* x : q->extra_operands) {
            Type* xt = x ? m_types.canonicalize(x->type) : nullptr;
            text += ", " + (xt ? type_str(xt) : std::string("?"));
        }
        failed = text + ")";
    }

    return false;
}

bool TypeWalker::member_constraint_holds(Symbol* fn, const SubstEnv* env, std::string& failed) {
    if (!fn || !fn->decl) return true;
    nodes::ASTNode* clause = nullptr;
    if (fn->decl->kind == K::FunctionDeclaration) clause = static_cast<nodes::FunctionDeclaration*>(fn->decl)->member_requires;
    else if (fn->decl->kind == K::OperatorFunctionDeclaration) clause = static_cast<nodes::OperatorFunctionDeclaration*>(fn->decl)->member_requires;
    if (!clause) return true;
    if (env) m_types.push_subst(env);
    Scope* saved = m_current;
    if (fn->owner) m_current = fn->owner;
    const bool ok = constraint_holds(clause, failed);
    m_current = saved;
    if (env) m_types.pop_subst();
    return ok;
}

bool TypeWalker::constraints_satisfied(nodes::TemplateDeclaration* tmpl, const SubstEnv& env, std::string& failed) {
    if (!tmpl) return true;
    bool has_any = tmpl->m_requires_clause != nullptr;
    for (nodes::TemplateParameter* p : tmpl->params()) has_any = has_any || p->is_constrained();
    if (!has_any) return true;

    m_types.push_subst(&env);
    Scope* saved = m_current;
    if (tmpl->scope) m_current = tmpl->scope;
    bool ok = true;

    for (nodes::TemplateParameter* p : tmpl->params()) {
        if (!ok) break;
        if (!p->is_constrained() || !p->symbol || p->is_pack()) continue;
        Symbol* cs = p->get_constraint().resolved;
        if (cs && cs->kind == SymbolKind::Import && cs->import_target) cs = cs->import_target;
        if (!cs || cs->kind != SymbolKind::Concept) continue;
        TemplateArgs args;
        if      (Type* t = env.lookup_type(p->symbol))           args.push_back(TemplateArg::of_type(t));
        else if (const WideInt* v = env.lookup_value(p->symbol))  args.push_back(TemplateArg::of_int(v));
        else continue;
        bool aok = true;
        std::vector<Instantiator::Arg> extra = m_inst ? m_inst->canon_args(p->get_constraint().template_args, aok) : std::vector<Instantiator::Arg>{};
        args.insert(args.end(), extra.begin(), extra.end());

        if (!aok || !concept_satisfied(cs, args)) {
            std::ostringstream os;
            os << cs->name << '<';
            if (args[0].is_type && args[0].type) args[0].type->write_to(os); else if (args[0].value) os << *args[0].value;
            os << '>';
            failed = os.str();
            ok = false;
        }
    }

    if (ok && tmpl->m_requires_clause) ok = constraint_holds(tmpl->m_requires_clause, failed);
    m_current = saved;
    m_types.pop_subst();
    return ok;
}

bool TypeWalker::is_trivial_scalar(Type* t) {
    t = m_types.strip_cv(t);
    if (!t) return false;
    if (t->is_pointer()) return true;
    return is_numeric_t(t) || (t->is_builtin() && static_cast<BuiltinType*>(t)->is_bool());
}

void TypeWalker::note_read(Symbol* s, nodes::ASTNode* site) {
    if (!s || !m_unassigned.erase(s)) return;
    SemanticWarning::emit(m_warnings, file_of(site), site->line, "Initialization", "'" + std::string(s->name) + "' may be used before it is assigned");
}

void TypeWalker::touch_record(Type* t, nodes::ASTNode* site) {
    if (!t || !m_inst) return;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    while (t && t->is_pointer()) t = static_cast<PointerType*>(t)->pointee();
    t = m_types.strip_cv(t);
    if (!t || !t->is_record() || t->is_dependent()) return;
    auto* rt = static_cast<RecordType*>(t);
    if (rt->is_instantiation()) m_inst->for_record(rt, site);
}

void TypeWalker::adapt_contains_literal(nodes::CallExpression* c, std::vector<Type*>& args) {
    if (!c || c->m_arguments.size() != 2 || args.size() != 2 || !args[0] || !args[1] || !is_plain_literal(c->m_arguments[1])) return;
    if (!c->m_callee || c->m_callee->kind != K::QualifiedIdentifier) return;
    const auto& parts = static_cast<nodes::QualifiedIdentifier*>(c->m_callee)->parts();
    if (parts.size() != 3 || parts[0] != "std" || parts[1] != "core" || parts[2] != "contains_in") return;
    Scope* ms = member_scope_of(args[0]);
    if (!ms) return;

    for (Symbol* o = ms->find_member("contains"); o; o = o->next_overload) {
        EnvGuard g(m_types, env_of_symbol(o));
        const std::vector<ParamShape> shapes = shapes_of(o);
        if (shapes.size() != 1 || shapes[0].is_pack || !shapes[0].element) continue;
        Type* want = shapes[0].element;
        if (want->is_reference()) want = static_cast<ReferenceType*>(want)->referent();
        want = m_types.strip_cv(want);
        if (!want || want->is_dependent() || want == m_types.strip_cv(args[1])) return;
        if (!constant_fits(c->m_arguments[1], args[1], want)) continue;
        args[1] = want;
        c->m_arguments[1]->expr_type.type = want;
        return;
    }
}

CandidateMatch TypeWalker::match_with_constants(nodes::CallExpression* c, const std::vector<ParamShape>& shapes, const std::vector<Type*>& args) {
    return match_with_constants(c ? &c->m_arguments : nullptr, shapes, args);
}

CandidateMatch TypeWalker::match_with_constants(const ArenaVector<nodes::ASTNode*>* nodes, const std::vector<ParamShape>& shapes, const std::vector<Type*>& args) {
    CandidateMatch m = match_arguments(shapes, args, m_types);
    if (m.viable || !nodes || nodes->size() != args.size()) return m;
    std::vector<Type*> adjusted = args;
    std::vector<bool>  changed(args.size(), false);
    std::vector<bool>  const_ref(args.size(), false);
    bool any = false;

    auto adjust = [&](std::size_t i, Type* want) {
        if (!args[i] || !want || !(*nodes)[i]) return;
        if (want->is_reference()) {
            auto* rt = static_cast<ReferenceType*>(want);
            if (rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const) return;
            if (rt->ref_qual() == RefQual::LValue) const_ref[i] = true;
            want = rt->referent();
        }
        if (constant_fits((*nodes)[i], args[i], want)) { adjusted[i] = m_types.strip_cv(want); changed[i] = true; any = true; }
    };

    for (std::size_t i = 0; i < shapes.size() && i < args.size(); ++i) {
        if (shapes[i].is_pack) {
            if (i + 1 == shapes.size()) for (std::size_t j = i; j < args.size(); ++j) adjust(j, shapes[i].element);
            break;
        }
        adjust(i, shapes[i].element);
    }

    if (!any) return m;
    CandidateMatch a = match_arguments(shapes, adjusted, m_types);
    if (!a.viable) return m;
    for (std::size_t i = 0; i < a.ranks.size() && i < changed.size(); ++i) {
        if (!changed[i]) continue;
        const bool walnut_int = adjusted[i] && adjusted[i]->is_builtin() && static_cast<BuiltinType*>(adjusted[i])->base() == parser_types::PrimitiveType::BaseKind::Int && !static_cast<BuiltinType*>(adjusted[i])->is_hardware();
        const ConversionRank floor = walnut_int ? (const_ref[i] ? ConversionRank::ConstantFitConstRef : ConversionRank::ConstantFit) : ConversionRank::Conversion;
        if (a.ranks[i] < floor) a.ranks[i] = floor;
    }
    return a;
}

void TypeWalker::check_arg_bindings(nodes::CallExpression* c, const std::vector<ParamShape>& shapes) {
    std::size_t i = 0;

    for (const ParamShape& p : shapes) {
        if (p.is_pack || i >= c->m_arguments.size()) break;
        nodes::ASTNode* a = c->m_arguments[i++];
        if (a && a->kind == K::BraceInitializerList) { check_brace_init(static_cast<nodes::BraceInitializerList*>(a), p.element, c); continue; }
        if (binds_nonconst_lvalue_ref_to_rvalue(a, type_of(a), p.element))
            SemanticError::bad_reference_binding(m_reporter, file_of(a), a->line, type_str(p.element), true);
        check_copy_or_move(a, type_of(a), p.element, c);
    }
}

void TypeWalker::check_constant_init(nodes::VariableDeclaration* v, Type* target) {
    using RM = modifiers::RawModifiers;
    const auto& mods = v->get_type_info().modifiers;
    const bool required = mods.has(RM::Constexpr) || mods.has(RM::Constinit) || mods.has(RM::Inline);

    if (!v->has_initializer()) {
        if (required) SemanticError::constant_needs_initializer(m_reporter, file_of(v), v->line, mods.has(RM::Constexpr) ? "constexpr" : mods.has(RM::Inline) ? "inline" : "constinit", v->get_name());
        return;
    }

    Type* t = target ? m_types.strip_cv(target) : nullptr;
    if (!t || t->is_dependent() || !ConstEvaluator::is_constant_type(t)) return;

    ConstValue raw;
    if (mods.has(RM::Constexpr) || mods.has(RM::Constinit)) {
        ConstEvaluator::RandomPermit permit(m_eval, v);
        raw = eval_checked(v->m_initializer, true);
    } else {
        raw = eval_checked(v->m_initializer, required);
    }
    if (raw.is_error()) return;

    if (!raw.ok()) {
        if (required) SemanticError::not_a_constant(m_reporter, file_of(v), v->line, fail_message(ConstValue::Fail::NotConstant));
        return;
    }

    ConstValue fitted = m_eval.coerce(raw, t);
    Type* from = type_of(v->m_initializer);
    const bool type_error = from && target && !from->is_error() && rank_conversion(from, target, m_types) == ConversionRank::None;
    if (fitted.is_error() && !type_error) SemanticError::not_a_constant(m_reporter, file_of(v), v->line, fail_message(fitted.fail));
}

auto TypeWalker::operand_class(Type* t) -> OperandClass {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t || t->is_error() || t->is_dependent() || is_dynamic_t(t)) return OperandClass::Unknown;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    if (t->is_pointer() || t->is_array()) return OperandClass::Pointer;
    if (t->is_null()) return OperandClass::Null;
    if (t->is_special_value()) return OperandClass::Special;
    if (t->is_enum()) return OperandClass::Enum;
    if (!t->is_builtin()) return OperandClass::Unknown;

    switch (static_cast<BuiltinType*>(t)->base()) {
        case BK::Int:    return OperandClass::Int;
        case BK::Char:   return OperandClass::Char;
        case BK::Float:  return OperandClass::Float;
        case BK::Bool:   return OperandClass::Bool;
        case BK::String:
        case BK::Text:
        case BK::StringView: return OperandClass::String;
        case BK::Auto:   return OperandClass::Unknown;
        default:         return OperandClass::Other;
    }
}

tokenizing::Token::Kind TypeWalker::compound_base(tokenizing::Token::Kind op) {
    using TK = tokenizing::Token::Kind;
    switch (op) {
        case TK::PlusEqual:           return TK::Plus;
        case TK::MinusEqual:          return TK::Minus;
        case TK::AsteriskEqual:       return TK::Asterisk;
        case TK::SlashEqual:          return TK::Slash;
        case TK::PercentEqual:        return TK::Percent;
        case TK::DoubleAsteriskEqual: return TK::DoubleAsterisk;
        case TK::AmpersandEqual:      return TK::Ampersand;
        case TK::PipeEqual:           return TK::Pipe;
        case TK::CaretEqual:          return TK::Caret;
        case TK::ShiftLeftEqual:      return TK::DoubleLessThan;
        case TK::ShiftRightEqual:     return TK::DoubleGreaterThan;
        default:                      return op;
    }
}

bool TypeWalker::builtin_operands_ok(tokenizing::Token::Kind op, OperandClass a, OperandClass b) {
    using TK = tokenizing::Token::Kind;
    using OC = OperandClass;
    if (a == OC::Unknown || b == OC::Unknown) return true;

    auto numeric  = [](OC c) { return c == OC::Int || c == OC::Char || c == OC::Float || c == OC::Special; };
    auto integral = [](OC c) { return c == OC::Int || c == OC::Char || c == OC::Special; };
    auto same_family = [](OC x, OC y) { return (x == OC::Char || y == OC::Char) ? (x == OC::Char && y == OC::Char) : true; };
    auto arith    = [&](OC x, OC y) { return numeric(x) && numeric(y) && same_family(x, y); };
    auto int_pair = [&](OC x, OC y) { return integral(x) && integral(y) && same_family(x, y) && !(x == OC::Special && y == OC::Special); };
    auto bitwise  = [&](OC x, OC y) {
        if (x == OC::Bool || y == OC::Bool) return x == OC::Bool && y == OC::Bool;
        if (x == OC::Enum || y == OC::Enum) return x == OC::Enum && y == OC::Enum;
        return int_pair(x, y);
    };
    auto ordered  = [&](OC x, OC y) {
        return arith(x, y)
            || (x == OC::Pointer && y == OC::Pointer)
            || (x == OC::String  && y == OC::String)
            || (x == OC::Enum    && (y == OC::Enum || y == OC::Int))
            || (y == OC::Enum    && x == OC::Int);
    };

    switch (op) {
        case TK::Plus:
            return arith(a, b)
                || (a == OC::Char && b == OC::Int) || (a == OC::Int && b == OC::Char)
                || (a == OC::Pointer && b == OC::Int) || (a == OC::Int && b == OC::Pointer)
                || (a == OC::String && (b == OC::String || b == OC::Char))
                || (a == OC::Char && b == OC::String);

        case TK::Minus:
            return arith(a, b) || (a == OC::Char && b == OC::Int) || (a == OC::Pointer && (b == OC::Int || b == OC::Pointer));

        case TK::Asterisk:
        case TK::Slash:
        case TK::DoubleAsterisk:
            return arith(a, b);

        case TK::Percent:
            return int_pair(a, b);

        case TK::Ampersand:
        case TK::Pipe:
        case TK::Caret:
            return bitwise(a, b);

        case TK::DoubleLessThan:
        case TK::DoubleGreaterThan:
            return (integral(a) || a == OC::Enum) && (integral(b) || b == OC::Enum) && !(a == OC::Special && b == OC::Special);

        case TK::LessThan:
        case TK::GreaterThan:
        case TK::LessEqual:
        case TK::GreaterEqual:
            return ordered(a, b);

        case TK::LogicEqual:
        case TK::NotEqual:
            return ordered(a, b)
                || (a == OC::Bool && b == OC::Bool)
                || ((a == OC::Pointer || a == OC::Null) && (b == OC::Pointer || b == OC::Null));

        default:
            return true;
    }
}

bool TypeWalker::check_builtin_operands(nodes::BinaryExpression* b, Type* l, Type* r) {
    using TK = tokenizing::Token::Kind;
    if (!l || !r || b->op == TK::Equal || is_logical_op(b->op)) return true;
    const TK base = compound_base(b->op);
    const bool bit = base == TK::Ampersand || base == TK::Pipe || base == TK::Caret;
    auto plain = [this](Type* t) { return m_types.strip_cv(t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t); };
    const bool same_enums = !bit || operand_class(l) != OperandClass::Enum || plain(l) == plain(r);
    if (same_enums && builtin_operands_ok(base, operand_class(l), operand_class(r))) return true;
    SemanticError::bad_binary_operands(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(nodes::classify_single_operator(b->op)), type_str(l), type_str(r));
    return false;
}

void TypeWalker::ensure_conversions(Type* t) {
    if (!t || !t->is_record()) return;
    Type* key = m_types.strip_cv(t);
    if (!m_types.user_conversions().mark_if_new(key)) return;     
    Symbol* decl = static_cast<RecordType*>(key)->decl();
    nodes::ASTNode*     node = decl ? decl->decl : nullptr;       
    Instantiator::Instantiation* inst = nullptr;

    if (static_cast<RecordType*>(key)->is_instantiation() && m_inst) {
        inst = m_inst->for_record(static_cast<RecordType*>(key), nullptr);
        if (inst && inst->decl) node = inst->decl;
    }

    if (!node || node->kind != K::RecordDeclaration) return;
    auto* rd = static_cast<nodes::RecordDeclaration*>(node);
    EnvGuard env_guard(m_types, inst ? &inst->env : nullptr);

    std::vector<Type*> own;

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (m.node && m.node->kind == K::TemplateDeclaration) {
            nodes::ASTNode* inner = static_cast<nodes::TemplateDeclaration*>(m.node)->m_declaration;
            if (inner && inner->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(inner)->m_lowered_operator == nodes::OverloadableOperator::Conversion) {
                auto* fd = static_cast<nodes::FunctionDeclaration*>(inner);
                UserConversion uc;
                uc.generic   = fd->symbol;
                uc.explicit_ = fd->qualifiers().has(modifiers::FunctionQualifiers::Explicit);
                if (uc.generic) m_types.user_conversions().add(key, uc);
            }
            continue;
        }
        if (!m.node || m.node->kind != K::OperatorFunctionDeclaration) continue;
        auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(m.node);
        if (!op->is_conversion() || op->is_deleted()) continue;   
        UserConversion uc;
        uc.dst       = m_types.strip_cv(m_types.canonicalize(op->get_conversion_type()));
        uc.op        = op->symbol;
        uc.explicit_ = op->qualifiers().has(modifiers::FunctionQualifiers::Explicit);
        m_types.user_conversions().add(key, uc);
        own.push_back(uc.dst);
    }

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        Type* base = m_types.strip_cv(m_types.canonicalize(*info));
        if (!base || !base->is_record()) continue;
        ensure_conversions(base);
        const auto* inherited = m_types.user_conversions().find(base);
        if (!inherited) continue;

        for (const UserConversion& uc : *inherited) {
            if (std::find(own.begin(), own.end(), uc.dst) != own.end()) continue;
            m_types.user_conversions().add(key, uc);
            own.push_back(uc.dst);
        }
    }
}

void TypeWalker::check_type_query_overload(nodes::TypeQueryOverload* q) {
    const std::string name(q->name);
    const std::string spelled = "'operator(" + name + ")'";
    const bool in_record = m_current && m_current->node && m_current->node->kind == K::RecordDeclaration;

    if (!in_record) {
        SemanticError::emit(m_reporter, file_of(q), q->line, "Overload", spelled + " can only be declared inside a class, struct or union");
        return;
    }

    const std::string owner = m_current->owner_symbol ? std::string(m_current->owner_symbol->name) : std::string("this class");

    if (parser_types::type_query_overload_policy(q->name) == parser_types::OverloadPolicy::Warns) {
        SemanticWarning::emit(m_warnings, file_of(q), q->line, "Overload", "'" + owner + "' overloads '" + name + "', which normally reports what kind of type it is; '" + name + "(" + owner + ")' will now use this overload");
    }

    const std::size_t wanted = (!q->is_trait && q->op == parser_types::TypeOp::CommonType) ? 1 : 0;

    if (q->m_params.size() != wanted) {
        SemanticError::emit(m_reporter, file_of(q), q->line, "Overload", wanted == 1
            ? spelled + " takes exactly one parameter type: the type '" + owner + "' is combined with"
            : spelled + " takes no parameters");
    }

    for (parser_types::TypeInfo& p : q->m_params) m_types.canonicalize(p);
    Type* result = m_types.canonicalize(q->m_return_type);
    if (!q->is_trait || !result || result->is_error()) return;

    const bool gives_int = parser_types::type_trait_gives_int(q->trait);
    Type* expected = gives_int ? m_types.builtin(parser_types::PrimitiveType::BaseKind::Int) : m_types.bool_();

    if (m_types.strip_cv(result) != expected) {
        SemanticError::emit(m_reporter, file_of(q), q->line, "Overload", spelled + " must return '" + std::string(gives_int ? "int" : "bool") + "', not '" + type_str(result) + "'");
    }

    walk_expr(q->m_value);

    if (m_current->owner_symbol && !m_current->owner_symbol->template_decl && !m_eval.eval(q->m_value).ok()) {
        SemanticError::emit(m_reporter, file_of(q), q->line, "Overload", spelled + " must return a constant");
    }
}

void TypeWalker::check_operator_signature(nodes::OperatorFunctionDeclaration* od, Symbol* sym) {
    const bool is_friend = od->get_modifiers().has(modifiers::RawModifiers::Friend);
    bool is_member = !is_friend && m_current && m_current->node && m_current->node->kind == K::RecordDeclaration;
    nodes::OverloadableOperator op = od->get_overload();
    std::size_t n = od->get_parameters() ? od->get_parameters()->size() : 0;
    const char* why = nullptr;
    if (!operator_arity_ok(op, is_member, n, why)) SemanticError::bad_operator_arity(m_reporter, file_of(od), od->line, nodes::overloadable_operator_name(op), why);
    check_signature_defaults(sym, od->get_parameters(), od);

    using OO = nodes::OverloadableOperator;
    const bool allocation = op == OO::New || op == OO::Delete || op == OO::NewArray || op == OO::DeleteArray;

    if (!is_member && !allocation && n > 0) {
        bool user_type = false;
        bool unknown = false;

        for (nodes::FunctionParameter* p : od->get_parameters()->m_params) {
            Type* t = m_types.canonicalize(p->get_type());
            if (!t || t->is_error() || t->is_dependent()) { unknown = true; break; }
            if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
            t = m_types.strip_cv(t);
            if (t->is_record() || t->is_enum()) user_type = true;
        }

        if (!unknown && !user_type) {
            SemanticError::emit(m_reporter, file_of(od), od->line, "Overload", "'operator(" + std::string(nodes::overloadable_operator_name(op)) + ")' only uses built-in types, so it would change how the language itself behaves; an operator overload needs at least one class, struct, union or enum parameter");
        }
    }
}

Type* TypeWalker::resolve_member_operator(Type* recv, nodes::OverloadableOperator wanted, const std::vector<Type*>& extra, Symbol** chosen) {
    if (chosen) *chosen = nullptr;
    Scope* ms = member_scope_of(recv);
    if (!ms) return nullptr;
    Symbol* bucket = ms->find_member("operator");
    std::vector<CandidateMatch> matches;
    std::vector<Symbol*>        cands;

    for (Symbol* o = bucket; o; o = o->next_overload) {
        if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(o->decl);
        if (od->get_overload() != wanted) continue;
        {
            std::string failed;
            if (!member_constraint_holds(o, env_of_symbol(o), failed)) {
                m_last_constraint_failure = failed;
                continue;
            }
        }
        EnvGuard g(m_types, env_of_symbol(o));
        matches.push_back(match_arguments(shapes_of(o), extra, m_types));
        cands.push_back(o);
    }

    nodes::ASTNode* site = m_operator_site ? m_operator_site : m_site;

    if (m_inst && site) {
        const std::string lowered = "operator(" + std::string(nodes::overloadable_operator_name(wanted)) + ")";

        for (Symbol* o = ms->find_member(lowered); o; o = o->next_overload) {
            if (!o->template_decl) continue;
            auto* I = m_inst->instantiate_for_call(o, {}, extra, site);
            if (!I || !I->sym) continue;
            CandidateMatch m = match_arguments(I->fn_shapes, extra, m_types);
            if (!m.viable) continue;
            matches.push_back(std::move(m));
            cands.push_back(I->sym);
        }
    }

    if (cands.empty()) return nullptr;
    std::vector<bool> from_template;
    for (Symbol* s : cands) from_template.push_back(is_template_instance(s));
    Selection sel = select_overload(matches, from_template);
    if (sel.status != SelectStatus::Ok) return nullptr;
    Symbol* pick = cands[sel.index];
    {
        std::vector<Symbol*> origins;
        for (Symbol* c : cands) origins.push_back(overload_origin(c));
        remember_overloads(nullptr, matches, origins, sel.index);
    }
    if (from_template[sel.index]) if (Instantiator::Instantiation* I = m_inst->owning(pick)) ensure_typed(I);
    if (chosen) *chosen = pick;
    EnvGuard g(m_types, env_of_symbol(pick));
    return pick->type ? m_types.canonicalize(*pick->type) : nullptr;
}

Type* TypeWalker::synthesize_comparison(nodes::BinaryExpression* b, Type* l, Type* r, Symbol*& resolved_op) {
    using TK = tokenizing::Token::Kind;
    struct Alt { TK op; bool swap; bool negate; };
    std::vector<Alt> alts;

    switch (b->op) {
        case TK::LogicEqual:   alts = { { TK::LogicEqual, true, false } }; break;
        case TK::NotEqual:     alts = { { TK::LogicEqual, false, true }, { TK::LogicEqual, true, true } }; break;
        case TK::LessThan:     alts = { { TK::GreaterThan, true, false }, { TK::GreaterEqual, false, true }, { TK::LessEqual, true, true } }; break;
        case TK::GreaterThan:  alts = { { TK::LessThan, true, false }, { TK::LessEqual, false, true }, { TK::GreaterEqual, true, true } }; break;
        case TK::LessEqual:    alts = { { TK::GreaterEqual, true, false }, { TK::GreaterThan, false, true }, { TK::LessThan, true, true } }; break;
        case TK::GreaterEqual: alts = { { TK::LessEqual, true, false }, { TK::LessThan, false, true }, { TK::GreaterThan, true, true } }; break;
        default: return nullptr;
    }

    for (const Alt& a : alts) {
        OpResult res = resolve_operator(nodes::classify_single_operator(a.op), a.swap ? std::vector<Type*>{ r, l } : std::vector<Type*>{ l, r });
        if (res.status != SelectStatus::Ok) continue;
        if (a.negate && res.type && !is_bool_testable(res.type)) continue;
        resolved_op      = res.chosen;
        b->swap_operands = a.swap;
        b->negate_result = a.negate;
        return a.negate ? m_types.bool_() : res.type;
    }

    if (b->op != TK::LogicEqual && b->op != TK::NotEqual) return nullptr;

    auto plain = [this](Type* t) -> Type* {
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    };

    Type* lt = plain(l);
    if (!lt || lt != plain(r) || !lt->is_record() || lt->is_dependent() || !memberwise_equality(lt, 0)) return nullptr;
    b->memberwise    = lt;
    b->negate_result = b->op == TK::NotEqual;
    return m_types.bool_();
}

std::string TypeWalker::equality_failure_of(Type* l, Type* r) {
    auto plain = [this](Type* t) -> Type* {
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    };

    Type* t = plain(l);
    if (!t || t != plain(r) || !t->is_record()) return {};
    auto* rt = static_cast<RecordType*>(t);
    Symbol* owner = rt->decl();

    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        owner = sc ? sc->owner_symbol : nullptr;
    }

    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return {};
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);
    return rd->equality_state == 2 ? rd->equality_failure : std::string();
}

bool TypeWalker::memberwise_equality(Type* t, int depth) {
    using RM = modifiers::RawModifiers;
    if (!t || !t->is_record() || depth > 64) return false;
    auto* rt = static_cast<RecordType*>(t);
    Symbol* owner = rt->decl();

    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        owner = sc ? sc->owner_symbol : nullptr;
    }

    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return false;
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);

    if (rd->is_union()) {
        rd->equality_state   = 2;
        rd->equality_failure = "unions cannot be compared member by member";
        return false;
    }
    if (rd->m_native) {
        rd->equality_state   = 2;
        rd->equality_failure = "a C++ class is only compared with an operator(==) declared in its extern block";
        return false;
    }
    if (rd->equality_state == 1 || rd->equality_state == 3) return true;
    if (rd->equality_state == 2) return false;
    rd->equality_state = 3;
    rd->equality_steps.clear();
    rd->equality_failure.clear();
    EnvGuard g(m_types, env_of_symbol(owner));

    auto plain_value = [this](Type* v) -> bool {
        v = m_types.strip_cv(v);
        if (!v) return false;
        if (v->is_pointer() || v->is_enum() || v->kind() == TypeKind::Null) return true;
        return v->is_builtin() && !static_cast<BuiltinType*>(v)->is_void() && static_cast<BuiltinType*>(v)->base() != parser_types::PrimitiveType::BaseKind::Auto;
    };

    auto add_step = [&](nodes::ASTNode* field, Type* ft) -> bool {
        ft = m_types.strip_cv(ft);
        std::string what = "base class";
        if (field && field->kind == K::VariableDeclaration) what = "member '" + std::string(static_cast<nodes::VariableDeclaration*>(field)->get_name()) + "'";
        if (field && field->kind == K::ArrayDeclaration)    what = "member '" + std::string(static_cast<nodes::ArrayDeclaration*>(field)->m_name) + "'";
        rd->equality_failure = what + (ft ? " of type '" + type_str(ft) + "'" : std::string()) + " cannot be compared with '=='";
        if (!ft || ft->is_reference() || ft->is_error() || ft->is_dependent()) return false;
        nodes::RecordDeclaration::EqualityStep step;
        step.field = field;
        if (!field) step.base = ft;

        if (ft->is_array()) {
            if (!plain_value(static_cast<ArrayType*>(ft)->element())) return false;
        } else if (ft->is_record()) {
            const bool saved_quiet = m_quiet_deleted;
            m_quiet_deleted = true;
            OpResult res = resolve_operator(nodes::OverloadableOperator::EqualEqual, { ft, ft });
            const bool deleted = res.status == SelectStatus::Ok && report_if_deleted(res.chosen);
            m_quiet_deleted = saved_quiet;

            if (deleted) {
                rd->equality_failure += ", because its 'operator(==)' is deleted";
                return false;
            }

            if (res.status == SelectStatus::Ok && res.type && is_bool_testable(res.type)) step.op = res.chosen;
            else if (memberwise_equality(ft, depth + 1)) step.nested = ft;
            else {
                const std::string inner = equality_failure_of(ft, ft);
                if (!inner.empty()) rd->equality_failure += ", because " + inner;
                return false;
            }
        } else if (!plain_value(ft)) {
            return false;
        }

        rd->equality_steps.push_back(step);
        rd->equality_failure.clear();
        return true;
    };

    bool ok = true;
    for (parser_types::TypeInfo* info : rd->all_bases()) if (ok) ok = add_step(nullptr, m_types.canonicalize(*info));

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!ok) break;
        if (!m.node) continue;

        if (m.node->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(m.node);
            if (v->get_type_info().modifiers.has(RM::Static) || v->get_type_info().modifiers.has(RM::Lazy)) continue;
            ok = add_step(v, m_types.canonicalize(v->get_type_info()));
        } else if (m.node->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
            if (a->get_array_modifiers().has(RM::Static)) continue;
            ok = add_step(a, array_symbol_type(a));
        }
    }


    rd->equality_state = ok ? 1 : 2;
    if (!ok) rd->equality_steps.clear();
    return ok;
}

void TypeWalker::check_alignas(parser_types::TemplateArgument* a, Type* natural, nodes::ASTNode* site) {
    if (!a || !site) return;
    long long value = 0;
    const bool named_value = a->is_type() && a->type.resolved && (a->type.resolved->kind == SymbolKind::Variable || a->type.resolved->kind == SymbolKind::EnumConstant);

    if (named_value) {
        ConstValue v = m_eval.eval_named(a->type.resolved);
        if (!v.ok() || !v.is_int() || v.i->is_negative() || !ConstTable::fits(*v.i, 32, false)) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Alignment", "alignas needs a positive integer constant, and '" + std::string(a->type.resolved->name) + "' is not one");
            return;
        }
        value = static_cast<long long>(v.i->get_lowest_bits());
    } else if (a->is_type()) {
        long long s = 0;
        Type* t = m_types.canonicalize(a->type);
        if (!t || t->is_error() || t->is_dependent()) return;
        if (!m_eval.layout_of(t, s, value)) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Alignment", "alignas('" + type_str(t) + "') needs a type with a fixed size and alignment");
            return;
        }
    } else {
        walk_expr(a->value);
        Type* vt = type_of(a->value);
        if (vt && vt->is_dependent()) return;
        ConstValue v = eval_checked(a->value, true);
        if (!v.ok()) return;
        if (!v.is_int() || v.i->is_negative() || !ConstTable::fits(*v.i, 32, false)) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Alignment", "alignas needs a positive integer constant");
            return;
        }
        value = static_cast<long long>(v.i->get_lowest_bits());
    }

    if (value <= 0 || (value & (value - 1)) != 0) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Alignment", "alignas(" + std::to_string(value) + ") is not a power of two");
        return;
    }

    long long ns = 0, na = 0;
    if (natural && !natural->is_error() && !natural->is_dependent() && m_eval.layout_of(natural, ns, na) && value < na) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Alignment", "alignas(" + std::to_string(value) + ") is weaker than the " + std::to_string(na) + "-byte alignment that '" + type_str(natural) + "' already needs");
        return;
    }

    a->align_value = value;
}

std::string TypeWalker::default_construct_failure(Type* t, int depth) {
    using RM = modifiers::RawModifiers;
    t = m_types.strip_cv(t);
    if (!t || depth > 32 || t->is_error() || t->is_dependent()) return {};
    if (t->is_array()) return default_construct_failure(static_cast<ArrayType*>(t)->element(), depth + 1);
    if (!t->is_record()) return {};
    auto* rt = static_cast<RecordType*>(t);
    Symbol* owner = rt->decl();

    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        owner = sc ? sc->owner_symbol : nullptr;
    }

    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration || !owner->inner_scope) return {};
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);
    if (rd->is_union()) return {};
    EnvGuard g(m_types, env_of_symbol(owner));
    const std::string name = "'" + type_str(t) + "'";

    bool has_ctor = false;
    bool defaulted = false;

    for (Symbol* o = owner->inner_scope->find_member("constructor"); o; o = o->next_overload) {
        if (!o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
        has_ctor = true;
        auto* cd = static_cast<nodes::ConstructorDeclaration*>(o->decl);
        if (cd->is_deleted() || !match_arguments(shapes_of(o), {}, m_types).viable) continue;
        if (!cd->is_defaulted()) return {};
        defaulted = true;
    }

    if (has_ctor && !defaulted) return name + " has no default constructor";

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        Type* base = m_types.canonicalize(*info);
        const std::string inner = default_construct_failure(base, depth + 1);
        if (!inner.empty()) return name + " cannot be default-constructed, because its base class " + inner;
    }

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node) continue;
        Type* ft = nullptr;
        std::string field;

        if (m.node->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(m.node);
            if (v->get_type_info().modifiers.has(RM::Static) || v->has_initializer()) continue;
            ft = m_types.canonicalize(v->get_type_info());
            field = std::string(v->get_name());
        } else if (m.node->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
            if (a->get_array_modifiers().has(RM::Static) || a->has_initializer()) continue;
            ft = array_symbol_type(a);
            field = std::string(a->m_name);
        } else {
            continue;
        }

        if (!ft) continue;
        if (ft->is_reference()) return name + " cannot be default-constructed, because its reference member '" + field + "' has no initializer";
        const std::string inner = default_construct_failure(ft, depth + 1);
        if (!inner.empty()) return name + " cannot be default-constructed, because its member '" + field + "' has no initializer and " + inner;
    }

    return {};
}

void TypeWalker::check_member_initialization(nodes::ConstructorDeclaration* ct, const std::vector<const Symbol*>& initialized) {
    using RM = modifiers::RawModifiers;
    if (!ct || !ct->symbol || ct->is_deleted() || !ct->symbol->owner) return;
    Symbol* owner = ct->symbol->owner->owner_symbol;
    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return;
    auto* rd = static_cast<nodes::RecordDeclaration*>(owner->decl);

    if (rd->is_union()) {
        if (initialized.size() > 1) SemanticError::emit(m_reporter, file_of(ct), ct->line, "Union", "a constructor of union '" + std::string(rd->get_name()) + "' can initialize only one member, because only one member holds a value at a time");
        return;
    }

    if (std::find(initialized.begin(), initialized.end(), owner) != initialized.end()) {
        if (initialized.size() > 1) SemanticError::emit(m_reporter, file_of(ct), ct->line, "Initialization", "a constructor that hands off to another constructor of '" + std::string(rd->get_name()) + "' can't initialize anything else; that constructor initializes everything");

        for (nodes::ASTNode* e : ct->get_init_list()) {
            Symbol* used = nullptr;
            if (e && e->kind == K::CallExpression) used = static_cast<nodes::CallExpression*>(e)->resolved;
            else if (e && e->kind == K::BraceConstructExpression) used = static_cast<nodes::BraceConstructExpression*>(e)->ctor;
            if (used && used == ct->symbol) SemanticError::emit(m_reporter, file_of(ct), ct->line, "Initialization", "this constructor hands off to itself, so it would never finish");
        }

        return;
    }

    const std::string where = ct->is_defaulted() ? "this defaulted constructor cannot initialize it" : "initialize it in this constructor's initializer list";

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        Type* base = m_types.canonicalize(*info);
        Symbol* bs = record_symbol_of(base);
        if (bs && std::find(initialized.begin(), initialized.end(), bs) == initialized.end()) {
            const std::string inner = default_construct_failure(base);
            if (!inner.empty()) SemanticError::emit(m_reporter, file_of(ct), ct->line, "Initialization", "the base class '" + type_str(base) + "' is never initialized, and " + inner + "; " + where);
        }
    }

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node) continue;
        Type* ft = nullptr;
        const Symbol* sym = nullptr;
        std::string field;
        bool is_const = false;

        if (m.node->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(m.node);
            if (v->get_type_info().modifiers.has(RM::Static) || v->has_initializer()) continue;
            ft = m_types.canonicalize(v->get_type_info());
            sym = v->symbol;
            field = std::string(v->get_name());
            is_const = v->get_type_info().modifiers.has(RM::Const);
        } else if (m.node->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
            if (a->get_array_modifiers().has(RM::Static) || a->has_initializer()) continue;
            ft = array_symbol_type(a);
            sym = a->symbol;
            field = std::string(a->m_name);
        } else {
            continue;
        }

        if (!ft || !sym || std::find(initialized.begin(), initialized.end(), sym) != initialized.end()) continue;
        std::string why;
        if (ft->is_reference()) why = "it is a reference";
        else if (is_const && !m_types.strip_cv(ft)->is_record()) why = "it is const";
        else why = default_construct_failure(ft);
        if (!why.empty()) SemanticError::emit(m_reporter, file_of(ct), ct->line, "Initialization", "member '" + field + "' is never initialized, and " + why + "; " + where);
    }
}

bool TypeWalker::prefer_rvalue_ref_for_temporaries(std::vector<CandidateMatch>& matches, const std::vector<Symbol*>& cands, const std::vector<bool>& member, const std::vector<Type*>& operands) {
    auto plain = [this](Type* t) -> Type* {
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    };

    std::vector<std::vector<Type*>> params(cands.size());
    for (std::size_t k = 0; k < cands.size(); ++k) {
        if (!matches[k].viable || !cands[k] || !cands[k]->decl) continue;
        const nodes::FunctionParameters* ps = nullptr;
        if (cands[k]->decl->kind == K::FunctionDeclaration) ps = static_cast<nodes::FunctionDeclaration*>(cands[k]->decl)->get_parameters();
        else if (cands[k]->decl->kind == K::OperatorFunctionDeclaration) ps = static_cast<nodes::OperatorFunctionDeclaration*>(cands[k]->decl)->get_parameters();
        if (!ps) continue;
        EnvGuard g(m_types, env_of_symbol(cands[k]));
        for (const nodes::FunctionParameter* p : ps->m_params) params[k].push_back(m_types.canonicalize(p->get_type()));
    }
    bool changed = false;
    nodes::ASTNode* operand_nodes[2] = { nullptr, nullptr };
    if (m_operator_site && m_operator_site->kind == K::BinaryExpression) {
        operand_nodes[0] = static_cast<nodes::BinaryExpression*>(m_operator_site)->left;
        operand_nodes[1] = static_cast<nodes::BinaryExpression*>(m_operator_site)->right;
    }

    for (std::size_t k = 0; k < cands.size(); ++k) {
        if (!matches[k].viable) continue;
        const std::size_t off = member[k] ? 1 : 0;

        for (std::size_t j = 0; j < params[k].size() && j + off < operands.size(); ++j) {
            Type* pt = params[k][j];
            if (!pt || !pt->is_reference()) continue;
            auto* rt = static_cast<ReferenceType*>(pt);
            nodes::ASTNode* on = j + off < 2 ? operand_nodes[j + off] : nullptr;
            if (rt->ref_qual() == RefQual::RValue && on && is_lvalue(on) && plain(operands[j + off]) == m_types.strip_cv(rt->referent())) {
                matches[k].viable = false;
                changed = true;
                break;
            }
            if (rt->ref_qual() != RefQual::LValue || !rt->referent()->cv().is_const) continue;
            Type* target = m_types.strip_cv(rt->referent());
            nodes::ASTNode* node = j + off < 2 ? operand_nodes[j + off] : nullptr;
            const bool temporary = plain(operands[j + off]) != target || (node && !is_lvalue(node));
            if (!temporary) continue;

            for (std::size_t l = 0; l < cands.size(); ++l) {
                if (l == k || !matches[l].viable || params[l].size() != params[k].size() || member[l] != member[k]) continue;
                bool twin = true;

                for (std::size_t i = 0; i < params[k].size() && twin; ++i) {
                    if (i != j) { twin = params[l][i] == params[k][i]; continue; }
                    Type* q = params[l][i];
                    twin = q && q->is_reference() && static_cast<ReferenceType*>(q)->ref_qual() == RefQual::RValue && m_types.strip_cv(static_cast<ReferenceType*>(q)->referent()) == target;
                }

                if (twin) { matches[k].viable = false; changed = true; break; }
            }

            if (!matches[k].viable) break;
        }
    }

    return changed;
}

auto TypeWalker::resolve_operator(nodes::OverloadableOperator want, const std::vector<Type*>& operands) -> OpResult {
    std::vector<CandidateMatch> matches;
    std::vector<Symbol*>        cands;
    std::vector<bool>           member;

    if (!operands.empty()) {
        std::vector<Type*> rest(operands.begin() + 1, operands.end());
        collect_member_ops(operands[0], want, rest, matches, cands, member);
    }

    collect_free_ops(want, operands, matches, cands, member);
    OpResult res;
    if (cands.empty()) return res;                       
    std::vector<bool> from_template;
    for (Symbol* s : cands) from_template.push_back(is_template_instance(s));
    Selection sel = select_overload(matches, from_template);
    if (sel.status == SelectStatus::Ambiguous && prefer_rvalue_ref_for_temporaries(matches, cands, member, operands)) sel = select_overload(matches, from_template);
    res.status = sel.status;
    if (sel.status != SelectStatus::Ok) return res;
    Symbol* pick   = cands[sel.index];
    {
        std::vector<Symbol*> origins;
        for (Symbol* c : cands) origins.push_back(overload_origin(c));
        remember_overloads(nullptr, matches, origins, sel.index);
    }
    if (from_template[sel.index]) if (Instantiator::Instantiation* I = m_inst->owning(pick)) ensure_typed(I);
    ensure_member_body(pick);
    res.chosen     = pick;
    res.via_member = member[sel.index];
    report_if_deleted(pick);
    EnvGuard g(m_types, env_of_symbol(pick));
    res.type       = pick->type ? m_types.canonicalize(*pick->type) : nullptr;
    return res;
}

nodes::RecordDeclaration* TypeWalker::record_decl_of(Type* t, Symbol*& owner) {
    owner = nullptr;
    t = m_types.strip_cv(t);
    if (!t || !t->is_record() || t->is_dependent()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);
    owner = rt->decl();

    if (rt->is_instantiation() && m_types.record_scope_hook) {
        Scope* sc = m_types.record_scope_hook(rt);
        owner = sc ? sc->owner_symbol : nullptr;
    }

    if (!owner || !owner->decl || owner->decl->kind != K::RecordDeclaration) return nullptr;
    return static_cast<nodes::RecordDeclaration*>(owner->decl);
}

int TypeWalker::self_ref_kind(const nodes::FunctionParameters* ps, Type* self) {
    if (!ps || ps->size() != 1 || !(*ps)[0]) return 0;
    Type* t = m_types.canonicalize((*ps)[0]->get_type());
    if (!t || !t->is_reference()) return 0;
    auto* r = static_cast<ReferenceType*>(t);
    Type* v = m_types.strip_cv(r->referent());
    if (v != m_types.strip_cv(self) && record_symbol_of(v) != record_symbol_of(self)) return 0;
    return r->ref_qual() == RefQual::RValue ? 2 : 1;
}

void TypeWalker::special_decls(nodes::RecordDeclaration* rd, Type* self, int& copy, int& move) {
    copy = 0;
    move = 0;

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node || m.node->kind != K::ConstructorDeclaration) continue;
        auto* c = static_cast<nodes::ConstructorDeclaration*>(m.node);
        const int kind = self_ref_kind(c->get_parameters(), self);
        const int state = c->is_deleted() ? 2 : 1;
        if (kind == 1) copy = state;
        if (kind == 2) move = state;
    }
}

std::string TypeWalker::special_failure(Type* t, SpecialOp op, int depth) {
    using RM = modifiers::RawModifiers;
    if (depth > 64) return {};
    Symbol* owner = nullptr;
    nodes::RecordDeclaration* rd = record_decl_of(t, owner);
    if (!rd) return {};
    Type* self = m_types.strip_cv(t);
    EnvGuard g(m_types, env_of_symbol(owner));
    int copy = 0, move = 0;
    special_decls(rd, self, copy, move);

    switch (op) {
        case SpecialOp::CopyCtor:
            if (copy == 2) return "its copy constructor is deleted";
            if (copy == 1) return {};
            if (move == 1) return "it has its own move constructor, so it gets no copy constructor";
            break;

        case SpecialOp::CopyAssign:
            if (copy == 2) return "its copy constructor is deleted, so it cannot be copy-assigned either";
            if (move == 1) return "it has its own move constructor, so it gets no copy assignment";
            break;

        case SpecialOp::MoveCtor:
            if (move == 1) return {};

            if (move == 2 || copy == 1) {
                const std::string why = special_failure(t, SpecialOp::CopyCtor, depth + 1);
                return why.empty() ? why : (move == 2 ? "its move constructor is deleted and it cannot be copied instead: " : "it moves by copying, and ") + why;
            }

            break;

        case SpecialOp::MoveAssign:
            if (move == 1) return "it has its own move constructor but no 'operator(=)' that takes it by '&&'";

            if (move == 2 || copy == 1) {
                const std::string why = special_failure(t, SpecialOp::CopyAssign, depth + 1);
                return why.empty() ? why : (move == 2 ? "its move constructor is deleted and it cannot be copy-assigned instead: " : "it moves by copying, and ") + why;
            }

            break;
    }

    const bool assigning = op == SpecialOp::CopyAssign || op == SpecialOp::MoveAssign;
    const char* verb = op == SpecialOp::CopyCtor ? "copied" : op == SpecialOp::MoveCtor ? "moved" : op == SpecialOp::CopyAssign ? "copy-assigned" : "move-assigned";

    auto check = [&](const std::string& what, Type* ft, bool is_const) -> std::string {
        if (!ft) return {};
        if (assigning && ft->is_reference()) return what + " is a reference";
        if (assigning && (is_const || ft->cv().is_const)) return what + " is const";
        Type* v = ft->is_reference() ? nullptr : m_types.strip_cv(ft);
        while (v && v->is_array()) v = m_types.strip_cv(static_cast<ArrayType*>(v)->element());
        if (!v || !v->is_record()) return {};
        const std::string inner = special_failure(v, op, depth + 1);
        return inner.empty() ? inner : what + " of type '" + type_str(v) + "' cannot be " + verb + ", because " + inner;
    };

    for (parser_types::TypeInfo* info : rd->all_bases()) {
        if (std::string why = check("its base class", m_types.canonicalize(*info), false); !why.empty()) return why;
    }

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node) continue;
        std::string why;

        if (m.node->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(m.node);
            const auto& mods = v->get_type_info().modifiers;
            if (mods.has(RM::Static)) continue;
            why = check("member '" + std::string(v->get_name()) + "'", m_types.canonicalize(v->get_type_info()), mods.has(RM::Const) && !mods.has(RM::Mutable));
        } else if (m.node->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(m.node);
            const auto& mods = a->get_array_modifiers();
            if (mods.has(RM::Static)) continue;
            why = check("member '" + std::string(a->m_name) + "'", array_symbol_type(a), mods.has(RM::Const));
        }

        if (!why.empty()) return why;
    }

    return {};
}

bool TypeWalker::is_xvalue(nodes::ASTNode* e) {
    if (e && e->kind == K::CastExpression) {
        auto* c = static_cast<nodes::CastExpression*>(e);
        if (c->get_cast_kind() != nodes::CastExpression::CastKind::Static) return false;
        Type* dst = m_types.canonicalize(c->get_target());
        return dst && dst->is_reference() && static_cast<ReferenceType*>(dst)->ref_qual() == RefQual::RValue;
    }
    if (!e || e->kind != K::CallExpression) return false;
    if (static_cast<nodes::CallExpression*>(e)->returns_rvalue_ref) return true;
    Symbol* fn = static_cast<nodes::CallExpression*>(e)->resolved;
    if (!fn) return false;
    if (!fn->type) return false;
    EnvGuard g(m_types, env_of_symbol(fn));
    Type* ft = m_types.canonicalize(*fn->type);
    if (!ft || !ft->is_function()) return false;
    Type* ret = static_cast<FunctionType*>(ft)->ret();
    return ret && ret->is_reference() && static_cast<ReferenceType*>(ret)->ref_qual() == RefQual::RValue;
}

bool TypeWalker::implicitly_moved(nodes::ASTNode* e, nodes::ASTNode* site) {
    if (!site || site->kind != K::ReturnStatement || !e || e->kind != K::Identifier) return false;
    Symbol* s = static_cast<nodes::Identifier*>(e)->resolved;
    if (!s || (s->kind != SymbolKind::Variable && s->kind != SymbolKind::Parameter) || !s->owner) return false;
    if (s->owner->kind != Scope::Kind::Function && s->owner->kind != Scope::Kind::Block && s->owner->kind != Scope::Kind::Lambda) return false;
    if (!s->decl) return false;

    if (s->decl->kind == K::VariableDeclaration) {
        auto* v = static_cast<nodes::VariableDeclaration*>(s->decl);
        if (v->get_type_info().modifiers.has(modifiers::RawModifiers::Static)) return false;
        Type* t = m_types.canonicalize(v->get_type_info());
        return t && !t->is_reference();
    }

    if (s->decl->kind == K::FunctionParameter) {
        Type* t = m_types.canonicalize(static_cast<nodes::FunctionParameter*>(s->decl)->get_type());
        return t && !t->is_reference();
    }

    return false;
}

void TypeWalker::check_copy_or_move(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site) {
    if (!expr || !from || !to || to->is_reference() || from->is_error()) return;
    Type* target = m_types.strip_cv(to);
    if (!target || !target->is_record() || target->is_dependent()) return;
    Type* source = m_types.strip_cv(from->is_reference() ? static_cast<ReferenceType*>(from)->referent() : from);
    if (!source || !source->is_record()) return;
    if (source != target && record_symbol_of(source) != record_symbol_of(target) && !is_derived_record(source, target, m_types)) return;

    const bool assigning = site && site->kind == K::BinaryExpression && static_cast<nodes::BinaryExpression*>(site)->op == tokenizing::Token::Kind::Equal;
    const bool lvalue    = is_lvalue(expr) && !implicitly_moved(expr, site);
    if (!assigning && !lvalue && !is_xvalue(expr)) return;

    const SpecialOp op = assigning ? (lvalue ? SpecialOp::CopyAssign : SpecialOp::MoveAssign) : (lvalue ? SpecialOp::CopyCtor : SpecialOp::MoveCtor);
    const std::string why = special_failure(target, op, 0);
    if (why.empty()) return;
    const char* what = op == SpecialOp::CopyCtor ? "copy" : op == SpecialOp::MoveCtor ? "move" : op == SpecialOp::CopyAssign ? "copy-assign" : "move-assign";
    SemanticError::emit(m_reporter, file_of(site ? site : expr), (site ? site : expr)->line, "Deleted", "cannot " + std::string(what) + " '" + type_str(target) + "': " + why);
}

bool TypeWalker::report_if_deleted(Symbol* fn) {
    if (!fn || !fn->decl) return false;
    std::string what;

    if (fn->decl->kind == K::OperatorFunctionDeclaration) {
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(fn->decl);
        if (!od->is_deleted()) return false;
        what = od->is_conversion() ? "the conversion to '" + type_str(m_types.canonicalize(od->get_conversion_type())) + "'" : "'operator(" + std::string(nodes::overloadable_operator_name(od->get_overload())) + ")'";
    } else if (fn->decl->kind == K::ConstructorDeclaration) {
        if (!static_cast<nodes::ConstructorDeclaration*>(fn->decl)->is_deleted()) return false;
        what = "this constructor";
    } else {
        return false;
    }

    if (m_quiet_deleted || !m_site) return true;
    Symbol* owner = fn->owner ? fn->owner->owner_symbol : nullptr;
    const std::string of = owner && owner->decl && owner->decl->kind == K::RecordDeclaration ? " of '" + std::string(owner->name) + "'" : std::string();
    SemanticError::emit(m_reporter, file_of(m_site), m_site->line, "Deleted", what + of + " is deleted and cannot be used");
    return true;
}

auto TypeWalker::resolve_incdec(Type* recv, nodes::OverloadableOperator want, bool postfix) -> OpResult {
    const std::size_t member_n = postfix ? 1u : 0u;   
    const std::size_t free_n   = postfix ? 2u : 1u;
    std::vector<CandidateMatch> matches; std::vector<Symbol*> cands; std::vector<bool> member;

    auto consider = [&](Symbol* o, bool is_member) {
        if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) return;
        auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(o->decl);
        if (od->get_overload() != want) return;
        std::vector<ParamShape> sh = shapes_of(o);
        if (sh.size() != (is_member ? member_n : free_n)) return;       
        ConversionRank r = is_member ? ConversionRank::Exact : rank_conversion(recv, sh[0].element, m_types);  
        if (r == ConversionRank::None) return;
        CandidateMatch m; m.viable = true; m.ranks = { r };
        matches.push_back(m); cands.push_back(o); member.push_back(is_member);
    };

    const std::string lowered = "operator(" + std::string(nodes::overloadable_operator_name(want)) + ")";
    nodes::ASTNode* site = m_operator_site ? m_operator_site : m_site;
    Type* int_t = m_types.builtin(parser_types::PrimitiveType::BaseKind::Int);

    auto consider_template = [&](Symbol* o, bool is_member) {
        if (!o->template_decl || !m_inst || !site) return;
        std::vector<Type*> args;
        if (!is_member) args.push_back(recv);
        if (postfix) args.push_back(int_t);
        auto* I = m_inst->instantiate_for_call(o, {}, args, site);
        if (!I || !I->sym || I->fn_shapes.size() != args.size()) return;
        CandidateMatch m = match_arguments(I->fn_shapes, args, m_types);
        if (!m.viable) return;
        if (is_member) m.ranks.assign(1, ConversionRank::Exact);
        else m.ranks.resize(1);
        matches.push_back(std::move(m)); cands.push_back(I->sym); member.push_back(is_member);
    };

    if (Scope* ms = member_scope_of(recv)) {
        for (Symbol* o = ms->find_member("operator"); o; o = o->next_overload) consider(o, true);
        for (Symbol* o = ms->find_member(lowered); o; o = o->next_overload) consider_template(o, true);
    }

    std::unordered_set<const Scope*> seen;
    auto scan_free = [&](Scope* s) {
        if (!s || !seen.insert(s).second) return;
        for (Symbol* o = s->find_local("operator"); o; o = o->next_overload) consider(o, false);
        for (Symbol* o = s->find_local(lowered); o; o = o->next_overload) consider_template(o, false);
    };

    for (Scope* s = m_current; s; s = s->parent) {
        if (s->node && s->node->kind == K::RecordDeclaration) continue;
        scan_free(s);
    }

    if (Symbol* rec = record_symbol_of(recv)) {
        for (Scope* s = rec->owner; s; s = s->parent) if (s->kind != Scope::Kind::Record) scan_free(s);
    }

    OpResult res;
    if (cands.empty()) return res;
    std::vector<bool> from_template;
    for (Symbol* s : cands) from_template.push_back(is_template_instance(s));
    Selection sel = select_overload(matches, from_template);
    res.status = sel.status;
    if (sel.status != SelectStatus::Ok) return res;
    Symbol* pick = cands[sel.index];
    {
        std::vector<Symbol*> origins;
        for (Symbol* c : cands) origins.push_back(overload_origin(c));
        remember_overloads(nullptr, matches, origins, sel.index);
    }
    if (from_template[sel.index]) if (Instantiator::Instantiation* I = m_inst->owning(pick)) ensure_typed(I);
    res.chosen = pick; res.via_member = member[sel.index];
    report_if_deleted(pick);
    EnvGuard g(m_types, env_of_symbol(pick));
    res.type = pick->type ? m_types.canonicalize(*pick->type) : nullptr;
    return res;
}

Type* TypeWalker::template_param_type(Symbol* s) {
    if (!s || s->kind != SymbolKind::TemplateParam || !s->decl || s->decl->kind != K::TemplateParameter) return nullptr;
    if (!static_cast<nodes::TemplateParameter*>(s->decl)->is_type_param()) return nullptr;
    Type* t = m_types.apply_subst(m_types.type_param(s, CV{}));
    return (t && !t->is_dependent()) ? t : nullptr;
}

Type* TypeWalker::constructed_type(nodes::ASTNode* callee, Symbol* callee_sym, nodes::CallExpression* call) {
    if (callee_sym && callee_sym->kind == SymbolKind::TemplateParam) {
        if (Type* t = template_param_type(callee_sym)) return t;
    }

    if (callee_sym && callee_sym->kind == SymbolKind::TypeAlias && callee_sym->type) {
        parser_types::TypeInfo use;
        use.resolved = callee_sym;
        if (call) use.template_args = call->m_template_args.to_vector();
        Type* t = m_types.canonicalize(use);
        if (t && !t->is_error()) return m_types.strip_cv(t);
    }

    if (callee_sym && callee_sym->kind == SymbolKind::Type && !callee_sym->template_decl && call && (!call->m_template_args.empty() || call->has_angle_args) && m_inst) {
        Symbol* primary = m_inst->primary_of_instance(callee_sym);
        if (primary && primary != callee_sym && primary->template_decl) callee_sym = primary;
    }

    if (callee_sym && callee_sym->kind == SymbolKind::Type && callee_sym->decl && callee_sym->decl->kind == K::RecordDeclaration && !callee_sym->template_decl) {
        return record_type_of(callee_sym);
    }

    if (callee_sym && callee_sym->kind == SymbolKind::Type && callee_sym->template_decl && call && (!call->m_template_args.empty() || call->has_angle_args) && m_inst) {
        bool ok = true;
        std::vector<Instantiator::Arg> args = m_inst->canon_args(call->m_template_args, ok);
        if (!ok) return nullptr;
        Instantiator::Instantiation* I = m_inst->instantiate(callee_sym, std::move(args), call);
        return (I && I->type && I->type->is_record()) ? I->type : m_types.error_();
    }

    if (callee && callee->kind == K::TemplateInstantiation) {
        Type* t = type_of(callee);
        if (t && t->is_record()) return t;
    }

    return nullptr;
}

Symbol* TypeWalker::resolve_construction(Type* constructed, const std::vector<Type*>& args, nodes::ASTNode* site) {
    Type* t = m_types.strip_cv(constructed);
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);

    if (rt->is_instantiation() && m_inst) {
        Instantiator::Instantiation* I = m_inst->for_record(rt, site);
        if (!I || !I->sym) return nullptr;
        ensure_typed(I);
        EnvGuard g(m_types, &I->env);
        return resolve_constructor(I->sym, args, site);
    }

    return resolve_constructor(rt->decl(), args, site);
}

void TypeWalker::type_member_init(nodes::ASTNode* e) {
    if (!e || e->kind != K::CallExpression) { walk_expr(e); return; }
    auto* c = static_cast<nodes::CallExpression*>(e);
    Symbol* target = (c->m_callee && c->m_callee->kind == K::Identifier) ? static_cast<nodes::Identifier*>(c->m_callee)->resolved : nullptr;
    if (!target || target->kind != SymbolKind::Variable) { m_allow_abstract = true; walk_expr(e); m_allow_abstract = false; return; }
    std::vector<Type*> args;
    for (nodes::ASTNode* a : c->m_arguments) { walk_expr(a); args.push_back(type_of(a)); }
    Type* ft = symbol_type(target);
    assign_typed(c->m_callee, ft, VC::LValue);
    assign_typed(e, m_types.void_(), VC::RValue);
    if (!ft || ft->is_error()) return;
    for (Type* a : args) if (!a || a->is_error()) return;

    if (ft->is_reference()) {
        if (args.size() == 1) check_binding(c->m_arguments[0], args[0], ft, e);
        else SemanticError::emit(m_reporter, file_of(e), e->line, "Initialization", "reference member '" + std::string(target->name) + "' needs exactly one initializer");
        return;
    }

    Type* fv = m_types.strip_cv(ft);

    if (record_symbol_of(fv)) {
        c->resolved = resolve_construction(fv, args, e);
        return;
    }

    if (args.size() == 1) { if (!is_dynamic_t(fv)) check_binding(c->m_arguments[0], args[0], fv, e); }
    else if (args.size() > 1) SemanticError::emit(m_reporter, file_of(e), e->line, "Initialization", "too many initializers for member '" + std::string(target->name) + "'");
}

std::vector<Type*> TypeWalker::param_types_for(Symbol* fn) {
    std::vector<Type*> out;
    if (!fn || !fn->decl || fn->decl->kind != K::FunctionDeclaration) return out;
    EnvGuard g(m_types, env_of_symbol(fn));
    if (const nodes::FunctionParameters* ps = static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_parameters())
        for (const nodes::FunctionParameter* p : ps->m_params) out.push_back(m_types.strip_cv(m_types.canonicalize(p->get_type())));
    return out;
}

std::string TypeWalker::pure_virtual_left(Type* t) {
    Scope* top = member_scope_of(t);
    if (!top) return {};
    std::vector<Scope*> chain;
    std::function<void(Scope*, int)> walk = [&](Scope* sc, int depth) {
        if (!sc || depth > 32) return;
        chain.push_back(sc);
        for (Scope* b : sc->bases) walk(b, depth + 1);
    };
    walk(top, 0);

    for (std::size_t i = 0; i < chain.size(); ++i) {
        for (Symbol* head : chain[i]->symbols) {
            for (Symbol* f = head; f; f = f->next_overload) {
                if (f->kind != SymbolKind::Function || !f->decl || f->decl->kind != K::FunctionDeclaration) continue;
                auto* fd = static_cast<nodes::FunctionDeclaration*>(f->decl);
                if (fd->has_body() || !fd->qualifiers().has(modifiers::FunctionQualifiers::Virtual)) continue;
                const std::vector<Type*> sig = param_types_for(f);
                bool overridden = false;

                for (std::size_t j = 0; j < i && !overridden; ++j) {
                    for (Symbol* g = chain[j]->find_local(f->name); g && !overridden; g = g->next_overload) {
                        if (!g->decl || g->decl->kind != K::FunctionDeclaration) continue;
                        if (!static_cast<nodes::FunctionDeclaration*>(g->decl)->has_body()) continue;
                        if (param_types_for(g) == sig) overridden = true;
                    }
                }

                if (!overridden) return std::string(f->name);
            }
        }
    }

    return {};
}

bool TypeWalker::lambda_copies(nodes::LambdaExpression* l, Symbol* s) {
    using M = nodes::LambdaCaptureItem::Mode;
    if (!l->m_captures) return false;
    bool all_by_value = false;

    for (const auto& c : l->m_captures->m_captures) {
        const bool names_it = c.symbol == s || c.resolved == s;
        if ((c.mode == M::ByValue || c.mode == M::InitByValue) && names_it) return true;
        if (c.mode == M::ByReference && names_it) return false;
        if (c.mode == M::AllByValue) all_by_value = true;
    }

    return all_by_value && s->decl != l;
}

bool TypeWalker::captured_const(Symbol* s) {
    if (!s || (s->kind != SymbolKind::Variable && s->kind != SymbolKind::Parameter) || !s->owner) return false;
    const Scope::Kind ok = s->owner->kind;
    if (ok == Scope::Kind::Module || ok == Scope::Kind::Namespace || ok == Scope::Kind::Record) return false;

    for (Scope* sc = m_current; sc; sc = sc->parent) {
        if (sc->kind == Scope::Kind::Lambda && sc->node && sc->node->kind == K::LambdaExpression) {
            auto* l = static_cast<nodes::LambdaExpression*>(sc->node);
            if (sc == s->owner && s->decl != l) return false;
            if (lambda_copies(l, s) && !l->get_qualifiers().has(modifiers::FunctionQualifiers::Mutable)) return true;
        }
        if (sc == s->owner) return false;
    }

    return false;
}

bool TypeWalker::is_polymorphic(Type* t) {
    Scope* top = member_scope_of(t);
    std::vector<Scope*> chain;
    std::function<void(Scope*, int)> walk = [&](Scope* sc, int depth) {
        if (!sc || depth > 32) return;
        chain.push_back(sc);
        for (Scope* b : sc->bases) walk(b, depth + 1);
    };
    walk(top, 0);
    using FQ = modifiers::FunctionQualifiers;

    for (Scope* sc : chain) {
        nodes::ASTNode* rd = sc->owner_symbol ? sc->owner_symbol->decl : nullptr;
        if (rd && rd->kind == K::RecordDeclaration) {
            for (const auto& member : static_cast<nodes::RecordDeclaration*>(rd)->get_members()) {
                nodes::ASTNode* n = member.node;
                if (!n) continue;
                if (n->kind == K::FunctionDeclaration && static_cast<nodes::FunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) return true;
                if (n->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(n)->qualifiers().has_any(FQ::Virtual | FQ::Override)) return true;
                if (n->kind == K::DestructorDeclaration && static_cast<nodes::DestructorDeclaration*>(n)->m_qualifiers.has(FQ::Virtual)) return true;
            }
        }
    }

    return false;
}

void TypeWalker::check_not_abstract(Type* t, nodes::ASTNode* site) {
    if (m_allow_abstract || !t || t->is_reference() || t->is_pointer() || !record_symbol_of(t) || t->is_dependent()) return;
    const std::string missing = pure_virtual_left(t);
    if (!missing.empty()) SemanticError::emit(m_reporter, file_of(site), site->line, "Abstract", "cannot create an object of abstract type '" + type_str(m_types.strip_cv(t)) + "' ('" + missing + "' has no implementation)");
}

bool TypeWalker::is_copy_source(Symbol* rec, Type* arg) {
    if (!rec || !arg) return false;
    Type* a = arg->is_reference() ? static_cast<ReferenceType*>(arg)->referent() : arg;
    a = m_types.strip_cv(a);
    if (!a || !a->is_record()) return false;
    Type* self = record_type_of(rec);
    if (a == m_types.strip_cv(self)) return true;
    if (record_symbol_of(a) == rec) return true;
    return is_derived_record(a, self, m_types);
}

Symbol* TypeWalker::resolve_constructor(Symbol* rec, const std::vector<Type*>& args, nodes::ASTNode* site) {
    if (!rec || !rec->inner_scope) return nullptr;
    Symbol* bucket = rec->inner_scope->find_member("constructor");   

    auto implicit_copy = [&]() {
        if (site && site->kind == K::CallExpression && !static_cast<nodes::CallExpression*>(site)->m_arguments.empty()) {
            check_copy_or_move(static_cast<nodes::CallExpression*>(site)->m_arguments[0], args[0], record_type_of(rec), site);
        }
    };

    auto check_default = [&]() {
        if (!site) return;
        const std::string why = default_construct_failure(record_type_of(rec));
        if (!why.empty()) SemanticError::emit(m_reporter, file_of(site), site->line, "Initialization", why);
    };

    if (!bucket) {
        if (args.size() == 1 && is_copy_source(rec, args[0])) { implicit_copy(); return nullptr; }
        if (!args.empty()) SemanticError::emit(m_reporter, file_of(site), site->line, "Overload", "'" + std::string(rec->name) + "' has no constructor that takes " + std::to_string(args.size()) + (args.size() == 1 ? " argument" : " arguments") + ": it declares no constructors, so it can only be made empty or copied from another '" + std::string(rec->name) + "'; add one such as 'constructor(int: value) { ... }', or fill its fields with a brace list like '" + std::string(rec->name) + "{...}'");
        else check_default();
        return nullptr;
    }

    std::vector<CandidateMatch> matches;
    std::vector<Symbol*>        cands;
    std::vector<Instantiator::Instantiation*> cand_inst;
    std::vector<Symbol*>        ctor_origins;
    Type* self = record_type_of(rec);
    std::vector<bool> lvalues;
    const ArenaVector<nodes::ASTNode*>* arg_nodes = m_ctor_arg_nodes;
    m_ctor_arg_nodes = nullptr;
    if (!arg_nodes && site && site->kind == K::CallExpression) arg_nodes = &static_cast<nodes::CallExpression*>(site)->m_arguments;
    if (!arg_nodes && site && site->kind == K::NewExpression) arg_nodes = &static_cast<nodes::NewExpression*>(site)->args;
    if (!arg_nodes && site && site->kind == K::BraceConstructExpression && static_cast<nodes::BraceConstructExpression*>(site)->m_init) arg_nodes = &static_cast<nodes::BraceConstructExpression*>(site)->m_init->m_elements;
    if (arg_nodes) for (nodes::ASTNode* a : *arg_nodes) lvalues.push_back(a && is_lvalue(a));
    struct CtorBraceHook {
        TypeContext& t; std::function<ConversionRank(std::size_t, Type*)> saved;
        CtorBraceHook(TypeContext& c, std::function<ConversionRank(std::size_t, Type*)> f) : t(c), saved(c.brace_arg_rank) { c.brace_arg_rank = std::move(f); }
        ~CtorBraceHook() { t.brace_arg_rank = saved; }
    } ctor_brace_hook(m_types, [this, arg_nodes](std::size_t a, Type* p) { return arg_nodes && a < arg_nodes->size() ? rank_brace_arg((*arg_nodes)[a], p) : ConversionRank::None; });

    for (Symbol* o = bucket; o; o = o->next_overload) {
        if (o->template_decl) {
            if (!m_inst) continue;
            auto* I = m_inst->instantiate_for_call(o, {}, args, site, lvalues.size() == args.size() ? &lvalues : nullptr);
            if (!I || !I->sym) continue;
            matches.push_back(match_with_constants(arg_nodes, I->fn_shapes, args));
            cands.push_back(I->sym);
            cand_inst.push_back(I);
            ctor_origins.push_back(o);
            continue;
        }
        if (!o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
        auto* cd = static_cast<nodes::ConstructorDeclaration*>(o->decl);
        if (cd->is_deleted() && self_ref_kind(cd->get_parameters(), self) == 2) continue;
        matches.push_back(match_with_constants(arg_nodes, shapes_of(o), args));
        cands.push_back(o);
        cand_inst.push_back(nullptr);
        ctor_origins.push_back(o);
    }

    bool has_template_ctor = false;
    for (Symbol* o = bucket; o; o = o->next_overload) has_template_ctor = has_template_ctor || o->template_decl != nullptr;

    if (cands.empty()) {
        if (args.size() == 1 && is_copy_source(rec, args[0])) { implicit_copy(); return nullptr; }
        if (args.empty() && has_template_ctor && site) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Initialization", "'" + type_str(m_types.strip_cv(self)) + "' has no constructor that takes no arguments: declaring a template constructor removes the automatic one, so add 'constructor() { }' or pass arguments");
            return nullptr;
        }
        if (!args.empty()) SemanticError::emit(m_reporter, file_of(site), site->line, "Overload", "no constructor of '" + std::string(rec->name) + "' takes these arguments" + candidate_notes(bucket, args, arg_nodes, rec->name));
        else check_default();
        return nullptr;
    }

    std::vector<std::size_t> fixed_params;
    for (Symbol* s : cands) fixed_params.push_back(fixed_parameter_count(s));
    std::vector<bool> from_template;
    for (Instantiator::Instantiation* I : cand_inst) from_template.push_back(I != nullptr);

    if (arg_nodes && arg_nodes->size() == args.size()) {
        std::vector<std::vector<ParamShape>> cand_shapes;
        for (std::size_t k = 0; k < cands.size(); ++k) cand_shapes.push_back(cand_inst[k] ? cand_inst[k]->fn_shapes : shapes_of(cands[k]));
        refine_by_value_category(matches, cand_shapes, *arg_nodes, 0);
    }

    {
        std::size_t viable = 0;
        for (const CandidateMatch& m : matches) if (m.viable) ++viable;

        if (viable > 1) {
            for (std::size_t k = 0; k < matches.size(); ++k) {
                if (!matches[k].viable || !cand_inst[k]) continue;
                for (std::size_t j : cand_inst[k]->forwarded_args) {
                    if (j < matches[k].ranks.size() && matches[k].ranks[j] == ConversionRank::Exact) matches[k].ranks[j] = ConversionRank::Promotion;
                }
            }
        }
    }

    Selection sel = select_overload(matches, from_template, fixed_params);
    if (sel.status == SelectStatus::NoMatch && args.size() == 1 && is_copy_source(rec, args[0])) { implicit_copy(); return nullptr; }
    if (sel.status == SelectStatus::NoMatch)   { SemanticError::emit(m_reporter, file_of(site), site->line, "Overload", "no constructor of '" + std::string(rec->name) + "' takes these arguments" + candidate_notes(bucket, args, arg_nodes, rec->name)); return nullptr; }
    if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(site), site->line, "constructor"); return nullptr; }
    remember_overloads(site, matches, ctor_origins, sel.index);

    if (cand_inst[sel.index] && args.size() == 1 && cand_inst[sel.index]->fn_shapes.size() == 1 && is_copy_source(rec, args[0])) {
        Type* p = cand_inst[sel.index]->fn_shapes[0].element;
        Type* a = args[0] && args[0]->is_reference() ? static_cast<ReferenceType*>(args[0])->referent() : args[0];
        Type* pv = p && p->is_reference() ? static_cast<ReferenceType*>(p)->referent() : p;
        const bool same = a && pv && m_types.strip_cv(a) == m_types.strip_cv(self) && m_types.strip_cv(pv) == m_types.strip_cv(self);
        const bool implicit_shape = p && (!p->is_reference()
            || (static_cast<ReferenceType*>(p)->ref_qual() == RefQual::LValue && pv->cv().is_const && !pv->cv().is_volatile)
            || (static_cast<ReferenceType*>(p)->ref_qual() == RefQual::RValue && pv->cv().bits() == 0));
        if (same && implicit_shape) { implicit_copy(); return nullptr; }
    }

    if (cand_inst[sel.index]) {
        ensure_typed(cand_inst[sel.index]);
        if (site && site->kind == K::CallExpression) {
            auto* call = static_cast<nodes::CallExpression*>(site);
            std::size_t i = 0;
            for (const ParamShape& p : cand_inst[sel.index]->fn_shapes) {
                if (p.is_pack || i >= call->m_arguments.size()) break;
                nodes::ASTNode* a = call->m_arguments[i++];
                check_copy_or_move(a, type_of(a), p.element, call);
            }
        }
        bind_brace_args(arg_nodes, cand_inst[sel.index]->fn_shapes, site);
        return cands[sel.index];
    }

    if (static_cast<nodes::ConstructorDeclaration*>(cands[sel.index]->decl)->is_deleted()) {
        if (args.size() == 1 && is_copy_source(rec, args[0])) implicit_copy();
        else report_if_deleted(cands[sel.index]);
    } else if (args.empty() && static_cast<nodes::ConstructorDeclaration*>(cands[sel.index]->decl)->is_defaulted()) {
        check_default();
    } else if (site && site->kind == K::CallExpression) {
        auto* call = static_cast<nodes::CallExpression*>(site);
        std::size_t i = 0;

        for (const ParamShape& p : shapes_of(cands[sel.index])) {
            if (p.is_pack || i >= call->m_arguments.size()) break;
            nodes::ASTNode* a = call->m_arguments[i++];
            check_copy_or_move(a, type_of(a), p.element, call);
        }
    }

    bind_brace_args(arg_nodes, shapes_of(cands[sel.index]), site);
    return cands[sel.index];
}

Symbol* TypeWalker::find_alloc_operator(Type* in_record, nodes::OverloadableOperator want) {
    if (Symbol* rec = record_symbol_of(in_record)) {
        if (rec->inner_scope)
            for (Symbol* o = rec->inner_scope->find_member("operator"); o; o = o->next_overload)
                if (o->decl && o->decl->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() == want) return o;
    }

    for (Scope* s = m_current; s; s = s->parent) {
        if (s->node && s->node->kind == K::RecordDeclaration) continue;

        for (Symbol* o = s->find_local("operator"); o; o = o->next_overload)
            if (o->decl && o->decl->kind == K::OperatorFunctionDeclaration && static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() == want) return o;
    }

    return nullptr;   
}

FunctionType* TypeWalker::as_function_type(Type* t) {
    if (!t) return nullptr;
    t = m_types.strip_cv(t);
    if (t->is_pointer()) t = m_types.strip_cv(static_cast<PointerType*>(t)->pointee());
    return (t && t->is_function()) ? static_cast<FunctionType*>(t) : nullptr;
}

bool TypeWalker::is_addressable(nodes::ASTNode* n) {
    if (is_lvalue(n)) return true;
    Symbol* s = callee_symbol(n);                 
    return s && s->kind == SymbolKind::Function;
}

bool TypeWalker::is_int_type(Type* t) {
    return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == parser_types::PrimitiveType::BaseKind::Int;
}

bool TypeWalker::is_numeric_t(Type* t) {
    if (!t || !t->is_builtin()) return false;
    using BK = parser_types::PrimitiveType::BaseKind;

    switch (static_cast<BuiltinType*>(t)->base()) {
        case BK::Int: case BK::Float: case BK::Char: return true;
        default: return false;
    }
}

bool TypeWalker::is_integral_t(Type* t) {
    if (!t || !t->is_builtin()) return false;
    using BK = parser_types::PrimitiveType::BaseKind;

    switch (static_cast<BuiltinType*>(t)->base()) {
        case BK::Int: case BK::Char: case BK::Bool: return true;
        default: return false;
    }
}

bool TypeWalker::is_bool_testable(Type* t) {
    if (!t) return true;                           
    if (t->is_error()) return true;                
    if (is_dynamic_t(t)) return true; // runtime-checked
    if (t->is_pointer() || t->is_null()) return true;   

    if (t->is_builtin()) {
        auto* b = static_cast<BuiltinType*>(t);
        if (b->is_bool()) return true;
        using BK = parser_types::PrimitiveType::BaseKind;
        switch (b->base()) { case BK::Int: case BK::Float: case BK::Char: return true; default: return false; }
    }

    if (t->is_record()) {
        ensure_conversions(t);
        return rank_user_defined(t, m_types.bool_(), m_types, true, nullptr) != ConversionRank::None;
    }
    return false;
}

bool TypeWalker::prim_is(const parser_types::TypeInfo& ti, parser_types::PrimitiveType::BaseKind bk) {
    if (!ti.type || !ti.type->is_primitive()) return false;
    return static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == bk;
}

bool TypeWalker::is_single(const nodes::ASTNode* n, nodes::SingleStatement::Variant v) {
    return n && n->kind == K::SingleStatement && static_cast<const nodes::SingleStatement*>(n)->variant == v;
}

bool TypeWalker::terminates(const nodes::ASTNode* s) {
    if (!s) return false;

    switch (s->kind) {
        case K::ReturnStatement:
            return true;

        case K::SingleStatement: {
            using V = nodes::SingleStatement::Variant;
            const auto v = static_cast<const nodes::SingleStatement*>(s)->variant;
            return v == V::Break || v == V::Continue || v == V::Repeat || v == V::Goto;
        }

        case K::ExpressionStatement:
            return static_cast<const nodes::ExpressionStatement*>(s)->expr && static_cast<const nodes::ExpressionStatement*>(s)->expr->kind == K::ThrowExpression;

        case K::BlockStatement: {
            const auto& body = static_cast<const nodes::BlockStatement*>(s)->get_statements();
            return !body.empty() && terminates(body.back());
        }

        case K::IfStatement: {
            auto* i = static_cast<const nodes::IfStatement*>(s);
            if (!i->get_else()) return false;                  
            for (const nodes::IfBranch* br : i->get_branches()) if (!terminates(br->get_body())) return false;
            return terminates(i->get_else());
        }

        default:
            return false;
    }
}

bool TypeWalker::breaks_out(const nodes::ASTNode* s) {
    if (!s) return false;

    switch (s->kind) {
        case K::SingleStatement: return is_single(s, nodes::SingleStatement::Variant::Break);
        case K::BlockStatement:
            for (const nodes::ASTNode* x : static_cast<const nodes::BlockStatement*>(s)->get_statements()) if (breaks_out(x)) return true;
            return false;
        case K::IfStatement: {
            auto* i = static_cast<const nodes::IfStatement*>(s);
            for (const nodes::IfBranch* br : i->get_branches()) if (breaks_out(br->get_body())) return true;
            return breaks_out(i->get_else());
        }
        case K::TryCatchStatement: {
            auto* t = static_cast<const nodes::TryCatchStatement*>(s);
            if (breaks_out(t->try_body)) return true;
            for (const nodes::TryCatchStatement* h = t; h; h = h->next_handler) if (h->catch_body && breaks_out(h->catch_body)) return true;
            return false;
        }
        default: return false;
    }
}

bool TypeWalker::constant_true(nodes::ASTNode* cond) {
    if (!cond) return true;
    if (cond->kind == K::VariableDeclaration) return false;
    bool ok = false;
    const bool t = ConstEvaluator::truthy(m_eval.eval(cond), ok);
    return ok && t;
}

bool TypeWalker::always_exits(nodes::ASTNode* s) {
    if (!s) return false;

    switch (s->kind) {
        case K::ReturnStatement: return true;

        case K::ExpressionStatement: {
            auto* e = static_cast<nodes::ExpressionStatement*>(s)->expr;
            return e && e->kind == K::ThrowExpression;
        }

        case K::BlockStatement:
            for (nodes::ASTNode* x : static_cast<nodes::BlockStatement*>(s)->get_statements()) if (always_exits(x)) return true;
            return false;

        case K::IfStatement: {
            auto* i = static_cast<nodes::IfStatement*>(s);

            for (nodes::IfBranch* br : i->get_branches()) {
                const int folded = br->is_constexpr ? br->constant_value : (constant_true(br->condition) ? 1 : -1);
                if (folded == 0) continue;
                if (!always_exits(br->body)) return false;
                if (folded == 1) return true;
            }

            return always_exits(i->else_branch);
        }

        case K::WhileStatement: {
            auto* w = static_cast<nodes::WhileStatement*>(s);
            return constant_true(w->condition) && !breaks_out(w->body);
        }

        case K::ForStatement: {
            auto* f = static_cast<nodes::ForStatement*>(s);
            return constant_true(f->condition) && !breaks_out(f->body);
        }

        case K::DoWhileStatement: {
            auto* d = static_cast<nodes::DoWhileStatement*>(s);
            if (breaks_out(d->body)) return false;
            return always_exits(d->body) || constant_true(d->condition);
        }

        case K::SwitchStatement: {
            auto* sw = static_cast<nodes::SwitchStatement*>(s);
            bool has_default = false;
            for (nodes::SwitchCase* c : sw->cases) {
                if (!c->value) has_default = true;
                for (nodes::ASTNode* x : c->body) if (breaks_out(x)) return false;
            }
            if (!has_default || sw->cases.empty()) return false;
            for (nodes::ASTNode* x : sw->cases.back()->body) if (always_exits(x)) return true;
            return false;
        }

        case K::TryCatchStatement: {
            auto* t = static_cast<nodes::TryCatchStatement*>(s);
            if (t->finally_body && always_exits(t->finally_body)) return true;
            if (!always_exits(t->try_body)) return false;
            for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) if (h->catch_body && !always_exits(h->catch_body)) return false;
            return true;
        }

        default: return false;
    }
}

void TypeWalker::check_chosen_overload_visibility(nodes::CallExpression* c) {
    const Symbol* chosen = c ? c->resolved : nullptr;
    if (!chosen || chosen->kind != SymbolKind::Function || !chosen->owner) return;
    if (c->m_callee && c->m_callee->kind == K::QualifiedIdentifier && static_cast<nodes::QualifiedIdentifier*>(c->m_callee)->from_compiler) return;
    const Symbol* first = chosen->owner->find_local(chosen->name);
    if (!first || !first->next_overload) return;
    const std::string why = visibility_violation(chosen, m_current, c);
    if (why.empty()) return;

    for (const Symbol* o = first; o; o = o->next_overload) {
        if (visibility_violation(o, m_current, c).empty()) {
            SemanticError::emit(m_reporter, file_of(c), c->line, "Visibility", why + "; another overload is visible here, but the arguments only match this one");
            return;
        }
    }
}

void TypeWalker::check_discarded_result(nodes::ASTNode* e) {
    if (!e || e->kind != K::CallExpression) return;
    const Symbol* s = static_cast<nodes::CallExpression*>(e)->resolved;
    if (!s || !s->decl) return;
    const modifiers::FunctionQualifiers* q = nullptr;
    std::string name(s->name);

    if (s->decl->kind == K::FunctionDeclaration) {
        q = &static_cast<const nodes::FunctionDeclaration*>(s->decl)->qualifiers();
    } else if (s->decl->kind == K::OperatorFunctionDeclaration) {
        q = &static_cast<const nodes::OperatorFunctionDeclaration*>(s->decl)->qualifiers();
        name = "operator";
    }

    if (!q || !q->has(modifiers::FunctionQualifiers::Nodiscard)) return;
    std::string detail = "the result of '" + name + "' is marked nodiscard but is ignored; wrap the call in discard_nodiscard(...) to ignore it on purpose";

    if (q->has_nodiscard_message()) {
        std::string_view msg = q->nodiscard_message();
        if (msg.size() >= 2 && (msg.front() == '"' || msg.front() == '`') && msg.back() == msg.front()) msg = msg.substr(1, msg.size() - 2);
        detail += " (" + std::string(msg) + ")";
    }

    SemanticWarning::emit(m_warnings, file_of(e), e->line, "Nodiscard", detail);
}

void TypeWalker::check_falls_off(Type* ret, nodes::ASTNode* body, std::string_view name, nodes::ASTNode* site) {
    if (!body || !ret || ret->is_error() || is_void_t(ret) || is_dynamic_t(ret) || ret->is_dependent() || generator_type(ret)) return;
    if (name == "main" && site && site->kind == K::FunctionDeclaration && m_returns.empty()) return;
    if (always_exits(body)) return;
    SemanticError::emit(m_reporter, file_of(site), site->line, "Return", "control can reach the end of '" + std::string(name) + "' without returning a value");
}

bool TypeWalker::marked_fallthrough(const nodes::SwitchCase* c) {
    using V = nodes::SingleStatement::Variant;
    if (c->has_fallthrough) return true;                      
    return !c->body.empty() && is_single(c->body.back(), V::Fallthrough);
}

bool TypeWalker::operator_arity_ok(nodes::OverloadableOperator op, bool is_member, std::size_t n, const char*& why) {
    using OP = nodes::OverloadableOperator;
        
    auto member_only = [&](std::initializer_list<std::size_t> ok) {
        if (!is_member) { why = "must be a member"; return false; }
        for (std::size_t c : ok) if (n == c) return true;
        why = "wrong parameter count"; return false;
    };

    auto fixed  = [&](std::size_t m, std::size_t f) { if (n != (is_member ? m : f)) { why = "wrong parameter count"; return false; } return true; };

    auto either = [&](std::size_t mlo, std::size_t mhi, std::size_t flo, std::size_t fhi) {
        std::size_t lo = is_member ? mlo : flo, hi = is_member ? mhi : fhi;
        if (n < lo || n > hi) { why = "wrong parameter count"; return false; } return true;
    };

    switch (op) {
        case OP::None:          why = "not an operator"; return false;

        case OP::LogicalNot:
        case OP::BitNot:        return fixed(0, 1);

        case OP::Plus:
        case OP::Minus:
        case OP::Star:
        case OP::BitAnd:        return either(0, 1, 1, 2);

        case OP::Increment:
        case OP::Decrement:     return either(0, 1, 1, 2);

        case OP::Subscript:     return member_only({1});
        case OP::Arrow:         return member_only({0});
        case OP::Conversion:    return member_only({0});
        case OP::Assign:        return member_only({1});
        case OP::Call:          if (!is_member) { why = "must be a member"; return false; } return true;  

        case OP::New:
        case OP::NewArray:
        case OP::Delete:
        case OP::DeleteArray:   if (n < 1) { why = "requires at least one parameter"; return false; } return true;

        default:                return fixed(1, 2);
    }
}

void TypeWalker::report_overflow(nodes::ASTNode* at, const std::string& detail) {
    if (m_types.overflow_errors) SemanticError::emit(m_reporter, file_of(at), at->line, "Overflow", detail + " (an error because of --check-overflow)");
    else SemanticWarning::emit(m_warnings, file_of(at), at->line, "Overflow", detail);
}

void TypeWalker::check_clamped_cast(nodes::CastExpression* c, Type* dst) {
    if (!c || !dst || dst->is_dependent()) return;
    Type* t = m_types.strip_cv(dst);
    if (!t || !t->is_builtin()) return;
    const ConstValue v = m_eval.eval(c);
    if (!v.ok() || v.fail != ConstValue::Fail::Clamped || !v.is_int()) return;
    report_overflow(c, "this cast's value doesn't fit in '" + type_str(t) + "', so it is clamped to " + v.i->to_string());
}

namespace {

const BuiltinType* integer_builtin(TypeContext& types, Type* t) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = types.strip_cv(t);
    if (!t || !t->is_builtin() || t->is_dependent()) return nullptr;
    const auto* b = static_cast<const BuiltinType*>(t);
    return b->base() == BK::Int ? b : nullptr;
}

bool fails_to_fit(const ConstValue& v) {
    return v.fail == ConstValue::Fail::Overflow || v.fail == ConstValue::Fail::Wrapped || v.fail == ConstValue::Fail::ShiftWidth;
}

} // namespace

namespace {

const parser_types::TypeInfo* declared_type_of(nodes::ASTNode* e) {
    semantics::Symbol* s = nullptr;
    if (e && e->kind == nodes::ASTNode::Kind::Identifier) s = static_cast<nodes::Identifier*>(e)->resolved;
    else if (e && e->kind == nodes::ASTNode::Kind::MemberAccessExpression) s = static_cast<nodes::MemberAccessExpression*>(e)->resolved;
    if (!s || !s->decl) return nullptr;
    if (s->decl->kind == nodes::ASTNode::Kind::VariableDeclaration) return &static_cast<nodes::VariableDeclaration*>(s->decl)->m_type_info;
    if (s->decl->kind == nodes::ASTNode::Kind::FunctionParameter) return &static_cast<nodes::FunctionParameter*>(s->decl)->get_type();
    return nullptr;
}

bool spelled_unsigned_integer(nodes::ASTNode* e) {
    const parser_types::TypeInfo* t = declared_type_of(e);
    if (!t || !t->type || !t->type->is_primitive() || !t->indirection.empty()) return false;
    const auto* p = static_cast<const parser_types::PrimitiveType*>(t->type);
    return p->is_unsigned() && p->base_kind() == parser_types::PrimitiveType::BaseKind::Int;
}

} // namespace

void TypeWalker::check_unsigned_comparison(nodes::BinaryExpression* b) {
    using TK = tokenizing::Token::Kind;
    if (!b || !b->left || !b->right || m_dead_constant > 0) return;
    const TK op = b->op;
    if (op != TK::LessThan && op != TK::GreaterThan && op != TK::LessEqual && op != TK::GreaterEqual && op != TK::LogicEqual && op != TK::NotEqual) return;
    nodes::ASTNode* var = nullptr;
    nodes::ASTNode* cst = nullptr;
    bool var_left = true;
    if (spelled_unsigned_integer(b->left)) { var = b->left; cst = b->right; }
    else if (spelled_unsigned_integer(b->right)) { var = b->right; cst = b->left; var_left = false; }
    if (!var) return;
    const ConstValue cv = m_eval.eval(cst);
    if (!cv.ok() || !cv.is_int()) return;
    const bool negative = cv.i->is_negative();
    const bool zero = cv.i->is_zero();
    if (!negative && !zero) return;
    TK o = op;
    if (!var_left) {
        if (o == TK::LessThan) o = TK::GreaterThan;
        else if (o == TK::GreaterThan) o = TK::LessThan;
        else if (o == TK::LessEqual) o = TK::GreaterEqual;
        else if (o == TK::GreaterEqual) o = TK::LessEqual;
    }
    int verdict = -1;
    if (negative) {
        if (o == TK::LessThan || o == TK::LessEqual || o == TK::LogicEqual) verdict = 0;
        else verdict = 1;
    } else {
        if (o == TK::GreaterEqual) verdict = 1;
        else if (o == TK::LessThan) verdict = 0;
    }
    if (verdict < 0) return;
    const std::string name = std::string(var->kind == nodes::ASTNode::Kind::Identifier ? static_cast<nodes::Identifier*>(var)->name : static_cast<nodes::MemberAccessExpression*>(var)->m_member);
    std::string text = "'" + name + "' is unsigned, so this comparison with " + cv.i->to_string() + " is always " + (verdict ? "true" : "false");
    if (negative) text += "; an unsigned value is never negative, so compare with std::npos or another real value";
    else if (verdict) text += "; a loop counting down with 'i >= 0' never ends, so count down with 'i > 0' and use i - 1";
    SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", text);
}

void TypeWalker::check_bad_constants(nodes::BinaryExpression* b) {
    using TK = tokenizing::Token::Kind;
    if (!b || !b->left || !b->right) return;
    if (m_dead_constant > 0) return;
    check_unsigned_comparison(b);
    const BuiltinType* lt = integer_builtin(m_types, type_of(b->left));
    const BuiltinType* rt = integer_builtin(m_types, type_of(b->right));
    if (!lt || !rt) return;
    const BuiltinType* result = integer_builtin(m_types, type_of(b));
    const bool assign = is_assignment_op(b->op);
    const BuiltinType* shown = assign || !result ? lt : result;
    const std::string tname = type_str(const_cast<BuiltinType*>(shown));

    const TK op = b->op;
    const bool shift = op == TK::DoubleLessThan || op == TK::DoubleGreaterThan || op == TK::ShiftLeftEqual || op == TK::ShiftRightEqual;
    const bool divide = op == TK::Slash || op == TK::Percent || op == TK::SlashEqual || op == TK::PercentEqual;
    const bool arith = op == TK::Plus || op == TK::Minus || op == TK::Asterisk || op == TK::DoubleAsterisk || shift;

    const ConstValue rv = m_eval.eval(b->right);
    const bool right_known = rv.ok() && rv.is_int();

    if (shift && right_known) {
        const unsigned bits = bit_width_of_rank(shown->width());
        if (rv.i->is_negative()) {
            SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", "can't shift by " + rv.i->to_string() + ": a shift count can't be negative");
            return;
        }
        if (bits && *rv.i >= WideInt(std::uint64_t(bits))) {
            SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", "can't shift a '" + tname + "' by " + rv.i->to_string() + ": it has " + std::to_string(bits) + " bits, so the count must be 0 to " + std::to_string(bits - 1) + "; cast the left side to a wider type first");
            return;
        }
    }

    if (divide && right_known && rv.i->is_zero()) {
        SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", std::string(op == TK::Percent || op == TK::PercentEqual ? "remainder" : "division") + " by zero: the right side is always 0");
        return;
    }

    if (!arith || assign || !right_known) return;
    const ConstValue lv = m_eval.eval(b->left);
    if (!lv.ok() || !lv.is_int()) return;
    const ConstValue v = m_eval.eval(b);
    if (!fails_to_fit(v)) return;
    if (v.fail == ConstValue::Fail::Wrapped) {
        SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", "this constant goes below zero, which '" + tname + "' can't hold; use a signed type, or check the order of the operands");
        return;
    }
    SemanticError::emit(m_reporter, file_of(b), b->line, "Constant", "this constant doesn't fit in '" + tname + "'; use a wider type, such as by writing a literal with a suffix like 'l' or casting one side first");
}

void TypeWalker::check_bad_constants(nodes::UnaryExpression* u) {
    using TK = tokenizing::Token::Kind;
    if (!u || !u->operand || u->op != TK::Minus) return;
    if (m_dead_constant > 0) return;
    const BuiltinType* t = integer_builtin(m_types, type_of(u));
    if (!t) return;
    const ConstValue inner = m_eval.eval(u->operand);
    if (!inner.ok() || !inner.is_int()) return;
    const ConstValue v = m_eval.eval(u);
    if (!fails_to_fit(v)) return;
    SemanticError::emit(m_reporter, file_of(u), u->line, "Constant", "negating this constant doesn't give a value '" + type_str(const_cast<BuiltinType*>(t)) + "' can hold");
}

void TypeWalker::check_constant_index(nodes::SubscriptExpression* s, Type* arr) {
    if (!s || !arr || !arr->is_array()) return;
    if (m_dead_constant > 0) return;
    const auto extent = static_cast<ArrayType*>(arr)->extent();
    if (!extent) return;
    const ConstValue iv = m_eval.eval(s->m_index);
    if (!iv.ok() || !iv.is_int()) return;
    if (iv.i->is_negative() || *iv.i >= WideInt(std::uint64_t(*extent))) {
        SemanticError::emit(m_reporter, file_of(s), s->line, "Constant", "index " + iv.i->to_string() + " is out of range for an array of " + std::to_string(*extent) + " elements, which has indexes 0 to " + (*extent ? std::to_string(*extent - 1) : std::string("none")));
    }
}

ConstValue TypeWalker::eval_checked(nodes::ASTNode* e, bool required) {
    ConstValue v = m_eval.eval(e);
    if (!e) return v;

    if (v.is_error()) {
        const bool already = fails_to_fit(v) && (e->kind == K::BinaryExpression || (e->kind == K::UnaryExpression && static_cast<nodes::UnaryExpression*>(e)->op == tokenizing::Token::Kind::Minus));
        if (required && !already) SemanticError::not_a_constant(m_reporter, file_of(e), e->line, fail_message(v.fail));
        return v;
    }

    if (!v.ok()) return v; 

    switch (v.fail) {
        case ConstValue::Fail::Precision:
            SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "constant expression lost precision: " + std::string(fail_message(v.fail)));
            break;

        case ConstValue::Fail::Wrapped:
            if (e->kind != K::BinaryExpression && e->kind != K::UnaryExpression) report_overflow(e, "constant expression went below zero: " + std::string(fail_message(v.fail)));
            break;

        case ConstValue::Fail::Undefined:
            if (v.is_special()) SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "this constant expression is undefined (for example, a division by zero), so its value is 'undefined'");
            break;

        case ConstValue::Fail::Indeterminate:
            if (v.is_special()) SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "this constant expression is an indeterminate form (such as 0/0, inf - inf or 0 ** 0), so its value is 'indeterminate'");
            break;

        case ConstValue::Fail::Domain:
            if (v.is_special()) SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "this constant expression has no real value (its result would be complex), so its value is 'nan'");
            break;

        default:
            break;
    }

    return v;
}

bool TypeWalker::reject_type_as_value(nodes::ASTNode* e) {
    if (!e) return false;
    Symbol* s = nullptr;
    std::string name;

    if (e->kind == K::Identifier) {
        auto* id = static_cast<nodes::Identifier*>(e);
        s = id->resolved;
        name = std::string(id->get_name());
    } else if (e->kind == K::QualifiedIdentifier) {
        auto* q = static_cast<nodes::QualifiedIdentifier*>(e);
        s = q->resolved;
        if (q->is_global()) name = "::";
        for (std::size_t i = 0; i < q->parts().size(); ++i) {
            if (i) name += "::";
            name += std::string(q->parts()[i]);
        }
    } else if (e->kind == K::MemberAccessExpression && static_cast<nodes::MemberAccessExpression*>(e)->is_scope()) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(e);
        s = m->resolved;
        name = std::string(m->get_member());
    } else {
        return false;
    }

    if (s && s->kind == SymbolKind::Import && s->import_target) s = s->import_target;
    if (!s) return false;

    std::string msg;
    switch (s->kind) {
        case SymbolKind::Type:
        case SymbolKind::TypeAlias:
        case SymbolKind::Enum:
            msg = "'" + name + "' is a type, not a value, so it can't be used here; make an object with '" + name + "(...)' or '" + name + "{...}', or use typeid(" + name + ") to get its name as a string";
            break;
        case SymbolKind::TemplateParam:
            if (!s->decl || s->decl->kind != K::TemplateParameter || !static_cast<nodes::TemplateParameter*>(s->decl)->is_type_param()) return false;
            msg = "'" + name + "' is a type template parameter, not a value, so it can't be used here; make an object with '" + name + "(...)' or '" + name + "{...}', or use typeid(" + name + ") to get its name as a string";
            break;
        case SymbolKind::Namespace:
            msg = "'" + name + "' is a namespace, not a value, so it can't be used here; name something inside it, as in '" + name + "::member'";
            break;
        case SymbolKind::Module:
            msg = "'" + name + "' is a module, not a value, so it can't be used here; name something inside it, as in '" + name + "::member'";
            break;
        case SymbolKind::Concept:
            msg = "'" + name + "' is a concept, not a value, so it can't be used here; check a type against it with '" + name + "<T>'";
            break;
        default:
            return false;
    }

    SemanticError::emit(m_reporter, file_of(e), e->line, "Type", msg);
    assign_typed(e, m_types.error_(), VC::RValue);
    return true;
}

std::string_view TypeWalker::callee_name(nodes::ASTNode* n) {
    if (!n) return "<callee>";
    switch (n->kind) {
        case K::Identifier:             return static_cast<nodes::Identifier*>(n)->get_name();
        case K::QualifiedIdentifier:    return static_cast<nodes::QualifiedIdentifier*>(n)->simple_name();
        case K::MemberAccessExpression: return static_cast<nodes::MemberAccessExpression*>(n)->get_member();
        default: return "<callee>";
    }
}

} // namespace semantics
} // namespace walnut
