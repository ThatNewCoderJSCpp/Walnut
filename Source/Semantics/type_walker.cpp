#include <cstdlib>
#include "Semantics/type_walker.hpp"
#include "Semantics/type_ops.hpp"
#include "Parser/Nodes/node_children.hpp"
#include <unordered_set>
#include <algorithm>
#include <deque>
#include "Lexer/numeric_literal.hpp"

namespace walnut {
namespace semantics {

TypeWalker::TypeWalker(
    TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings, Scope* root,
    Instantiator* inst, ThrowContext* throws 
) noexcept : m_types(types), m_reporter(reporter), m_warnings(warnings), m_root(root), m_current(root)
    , m_inst(inst), m_eval(types), m_throws(throws)
{
    m_eval.env_of = [this](Symbol* s) -> const SubstEnv* {
        Instantiator::Instantiation* I = m_inst ? m_inst->owning(s) : nullptr;
        return I ? &I->env : nullptr;
    };

    m_eval.concept_value = [this](nodes::TemplateInstantiation* t) { return concept_id_value(t); };
    m_eval.variable_template_value = [this](nodes::TemplateInstantiation* t) { return variable_template_value(t); };
    m_eval.scoped_member_value = [this](nodes::MemberAccessExpression* m) { return scoped_member_value(m); };

    m_saved_walk_hook = m_types.expr_walk_hook;
    m_saved_error_hook = m_types.type_error_hook;
    m_saved_conversions_hook = m_types.conversions_hook;
    m_types.conversions_hook = [this](Type* t) { ensure_conversions(t); };
    m_saved_range_element_hook = m_types.range_element_hook;
    m_saved_lifecycle_hook = m_types.lifecycle_trait_hook;
    m_types.lifecycle_trait_hook = [this](int trait, const std::vector<Type*>& args) { return lifecycle_trait(static_cast<parser_types::TypeTrait>(trait), args); };
    m_types.range_element_hook = [this](Type* t, nodes::ASTNode* site) { return range_element(t, site); };
    m_saved_member_value_hook = m_types.member_value_hook;
    m_types.member_value_hook = [this](Type* rec, std::string_view name, TemplateArg& out) {
        if (!member_value(rec, name, out)) return false;
        m_types.member_value_memo[{ rec, std::string(name) }] = out;
        return true;
    };
    m_types.template_conversion_hook = [this](Symbol* generic, Type* target) -> Symbol* {
        if (!m_inst) return nullptr;
        Instantiator::Instantiation* I = m_inst->instantiate_for_conversion(generic, target, m_site);
        if (!I || !I->sym) return nullptr;
        ensure_typed(I);
        return I->sym;
    };
    m_types.expr_walk_hook = [this](nodes::ASTNode* e) {
        nodes::ASTNode* saved = m_site;
        walk_expr(e);
        m_site = saved;
    };
    m_types.type_error_hook = [this](const std::string& msg) {
        if (!m_site) return;
        if (!m_reported_type_errors.insert({ m_site->file_id, m_site->line, msg }).second) return;
        SemanticError::emit(m_reporter, file_of(m_site), m_site->line, "Type", msg);
    };
    m_types.closure_hook = [this](Type* closure, Type* fn) -> bool {
        auto* gl = static_cast<nodes::LambdaExpression*>(static_cast<ClosureType*>(closure)->lambda());
        auto* ft = static_cast<FunctionType*>(fn);
        nodes::LambdaExpression* spec = specialize_lambda(gl, ft->params(), nullptr, true);
        FunctionType* sf = spec ? as_function_type(type_of(spec)) : nullptr;
        if (!sf) return false;
        Type* want = ft->ret();
        if (is_void_t(want)) return true;
        return sf->ret() && rank_conversion(sf->ret(), want, m_types) != ConversionRank::None;
    };

    if (m_inst) {
        m_saved_concept_value = m_inst->evaluator().concept_value;
        m_saved_variable_template_value = m_inst->evaluator().variable_template_value;
        m_saved_scoped_member_value = m_inst->evaluator().scoped_member_value;
        m_inst->evaluator().concept_value = [this](nodes::TemplateInstantiation* t) { return concept_id_value(t); };
        m_inst->evaluator().variable_template_value = [this](nodes::TemplateInstantiation* t) { return variable_template_value(t); };
        m_inst->evaluator().scoped_member_value = [this](nodes::MemberAccessExpression* m) { return scoped_member_value(m); };
        m_inst->check_constraints = [this](nodes::TemplateDeclaration* tmpl, const SubstEnv& env, nodes::ASTNode* site, bool quiet) {
            std::string failed;
            if (constraints_satisfied(tmpl, env, failed)) return true;
            m_last_constraint_failure = failed;
            if (!quiet) {
                const FileId file = file_of(site);
                const std::size_t line = site ? site->line : 0;
                const bool seen_template = m_reported_constraints.count({ tmpl, FileId(), 0 }) > 0;
                if (!(line == 0 && seen_template) && m_reported_constraints.insert({ tmpl, file, line }).second) {
                    m_reported_constraints.insert({ tmpl, FileId(), 0 });
                    SemanticError::constraints_not_satisfied(m_reporter, file, line, template_name_of(tmpl), failed);
                }
            }
            return false;
        };
    }
}

void TypeWalker::type_pending_instances() {
    if (!m_inst) return;

    for (int round = 0; round < 64; ++round) {
        std::vector<Instantiator::Instantiation*> pending;
        m_inst->for_each([&](Instantiator::Instantiation& I) { if (!I.typed && !I.speculative && I.decl) pending.push_back(&I); });
        if (pending.empty()) return;
        for (Instantiator::Instantiation* I : pending) ensure_typed(I);
    }
}

void TypeWalker::run(nodes::BlockStatement* program) {
    if (!program) return;
    m_current = m_root;
    for (nodes::ASTNode* s : program->get_statements()) build(s);
}

TypeWalker::ControlContextGuard::~ControlContextGuard() {
    w.resolve_gotos();
    w.m_loop_depth = loops;
    w.m_switch_depth = switches;
    w.m_in_finally = in_finally;
    w.m_loop_labels.swap(labels);
    w.m_stmt_path.swap(path);
    w.m_jump_labels.swap(jump_labels);
    w.m_jumps.swap(jumps);
}

void TypeWalker::build_list(Span<nodes::ASTNode* const> stmts) {
    for (std::size_t i = 0; i < stmts.size(); ++i) {
        m_stmt_path.emplace_back(stmts, i);
        build(stmts[i]);
        m_stmt_path.pop_back();
    }
}

void TypeWalker::resolve_gotos() {
    for (auto& [g, gpath] : m_jumps) {
        const std::pair<nodes::SingleStatement*, StmtPath>* target = nullptr;
        for (const auto& l : m_jump_labels) if (l.first->label == g->label) { target = &l; break; }

        if (!target) {
            SemanticError::emit(m_reporter, file_of(g), g->line, "Name", "there is no 'label " + std::string(g->label) + ";' in this function for 'goto " + std::string(g->label) + ";' to jump to");
            continue;
        }

        const StmtPath& lpath = target->second;
        if (lpath.empty() || gpath.empty()) continue;
        const std::size_t k = lpath.size() - 1;
        bool reachable = gpath.size() > k && gpath[k].first.data() == lpath[k].first.data();
        for (std::size_t i = 0; reachable && i < k; ++i) reachable = gpath[i].first.data() == lpath[i].first.data() && gpath[i].second == lpath[i].second;

        if (!reachable) {
            SemanticError::emit(m_reporter, file_of(g), g->line, "Jump", "'goto " + std::string(g->label) + ";' would jump into a block that it isn't inside; a goto can only reach a label in its own block or in a block around it (line " + std::to_string(target->first->line) + ")");
            continue;
        }

        const std::size_t from = gpath[k].second;
        const std::size_t to = lpath[k].second;
        const auto& list = lpath[k].first;

        for (std::size_t j = from + 1; j < to && j < list.size(); ++j) {
            nodes::ASTNode* s = list[j];
            std::string_view name;
            if (s && s->kind == K::VariableDeclaration) name = static_cast<nodes::VariableDeclaration*>(s)->get_name();
            else if (s && s->kind == K::ArrayDeclaration) name = static_cast<nodes::ArrayDeclaration*>(s)->m_name;
            else continue;
            SemanticError::emit(m_reporter, file_of(g), g->line, "Jump", "'goto " + std::string(g->label) + ";' jumps over the declaration of '" + std::string(name) + "' (line " + std::to_string(s->line) + "), which would leave it uninitialized; declare it before the goto, or put it and its uses in their own { } block");
            break;
        }
    }

    m_jumps.clear();
    m_jump_labels.clear();
}

TypeWalker::ControlContextGuard::ControlContextGuard(TypeWalker& tw) : w(tw), loops(tw.m_loop_depth), switches(tw.m_switch_depth), in_finally(tw.m_in_finally) {
    w.m_loop_depth = w.m_switch_depth = 0;
    w.m_in_finally = false;
    labels.swap(w.m_loop_labels);
    path.swap(w.m_stmt_path);
    jump_labels.swap(w.m_jump_labels);
    jumps.swap(w.m_jumps);
}

Symbol* TypeWalker::record_symbol_of(Type* t) {
    if (!t) return nullptr;
    t = m_types.strip_cv(t);
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();  
    if (t) t = m_types.strip_cv(t);
    return (t && t->is_record()) ? static_cast<RecordType*>(t)->decl() : nullptr;
}

void TypeWalker::collect_conversions(nodes::ASTNode* n) {
    if (!n) return;

    switch (n->kind) {
        case K::OperatorFunctionDeclaration:
            register_conversion(static_cast<nodes::OperatorFunctionDeclaration*>(n));
            break;

        case K::RecordDeclaration: {
            auto* r = static_cast<nodes::RecordDeclaration*>(n);
            for (nodes::RecordDeclaration::Member m : r->get_members()) collect_conversions(m.node);
            break;
        }

        case K::NamespaceDeclaration: {
            auto* ns = static_cast<nodes::NamespaceDeclaration*>(n);
            for (nodes::ASTNode* m : ns->get_body()->get_statements()) collect_conversions(m);
            break;
        }

        case K::TemplateDeclaration:
            collect_conversions(mn(static_cast<nodes::TemplateDeclaration*>(n)->get_declaration()));
            break;

        default: break;
    }
}

void TypeWalker::register_conversion(nodes::OperatorFunctionDeclaration* op) {
    if (!op->is_conversion() || op->is_deleted()) return;  
    Symbol* owner = enclosing_record_symbol(op);
    if (!owner) return;                
    Type* src = record_type_of(owner);           
    Type* dst = m_types.canonicalize(op->get_conversion_type());
    if (!dst) return;
    UserConversion uc;
    uc.dst       = m_types.strip_cv(dst);
    uc.op        = op->symbol;
    uc.explicit_ = op->qualifiers().has(modifiers::FunctionQualifiers::Explicit);
    m_types.user_conversions().add(m_types.strip_cv(src), uc);
}

void TypeWalker::ensure_typed(Instantiator::Instantiation* I) {
    if (!I || I->typed || !I->decl) return;
    I->typed = true;                       
    I->speculative = false;

    if (m_typing_depth >= 200) {
        if (!m_typing_depth_reported) {
            m_typing_depth_reported = true;
            SemanticError::template_depth_exceeded(m_reporter, file_of(I->decl), I->decl->line, I->generic ? I->generic->name : std::string_view("template"));
        }
        return;
    }

    struct DepthGuard { int& d; explicit DepthGuard(int& x) : d(x) { ++d; } ~DepthGuard() { --d; } } depth_guard(m_typing_depth);
    m_types.push_subst(&I->env);
    Scope* saved = m_current;
    const bool saved_const = m_in_const_method;
    m_in_const_method = false;
    m_current = I->scope;
    build(I->decl);                        
    m_current = saved;
    m_in_const_method = saved_const;
    m_types.pop_subst();
    if (m_inst) m_inst->refresh_function_type(I);   
}

Symbol* TypeWalker::enclosing_record_symbol(nodes::ASTNode* /*op*/) const {
    for (Scope* s = m_current; s; s = s->parent) {
        if (s->node && s->node->kind == K::RecordDeclaration && s->owner_symbol) return s->owner_symbol;
    }
    return nullptr;
}

void TypeWalker::check_condition(nodes::ASTNode* expr) {
    if (!expr) return;
    Type* t = type_of(expr);
    if (t && !is_bool_testable(t)) SemanticError::condition_not_bool(m_reporter, file_of(expr), expr->line, type_str(t));
}

void TypeWalker::check_signature_defaults(Symbol* fn, const nodes::FunctionParameters* ps, nodes::ASTNode* site) {
    if (!ps) return;
    std::vector<ParamShape> shapes;
    shapes.reserve(ps->size());
    for (const nodes::FunctionParameter* p : ps->m_params) shapes.push_back(shape_of(p, m_types));
    std::size_t bi = 0, bj = 0;
    if (!signature_defaults_ok(shapes, bi, bj)) {
        const std::string first(ps->m_params[bi]->get_name());
        const std::string second(ps->m_params[bj]->get_name());
        const std::string ty = shapes[bi].element ? type_str(shapes[bi].element) : std::string("?");
        SemanticError::emit(m_reporter, file_of(site), site->line, "Parameter", "'" + first + "' has a default value, but '" + second + "' after it also takes '" + ty + "', so a call couldn't tell which of the two an '" + ty + "' argument is for; give them different types, or put a parameter between them that always needs an argument");
    }

    for (std::size_t i = 0; i + 1 < shapes.size(); ++i) {
        if (!shapes[i].is_pack || !shapes[i].unbounded || !shapes[i + 1].is_pack) continue;
        Type* a = shapes[i].element ? m_types.strip_cv(shapes[i].element) : nullptr;
        Type* b = shapes[i + 1].element ? m_types.strip_cv(shapes[i + 1].element) : nullptr;
        if (!a || !b || a->is_dependent() || b->is_dependent() || a != b) continue;
        const std::string first(ps->m_params[i]->get_name());
        const std::string second(ps->m_params[i + 1]->get_name());
        SemanticError::emit(m_reporter, file_of(site), site->line, "Parameter", "'" + second + "' comes right after '" + first + "', which takes any number of '" + type_str(a) + "' values, so '" + second + "' could never receive anything");
    }
}

void TypeWalker::build(nodes::ASTNode* node) {
    if (!node) return;
    m_site = node;
    check_safe_statement(node);

    switch (node->kind) {
        case K::BlockStatement: {
            auto* b = static_cast<nodes::BlockStatement*>(node);
            Scope* saved = m_current;
            if (b->scope) m_current = b->scope;
            build_list(b->get_statements());
            m_current = saved;
            break;
        }

        case K::VariableDeclaration: {
            auto* v = static_cast<nodes::VariableDeclaration*>(node);
            walk_expr(v->m_initializer);
            if (!type_of(v->m_initializer)) reject_type_as_value(v->m_initializer);
            Type* init = type_of(v->m_initializer);
            parser_types::TypeInfo& ti = v->get_type_info();

            if (v->is_structured_binding()) {
                type_bindings(v, v->m_initializer, init, ti, ti.modifiers.has(modifiers::RawModifiers::Const), v->binding_symbols, v->binding_source, v->binding_by_ref, false);
                break;
            }

            if (prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) {
                if (!init) {
                    if (!v->has_initializer()) SemanticError::auto_needs_initializer(m_reporter, file_of(v), v->line, v->get_name());
                    ti.canonical = m_types.error_();
                    break;
                }
                if (init->is_error()) {
                    ti.canonical = m_types.error_();
                    break;
                }
                ti.canonical = deduce_auto_declared(ti, init);
                if (ti.canonical && ti.canonical->is_reference() && v->m_initializer) check_binding(v->m_initializer, init, ti.canonical, v);
                check_constant_init(v, ti.canonical);
                break;
            }

            Type* declared = m_types.canonicalize(ti);
            touch_record(declared, v);
            check_alignas(ti.alignment, declared, v);

            if (declared && declared->is_reference() && !v->has_initializer() && !ti.modifiers.has(modifiers::RawModifiers::Extern) && !(v->symbol && v->symbol->owner && v->symbol->owner->kind == Scope::Kind::Record)) {
                SemanticError::emit(m_reporter, file_of(v), v->line, "Initialization", "reference '" + std::string(v->get_name()) + "' must be initialized with the object it refers to, as in '" + type_str(declared) + ": " + std::string(v->get_name()) + " = other;'");
            }
            if (!(v->symbol && v->symbol->owner && v->symbol->owner->kind == Scope::Kind::Record)) check_not_abstract(declared, v);
            if (!v->has_initializer() && declared && !declared->is_reference() && record_symbol_of(declared) && !declared->is_dependent() && !(v->symbol && v->symbol->owner && v->symbol->owner->kind == Scope::Kind::Record)) resolve_construction(declared, {}, v);
            if (!v->has_initializer() && !v->synthetic && !m_returns.empty() && v->symbol && v->symbol->owner && v->symbol->owner->kind != Scope::Kind::Record && v->symbol->owner->kind != Scope::Kind::Module && v->symbol->owner->kind != Scope::Kind::Namespace && is_trivial_scalar(declared)) m_unassigned.insert(v->symbol);
            if (declared && v->m_initializer && v->m_initializer->kind == K::BraceInitializerList) check_brace_init(static_cast<nodes::BraceInitializerList*>(v->m_initializer), declared, v);
            if (declared && init && !is_dynamic_t(declared)) check_binding(v->m_initializer, init, declared, v);
            if (declared && init) check_view_source(v->m_initializer, init, declared, v, false);
            else if (declared && init && !init->is_error() && !init->is_dependent() && rank_conversion(init, declared, m_types) == ConversionRank::None) report_not_dynamic(init, v);
            check_constant_init(v, declared);
            break;
        }

        case K::ArrayDeclaration: {
            auto* a = static_cast<nodes::ArrayDeclaration*>(node);
            walk_expr(a->m_dimension_expr);
            walk_expr(a->m_initializer);
            check_array_declaration(a);
            check_alignas(a->m_alignment, array_symbol_type(a), a);
            if (a->m_element_type.alignment) SemanticError::emit(m_reporter, file_of(a), a->line, "Alignment", "alignas on an array goes right after 'array', as in 'array alignas(16) [4] int'; array elements can't be aligned one by one");
            break;
        }

        case K::FunctionDeclaration: {
            auto* fn = static_cast<nodes::FunctionDeclaration*>(node);
            check_signature_defaults(fn->symbol, fn->get_parameters(), fn);
            if (defer_member_body(fn, fn->symbol, fn->has_body(), fn->qualifiers().has_any(modifiers::FunctionQualifiers::Virtual | modifiers::FunctionQualifiers::Override) || fn->m_lowered_operator != nodes::OverloadableOperator::None)) {
                fn->lazy_untyped = true;
                break;
            }
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(fn->symbol)) m_current = fs;
            const bool saved_const = m_in_const_method;
            m_in_const_method = is_member_function(fn->symbol) && fn->qualifiers().has(modifiers::FunctionQualifiers::Const);
            const SelfRules saved_self = enter_self_rules(fn, fn->symbol, fn->get_parameters(), false);
            walk_param_defaults(fn->get_parameters());
            push_return(fn->get_return_type(), fn);
            if (fn->has_body()) build(mn(fn->get_body()));
            if (fn->has_body()) check_self_recursion(fn->symbol, mn(fn->get_body()), fn);
            check_falls_off(pop_return(fn->get_return_type()), fn->has_body() ? mn(fn->get_body()) : nullptr, fn->get_name(), fn);
            restore_self_rules(saved_self);
            m_in_const_method = saved_const;
            m_current = saved;
            if (fn->has_body() && fn->symbol) m_fn_bodies.push_back(node);  
            if (fn->has_body() && fn->symbol && m_throws) m_throws->pending.push_back(node);   
            break;
        }

        case K::OperatorFunctionDeclaration: {
            auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(node);
            check_operator_signature(op, op->symbol);
            if (defer_member_body(op, op->symbol, op->has_body(), op->qualifiers().has_any(modifiers::FunctionQualifiers::Virtual | modifiers::FunctionQualifiers::Override) || op->is_conversion() || op->get_overload() == nodes::OverloadableOperator::Assign)) {
                op->lazy_untyped = true;
                break;
            }
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(op->symbol)) m_current = fs;
            const bool saved_const = m_in_const_method;
            m_in_const_method = is_member_function(op->symbol) && op->qualifiers().has(modifiers::FunctionQualifiers::Const);
            const SelfRules saved_self = enter_self_rules(op, op->symbol, op->get_parameters(), op->is_conversion());
            walk_param_defaults(op->get_parameters());
            push_return(op->get_return_type(), op);
            if (op->has_body()) build(mn(op->get_body()));
            check_falls_off(pop_return(op->get_return_type()), op->has_body() ? mn(op->get_body()) : nullptr, "operator", op);
            restore_self_rules(saved_self);
            m_in_const_method = saved_const;
            m_current = saved;
            if (op->has_body() && op->symbol) m_fn_bodies.push_back(node); 
            if (op->has_body() && op->symbol && m_throws) m_throws->pending.push_back(node);    
            break;
        }

        case K::TypeQueryOverload:
            check_type_query_overload(static_cast<nodes::TypeQueryOverload*>(node));
            break;

        case K::ConstructorDeclaration: {
            auto* ct = static_cast<nodes::ConstructorDeclaration*>(node);
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(ct->symbol)) m_current = fs;
            const bool saved_const_ct = m_in_const_method;
            m_in_const_method = false;
            const SelfRules saved_self_ct = enter_self_rules(ct, ct->symbol, ct->get_parameters(), true);
            walk_param_defaults(ct->get_parameters());
            check_signature_defaults(ct->symbol, ct->get_parameters(), ct);
            std::vector<const Symbol*> initialized;

            for (nodes::ASTNode* e : ct->get_init_list()) {
                type_member_init(e);
                const nodes::ASTNode* callee = nullptr;
                if (e && e->kind == K::CallExpression) callee = static_cast<nodes::CallExpression*>(e)->m_callee;
                else if (e && e->kind == K::BraceConstructExpression) callee = static_cast<nodes::BraceConstructExpression*>(e)->m_callee;
                const Symbol* target = callee && callee->kind == K::Identifier ? static_cast<const nodes::Identifier*>(callee)->resolved : nullptr;
                if (!target) continue;

                if ((target->kind == SymbolKind::Variable || target->kind == SymbolKind::Parameter) && (!target->owner || target->owner->kind != Scope::Kind::Record)) {
                    SemanticError::emit(m_reporter, file_of(e), e->line, "Initialization", "'" + std::string(target->name) + "' is not a field of this class, so the constructor cannot initialize it here");
                    continue;
                }

                if (std::find(initialized.begin(), initialized.end(), target) != initialized.end()) {
                    SemanticError::emit(m_reporter, file_of(e), e->line, "Initialization", "'" + std::string(target->name) + "' is initialized more than once in this constructor");
                    continue;
                }

                initialized.push_back(target);
            }

            check_member_initialization(ct, initialized);
            push_return(m_types.void_(), ct);
            if (ct->has_body()) build(mn(ct->get_body()));
            pop_return_fixed();
            restore_self_rules(saved_self_ct);
            m_in_const_method = saved_const_ct;
            m_current = saved;
            if (ct->has_body() && ct->symbol) m_fn_bodies.push_back(node);    
            if (ct->has_body() && ct->symbol && m_throws) m_throws->pending.push_back(node);
            break;
        }

        case K::DestructorDeclaration: {
            auto* dt = static_cast<nodes::DestructorDeclaration*>(node);
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(dt->symbol)) m_current = fs;
            const SelfRules saved_self_dt = enter_self_rules(dt, dt->symbol, nullptr, true);
            push_return(m_types.void_(), dt);
            if (dt->has_body()) build(mn(dt->get_body()));
            pop_return_fixed();
            restore_self_rules(saved_self_dt);
            m_current = saved;
            if (dt->has_body() && dt->symbol) m_fn_bodies.push_back(node);
            if (dt->has_body() && dt->symbol && m_throws) m_throws->pending.push_back(node);
            break;
        }

        case K::RecordDeclaration: {
            auto* r = static_cast<nodes::RecordDeclaration*>(node);
            rebase_on_instance(r);
            Scope* saved = m_current;
            if (r->scope) m_current = r->scope;
            for (nodes::RecordDeclaration::Member m : r->get_members()) build(m.node);
            m_current = saved;
            if (r->is_union()) check_union(r);
            check_template_receivers(r);

            for (const nodes::RecordDeclaration::Member& m : r->get_members()) {
                if (!m.node || m.node->kind != K::FunctionDeclaration) continue;
                auto* fd = static_cast<nodes::FunctionDeclaration*>(m.node);
                const auto& q = fd->qualifiers();
                if (fd->has_body() || !q.has(modifiers::FunctionQualifiers::Virtual) || q.has(modifiers::FunctionQualifiers::Abstract) || fd->get_modifiers().has(modifiers::RawModifiers::Friend)) continue;
                static std::set<std::pair<std::uint32_t, std::uint32_t>> warned;
                if (!warned.insert({ file_of(fd), fd->line }).second) continue;
                SemanticWarning::emit(m_warnings, file_of(fd), fd->line, "Abstract", "'" + std::string(fd->get_name()) + "' is virtual with no body and isn't defined outside the class, so '" + std::string(r->get_name()) + "' is abstract; write 'abstract' after its parameters to say that on purpose, or give it a body");
            }

            {
                using OO = nodes::OverloadableOperator;
                bool has[4] = { false, false, false, false };
                for (const nodes::RecordDeclaration::Member& m : r->get_members()) {
                    if (!m.node || m.node->kind != K::OperatorFunctionDeclaration) continue;
                    const OO o = static_cast<nodes::OperatorFunctionDeclaration*>(m.node)->get_overload();
                    if (o == OO::New) has[0] = true;
                    if (o == OO::Delete) has[1] = true;
                    if (o == OO::NewArray) has[2] = true;
                    if (o == OO::DeleteArray) has[3] = true;
                }
                auto pair_error = [&](const char* have, const char* missing) {
                    SemanticError::emit(m_reporter, file_of(r), r->line, "Allocation", "'" + std::string(r->get_name()) + "' has its own 'operator(" + have + ")' but no 'operator(" + missing + ")'; memory from one has to be released by the other, so define both");
                };
                if (has[0] && !has[1]) pair_error("new", "delete");
                if (has[1] && !has[0]) pair_error("delete", "new");
                if (has[2] && !has[3]) pair_error("new[]", "delete[]");
                if (has[3] && !has[2]) pair_error("delete[]", "new[]");
            }
            if (!r->is_union()) {
                for (parser_types::TypeInfo* info : r->all_bases()) {
                    Symbol* bs = record_symbol_of(m_types.canonicalize(*info));
                    if (bs && bs->decl && bs->decl->kind == K::RecordDeclaration && static_cast<nodes::RecordDeclaration*>(bs->decl)->is_union()) SemanticError::emit(m_reporter, file_of(r), r->line, "Union", "'" + std::string(r->get_name()) + "' cannot inherit from union '" + std::string(bs->name) + "'");
                }
            }

            if (parser_types::TemplateArgument* align = r->m_alignment) {
                r->m_alignment = nullptr;
                align->align_value = 0;
                check_alignas(align, r->symbol ? record_type_of(r->symbol) : nullptr, r);
                r->m_alignment = align;
            }
            break;
        }

        case K::EnumDeclaration: {
            auto* en = static_cast<nodes::EnumDeclaration*>(node);
            Type* base = en->has_type() ? m_types.canonicalize(en->get_underlying_type()) : m_types.builtin_ranked(parser_types::PrimitiveType::BaseKind::Int, 0, true);
            Type* plain_base = base ? m_types.strip_cv(base) : nullptr;
            bool base_ok = plain_base && is_int_type(plain_base) && plain_base->is_builtin();
            if (en->has_type() && plain_base && !is_int_type(plain_base)) {
                SemanticError::enum_base_not_int(m_reporter, file_of(en), en->line, type_str(plain_base));
                base_ok = false;
            } else if (base_ok && !static_cast<BuiltinType*>(plain_base)->is_unsigned()) {
                SemanticError::emit(m_reporter, file_of(en), en->line, "Type", "the underlying type of enum '" + std::string(en->name) + "' has to be an unsigned integer type, such as 'unsigned int' or 'std::hardware_uint32', not '" + type_str(plain_base) + "', because enumerators can't be negative");
                base_ok = false;
            }
            Scope* saved = m_current;
            if (Scope* es = inner_of(en->symbol)) m_current = es;

            for (nodes::EnumValue* v : en->get_values()) {
                walk_expr(v->initializer);
                if (v->initializer && base_ok) {
                    Type* it = type_of(v->initializer);
                    Type* plain_it = it ? m_types.strip_cv(it->is_reference() ? static_cast<ReferenceType*>(it)->referent() : it) : nullptr;
                    if (it && !(plain_it && is_int_type(plain_it))) check_binding(v->initializer, it, base, v);
                }
            }

            if (base_ok) {
                auto* bb = static_cast<BuiltinType*>(plain_base);
                const unsigned int bits = bit_width_of_rank(bb->width());
                for (nodes::EnumValue* v : en->get_values()) {
                    if (!v || !v->symbol) continue;
                    const ConstValue cv = m_eval.enum_constant(v->symbol);
                    if (!cv.ok() || !cv.is_int()) continue;
                    if (cv.i->is_negative()) {
                        SemanticError::emit(m_reporter, file_of(v), v->line ? v->line : en->line, "Type", "enumerator '" + std::string(v->name) + "' of enum '" + std::string(en->name) + "' is " + cv.i->to_string() + ", but enumerators can't be negative");
                    } else if (!ConstTable::fits(*cv.i, bits, false, bb->is_hardware())) {
                        SemanticError::emit(m_reporter, file_of(v), v->line ? v->line : en->line, "Type", "enumerator '" + std::string(v->name) + "' of enum '" + std::string(en->name) + "' is " + cv.i->to_string() + ", which doesn't fit in its underlying type '" + type_str(plain_base) + "'; pick a wider unsigned type, such as 'unsigned long long int'");
                    }
                }
            }

            m_current = saved;
            break;
        }

        case K::NamespaceDeclaration: {
            auto* ns = static_cast<nodes::NamespaceDeclaration*>(node);
            Scope* saved = m_current;
            if (Scope* nsc = inner_of(ns->symbol)) m_current = nsc;
            if (auto* body = ns->get_body()) for (nodes::ASTNode* s : body->get_statements()) build(s);
            m_current = saved;
            break;
        }

        case K::ModuleDeclaration: {
            auto* m = static_cast<nodes::ModuleDeclaration*>(node);
            Scope* saved = m_current;
            if (Scope* ms = inner_of(m->symbol)) m_current = ms;
            for (const auto& it : m->get_items()) if (it.decl) build(it.decl);
            m_current = saved;
            break;
        }

        case K::TemplateDeclaration:
            check_template_receivers(static_cast<nodes::TemplateDeclaration*>(node)->m_declaration);
            break;

        case K::ReturnStatement: {
            auto* rs = static_cast<nodes::ReturnStatement*>(node);
            walk_expr(rs->get_value());
            if (m_in_finally) SemanticError::emit(m_reporter, file_of(rs), rs->line, "Jump", "'return' can't be used inside 'finally'; it would hide the value or exception the try block is already leaving with. Return after the try statement instead");
            if (rs->has_value() && !type_of(rs->get_value())) reject_type_as_value(rs->get_value());
            if (m_returns.empty()) break;
            ReturnFrame& f = m_returns.back();
            if (rs->has_value()) f.saw_value_return = true;

            if (f.deducing) {
                contribute_return(
                    rs->has_value() ? deduce_auto(type_of(rs->get_value())) : m_types.void_(),
                    rs
                );

                break;
            }

            Type* want = f.declared;

            if (CoroutineType* gen = generator_type(want)) {
                if (rs->has_value()) SemanticError::emit(m_reporter, file_of(rs), rs->line, "Coroutine", "a generator cannot return a value; produce values with 'co_yield' and finish with 'return;' or 'co_return;'");
                (void)gen;
                break;
            }

            if (want) {
                Type* val = type_of(rs->get_value());

                if (!rs->has_value()) {
                    if (!(is_void_t(want) || is_dynamic_t(want))) SemanticError::missing_return_value(m_reporter, file_of(rs), rs->line, type_str(want));
                } else if (is_void_t(want)) {
                    if (val && !(is_void_t(val) || is_dynamic_t(val))) SemanticError::return_value_in_void(m_reporter, file_of(rs), rs->line);
                } else if (val) {
                    check_binding(rs->get_value(), val, want, rs);
                    check_view_source(rs->get_value(), val, want, rs, true);
                } else if (rs->get_value() && rs->get_value()->kind == K::BraceInitializerList) {
                    check_brace_init(static_cast<nodes::BraceInitializerList*>(const_cast<nodes::ASTNode*>(rs->get_value())), want, rs);
                }
            }

            break;
        }

        case K::InvalidateStatement: {
            auto* inv = static_cast<nodes::InvalidateStatement*>(node);

            for (nodes::ASTNode* t : inv->targets) {
                walk_expr(t);
                Symbol* s = nullptr;
                if (t->kind == K::Identifier) s = static_cast<nodes::Identifier*>(t)->resolved;
                else if (t->kind == K::MemberAccessExpression && !static_cast<nodes::MemberAccessExpression*>(t)->is_scope()) s = static_cast<nodes::MemberAccessExpression*>(t)->resolved;

                if (!s) continue;
                if (!is_lazy_field(s)) {
                    SemanticError::emit(m_reporter, file_of(t), t->line, "Lazy", "'invalidate' works on lazy fields, and '" + std::string(s->name) + "' isn't one");
                    continue;
                }

                const std::string why = lazy_write_problem(t);
                if (!why.empty()) SemanticError::emit(m_reporter, file_of(t), t->line, "Lazy", why);
            }

            break;
        }

        case K::CoReturnStatement: {
            auto* cr = static_cast<nodes::CoReturnStatement*>(node);
            walk_expr(cr->m_value);
            note_coroutine(cr, "co_return");
            if (m_returns.empty()) break;
            if (!generator_type(m_returns.back().declared)) SemanticError::emit(m_reporter, file_of(cr), cr->line, "Coroutine", "'co_return' can only be used in a function that returns generator<T>");
            else if (cr->m_value) SemanticError::emit(m_reporter, file_of(cr), cr->line, "Coroutine", "'co_return' in a generator cannot carry a value; produce values with 'co_yield'");
            break;
        }

        case K::ExpressionStatement:
            m_statement_yield = static_cast<nodes::ExpressionStatement*>(node)->expr;
            walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
            check_discarded_result(static_cast<nodes::ExpressionStatement*>(node)->expr);
            break;

        case K::IfStatement: {
            auto* s = static_cast<nodes::IfStatement*>(node);
            const bool instantiating = m_types.active_subst() != nullptr;
            bool rest_discarded = false;

            for (nodes::IfBranch* br : s->get_branches()) {
                br->constant_value = -1;
                if (rest_discarded) continue;
                nodes::ASTNode* cond = br->condition;
                Scope* saved_branch_scope = m_current;
                if (br->scope) m_current = br->scope;
                struct RestoreScope { Scope*& cur; Scope* saved; ~RestoreScope() { cur = saved; } } restore_branch{ m_current, saved_branch_scope };

                if (cond && cond->kind == K::VariableDeclaration) {
                    build(cond);                             
                    auto* v = static_cast<nodes::VariableDeclaration*>(cond);
                    Type* tested = m_types.canonicalize(v->get_type_info());   
                    if (tested && !is_bool_testable(tested)) SemanticError::condition_not_bool(m_reporter, file_of(v), v->line, type_str(tested));
                    if (br->is_constexpr) SemanticError::not_a_constant(m_reporter, file_of(v), v->line, "'if constexpr' cannot declare a variable in its condition");
                } else {
                    walk_expr(cond);
                    check_condition(cond);

                    if (br->is_constexpr) {
                        ConstValue v;
                        {
                            ConstEvaluator::RandomPermit permit(m_eval, cond);
                            v = eval_checked(cond, true);
                        }
                        bool ok = false;
                        const bool taken = v.ok() && ConstEvaluator::truthy(v, ok);
                        if (v.ok() && ok) br->constant_value = taken ? 1 : 0;
                        else if (!v.is_error()) SemanticError::not_a_constant(m_reporter, file_of(cond), cond->line, fail_message(ConstValue::Fail::NotConstant));
                    }
                }

                if (br->constant_value == 0 && instantiating) continue;
                build(br->body);
                if (br->constant_value == 1) rest_discarded = instantiating;
            }

            if (!rest_discarded) build(mn(s->get_else()));
            break;
        }

        case K::WhileStatement: {
            auto* s = static_cast<nodes::WhileStatement*>(node);
            walk_expr(mn(s->get_condition()));
            check_condition(mn(s->get_condition()));
            ++m_loop_depth;
            m_loop_labels.push_back(s->label);
            build(mn(s->get_body()));
            m_loop_labels.pop_back();
            --m_loop_depth;
            check_loop(s);
            break;
        }

        case K::DoWhileStatement: {
            auto* s = static_cast<nodes::DoWhileStatement*>(node);
            ++m_loop_depth;
            m_loop_labels.push_back(s->label);
            build(mn(s->get_body()));
            m_loop_labels.pop_back();
            --m_loop_depth;
            walk_expr(mn(s->get_condition()));
            check_condition(mn(s->get_condition()));
            check_loop(s);
            break;
        }

        case K::ForStatement: {
            auto* s = static_cast<nodes::ForStatement*>(node);
            Scope* saved = m_current;
            if (s->scope) m_current = s->scope;
            build(s->var_init);
            build(s->initializer);
            if (s->condition) { walk_expr(s->condition); check_condition(s->condition); }
            walk_expr(s->increment);
            ++m_loop_depth;
            m_loop_labels.push_back(s->label);
            build(s->body);
            m_loop_labels.pop_back();
            --m_loop_depth;
            check_loop(s);
            m_current = saved;
            break;
        }

        case K::ForEachStatement: {
            auto* s = static_cast<nodes::ForEachStatement*>(node);
            walk_expr(s->m_container);
            type_for_each(s);
            ++m_loop_depth;
            m_loop_labels.push_back(s->label);
            build(s->m_body);
            m_loop_labels.pop_back();
            --m_loop_depth;
            break;
        }

        case K::SwitchStatement: {
            auto* s = static_cast<nodes::SwitchStatement*>(node);
            Scope* saved = m_current;
            if (s->scope) m_current = s->scope;
            walk_expr(s->get_condition());
            Type* ct = type_of(s->get_condition());
            if (ct && !is_dynamic_t(ct) && !is_switchable(ct)) SemanticError::switch_not_integral(m_reporter, file_of(s), s->line, type_str(ct));
            ++m_switch_depth;

            for (nodes::SwitchCase* c : s->get_cases()) {
                walk_expr(c->value);
                if (c->value && ct && !is_dynamic_t(ct)) { if (Type* lv = type_of(c->value); lv && !(operand_class(lv) == OperandClass::String && operand_class(ct) == OperandClass::String) && !constant_fits(c->value, lv, m_types.strip_cv(ct->is_reference() ? static_cast<ReferenceType*>(ct)->referent() : ct))) check_convertible(lv, ct, c); }
                build_list(c->body);
            }

            --m_switch_depth;
            const auto& cases = s->get_cases();

            for (std::size_t i = 0; i + 1 < cases.size(); ++i) {
                nodes::SwitchCase* c = cases[i];
                if (c->body.empty())       continue;    
                if (case_terminates(c))    continue;
                if (marked_fallthrough(c)) continue;

                if (nodes::in_safe_code(s)) {
                    safety_error(c, c->line, "in safe code a case can't fall through to the next case by accident; end it with 'break;', or with 'fallthrough;' if that is intentional");
                    continue;
                }

                SemanticWarning::emit(
                    m_warnings, file_of(c), c->line, "Switch",
                    "case falls through to the next case; add 'fallthrough' if that is intentional"
                );
            }

            if (nodes::in_safe_code(s) && ct) {
                Type* et = m_types.strip_cv(ct);
                if (et && et->is_reference()) et = m_types.strip_cv(static_cast<ReferenceType*>(et)->referent());
                bool has_default = false;
                for (nodes::SwitchCase* c : cases) has_default = has_default || c->is_default();
                Symbol* enum_sym = et && et->is_enum() ? static_cast<EnumType*>(et)->decl() : nullptr;
                auto* enum_decl = enum_sym && enum_sym->decl && enum_sym->decl->kind == K::EnumDeclaration ? static_cast<nodes::EnumDeclaration*>(enum_sym->decl) : nullptr;
                if (!has_default && enum_decl) {
                    std::vector<const Symbol*> covered;
                    for (nodes::SwitchCase* c : cases) if (Symbol* v = callee_symbol(c->value)) covered.push_back(v);
                    std::string missing;
                    std::size_t count = 0;
                    for (nodes::EnumValue* v : enum_decl->values) {
                        if (!v || !v->symbol) continue;
                        if (std::find(covered.begin(), covered.end(), v->symbol) != covered.end()) continue;
                        bool same_name = false;
                        for (const Symbol* c : covered) same_name = same_name || (c && c->name == v->name);
                        if (same_name) continue;
                        if (count < 4) missing += std::string(count ? ", " : "") + std::string(enum_sym->name) + "::" + std::string(v->name);
                        ++count;
                    }
                    if (count > 4) missing += " and " + std::to_string(count - 4) + " more";
                    if (count) safety_error(s, s->line, "in safe code a switch on an enum handles every value or has a 'default:'; this one is missing " + missing);
                }
            }

            for (std::size_t i = 0; i < cases.size(); ++i) {
                nodes::SwitchCase* c = cases[i];
                const bool last_case = (i + 1 == cases.size());

                for (std::size_t j = 0; j < c->body.size(); ++j) {
                    using V = nodes::SingleStatement::Variant;
                    if (!is_single(c->body[j], V::Fallthrough)) continue;

                    if (j + 1 != c->body.size())
                        SemanticError::fallthrough_not_last(m_reporter, file_of(c->body[j]), c->body[j]->line);
                    else if (last_case)
                        SemanticError::fallthrough_at_end(m_reporter, file_of(c->body[j]), c->body[j]->line);
                }
            }

            m_current = saved;
            break;
        }

        case K::TryCatchStatement: {
            auto* s = static_cast<nodes::TryCatchStatement*>(node);
            build(s->get_try_body());
            ++m_catch_depth;
            for (nodes::TryCatchStatement* h = s; h; h = h->next_handler) if (h->is_handler()) build(h->get_catch_body());
            --m_catch_depth;
            if (s->finally_body) {
                ControlContextGuard ctx(*this);
                m_in_finally = true;
                build(s->finally_body);
            }
            break;
        }

        case K::SingleStatement: {
            auto* ss = static_cast<nodes::SingleStatement*>(node);
            using V = nodes::SingleStatement::Variant;

            if (ss->variant == V::Goto || ss->variant == V::Label) {
                if (m_returns.empty()) {
                    SemanticError::emit(m_reporter, file_of(ss), ss->line, "Jump", std::string("'") + (ss->variant == V::Goto ? "goto" : "label") + "' can only be used inside a function");
                    break;
                }

                if (ss->variant == V::Goto) {
                    m_jumps.emplace_back(ss, m_stmt_path);
                    break;
                }

                bool duplicate = false;
                for (const auto& l : m_jump_labels) {
                    if (l.first->label != ss->label) continue;
                    SemanticError::emit(m_reporter, file_of(ss), ss->line, "Name", "'label " + std::string(ss->label) + ";' is already used on line " + std::to_string(l.first->line) + " in this function");
                    duplicate = true;
                    break;
                }
                if (!duplicate) m_jump_labels.emplace_back(ss, m_stmt_path);
                break;
            }

            if (!ss->label.empty()) {
                bool known = false;
                for (std::string_view l : m_loop_labels) known = known || l == ss->label;
                if (!known && m_in_finally) {
                    SemanticError::emit(m_reporter, file_of(ss), ss->line, "Jump", "'" + std::string(ss->variant == V::Continue ? "continue" : ss->variant == V::Repeat ? "repeat" : "break") + " " + std::string(ss->label) + ";' can't jump out of 'finally'; only loops inside the finally block can be left this way");
                    break;
                }
                if (!known) {
                    SemanticError::emit(m_reporter, file_of(ss), ss->line, "Name", "there is no loop labelled '" + std::string(ss->label) + "' around this statement; label a loop as '" + std::string(ss->label) + ": while (...)' or '" + std::string(ss->label) + ": for (...)'");
                    break;
                }
            }

            if (m_in_finally && m_loop_depth == 0 && (ss->variant == V::Continue || ss->variant == V::Repeat || (ss->variant == V::Break && m_switch_depth == 0))) {
                const char* kw = ss->variant == V::Break ? "break" : ss->variant == V::Continue ? "continue" : "repeat";
                SemanticError::emit(m_reporter, file_of(ss), ss->line, "Jump", std::string("'") + kw + "' can't jump out of 'finally'; it would cancel the return, break or exception the try block is leaving with");
                break;
            }

            switch (ss->variant) {
                case V::Break:
                    if (m_loop_depth == 0 && m_switch_depth == 0) SemanticError::misplaced_control(m_reporter, file_of(ss), ss->line, "break", "a loop or switch");
                    break;
                case V::Continue:
                    if (m_loop_depth == 0) SemanticError::misplaced_control(m_reporter, file_of(ss), ss->line, "continue", "a loop");
                    break;
                case V::Repeat:
                    if (m_loop_depth == 0) SemanticError::misplaced_control(m_reporter, file_of(ss), ss->line, "repeat", "a loop");
                    break;
                case V::Fallthrough:
                    if (m_switch_depth == 0) SemanticError::misplaced_control(m_reporter, file_of(ss), ss->line, "fallthrough", "a switch case");
                    break;
            }

            break;
        }

        case K::ImportExportDeclaration: {
            auto* ie = static_cast<nodes::ImportExportDeclaration*>(node);
            for (const auto& it : ie->get_items()) if (it.decl) build(it.decl);
            break;
        }

        case K::StaticAssertDeclaration: {
            auto* sa = static_cast<nodes::StaticAssertDeclaration*>(node);
            const std::size_t errors_before = m_reporter.error_count();
            walk_expr(sa->constraint);

            if (Type* t = type_of(sa->constraint))
                if (!is_bool_testable(t)) SemanticError::condition_not_bool(m_reporter, file_of(sa), sa->line, type_str(t));

            ConstValue v;
            {
                ConstEvaluator::RandomPermit permit(m_eval, sa->constraint);
                v = eval_checked(sa->constraint, true);
            }

            if (v.ok()) {
                bool ok = false;
                if (!ConstEvaluator::truthy(v, ok) && ok) SemanticError::static_assert_failed(m_reporter, file_of(sa), sa->line, sa->message);
            } else if (!v.is_error() && m_reporter.error_count() == errors_before) {
                SemanticError::not_a_constant(m_reporter, file_of(sa), sa->line, fail_message(ConstValue::Fail::NotConstant));
            }
                
            break;
        }

        case K::ConceptDeclaration:
            // Constraint is dependent on the constrained type
            // evaluate in concept/requires pass 
            break;

        case K::UsingDeclaration: {
            auto* u = static_cast<nodes::UsingDeclaration*>(node);
            if (u->is_alias() && u->aliased_expr) walk_expr(u->aliased_expr);
            break;
        }

        default:
            break;
    }
}

void TypeWalker::walk_children(nodes::ASTNode* node) {
    if (node->kind == K::ExpressionStatement) walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
}

void TypeWalker::walk_expr(nodes::ASTNode* e) {
    if (!e) return;
    SiteGuard site_guard(m_site, e);
    switch (e->kind) {
        case K::Literal: assign_type(e, type_of_literal(static_cast<nodes::Literal*>(e))); break;

        case K::Identifier: {
            auto* id = static_cast<nodes::Identifier*>(e);
            refresh_member(id);
            check_bare_member(id);

            if (id->resolved && id->resolved->owner && id->resolved->owner->kind == Scope::Kind::Record) {
                for (Scope* sc = m_current; sc; sc = sc->parent) {
                    if (sc->kind != Scope::Kind::Record) continue;
                    check_member_ambiguity(sc, id->name, id);
                    if (sc != id->resolved->owner) check_member_access(id->resolved, id);
                    break;
                }
            }

            if (id->resolved && in_friend_body() && (is_instance_field(id->resolved) || (is_member_function(id->resolved) && !is_static_member_function(id->resolved)))) {
                std::string_view rec = id->resolved->owner && id->resolved->owner->owner_symbol ? id->resolved->owner->owner_symbol->name : std::string_view{"the class"};
                SemanticError::emit(m_reporter, file_of(id), id->line, "Name", "'" + std::string(id->name) + "' belongs to an object of '" + std::string(rec) + "', but a friend function isn't a member, so it has no object of its own; write it through a parameter, as in 'obj." + std::string(id->name) + "'");
                assign_typed(e, m_types.error_(), VC::RValue);
                break;
            }

            note_read(id->resolved, e);
            VC vc = symbol_is_object(id->resolved) ? VC::LValue : VC::RValue;
            Type* t = decay_ref(symbol_type(id->resolved), vc);
            if (symbol_is_object(id->resolved)) vc = VC::LValue;
            if (m_in_const_method && is_instance_field(id->resolved)) t = add_const(t);
            if (lazy_blocked(id->resolved, false)) t = add_const(t);
            if (vc == VC::LValue && captured_const(id->resolved)) t = add_const(t);
            assign_typed(e, t, vc);
            break;
        }

        case K::QualifiedIdentifier: {
            auto* q = static_cast<nodes::QualifiedIdentifier*>(e);
            if (!q->resolved && q->dependent_head) {
                Type* ht = q->dependent_head->kind == SymbolKind::TypeAlias
                         ? m_types.strip_cv(m_types.canonicalize(*q->dependent_head->type))
                         : m_types.strip_cv(m_types.apply_subst(m_types.type_param(q->dependent_head, CV{})));
                if (!ht || ht->is_dependent()) { assign_typed(e, nullptr, VC::RValue); break; }
                Symbol* found = nullptr;
                if (ht->is_record()) {
                    auto* rt = static_cast<RecordType*>(ht);
                    Scope* sc = nullptr;
                    if (rt->is_instantiation() && m_inst) {
                        if (Instantiator::Instantiation* I = m_inst->for_record(rt, q)) {
                            ensure_typed(I);
                            if (I->sym) sc = I->sym->inner_scope;
                        }
                    }
                    if (!sc && rt->decl()) sc = rt->decl()->inner_scope;
                    for (std::size_t i = 1; sc && i < q->parts().size(); ++i) {
                        found = sc->find_member(q->parts()[i]);
                        if (!found) break;
                        sc = i + 1 < q->parts().size() ? found->inner_scope : nullptr;
                    }
                }
                if (!found) {
                    std::string full(q->dependent_head->name);
                    for (std::size_t i = 1; i < q->parts().size(); ++i) { full += "::"; full += std::string(q->parts()[i]); }
                    SemanticError::emit(m_reporter, file_of(q), q->line, "Name", "'" + type_str(ht) + "' has no member named '" + full.substr(full.find("::") + 2) + "', so '" + full + "' can't be used");
                    assign_typed(e, m_types.error_(), VC::RValue);
                    break;
                }
                q->resolved = found;
            }
            rebind_inherited(q);
            VC vc = symbol_is_object(q->resolved) ? VC::LValue : VC::RValue;
            Type* t = decay_ref(symbol_type(q->resolved), vc);
            if (symbol_is_object(q->resolved)) vc = VC::LValue;
            assign_typed(e, t, vc);
            break;
        }

        case K::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);

            if (b->op == tokenizing::Token::Kind::Equal && b->left && b->left->kind == K::Identifier) {
                walk_expr(b->right);
                m_unassigned.erase(static_cast<nodes::Identifier*>(b->left)->resolved);
                walk_expr(b->left);
            } else {
                walk_expr(b->left); walk_expr(b->right);
            }

            if (!type_of(b->left)) reject_type_as_value(b->left);
            if (!type_of(b->right)) reject_type_as_value(b->right);

            VC vc = is_assignment_op(b->op) ? VC::LValue : VC::RValue;
            Symbol* op = nullptr;
            Type* t = decay_ref(type_binary(b, op), vc);
            b->resolved = op;                            
            keep_overloads(b, op);
            ensure_member_body(op);
            assign_typed(e, t, vc);
            if (!op) check_bad_constants(b);
            break;
        }

        case K::UnaryExpression: {
            auto* u = static_cast<nodes::UnaryExpression*>(e);
            walk_expr(u->operand);
            using TK = tokenizing::Token::Kind;
            if (u->op == TK::Ellipsis) { assign_typed(e, m_runtime_spreads.count(e) ? type_of(u->operand) : nullptr, VC::RValue); break; }
            VC vc = ((u->op == TK::DoublePlus || u->op == TK::DoubleMinus) && u->is_prefix()) ? VC::LValue : VC::RValue;
            Symbol* op = nullptr;
            Type* t = decay_ref(type_unary(u, op), vc);
            u->resolved = op;
            keep_overloads(u, op);
            ensure_member_body(op);
            assign_typed(e, t, vc);
            if (!op) check_bad_constants(u);
            break;
        }

        case K::DereferenceExpression: {
            auto* d = static_cast<nodes::DereferenceExpression*>(e);
            walk_expr(d->operand);
            Type* t = type_of(d->operand);
            VC vc = VC::LValue;
            Type* res = nullptr;

            if (record_symbol_of(t)) {
                OpResult r = resolve_operator(nodes::OverloadableOperator::Star, { t });
                if (r.status == SelectStatus::Ok)             { d->resolved = r.chosen; keep_overloads(d, r.chosen); res = decay_ref(r.type, vc); }
                else if (r.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(d), d->line, "*");
                else                                          SemanticError::no_matching_overload(m_reporter, file_of(d), d->line, "*");
            } else if (t && t->is_pointer()) {
                res = static_cast<PointerType*>(t)->pointee();
            } else if (t && !is_dynamic_t(t)) {
                SemanticError::bad_operand(m_reporter, file_of(d), d->line, "*", type_str(t));
            }

            assign_typed(e, res, vc);
            break;
        }

        case K::ReferenceExpression: {
            auto* r = static_cast<nodes::ReferenceExpression*>(e);
            if (r->operand && r->operand->kind == K::Identifier) m_unassigned.erase(static_cast<nodes::Identifier*>(r->operand)->resolved);
            walk_expr(r->operand);
            Type* t = type_of(r->operand);

            if (const Symbol* iv = r->operand && r->operand->kind == K::Identifier ? static_cast<nodes::Identifier*>(r->operand)->resolved : nullptr;
                iv && iv->decl && iv->decl->kind == K::VariableDeclaration && static_cast<const nodes::VariableDeclaration*>(iv->decl)->get_type_info().modifiers.has(modifiers::RawModifiers::Inline)) {
                SemanticError::emit(m_reporter, file_of(r), r->line, "Inline", "cannot take the address of inline variable '" + std::string(iv->name) + "': its value is pasted where it is used, so it has no storage");
                assign_typed(e, nullptr, VC::RValue);
                break;
            }

            if (known_t(t) && !is_addressable(r->operand)) SemanticError::not_addressable(m_reporter, file_of(r), r->line);
            assign_typed(e, t ? m_types.pointer(t, CV{}) : nullptr, VC::RValue);
            break;
        }

        case K::TernaryExpression: {
            auto* t = static_cast<nodes::TernaryExpression*>(e);
            walk_expr(t->condition);
            const ConstValue known = m_eval.eval(t->condition);
            const bool fixed = known.ok() && known.is_bool();
            if (fixed && !known.b) ++m_dead_constant;
            walk_expr(t->true_branch);
            if (fixed && !known.b) --m_dead_constant;
            if (fixed && known.b) ++m_dead_constant;
            walk_expr(t->false_branch);
            if (fixed && known.b) --m_dead_constant;
            check_condition(t->condition);
            Type* ct = common_type(type_of(t->true_branch), type_of(t->false_branch), t);
            VC vc = (is_lvalue(t->true_branch) && is_lvalue(t->false_branch) && type_of(t->true_branch) == type_of(t->false_branch)) ? VC::LValue : VC::RValue;
            assign_typed(e, ct, vc);
            break;
        }

        case K::CallExpression: {
            auto* c = static_cast<nodes::CallExpression*>(e);
            nodes::ASTNode* pre_walked = nullptr;

            if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression && !static_cast<nodes::MemberAccessExpression*>(c->m_callee)->is_scope()) {
                auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
                walk_expr(m->m_object);

                if (rewrite_extension_call(c)) {
                    pre_walked = c->m_arguments.empty() ? nullptr : c->m_arguments.front();
                    walk_expr(c->m_callee);
                } else if (is_builtin_method_call(c)) {
                    std::vector<Type*> args;
                    for (nodes::ASTNode* a : c->m_arguments) { walk_expr(a); args.push_back(type_of(a)); }
                    assign_typed(e, type_builtin_method(c, args), VC::RValue);
                    check_safe_expression(m);
                    break;
                } else {
                    SiteGuard callee_guard(m_site, m);
                    finish_member_access(m, true);
                    check_safe_expression(m);
                }
            } else {
                walk_expr(c->m_callee);
            }

            if (!apply_keyword_arguments(c)) {
                for (nodes::ASTNode* a : c->m_arguments) walk_expr(a);
                assign_typed(e, m_types.error_(), VC::RValue);
                break;
            }
            expand_pack_arguments(c);
            std::vector<Type*> args;
            args.reserve(c->m_arguments.size());
            for (nodes::ASTNode* a : c->m_arguments) {
                if (a && a->kind == K::Identifier) m_unassigned.erase(static_cast<nodes::Identifier*>(a)->resolved);
                if (a != pre_walked) walk_expr(a);
                if (!type_of(a)) reject_type_as_value(a);
                args.push_back(type_of(a));
            }
            adapt_contains_literal(c, args);
            VC vc = VC::RValue;
            Type* t = type_call(c, args, vc);            
            assign_typed(e, t, vc);
            check_chosen_overload_visibility(c);
            break;
        }

        case K::SubscriptExpression: {
            auto* s = static_cast<nodes::SubscriptExpression*>(e);
            walk_expr(s->m_array); walk_expr(s->m_index);
            Type* arr = type_of(s->m_array);
            VC vc = VC::RValue;
            Type* el = nullptr;

            if (nodes::VariableDeclaration* group = pack_field_group(s->m_array)) {
                const ConstValue iv = m_eval.eval(s->m_index);
                if (!iv.ok() || !iv.is_int()) {
                    SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "the fields in the pack '" + std::string(group->get_name()) + "' have different types, so the index in '" + std::string(group->get_name()) + "[...]' must be a constant");
                    assign_typed(e, nullptr, vc);
                    break;
                }
                const long long k = iv.i->is_negative() ? -1 : static_cast<long long>(iv.i->get_lowest_bits());
                if (k < 0 || static_cast<std::size_t>(k) >= group->pack_elements.size() || !group->pack_elements[static_cast<std::size_t>(k)]) {
                    SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "index " + iv.i->to_string() + " is out of range for the pack '" + std::string(group->get_name()) + "', which has " + std::to_string(group->pack_elements.size()) + " fields");
                    assign_typed(e, nullptr, vc);
                    break;
                }
                s->pack_element = group->pack_elements[static_cast<std::size_t>(k)];
                el = symbol_type(s->pack_element);
                if (el && el->is_reference()) el = static_cast<ReferenceType*>(el)->referent();
                if (el && arr && type_is_const(arr)) { CV c = el->cv(); c.is_const = true; el = m_types.with_cv(el, c); }
                assign_typed(e, el, VC::LValue);
                break;
            }

            if (nodes::FunctionParameter* vp = s->m_array && s->m_array->kind == K::Identifier ? variadic_param_of(static_cast<nodes::Identifier*>(s->m_array)->resolved) : nullptr) {
                std::vector<Symbol*> pks;
                if (!vp->expanded_pack) collect_pack_params(m_types.canonicalize(vp->get_type()), pks);
                if (vp->expanded_pack || !pks.empty()) {
                    if (!vp->expanded_pack) {
                        assign_typed(e, arr, VC::LValue);
                        break;
                    }
                    const ConstValue iv = m_eval.eval(s->m_index);
                    const std::string pack_name(static_cast<nodes::Identifier*>(s->m_array)->name);
                    if (!iv.ok() || !iv.is_int()) {
                        SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "the arguments in the pack '" + pack_name + "' can have different types, so the index in '" + pack_name + "[...]' must be a constant");
                        assign_typed(e, nullptr, vc);
                        break;
                    }
                    const std::size_t skip = static_cast<nodes::Identifier*>(s->m_array)->pack_offset;
                    const long long k = iv.i->is_negative() ? -1 : static_cast<long long>(iv.i->get_lowest_bits()) + static_cast<long long>(skip);
                    if (k < static_cast<long long>(skip) || static_cast<std::size_t>(k) >= vp->pack_symbols.size()) {
                        SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "index " + iv.i->to_string() + " is out of range for the pack '" + pack_name + "', which has " + std::to_string(vp->pack_symbols.size() > skip ? vp->pack_symbols.size() - skip : 0) + " arguments");
                        assign_typed(e, nullptr, vc);
                        break;
                    }
                    s->pack_element = vp->pack_symbols[static_cast<std::size_t>(k)];
                    el = symbol_type(s->pack_element);
                    if (el && el->is_reference()) el = static_cast<ReferenceType*>(el)->referent();
                    assign_typed(e, el, VC::LValue);
                    break;
                }
            }

            if (record_symbol_of(arr)) {
                const ArenaVector<nodes::ASTNode*> index_operand{ s->m_index };
                m_operator_args = &index_operand;
                OpResult res = resolve_operator(nodes::OverloadableOperator::Subscript, { arr, type_of(s->m_index) });
                m_operator_args = nullptr;
                if (res.status == SelectStatus::Ok)             { s->resolved = res.chosen; keep_overloads(s, res.chosen); el = decay_ref(res.type, vc); ensure_member_body(res.chosen); }
                else if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(s), s->line, "[]");
                else                                            SemanticError::no_matching_overload(m_reporter, file_of(s), s->line, "[]");
            } else if (arr && arr->is_array()) { el = static_cast<ArrayType*>(arr)->element();  vc = VC::LValue; check_constant_index(s, arr); }
            else if (arr && arr->is_pointer()) { el = static_cast<PointerType*>(arr)->pointee(); vc = VC::LValue; }
            else if (Type* sv = m_types.strip_cv(arr && arr->is_reference() ? static_cast<ReferenceType*>(arr)->referent() : arr); sv && sv->is_builtin()) {
                using BK = parser_types::PrimitiveType::BaseKind;
                const BK sb = static_cast<BuiltinType*>(sv)->base();
                if (sb == BK::String || sb == BK::Text || sb == BK::StringView) {
                    el = m_types.builtin(BK::Char);
                    vc = sb != BK::StringView && is_lvalue(s->m_array) && !type_is_const(arr) ? VC::LValue : VC::RValue;
                }
            }
            assign_typed(e, el, vc);
            break;
        }

        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            walk_expr(m->m_object);
            finish_member_access(m);
            break;
        }

        case K::CastExpression: {
            auto* c = static_cast<nodes::CastExpression*>(e);
            walk_expr(c->operand);
            Type* src = type_of(c->operand);

            if (c->get_cast_kind() == nodes::CastExpression::CastKind::Is || c->get_cast_kind() == nodes::CastExpression::CastKind::As) {
                type_type_test(c);
                break;
            }

            if (c->interpolation) {
                if (!text_displayable(src)) {
                    SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "a value of type '" + type_str(m_types.strip_cv(src && src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src)) + "' can't be written into a text literal with '${...}'; give the class an 'operator(string)' or 'operator(text)' conversion");
                }
                assign_typed(c, m_types.builtin(parser_types::PrimitiveType::BaseKind::Text), VC::RValue);
                break;
            }

            Type* dst = m_types.canonicalize(c->get_target());
            check_cast(c, src, dst);
            report_redundant_cast(c, src, dst);
            const bool lref = dst && dst->is_reference() && static_cast<ReferenceType*>(dst)->ref_qual() == RefQual::LValue;
            const bool rref = dst && dst->is_reference() && static_cast<ReferenceType*>(dst)->ref_qual() == RefQual::RValue;
            assign_typed(c, (lref || rref) ? static_cast<ReferenceType*>(dst)->referent() : dst, lref ? VC::LValue : VC::RValue);
            if (!lref && !rref) check_clamped_cast(c, dst);
            break;
        }

        case K::NewExpression: {
            auto* n = static_cast<nodes::NewExpression*>(e);
            if (n->array_size) walk_expr(n->array_size);
            expand_pack_list(n->args);
            for (nodes::ASTNode* a : n->args) walk_expr(a);
            Type* allocated = m_types.canonicalize(n->type);

            check_not_abstract(allocated, n);

            if (n->is_array) {                                  
                Type* sz = type_of(n->array_size);
                if (sz && !is_integral_t(sz) && !is_dynamic_t(sz)) SemanticError::bad_operand(m_reporter, file_of(n), n->line, "new[] size", type_str(sz));
            } else if (Symbol* rec = record_symbol_of(allocated)) {   
                std::vector<Type*> args;
                args.reserve(n->args.size());
                for (nodes::ASTNode* a : n->args) args.push_back(type_of(a));
                n->ctor = resolve_construction(allocated, args, n);     
            }

            n->alloc = find_alloc_operator(allocated, n->is_array ? nodes::OverloadableOperator::NewArray : nodes::OverloadableOperator::New);
            assign_typed(e, allocated ? m_types.pointer(allocated, CV{}) : nullptr, VC::RValue);
            break;
        }

        case K::DeleteExpression: {
            auto* d = static_cast<nodes::DeleteExpression*>(e);
            walk_expr(d->operand);
            Type* t = type_of(d->operand);

            if (t && !t->is_pointer() && !is_dynamic_t(t)) {
                SemanticError::bad_operand(m_reporter, file_of(d), d->line, d->is_array ? "delete[]" : "delete", type_str(t));
            } else if (t && t->is_pointer()) {                   
                Type* pointee = static_cast<PointerType*>(t)->pointee();
                d->dealloc = find_alloc_operator(pointee, d->is_array ? nodes::OverloadableOperator::DeleteArray : nodes::OverloadableOperator::Delete);
            }

            assign_type(e, m_types.void_());                     
            break;
        }

        case K::BitwiseNotExpression: {
            auto* x = static_cast<nodes::BitwiseNotExpression*>(e);
            walk_expr(x->operand);
            Type* t = type_of(x->operand);

            if (record_symbol_of(t)) {
                OpResult res = resolve_operator(nodes::OverloadableOperator::BitNot, { t });
                if (res.status == SelectStatus::Ok) { x->resolved = res.chosen; keep_overloads(x, res.chosen); assign_typed(e, res.type, VC::RValue); break; }
                SemanticError::no_matching_overload(m_reporter, file_of(x), x->line, "~");
                assign_typed(e, nullptr, VC::RValue);
                break;
            }

            if (t && !is_integral_t(t) && !is_dynamic_t(t) && !(m_types.strip_cv(t->is_reference() ? static_cast<ReferenceType*>(t)->referent() : t)->is_enum())) SemanticError::bad_operand(m_reporter, file_of(x), x->line, "~", type_str(t));
            assign_typed(e, t, VC::RValue);
            break;
        }

        case K::LambdaExpression: {
            auto* l = static_cast<nodes::LambdaExpression*>(e);
            if (l->m_captures) {
                for (auto& c : l->m_captures->m_captures) {
                    if (!c.init) continue;
                    walk_expr(c.init);
                    Type* it = type_of(c.init);
                    if (it && it->is_reference()) it = static_cast<ReferenceType*>(it)->referent();
                    if (c.symbol) c.symbol->bound_type = it ? m_types.strip_cv(it) : m_types.error_();
                }
            }

            if (is_generic_lambda(l)) {
                if (auto* ps = l->get_parameters()) for (nodes::FunctionParameter* p : ps->m_params) walk_expr(p->m_initializer);
                assign_typed(e, m_types.closure(l), VC::RValue);
                break;
            }

            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (l->scope) m_current = l->scope;
            std::vector<Type*> param_types;
            std::vector<FnParamInfo> lambda_info;

            if (auto* ps = l->get_parameters()) {
                param_types.reserve(ps->size());
                    
                for (nodes::FunctionParameter* p : ps->m_params) {
                    walk_expr(p->m_initializer);
                    Type* pt = m_types.canonicalize(p->get_type());
                    param_types.push_back(p->is_variadic() && pt ? m_types.array(m_types.strip_cv(pt), std::nullopt) : pt);
                    FnParamInfo pi;

                    if (p->is_variadic()) {
                        pi.pack = p->is_infinite_variadic() ? FnParamInfo::Pack::Unbounded : (p->is_exact_variadic() ? FnParamInfo::Pack::Exact : FnParamInfo::Pack::UpTo);
                        pi.cap = p->is_infinite_variadic() ? 0 : p->variadic_length();
                    }

                    if (p->m_initializer) {
                        pi.def = p->m_initializer;

                        if (!m_types.default_key(p->m_initializer, pi.def_key)) {
                            SemanticError::emit(m_reporter, file_of(p->m_initializer), p->m_initializer->line, "Parameter", "the default value of '" + std::string(p->get_name()) + "' has to be a constant, because a lambda can be called far from where it was written, where the names in the default may not exist");
                        }
                    }

                    lambda_info.push_back(pi);
                }
            }

            push_return(l->get_return_type(), l);

            if (l->has_body()) {
                if (l->get_body()->kind == K::BlockStatement) {
                    build(mn(l->get_body()));
                } else {
                    walk_expr(mn(l->get_body()));
                    Type* bt = type_of(mn(l->get_body()));
                    contribute_return(deduce_auto(bt), mn(l->get_body()));
                    Type* decl = m_returns.back().declared;
                    if (decl && !is_void_t(decl) && bt) check_convertible(bt, decl, mn(l->get_body()));
                }
            }

            Type* ret = pop_return(l->get_return_type());
            if (l->has_body() && l->get_body()->kind == K::BlockStatement) check_falls_off(ret, mn(l->get_body()), "lambda", l);
            m_current = saved;
            using FQ = modifiers::FunctionQualifiers;
            const auto& q = l->get_qualifiers();
            RefQual rq = q.has(FQ::RValueRef) ? RefQual::RValue : q.has(FQ::LValueRef) ? RefQual::LValue : RefQual::None;
            if (q.has(FQ::Async) && ret) ret = m_types.coroutine(ret, false);
            assign_typed(e, m_types.function(ret, std::move(param_types), q.has(FQ::Const), rq, q.has(FQ::Noexcept), std::move(lambda_info)), VC::RValue);
            break;
        }

        case K::AwaitExpression: {
            auto* a = static_cast<nodes::AwaitExpression*>(e);
            walk_expr(a->operand);
            Type* ot = type_of(a->operand);
            if (ot && ot->is_reference()) ot = static_cast<ReferenceType*>(ot)->referent();
            ot = m_types.strip_cv(ot);

            if (ot && ot->kind() == TypeKind::Coroutine && static_cast<CoroutineType*>(ot)->is_task()) {
                assign_typed(e, static_cast<CoroutineType*>(ot)->value(), VC::RValue);
            } else {
                if (ot && !ot->is_error() && !ot->is_dependent()) SemanticError::emit(m_reporter, file_of(a), a->line, "Coroutine", "'await' needs a task (the result of calling an async function), not '" + type_str(ot) + "'");
                assign_typed(e, nullptr, VC::RValue);
            }
            break;
        }

        case K::CoYieldExpression: {
            auto* y = static_cast<nodes::CoYieldExpression*>(e);
            const bool as_statement = m_statement_yield == e;
            walk_expr(y->operand);
            note_coroutine(y, "co_yield");
            assign_typed(e, m_types.void_(), VC::RValue);
            if (m_returns.empty()) break;
            CoroutineType* gen = generator_type(m_returns.back().declared);

            if (!gen) {
                SemanticError::emit(m_reporter, file_of(y), y->line, "Coroutine", "'co_yield' can only be used in a function that returns generator<T>");
            } else if (!as_statement) {
                SemanticError::emit(m_reporter, file_of(y), y->line, "Coroutine", "'co_yield' must be used as a statement on its own");
            } else if (Type* vt = type_of(y->operand); vt && gen->value() && !gen->value()->is_dependent()) {
                check_binding(y->operand, vt, gen->value(), y);
            }
            break;
        }

        case K::RequiresExpression: {
            auto* r = static_cast<nodes::RequiresExpression*>(e);
            Scope* saved = m_current;
            if (r->scope) m_current = r->scope;
            // Satisfaction is checked at instantiation
            for (auto& req : r->get_requirements()) if (req.expr) walk_expr(req.expr);
            m_current = saved;
            assign_typed(e, m_types.bool_(), VC::RValue);
            break;
        }

        case K::NoexceptExpression: {
            auto* n = static_cast<nodes::NoexceptExpression*>(e);
            walk_expr(n->operand);

            if (m_throws) {
                n->is_nothrow = m_throws->analyzer.of_expr(n->operand).nothrow();
                n->computed   = true;
            }

            assign_typed(e, m_types.bool_(), VC::RValue);
            break;
        }

        case K::DiscardExpression: {
            auto* d = static_cast<nodes::DiscardExpression*>(e);
            walk_expr(d->operand);
            assign_typed(e, m_types.void_(), VC::RValue);   
            break;
        }

        case K::TypeQuery: {
            auto* q = static_cast<nodes::TypeQueryExpression*>(e);

            if (q->op == nodes::TypeQueryExpression::Op::Trait) {
                std::vector<Type*> args;

                if (trait_has_pack(q->extra_operands)) {
                    q->queried = nullptr;
                    q->extra_queried.clear();
                    args = trait_operands(m_types, q->operand, q->extra_operands);
                } else {
                    q->queried = q->operand ? m_types.canonicalize(q->operand->type) : nullptr;
                    q->extra_queried.clear();
                    for (parser_types::TemplateArgument* x : q->extra_operands) q->extra_queried.push_back(x ? m_types.canonicalize(x->type) : nullptr);
                    args.push_back(q->queried);
                    args.insert(args.end(), q->extra_queried.begin(), q->extra_queried.end());
                }
                std::string error;
                eval_type_trait(m_types, q->trait, args, error);
                if (!error.empty() && m_types.type_error_hook) m_types.type_error_hook(error);

                Type* trait_type = parser_types::type_trait_gives_int(q->trait) ? m_types.builtin(parser_types::PrimitiveType::BaseKind::Int) : m_types.bool_();
                if (q->trait == parser_types::TypeTrait::EnumBits) trait_type = widest_unsigned_int(m_types);
                if ((q->trait == parser_types::TypeTrait::EnumMin || q->trait == parser_types::TypeTrait::EnumMax) && !args.empty() && args.front()) {
                    Type* et = args.front()->is_reference() ? static_cast<ReferenceType*>(args.front())->referent() : args.front();
                    et = m_types.strip_cv(et);
                    trait_type = et && et->is_enum() ? enum_underlying_type(m_types, et) : widest_unsigned_int(m_types);
                }
                assign_typed(e, trait_type, VC::RValue);
                break;
            }

            Type* queried = nullptr;
            q->pack_count = -1;

            if (q->operand && q->op == nodes::TypeQueryExpression::Op::Countof) {
                nodes::ASTNode* target = q->operand->is_value() ? q->operand->value : nullptr;
                if (target && target->kind == K::MemberAccessExpression) walk_expr(target);
                nodes::VariableDeclaration* group = target ? pack_field_group(target) : nullptr;
                if (!group && q->operand->is_type()) group = pack_field_group_of(q->operand->type.resolved);

                if (group) {
                    q->pack_count = static_cast<long long>(group->pack_elements.size());
                    assign_typed(e, m_types.builtin(parser_types::PrimitiveType::BaseKind::Int), VC::RValue);
                    break;
                }
            }

            if (q->operand && q->op == nodes::TypeQueryExpression::Op::Countof) {
                Symbol* ps = nullptr;
                if (q->operand->is_value() && q->operand->value && q->operand->value->kind == K::Identifier) ps = static_cast<nodes::Identifier*>(q->operand->value)->resolved;
                else if (q->operand->is_type()) ps = q->operand->type.resolved;
                nodes::FunctionParameter* vp = variadic_param_of(ps);
                std::vector<Symbol*> pks;
                if (vp) collect_pack_params(m_types.canonicalize(vp->get_type()), pks);

                if (vp && (vp->expanded_pack || !pks.empty())) {
                    const std::size_t skip = q->operand->is_value() && q->operand->value && q->operand->value->kind == K::Identifier ? static_cast<nodes::Identifier*>(q->operand->value)->pack_offset : 0;
                    if (vp->expanded_pack) q->pack_count = static_cast<long long>(vp->pack_symbols.size() > skip ? vp->pack_symbols.size() - skip : 0);
                    assign_typed(e, m_types.builtin(parser_types::PrimitiveType::BaseKind::Int), VC::RValue);
                    break;
                }
            }

            if (q->operand) {
                if (q->operand->is_value()) {
                    walk_expr(q->operand->value);
                    queried = type_of(q->operand->value);
                } else if (q->operand->is_type()) {          
                    Symbol* vs = q->operand->type.resolved;
                    if (vs && (vs->kind == SymbolKind::Variable || vs->kind == SymbolKind::Parameter)) {
                        Type* st = symbol_type(vs);
                        queried = st && st->is_reference() ? static_cast<ReferenceType*>(st)->referent() : st;
                    } else {
                        queried = m_types.canonicalize(q->operand->type);  
                    }
                }                                           
            }

            using QOp = nodes::TypeQueryExpression::Op;

            if (q->op == QOp::Sizeof || q->op == QOp::Countof || q->op == QOp::Alignof) {
                q->queried = queried;
                Type* qv = m_types.strip_cv(queried);
                if (q->op == QOp::Countof && qv && !qv->is_error() && !qv->is_array() && !qv->is_enum() && !is_dynamic_t(qv)
                    && !(qv->is_builtin() && (static_cast<BuiltinType*>(qv)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(qv)->base() == parser_types::PrimitiveType::BaseKind::Text || static_cast<BuiltinType*>(qv)->base() == parser_types::PrimitiveType::BaseKind::StringView))) {
                    SemanticError::emit(m_reporter, file_of(q), q->line, "Type", "countof requires an array, a string or an enum, not '" + type_str(qv) + "'");
                }
                long long size = 0, alignment = 0;
                m_eval.layout_hit_native = false;
                if (q->op != QOp::Countof && qv && !qv->is_error() && !qv->is_dependent() && !m_eval.layout_of(qv, size, alignment) && !m_eval.layout_hit_native) {
                    SemanticError::emit(m_reporter, file_of(q), q->line, "Type", std::string(q->op_name()) + " is not defined for '" + type_str(qv) + "' because it has no fixed size");
                }
                assign_typed(e, m_types.builtin(parser_types::PrimitiveType::BaseKind::Int, parser_types::LengthModifier::Long, true), VC::RValue);
                break;
            }

            if (q->op == QOp::Typeof || q->op == QOp::Decltype) {
                const std::string name = q->op_name();
                SemanticError::emit(m_reporter, file_of(q), q->line, "Type", "'" + name + "' gives a type, so it can't be used as a value; write it where a type goes, as in '" + name + "(x): copy = x;', or use typeid(x) to get the type's name as a string");
                assign_typed(e, m_types.error_(), VC::RValue);
                break;
            }

            if (q->op == QOp::Typeid) {
                q->queried = queried;
                Type* plain = queried && queried->is_reference() ? static_cast<ReferenceType*>(queried)->referent() : queried;
                plain = m_types.strip_cv(plain);
                if (plain && !plain->is_dependent() && !plain->is_error()) {
                    static std::deque<std::string> names;
                    q->type_name = &names.emplace_back(type_str(plain));
                }
                assign_typed(e, m_types.builtin(parser_types::PrimitiveType::BaseKind::String), VC::RValue);
                break;
            }

            assign_typed(e, queried, VC::RValue);
            break;
        }

        case K::BraceConstructExpression: {
            auto* b = static_cast<nodes::BraceConstructExpression*>(e);
            walk_expr(b->m_callee);
            walk_expr(b->m_init);
            Type* constructed = type_of(b->m_callee);

            if (!constructed) {
                if (Symbol* s = callee_symbol(b->m_callee)) {
                    if (s->decl && s->decl->kind == K::RecordDeclaration) {
                        constructed = record_type_of(s);
                    } else if (s->kind == SymbolKind::TemplateParam) {
                        constructed = template_param_type(s);
                    } else {
                        constructed = symbol_type(s);
                    }
                }
            }

            if (record_symbol_of(constructed)) {
                Scope* ms = member_scope_of(constructed);
                if (ms && ms->find_member("constructor")) {
                    std::vector<Type*> args;

                    if (b->m_init) {
                        args.reserve(b->m_init->m_elements.size());
                        for (nodes::ASTNode* el : b->m_init->m_elements) args.push_back(type_of(el));
                    }

                    b->ctor = resolve_construction(constructed, args, b);  
                }
            }

            assign_typed(e, constructed, VC::RValue);
            break;
        }

        case K::TemplateInstantiation: {
            auto* t = static_cast<nodes::TemplateInstantiation*>(e);
            walk_expr(t->m_template);
            Type* result = nullptr;

            if (Symbol* g = callee_symbol(t->m_template); g && (g->kind == SymbolKind::Concept || (g->kind == SymbolKind::Import && g->import_target && g->import_target->kind == SymbolKind::Concept))) {
                t->concept_result = concept_id_value(t);
                assign_typed(e, m_types.bool_(), VC::RValue);
                break;
            }

            if (Symbol* g = callee_symbol(t->m_template); g && g->kind == SymbolKind::TypeAlias && g->template_decl && g->type) {
                parser_types::TypeInfo use;
                use.resolved = g;
                use.template_args = t->m_args.to_vector();
                Type* at = m_types.canonicalize(use);
                if (at && !at->is_error()) result = m_types.strip_cv(at);
            } else if (Symbol* g = callee_symbol(t->m_template); g && m_inst) {
                bool ok = true;
                std::vector<Instantiator::Arg> args = m_inst->canon_args(t->m_args, ok);
                    
                if (ok) {
                    if (auto* I = m_inst->instantiate(g, std::move(args), t)) {
                        if (I->decl && I->decl->kind == K::FunctionDeclaration) ensure_typed(I);   // functions: eager
                        result = I->type;                                                          // records: lazy members
                        if (I->decl && I->decl->kind == K::FunctionDeclaration && !is_variable_template(g)) t->fn_instance = I->sym;
                        if (is_variable_template(g)) {
                            t->var_instance = I->sym;
                            result = (I->type && I->type->kind() == TypeKind::Function) ? static_cast<FunctionType*>(I->type)->ret() : nullptr;
                        }
                    }
                }
            }

            assign_typed(e, result, VC::RValue);
            break;
        }

        case K::FoldExpression:
            type_fold(static_cast<nodes::FoldExpression*>(e));
            break;

        case K::BraceInitializerList: {
            auto* b = static_cast<nodes::BraceInitializerList*>(e);
            expand_pack_list(b->m_elements);
            for (nodes::ASTNode* el : b->m_elements) walk_expr(el);
            assign_typed(e, nullptr, VC::RValue);   
            break;
        }

        case K::ThrowExpression: {
            auto* t = static_cast<nodes::ThrowExpression*>(e);

            if (t->is_rethrow()) {
                if (m_catch_depth == 0) SemanticError::rethrow_outside_catch(m_reporter, file_of(t), t->line);
            } else {
                walk_expr(t->operand);
                if (Type* ot = type_of(t->operand))
                    if (is_void_t(ot)) SemanticError::bad_operand(m_reporter, file_of(t), t->line, "throw", type_str(ot));
            }

            assign_typed(e, m_types.void_(), VC::RValue);
            break;
        }

        default: break;
    }
    check_safe_expression(e);
}

void TypeWalker::report_redundant_cast(nodes::CastExpression* c, Type* src, Type* dst) {
    static const bool enabled = std::getenv("WALNUT_CAST_REPORT") != nullptr;
    if (!enabled || !c || !src || !dst || c->get_cast_kind() != nodes::CastExpression::CastKind::Static) return;
    if (dst->is_reference() || !c->target.type || !c->target.type->is_primitive() || !c->target.indirection.empty()) return;
    Type* s = m_types.strip_cv(src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src);
    if (s != m_types.strip_cv(dst)) return;
    nodes::ASTNode* op = c->operand;
    const parser_types::TypeInfo* declared = nullptr;
    std::string name;
    if (op && op->kind == K::Identifier) {
        auto* id = static_cast<nodes::Identifier*>(op);
        name = std::string(id->name);
        if (id->resolved && id->resolved->decl && id->resolved->decl->kind == K::VariableDeclaration) declared = &static_cast<nodes::VariableDeclaration*>(id->resolved->decl)->m_type_info;
        else if (id->resolved && id->resolved->decl && id->resolved->decl->kind == K::FunctionParameter) declared = &static_cast<nodes::FunctionParameter*>(id->resolved->decl)->get_type();
    } else if (op && op->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(op);
        name = std::string(m->m_member);
        if (m->resolved && m->resolved->decl && m->resolved->decl->kind == K::VariableDeclaration) declared = &static_cast<nodes::VariableDeclaration*>(m->resolved->decl)->m_type_info;
    } else if (op && op->kind == K::CallExpression) {
        auto* call = static_cast<nodes::CallExpression*>(op);
        Symbol* f = call->resolved;
        if (!f || !f->decl) return;
        if (f->decl->kind == K::FunctionDeclaration) declared = &static_cast<nodes::FunctionDeclaration*>(f->decl)->get_return_type();
        name = std::string(f->name) + "()";
    }
    if (!declared || !declared->type || !declared->type->is_primitive() || !declared->indirection.empty()) return;
    const auto* want = static_cast<const parser_types::PrimitiveType*>(c->target.type);
    const auto* have = static_cast<const parser_types::PrimitiveType*>(declared->type);
    if (want->base_kind() != have->base_kind() || want->is_unsigned() != have->is_unsigned()) return;
    SemanticError::emit(m_reporter, file_of(c), c->line, "RedundantCast", "redundant cast of " + name + " to " + type_str(dst));
}

void TypeWalker::check_cast(nodes::CastExpression* c, Type* src, Type* dst) {
    using CK = nodes::CastExpression::CastKind;
    if (!src || !dst) return;
    if (is_dynamic_t(src) || is_dynamic_t(dst)) return;   

    {
        using BK = parser_types::PrimitiveType::BaseKind;
        Type* s = m_types.strip_cv(src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src);
        Type* d = m_types.strip_cv(dst->is_reference() ? static_cast<ReferenceType*>(dst)->referent() : dst);
        auto base_of = [](Type* t) { return t && t->is_builtin() ? static_cast<BuiltinType*>(t)->base() : BK::Void; };
        const bool number = base_of(s) == BK::Int || base_of(s) == BK::Float || (s && s->is_enum());

        if (number && base_of(d) == BK::Char) {
            SemanticError::emit(m_reporter, file_of(c), c->line, "Cast", "a number can never become a 'char', not even with " + std::string(c->cast_name()) + "; a 'char' only comes from a character literal or another 'char' (going the other way, static_cast<int>(c) is allowed)");
            return;
        }

        if (number && base_of(d) == BK::Bool) {
            SemanticError::emit(m_reporter, file_of(c), c->line, "Cast", "a number can never become a 'bool', not even with " + std::string(c->cast_name()) + "; compare it instead, as in 'x != 0'");
            return;
        }
    }

    switch (c->get_cast_kind()) {
        case CK::Static: {
            Symbol* conv = nullptr;

            if (rank_builtin(src, dst) == ConversionRank::None) {                      
                if (rank_user_defined(src, dst, m_types, false, &conv) == ConversionRank::None) rank_user_defined(src, dst, m_types, true, &conv);                  
            }

            if (conv) c->conversion = conv;
            if (!is_static_castable(src, dst, m_types)) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
            break;
        }
        case CK::Const: {
            auto similar = [this](auto&& self, Type* a, Type* b, int depth) -> bool {
                a = m_types.strip_cv(a);
                b = m_types.strip_cv(b);
                if (!a || !b || depth > 16) return false;
                if (a == b) return true;
                if (a->is_pointer() && b->is_pointer()) return self(self, static_cast<PointerType*>(a)->pointee(), static_cast<PointerType*>(b)->pointee(), depth + 1);
                return false;
            };

            Type* from = src->is_reference() ? static_cast<ReferenceType*>(src)->referent() : src;
            Type* to   = m_types.strip_cv(dst);

            if (to && to->is_reference()) {
                if (!similar(similar, from, static_cast<ReferenceType*>(to)->referent(), 0)) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
            } else if (to && to->is_pointer()) {
                if (!m_types.strip_cv(from) || !m_types.strip_cv(from)->is_pointer() || !similar(similar, from, to, 0)) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
            } else {
                SemanticError::emit(m_reporter, file_of(c), c->line, "Cast", "const_cast needs a pointer or reference type such as '" + type_str(dst) + "&'; to get a non-const copy of a value, assign it to a non-const variable");
            }
            break;
        }
        case CK::Reinterpret: {                           
            bool ok = (src->is_pointer() && dst->is_pointer()) || (src->is_pointer() && is_integral_t(dst)) || (is_integral_t(src) && dst->is_pointer());
            if (!ok) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
            break;
        }
        case CK::Dynamic: {
            Type* s = m_types.strip_cv(src);
            Type* d = m_types.strip_cv(dst);
            Type* from = nullptr;
            Type* to = nullptr;

            if (d && d->is_pointer() && s && s->is_pointer()) {
                from = static_cast<PointerType*>(s)->pointee();
                to = static_cast<PointerType*>(d)->pointee();
            } else if (d && d->is_reference()) {
                from = s;
                to = static_cast<ReferenceType*>(d)->referent();
            }

            if (!record_symbol_of(from) || !record_symbol_of(to)) {
                SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
                break;
            }

            const bool upcast = rank_conversion(m_types.pointer(m_types.strip_cv(from)), m_types.pointer(m_types.strip_cv(to)), m_types) != ConversionRank::None;
            if (!upcast && !is_polymorphic(from)) SemanticError::emit(m_reporter, file_of(c), c->line, "Type", "dynamic_cast from '" + type_str(m_types.strip_cv(from)) + "' requires a polymorphic class (one with a virtual function)");
            break;
        }
        case CK::Bit: {                                    
            if (src->is_reference() || dst->is_reference()) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
            break;
        }
    }
}

Scope* TypeWalker::member_scope_of(Type* t) {
    if (!t) return nullptr;
    t = m_types.strip_cv(t);
    if (t && t->is_reference()) t = m_types.strip_cv(static_cast<ReferenceType*>(t)->referent());
    if (!t || !t->is_record()) return nullptr;
    auto* rt = static_cast<RecordType*>(t);

    if (rt->is_instantiation() && m_inst && !rt->is_dependent()) {
        if (Instantiator::Instantiation* I = m_inst->for_record(rt, nullptr)) {
            ensure_typed(I);
            if (I->sym && I->sym->inner_scope) return I->sym->inner_scope;
        }
    }

    return rt->decl() ? rt->decl()->inner_scope : nullptr;
}

void TypeWalker::collect_member_ops(
    Type* recv, nodes::OverloadableOperator want,
    const std::vector<Type*>& explicit_args,
    std::vector<CandidateMatch>& matches,
    std::vector<Symbol*>& cands, std::vector<bool>& member
) {
    const ArenaVector<nodes::ASTNode*>* op_args = m_operator_args;
    m_operator_args = nullptr;
    Scope* ms = member_scope_of(recv);
    if (!ms) return;
    std::unordered_set<const Scope*> visited;
    Type* recv_value = recv && recv->is_reference() ? static_cast<ReferenceType*>(recv)->referent() : recv;
    const bool recv_const = recv_value && recv_value->cv().is_const;

    std::function<void(const Scope*, int)> scan = [&](const Scope* s, int depth) {
        if (!s || depth > 64 || !visited.insert(s).second) return;
        bool declared_here = false;

        for (Symbol* o = s->find_local("operator"); o; o = o->next_overload) {
            if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
            if (static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() != want) continue;
            declared_here = true;
            EnvGuard g(m_types, env_of_symbol(o));
            CandidateMatch m = op_args ? match_with_constants(op_args, shapes_of(o), explicit_args) : match_arguments(shapes_of(o), explicit_args, m_types);
            if (!m.viable) continue;
            m.ranks.insert(m.ranks.begin(), is_const_method(o) == recv_const ? ConversionRank::Exact : ConversionRank::Promotion);
            matches.push_back(std::move(m)); cands.push_back(o); member.push_back(true);
        }

        if (declared_here) return;
        for (const Scope* b : s->bases) scan(b, depth + 1);
    };

    scan(ms, 0);

    nodes::ASTNode* site = m_operator_site ? m_operator_site : m_site;
    if (!m_inst || !site) return;
    const std::string lowered = "operator(" + std::string(nodes::overloadable_operator_name(want)) + ")";

    for (Symbol* o = ms->find_member(lowered); o; o = o->next_overload) {
        if (!o->template_decl) continue;
        auto* I = m_inst->instantiate_for_call(o, {}, explicit_args, site);
        if (!I || !I->sym) continue;
        CandidateMatch m = match_arguments(I->fn_shapes, explicit_args, m_types);
        if (!m.viable) continue;
        m.ranks.insert(m.ranks.begin(), ConversionRank::Exact);
        matches.push_back(std::move(m)); cands.push_back(I->sym); member.push_back(true);
    }
}

void TypeWalker::collect_free_ops(
    nodes::OverloadableOperator want, const std::vector<Type*>& operands,
    std::vector<CandidateMatch>& matches,
    std::vector<Symbol*>& cands, std::vector<bool>& member
) {
    std::unordered_set<const Scope*> seen;

    auto scan = [&](Scope* s) {
        if (!s || !seen.insert(s).second) return;

        for (Symbol* o = s->find_local("operator"); o; o = o->next_overload) {   
            if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
            if (static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() != want) continue;
            if (o->owner && o->owner->kind == Scope::Kind::Record) continue;
            CandidateMatch m = match_arguments(shapes_of(o), operands, m_types);
            if (!m.viable) continue;
            matches.push_back(std::move(m)); cands.push_back(o); member.push_back(false);
        }

        nodes::ASTNode* site = m_operator_site ? m_operator_site : m_site;
        if (!m_inst || !site) return;
        const std::string lowered = "operator(" + std::string(nodes::overloadable_operator_name(want)) + ")";

        for (Symbol* o = s->find_local(lowered); o; o = o->next_overload) {
            if (!o->template_decl) continue;
            auto* I = m_inst->instantiate_for_call(o, {}, operands, site);
            if (!I || !I->sym) continue;
            CandidateMatch m = match_arguments(I->fn_shapes, operands, m_types);
            if (!m.viable) continue;
            matches.push_back(std::move(m)); cands.push_back(I->sym); member.push_back(false);
        }
    };

    for (Scope* s = m_current; s; s = s->parent) {
        if (s->node && s->node->kind == K::RecordDeclaration) continue;   
        scan(s);
    }

    for (Type* t : operands) {
        Symbol* rec = record_symbol_of(t);
        if (!rec) continue;
        for (Scope* s = rec->owner; s; s = s->parent) {
            if (s->kind == Scope::Kind::Record) continue;
            scan(s);
        }
    }
}

void TypeWalker::push_return(const parser_types::TypeInfo& rt, nodes::ASTNode* site) {
    ReturnFrame f;

    if (prim_is(rt, parser_types::PrimitiveType::BaseKind::Auto)) {
        f.deducing = true;
    } else {
        f.declared = m_types.canonicalize(rt);
    }
        
    f.site = site;
    m_returns.push_back(f);
}

void TypeWalker::contribute_return(Type* contrib, nodes::ASTNode* site) {
    if (m_returns.empty()) return;
    ReturnFrame& f = m_returns.back();
    if (!f.deducing || !contrib) return;
    if (!f.seen) { f.deduced = contrib; f.seen = true; }
    else         { f.deduced = common_type(f.deduced, contrib, site); }
}

Type* TypeWalker::pop_return(const parser_types::TypeInfo& rt) {
    ReturnFrame f = m_returns.back();
    m_returns.pop_back();

    if (f.saw_co) {
        if (f.deducing && f.site) SemanticError::coroutine_deduced_return(m_reporter, file_of(f.site), f.site->line);
        return f.declared;
    }

    if (!f.deducing) return f.declared;
    Type* result = f.seen ? f.deduced : m_types.void_();
    const_cast<parser_types::TypeInfo&>(rt).canonical = result;
    return result;
}

void TypeWalker::note_coroutine(nodes::ASTNode* site, std::string_view kw) {
    if (m_in_finally) SemanticError::emit(m_reporter, file_of(site), site->line, "Coroutine", "'" + std::string(kw) + "' can't be used inside 'finally'");
    if (m_returns.empty()) {
        SemanticError::coroutine_outside_function(m_reporter, file_of(site), site->line, kw);
        return;
    }
    m_returns.back().saw_co = true;
}

CoroutineType* TypeWalker::generator_type(Type* t) {
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    return (t && t->kind() == TypeKind::Coroutine && static_cast<CoroutineType*>(t)->is_generator()) ? static_cast<CoroutineType*>(t) : nullptr;
}

bool TypeWalker::is_async_function(const Symbol* fn) {
    if (!fn || !fn->decl) return false;
    if (fn->decl->kind == K::FunctionDeclaration) return static_cast<nodes::FunctionDeclaration*>(fn->decl)->qualifiers().has(modifiers::FunctionQualifiers::Async);
    if (fn->decl->kind == K::OperatorFunctionDeclaration) return static_cast<nodes::OperatorFunctionDeclaration*>(fn->decl)->qualifiers().has(modifiers::FunctionQualifiers::Async);
    return false;
}

Type* TypeWalker::record_iteration(nodes::ForEachStatement* s, Type* container) {
    if (!m_inst) return nullptr;
    Type* c = container;
    if (c && c->is_reference()) c = static_cast<ReferenceType*>(c)->referent();
    if (!c || !record_symbol_of(c)) return nullptr;
    Scope* ms = member_scope_of(c);
    if (!ms || !ms->find_member("begin") || !ms->find_member("end")) return nullptr;

    static long long counter = 0;
    const std::string id = std::to_string(counter++);
    const std::uint32_t line = s->line;
    s->range_sym = synthetic_symbol("walnut_range_" + id, m_types.reference(c, is_lvalue(s->m_container) ? RefQual::LValue : RefQual::RValue), s);

    auto member_call = [&](std::string_view name) -> nodes::ASTNode* {
        auto* m = make_in<nodes::MemberAccessExpression>(m_inst->arena(), ident_for(s->range_sym, line), name, nodes::MemberAccessExpression::Op::Dot, line);
        auto* call = make_in<nodes::CallExpression>(m_inst->arena(), m, line);
        m->file_id = call->file_id = s->file_id;
        walk_expr(call);
        return call;
    };

    s->begin_call = member_call("begin");
    s->end_call = member_call("end");
    Type* it = type_of(s->begin_call);
    Type* et = type_of(s->end_call);
    if (it && it->is_reference()) it = static_cast<ReferenceType*>(it)->referent();
    if (et && et->is_reference()) et = static_cast<ReferenceType*>(et)->referent();
    if (!it || it->is_error() || !et || et->is_error()) return m_types.error_();
    it = m_types.strip_cv(it);
    et = m_types.strip_cv(et);

    s->it_sym = synthetic_symbol("walnut_it_" + id, it, s);
    s->end_sym = synthetic_symbol("walnut_end_" + id, et, s);
    s->iter_cond = binary_node(ident_for(s->it_sym, line), tokenizing::Token::Kind::NotEqual, ident_for(s->end_sym, line), line);
    auto* next = make_in<nodes::UnaryExpression>(m_inst->arena(), ident_for(s->it_sym, line), tokenizing::Token::Kind::DoublePlus, true, line);
    auto* get = make_in<nodes::DereferenceExpression>(m_inst->arena(), ident_for(s->it_sym, line), line);
    next->file_id = get->file_id = s->iter_cond->file_id = s->file_id;
    s->iter_next = next;
    s->iter_get = get;
    walk_expr(s->iter_cond);
    walk_expr(s->iter_next);
    walk_expr(s->iter_get);
    Type* el = type_of(s->iter_get);
    if (el && el->is_reference()) el = static_cast<ReferenceType*>(el)->referent();
    return el ? el : m_types.error_();
}

Type* TypeWalker::range_element(Type* container, nodes::ASTNode* site) {
    Type* c = container;
    if (c && c->is_reference()) c = static_cast<ReferenceType*>(c)->referent();
    if (!c || c->is_error()) return nullptr;
    if (Type* e = iterated_element(c)) return m_types.strip_cv(e);
    if (!m_inst || !record_symbol_of(c)) return nullptr;
    Scope* ms = member_scope_of(c);
    if (!ms || !ms->find_member("begin") || !ms->find_member("end")) return nullptr;
    static long long counter = 0;
    const std::uint32_t line = site ? site->line : 0;
    Symbol* r = synthetic_symbol("walnut_iterable_" + std::to_string(counter++), m_types.reference(c, RefQual::LValue), site);
    auto* m = make_in<nodes::MemberAccessExpression>(m_inst->arena(), ident_for(r, line), "begin", nodes::MemberAccessExpression::Op::Dot, line);
    auto* call = make_in<nodes::CallExpression>(m_inst->arena(), m, line);
    auto* get = make_in<nodes::DereferenceExpression>(m_inst->arena(), call, line);

    if (site) {
        m->file_id = call->file_id = get->file_id = site->file_id;
        m->at = call->at = get->at = site->at;
    }

    walk_expr(get);
    Type* el = type_of(get);
    if (el && el->is_reference()) el = static_cast<ReferenceType*>(el)->referent();
    if (!el || el->is_error()) return nullptr;
    return m_types.strip_cv(el);
}

Type* TypeWalker::iterated_element(Type* container) {
    Type* c = container;
    if (c && c->is_reference()) c = static_cast<ReferenceType*>(c)->referent();
    c = m_types.strip_cv(c);
    if (!c) return nullptr;
    if (CoroutineType* g = generator_type(c)) return g->value();
    if (c->is_array()) return static_cast<ArrayType*>(c)->element();
    if (c->is_builtin() && (static_cast<BuiltinType*>(c)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(c)->base() == parser_types::PrimitiveType::BaseKind::Text || static_cast<BuiltinType*>(c)->base() == parser_types::PrimitiveType::BaseKind::StringView)) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Char);
    if (is_dynamic_t(c)) return c;
    return nullptr;
}

void TypeWalker::type_for_each(nodes::ForEachStatement* s) {
    if (s->m_container && s->m_container->kind == K::BraceInitializerList && !s->is_structured_binding()) {
        auto* list = static_cast<nodes::BraceInitializerList*>(s->m_container);
        parser_types::TypeInfo& ti = s->m_element_type;
        const bool deduced = prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto);
        Type* el = nullptr;

        if (list->m_elements.empty()) {
            SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "a for-each loop over '{}' has nothing to loop over");
            return;
        }

        if (deduced) {
            for (nodes::ASTNode* x : list->m_elements) {
                Type* xt = type_of(x);
                if (xt && xt->is_reference()) xt = static_cast<ReferenceType*>(xt)->referent();
                xt = xt ? m_types.strip_cv(xt) : nullptr;
                if (!el || !xt || el == xt || xt->is_error()) { if (!el) el = xt; continue; }
                if (rank_conversion(xt, el, m_types) != ConversionRank::None) continue;
                if (rank_conversion(el, xt, m_types) != ConversionRank::None) { el = xt; continue; }
                SemanticError::emit(m_reporter, file_of(x), x->line, "Type", "the values in this brace list have no common type ('" + type_str(el) + "' and '" + type_str(xt) + "'); give the loop variable a type instead of 'auto'");
                return;
            }
            ti.canonical = el;
        } else {
            Type* declared = m_types.canonicalize(ti);
            if (declared && declared->is_reference()) declared = static_cast<ReferenceType*>(declared)->referent();
            el = declared ? m_types.strip_cv(declared) : nullptr;
            for (nodes::ASTNode* x : list->m_elements) if (Type* xt = type_of(x); xt && el) check_binding(x, xt, el, x);
        }

        if (el) assign_typed(list, m_types.array(el, list->m_elements.size(), CV{}), VC::RValue);
        return;
    }

    Type* ct = type_of(s->m_container);
    if (!ct || ct->is_error()) return;
    Type* el = iterated_element(ct);
    if (!el) el = record_iteration(s, ct);

    if (!el) {
        SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "type '" + type_str(ct) + "' cannot be iterated by a for-each loop");
        return;
    }

    if (s->is_structured_binding()) {
        const bool is_const = s->m_modifiers.has(modifiers::RawModifiers::Const) || s->m_element_type.modifiers.has(modifiers::RawModifiers::Const);
        type_bindings(s, nullptr, el, s->m_element_type, is_const, s->binding_symbols, s->binding_source, s->binding_by_ref, true);
        return;
    }

    parser_types::TypeInfo& ti = s->m_element_type;

    if (prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) {
        if (ti.indirection.empty()) {
            ti.canonical = el;
        } else if (ti.has_reference() && !ti.has_pointer()) {
            Type* b = el;
            const bool is_const = ti.modifiers.has(modifiers::RawModifiers::Const) || s->m_modifiers.has(modifiers::RawModifiers::Const);
            if (is_const) { CV cv = b->cv(); cv.is_const = true; b = m_types.with_cv(b, cv); }
            ti.canonical = m_types.reference(b, RefQual::LValue);
        } else {
            ti.canonical = m_types.canonicalize(ti);
        }
        return;
    }

    Type* declared = m_types.canonicalize(ti);
    if (!declared || declared->is_error() || is_dynamic_t(declared)) return;

    if (rank_conversion(el, declared, m_types) == ConversionRank::None && !is_dynamic_t(el)) {
        SemanticError::emit(m_reporter, file_of(s), s->line, "Type", "no implicit conversion from '" + type_str(el) + "' to '" + type_str(declared) + "' in for-each loop");
    }
}

Type* TypeWalker::array_symbol_type(nodes::ArrayDeclaration* a) {
    if (a->symbol) if (auto it = m_array_types.find(a->symbol); it != m_array_types.end()) return it->second;
    Type* el = m_types.canonicalize(a->m_element_type);
    if (!el || el->is_error()) return el;
    std::optional<std::size_t> extent = a->m_dimension;

    if (!extent && a->m_dimension_expr) {
        ConstValue v = m_eval.eval(a->m_dimension_expr);
        if (v.is_int() && !v.i->is_negative()) extent = static_cast<std::size_t>(v.i->get_lowest_bits());
    }

    if (!extent && !a->m_dimension_expr && a->m_initializer) extent = a->m_initializer->m_elements.size();
    Type* t = m_types.array(el, extent, CV{});
    if (a->symbol) m_array_types[a->symbol] = t;
    return t;
}

void TypeWalker::check_array_declaration(nodes::ArrayDeclaration* a) {
    Type* t = array_symbol_type(a);
    if (!t || !t->is_array()) return;
    auto* at = static_cast<ArrayType*>(t);
    Type* el = at->element();

    if (a->m_dimension_expr && !at->extent()) {
        Type* dt = type_of(a->m_dimension_expr);
        if (dt && !dt->is_error() && !is_numeric_t(m_types.strip_cv(dt)) && !is_dynamic_t(dt)) SemanticError::emit(m_reporter, file_of(a), a->line, "Type", "array dimension of '" + std::string(a->m_name) + "' must be an integer");
    }

    if (!a->m_initializer) return;
    const std::size_t n = a->m_initializer->m_elements.size();
    if (at->extent() && n > *at->extent()) SemanticError::emit(m_reporter, file_of(a), a->line, "Type", "too many initializers for array '" + std::string(a->m_name) + "' (" + std::to_string(n) + " for " + std::to_string(*at->extent()) + " elements)");

    for (nodes::ASTNode* x : a->m_initializer->m_elements) {
        if (!x || x->kind == K::BraceInitializerList) continue;
        Type* xt = type_of(x);
        if (xt && el && !is_dynamic_t(el)) check_binding(x, xt, el, a);
    }
}

Type* TypeWalker::symbol_type(Symbol* s) {
    if (!s) return nullptr;
    if (s->bound_type) return s->bound_type;
    if (s->decl && s->decl->kind == K::ArrayDeclaration && s->kind == SymbolKind::Variable) return array_symbol_type(static_cast<nodes::ArrayDeclaration*>(s->decl));

    if (nodes::FunctionParameter* vp = variadic_param_of(s)) {
        Type* el = m_types.canonicalize(vp->get_type());
        std::vector<Symbol*> pks;
        collect_pack_params(el, pks);
        if (!el || el->is_error() || !pks.empty() || vp->expanded_pack) return el;
        return m_types.array(el, std::nullopt, CV{});
    }

    if (s->kind == SymbolKind::Function && !s->template_decl && s->decl && s->decl->kind == K::FunctionDeclaration) {
        auto* fd = static_cast<nodes::FunctionDeclaration*>(s->decl);
        Instantiator::Instantiation* I = m_inst ? m_inst->owning(s) : nullptr;
        EnvGuard g(m_types, I ? &I->env : nullptr);
        std::vector<Type*> params;
        if (const nodes::FunctionParameters* ps = fd->get_parameters()) for (const nodes::FunctionParameter* p : ps->m_params) params.push_back(m_types.canonicalize(p->get_type()));
        Type* ret = m_types.canonicalize(fd->get_return_type());
        if (!ret) return nullptr;
        return m_types.function(async_result(s, ret), std::move(params), false, RefQual::None, false);
    }

    if (s->kind == SymbolKind::TemplateParam && s->decl && s->decl->kind == K::TemplateParameter) {
        auto* p = static_cast<nodes::TemplateParameter*>(s->decl);
        if (p->is_type_param() || p->m_is_pack) return nullptr;
        if (prim_is(p->m_type, parser_types::PrimitiveType::BaseKind::Auto)) return nullptr;
        return m_types.canonicalize(p->m_type);
    }

    if (s->kind == SymbolKind::EnumConstant) {                
        Symbol* en = s->owner ? s->owner->owner_symbol : nullptr;
        return en ? m_types.enum_(en, CV{}) : nullptr;
    }

    if (!s->type) return nullptr;
    Instantiator::Instantiation* owner = (m_inst && s->owner && s->owner->kind == Scope::Kind::Record) ? m_inst->owning(s) : nullptr;
    EnvGuard g(m_types, owner ? &owner->env : nullptr);
    return m_types.canonicalize(*s->type);
}

nodes::FunctionParameter* TypeWalker::variadic_param_of(Symbol* s) {
    if (!s || s->kind != SymbolKind::Parameter || !s->decl || s->decl->kind != K::FunctionParameter) return nullptr;
    auto* p = static_cast<nodes::FunctionParameter*>(s->decl);
    return (p->is_variadic() && p->symbol == s) ? p : nullptr;
}

Symbol* TypeWalker::find_pack_ref(nodes::ASTNode* n) {
    if (!n) return nullptr;
    if (n->kind == K::Identifier) {
        Symbol* s = static_cast<nodes::Identifier*>(n)->resolved;
        if (variadic_param_of(s)) return s;
    }
    Symbol* found = nullptr;
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { if (!found) found = find_pack_ref(c); });
    return found;
}

std::size_t TypeWalker::pack_offset_of(nodes::ASTNode* n, const Symbol* pack) {
    if (!n) return 0;
    std::size_t best = 0;
    if (n->kind == K::Identifier && static_cast<nodes::Identifier*>(n)->resolved == pack) best = static_cast<nodes::Identifier*>(n)->pack_offset;
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { best = std::max(best, pack_offset_of(c, pack)); });
    return best;
}

void TypeWalker::substitute_pack_types(nodes::ASTNode* n, const std::vector<Symbol*>& pks, std::size_t k) {
    if (!n || pks.empty()) return;
    const SubstEnv* active = m_types.active_subst();
    if (!active) return;
    SubstEnv one;
    for (Symbol* pk : pks) {
        const std::vector<Type*>* elems = active->lookup_pack(pk);
        if (!elems || k >= elems->size()) return;
        one.types[pk] = (*elems)[k];
    }

    auto fix = [&](auto& args) {
        for (parser_types::TemplateArgument* t : args) {
            if (!t || !t->is_type() || t->is_pack) continue;
            Type* pattern = m_types.canonicalize(t->type);
            std::vector<Symbol*> found;
            collect_pack_params(pattern, found);
            bool mentions = false;
            for (Symbol* s : found) mentions = mentions || std::find(pks.begin(), pks.end(), s) != pks.end();
            if (!mentions) continue;
            m_types.push_subst(&one);
            Type* concrete = m_types.apply_subst(pattern);
            m_types.pop_subst();
            if (concrete && !concrete->is_error()) t->type.canonical = concrete;
        }
    };

    if (n->kind == K::CallExpression) fix(static_cast<nodes::CallExpression*>(n)->m_template_args);
    if (n->kind == K::TemplateInstantiation) fix(static_cast<nodes::TemplateInstantiation*>(n)->m_args);
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { substitute_pack_types(c, pks, k); });
}

void TypeWalker::substitute_symbol(nodes::ASTNode* n, Symbol* from, Symbol* to) {
    if (!n) return;
    if (n->kind == K::Identifier && static_cast<nodes::Identifier*>(n)->resolved == from) static_cast<nodes::Identifier*>(n)->resolved = to;
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { substitute_symbol(c, from, to); });
}

nodes::ASTNode* TypeWalker::clone_resolved(nodes::ASTNode* n) {
    nodes::ASTNode* c = n->clone_into(m_inst->arena());
    if (m_inst->resolve_names) m_inst->resolve_names(c, m_current);
    return c;
}

Symbol* TypeWalker::synthetic_symbol(const std::string& name, Type* t, nodes::ASTNode* decl) {
    auto* s = make_in<Symbol>(m_inst->arena(), m_inst->keep_name(name), SymbolKind::Variable, decl);
    s->owner = m_current;
    s->bound_type = t;
    return s;
}

nodes::ASTNode* TypeWalker::ident_for(Symbol* s, std::uint32_t line) {
    auto* id = make_in<nodes::Identifier>(m_inst->arena(), s->name, line);
    id->resolved = s;
    return id;
}

nodes::ASTNode* TypeWalker::binary_node(nodes::ASTNode* l, tokenizing::Token::Kind op, nodes::ASTNode* r, std::uint32_t line) {
    auto* b = make_in<nodes::BinaryExpression>(m_inst->arena(), l, op, r, line);
    b->file_id = l ? l->file_id : 0;
    return b;
}

void TypeWalker::record_fields(Scope* sc, std::vector<Symbol*>& out, int depth) {
    if (!sc || depth > 32) return;
    for (Scope* b : sc->bases) record_fields(b, out, depth + 1);
    nodes::ASTNode* rd = sc->owner_symbol ? sc->owner_symbol->decl : nullptr;
    if (!rd || rd->kind != K::RecordDeclaration) return;

    for (const auto& member : static_cast<nodes::RecordDeclaration*>(rd)->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<nodes::VariableDeclaration*>(n);
            if (!v->get_type_info().modifiers.has(modifiers::RawModifiers::Static) && !v->get_type_info().modifiers.has(modifiers::RawModifiers::Lazy) && v->symbol) out.push_back(v->symbol);
        } else if (n->kind == K::ArrayDeclaration) {
            auto* a = static_cast<nodes::ArrayDeclaration*>(n);
            if (!a->get_array_modifiers().has(modifiers::RawModifiers::Static) && a->symbol) out.push_back(a->symbol);
        }
    }
}

void TypeWalker::type_bindings(nodes::ASTNode* site, nodes::ASTNode* init_expr, Type* init, parser_types::TypeInfo& ti, bool is_const,
                   SmallVector<Symbol*, 4>& symbols, Type*& source, bool& by_ref, bool from_loop) {
    source = nullptr;
    by_ref = false;
    for (Symbol* s : symbols) if (s) s->bound_type = m_types.error_();

    if (!prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "structured bindings must be declared with 'auto'");
        return;
    }

    if (!init || init->is_error()) {
        if (!from_loop && !init_expr) SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "structured bindings require an initializer");
        return;
    }

    for (const parser_types::IndirectionQualifier& q : ti.indirection) {
        if (q.is_pointer()) { SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "structured bindings cannot be declared as pointers"); return; }
        by_ref = true;
    }

    Type* src = init->is_reference() ? static_cast<ReferenceType*>(init)->referent() : init;
    const bool src_const = src && src->cv().is_const;
    src = m_types.strip_cv(src);

    if (by_ref && !is_const && !src_const && init_expr && !is_lvalue(init_expr)) {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "cannot bind 'auto&' structured bindings to a temporary; use 'auto' or 'const auto&'");
        return;
    }

    std::vector<Type*> elems;
    const std::size_t n = symbols.size();

    if (src->is_array()) {
        auto* a = static_cast<ArrayType*>(src);
        if (a->extent() && *a->extent() != n) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "structured binding declares " + std::to_string(n) + (n == 1 ? " name" : " names") + " but '" + type_str(src) + "' has " + std::to_string(*a->extent()) + " elements");
            return;
        }
        elems.assign(n, a->element());
    } else if (src->is_record()) {
        std::vector<Symbol*> fields;
        record_fields(member_scope_of(src), fields);
        if (fields.size() != n) {
            SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "structured binding declares " + std::to_string(n) + (n == 1 ? " name" : " names") + " but '" + type_str(src) + "' has " + std::to_string(fields.size()) + " fields");
            return;
        }
        for (Symbol* f : fields) {
            check_member_access(f, site);
            Type* ft = symbol_type(f);
            if (ft && ft->is_reference()) ft = static_cast<ReferenceType*>(ft)->referent();
            elems.push_back(ft);
        }
    } else {
        SemanticError::emit(m_reporter, file_of(site), site->line, "Type", "'" + type_str(src) + "' cannot be decomposed by a structured binding");
        return;
    }

    source = src;

    for (std::size_t i = 0; i < n; ++i) {
        if (!symbols[i] || !elems[i]) continue;
        Type* et = elems[i];
        if (is_const || (by_ref && src_const)) et = add_const(et);
        symbols[i]->bound_type = et;
    }
}

} // namespace semantics
} // namespace walnut
