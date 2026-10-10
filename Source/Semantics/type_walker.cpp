#include "Semantics/type_walker.hpp"

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
        m_inst->check_constraints = [this](nodes::TemplateDeclaration* tmpl, const SubstEnv& env, nodes::ASTNode* site, bool quiet) {
            std::string failed;
            if (constraints_satisfied(tmpl, env, failed)) return true;
            m_last_constraint_failure = failed;
            if (!quiet) SemanticError::constraints_not_satisfied(m_reporter, file_of(site), site ? site->line : 0, template_name_of(tmpl), failed);
            return false;
        };
    }
}

void TypeWalker::type_pending_instances() {
    if (!m_inst) return;

    for (int round = 0; round < 64; ++round) {
        std::vector<Instantiator::Instantiation*> pending;
        m_inst->for_each([&](Instantiator::Instantiation& I) { if (!I.typed && I.decl) pending.push_back(&I); });
        if (pending.empty()) return;
        for (Instantiator::Instantiation* I : pending) ensure_typed(I);
    }
}

void TypeWalker::run(nodes::BlockStatement* program) {
    if (!program) return;
    m_current = m_root;
    for (nodes::ASTNode* s : program->get_statements()) build(s);
}

TypeWalker::ControlContextGuard::ControlContextGuard(TypeWalker& tw) : w(tw), loops(tw.m_loop_depth), switches(tw.m_switch_depth) {
    w.m_loop_depth = w.m_switch_depth = 0;
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
    if (!op->is_conversion()) return;  
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
    if (!signature_defaults_ok(shapes, bi, bj)) SemanticError::ambiguous_defaults(m_reporter, file_of(site), site->line, fn ? fn->name : "<function>");
}

void TypeWalker::build(nodes::ASTNode* node) {
    if (!node) return;

    switch (node->kind) {
        case K::BlockStatement: {
            auto* b = static_cast<nodes::BlockStatement*>(node);
            Scope* saved = m_current;
            if (b->scope) m_current = b->scope;
            for (nodes::ASTNode* s : b->get_statements()) build(s);
            m_current = saved;
            break;
        }

        case K::VariableDeclaration: {
            auto* v = static_cast<nodes::VariableDeclaration*>(node);
            walk_expr(v->m_initializer);
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
                ti.canonical = deduce_auto_declared(ti, init);
                if (ti.canonical && ti.canonical->is_reference() && v->m_initializer) check_binding(v->m_initializer, init, ti.canonical, v);
                check_constant_init(v, ti.canonical);
                break;
            }

            Type* declared = m_types.canonicalize(ti);
            touch_record(declared, v);
            if (!(v->symbol && v->symbol->owner && v->symbol->owner->kind == Scope::Kind::Record)) check_not_abstract(declared, v);
            if (!v->has_initializer() && declared && !declared->is_reference() && record_symbol_of(declared) && !declared->is_dependent() && !(v->symbol && v->symbol->owner && v->symbol->owner->kind == Scope::Kind::Record)) resolve_construction(declared, {}, v);
            if (!v->has_initializer() && !m_returns.empty() && v->symbol && v->symbol->owner && v->symbol->owner->kind != Scope::Kind::Record && v->symbol->owner->kind != Scope::Kind::Module && v->symbol->owner->kind != Scope::Kind::Namespace && is_trivial_scalar(declared)) m_unassigned.insert(v->symbol);
            if (declared && init && !is_dynamic_t(declared)) check_binding(v->m_initializer, init, declared, v);
            check_constant_init(v, declared);
            break;
        }

        case K::ArrayDeclaration: {
            auto* a = static_cast<nodes::ArrayDeclaration*>(node);
            walk_expr(a->m_dimension_expr);
            walk_expr(a->m_initializer);
            check_array_declaration(a);
            break;
        }

        case K::FunctionDeclaration: {
            auto* fn = static_cast<nodes::FunctionDeclaration*>(node);
            check_signature_defaults(fn->symbol, fn->get_parameters(), fn);
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(fn->symbol)) m_current = fs;
            const bool saved_const = m_in_const_method;
            m_in_const_method = is_member_function(fn->symbol) && fn->qualifiers().has(modifiers::FunctionQualifiers::Const);
            walk_param_defaults(fn->get_parameters());
            push_return(fn->get_return_type(), fn);
            if (fn->has_body()) build(mn(fn->get_body()));
            check_falls_off(pop_return(fn->get_return_type()), fn->has_body() ? mn(fn->get_body()) : nullptr, fn->get_name(), fn);
            m_in_const_method = saved_const;
            m_current = saved;
            if (fn->has_body() && fn->symbol) m_fn_bodies.push_back(node);  
            if (fn->has_body() && fn->symbol && m_throws) m_throws->pending.push_back(node);   
            break;
        }

        case K::OperatorFunctionDeclaration: {
            auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(node);
            check_operator_signature(op, op->symbol);
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(op->symbol)) m_current = fs;
            const bool saved_const = m_in_const_method;
            m_in_const_method = is_member_function(op->symbol) && op->qualifiers().has(modifiers::FunctionQualifiers::Const);
            walk_param_defaults(op->get_parameters());
            push_return(op->get_return_type(), op);
            if (op->has_body()) build(mn(op->get_body()));
            check_falls_off(pop_return(op->get_return_type()), op->has_body() ? mn(op->get_body()) : nullptr, "operator", op);
            m_in_const_method = saved_const;
            m_current = saved;
            if (op->has_body() && op->symbol) m_fn_bodies.push_back(node); 
            if (op->has_body() && op->symbol && m_throws) m_throws->pending.push_back(node);    
            break;
        }

        case K::ConstructorDeclaration: {
            auto* ct = static_cast<nodes::ConstructorDeclaration*>(node);
            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (Scope* fs = inner_of(ct->symbol)) m_current = fs;
            const bool saved_const_ct = m_in_const_method;
            m_in_const_method = false;
            walk_param_defaults(ct->get_parameters());
            for (nodes::ASTNode* e : ct->get_init_list()) type_member_init(e);
            push_return(m_types.void_(), ct);
            if (ct->has_body()) build(mn(ct->get_body()));
            pop_return_fixed();
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
            push_return(m_types.void_(), dt);
            if (dt->has_body()) build(mn(dt->get_body()));
            pop_return_fixed();
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
            break;
        }

        case K::EnumDeclaration: {
            auto* en = static_cast<nodes::EnumDeclaration*>(node);
            Type* base = en->has_type() ? m_types.canonicalize(en->get_underlying_type()) : m_types.builtin(parser_types::PrimitiveType::BaseKind::Int);   
            if (en->has_type() && base && !(is_dynamic_t(base) || is_int_type(base))) SemanticError::enum_base_not_int(m_reporter, file_of(en), en->line, type_str(base));
            Scope* saved = m_current;
            if (Scope* es = inner_of(en->symbol)) m_current = es;

            for (nodes::EnumValue* v : en->get_values()) {
                walk_expr(v->initializer);
                if (v->initializer && is_int_type(base)) { if (Type* it = type_of(v->initializer)) check_binding(v->initializer, it, base, v); }
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
            // Generic bodies are typed at instantiation
            break;

        case K::ReturnStatement: {
            auto* rs = static_cast<nodes::ReturnStatement*>(node);
            walk_expr(rs->get_value());
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
                }
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
            break;

        case K::IfStatement: {
            auto* s = static_cast<nodes::IfStatement*>(node);
            const bool instantiating = m_types.active_subst() != nullptr;
            bool rest_discarded = false;

            for (nodes::IfBranch* br : s->get_branches()) {
                br->constant_value = -1;
                if (rest_discarded) continue;
                nodes::ASTNode* cond = br->condition;

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
                        ConstValue v = eval_checked(cond, true);
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
            build(mn(s->get_body()));
            --m_loop_depth;
            break;
        }

        case K::DoWhileStatement: {
            auto* s = static_cast<nodes::DoWhileStatement*>(node);
            ++m_loop_depth;
            build(mn(s->get_body()));
            --m_loop_depth;
            walk_expr(mn(s->get_condition()));
            check_condition(mn(s->get_condition()));
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
            build(s->body);
            --m_loop_depth;
            m_current = saved;
            break;
        }

        case K::ForEachStatement: {
            auto* s = static_cast<nodes::ForEachStatement*>(node);
            walk_expr(s->m_container);
            type_for_each(s);
            ++m_loop_depth;
            build(s->m_body);
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
                if (c->value && ct && !is_dynamic_t(ct)) { if (Type* lv = type_of(c->value)) check_convertible(lv, ct, c); }
                for (nodes::ASTNode* stmt : c->body) build(stmt);
            }

            --m_switch_depth;
            const auto& cases = s->get_cases();

            for (std::size_t i = 0; i + 1 < cases.size(); ++i) {
                nodes::SwitchCase* c = cases[i];
                if (c->body.empty())       continue;    
                if (case_terminates(c))    continue;
                if (marked_fallthrough(c)) continue;

                SemanticWarning::emit(
                    m_warnings, file_of(c), c->line, "Switch",
                    "case falls through to the next case; add 'fallthrough' if that is intentional"
                );
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
            for (nodes::TryCatchStatement* h = s; h; h = h->next_handler) build(h->get_catch_body());
            --m_catch_depth;
            break;
        }

        case K::SingleStatement: {
            auto* ss = static_cast<nodes::SingleStatement*>(node);
            using V = nodes::SingleStatement::Variant;

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
            walk_expr(sa->constraint);

            if (Type* t = type_of(sa->constraint))
                if (!is_bool_testable(t)) SemanticError::condition_not_bool(m_reporter, file_of(sa), sa->line, type_str(t));

            ConstValue v = eval_checked(sa->constraint, true);

            if (v.ok()) {
                bool ok = false;
                if (!ConstEvaluator::truthy(v, ok) && ok) SemanticError::static_assert_failed(m_reporter, file_of(sa), sa->line);
            } else if (!v.is_error()) {
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
    switch (e->kind) {
        case K::Literal: assign_type(e, type_of_literal(static_cast<nodes::Literal*>(e))); break;

        case K::Identifier: {
            auto* id = static_cast<nodes::Identifier*>(e);
            refresh_member(id);
            note_read(id->resolved, e);
            VC vc = symbol_is_object(id->resolved) ? VC::LValue : VC::RValue;
            Type* t = decay_ref(symbol_type(id->resolved), vc);
            if (m_in_const_method && is_instance_field(id->resolved)) t = add_const(t);
            if (vc == VC::LValue && captured_const(id->resolved)) t = add_const(t);
            assign_typed(e, t, vc);
            break;
        }

        case K::QualifiedIdentifier: {
            auto* q = static_cast<nodes::QualifiedIdentifier*>(e);
            VC vc = symbol_is_object(q->resolved) ? VC::LValue : VC::RValue;
            Type* t = decay_ref(symbol_type(q->resolved), vc);
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

            VC vc = is_assignment_op(b->op) ? VC::LValue : VC::RValue;
            Symbol* op = nullptr;
            Type* t = decay_ref(type_binary(b, op), vc);
            b->resolved = op;                            
            assign_typed(e, t, vc);
            break;
        }

        case K::UnaryExpression: {
            auto* u = static_cast<nodes::UnaryExpression*>(e);
            walk_expr(u->operand);
            using TK = tokenizing::Token::Kind;
            if (u->op == TK::Ellipsis) { assign_typed(e, nullptr, VC::RValue); break; }
            VC vc = ((u->op == TK::DoublePlus || u->op == TK::DoubleMinus) && u->is_prefix()) ? VC::LValue : VC::RValue;
            Symbol* op = nullptr;
            Type* t = decay_ref(type_unary(u, op), vc);
            u->resolved = op;
            assign_typed(e, t, vc);
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
                if (r.status == SelectStatus::Ok)             { d->resolved = r.chosen; res = decay_ref(r.type, vc); }
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
            if (known_t(t) && !is_addressable(r->operand)) SemanticError::not_addressable(m_reporter, file_of(r), r->line);
            assign_typed(e, t ? m_types.pointer(t, CV{}) : nullptr, VC::RValue);
            break;
        }

        case K::TernaryExpression: {
            auto* t = static_cast<nodes::TernaryExpression*>(e);
            walk_expr(t->condition); walk_expr(t->true_branch); walk_expr(t->false_branch);
            check_condition(t->condition);
            Type* ct = common_type(type_of(t->true_branch), type_of(t->false_branch), t);
            VC vc = (is_lvalue(t->true_branch) && is_lvalue(t->false_branch) && type_of(t->true_branch) == type_of(t->false_branch)) ? VC::LValue : VC::RValue;
            assign_typed(e, ct, vc);
            break;
        }

        case K::CallExpression: {
            auto* c = static_cast<nodes::CallExpression*>(e);
            walk_expr(c->m_callee);
            expand_pack_arguments(c);
            std::vector<Type*> args;
            args.reserve(c->m_arguments.size());
            for (nodes::ASTNode* a : c->m_arguments) {
                if (a && a->kind == K::Identifier) m_unassigned.erase(static_cast<nodes::Identifier*>(a)->resolved);
                walk_expr(a);
                args.push_back(type_of(a));
            }
            VC vc = VC::RValue;
            Type* t = type_call(c, args, vc);            
            assign_typed(e, t, vc);
            break;
        }

        case K::SubscriptExpression: {
            auto* s = static_cast<nodes::SubscriptExpression*>(e);
            walk_expr(s->m_array); walk_expr(s->m_index);
            Type* arr = type_of(s->m_array);
            VC vc = VC::RValue;
            Type* el = nullptr;

            if (record_symbol_of(arr)) {
                OpResult res = resolve_operator(nodes::OverloadableOperator::Subscript, { arr, type_of(s->m_index) });
                if (res.status == SelectStatus::Ok)             { s->resolved = res.chosen; el = decay_ref(res.type, vc); }
                else if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(s), s->line, "[]");
                else                                            SemanticError::no_matching_overload(m_reporter, file_of(s), s->line, "[]");
            } else if (arr && arr->is_array()) { el = static_cast<ArrayType*>(arr)->element();  vc = VC::LValue; }
            else if (arr && arr->is_pointer()) { el = static_cast<PointerType*>(arr)->pointee(); vc = VC::LValue; }
            assign_typed(e, el, vc);
            break;
        }

        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            walk_expr(m->m_object);
            Type* raw = type_member_access(m);               
            VC vc = symbol_is_object(m->resolved) ? VC::LValue : VC::RValue;
            if (vc == VC::LValue && m->is_dot() && !is_lvalue(m->m_object)) vc = VC::RValue;
            Type* t = decay_ref(raw, vc);                   
            assign_typed(e, t, vc);                          
            break;
        }

        case K::CastExpression: {
            auto* c = static_cast<nodes::CastExpression*>(e);
            walk_expr(c->operand);
            Type* src = type_of(c->operand);
            Type* dst = m_types.canonicalize(c->get_target());
            check_cast(c, src, dst);
            const bool lref = dst && dst->is_reference() && static_cast<ReferenceType*>(dst)->ref_qual() == RefQual::LValue;
            assign_typed(c, lref ? static_cast<ReferenceType*>(dst)->referent() : dst, lref ? VC::LValue : VC::RValue);
            break;
        }

        case K::NewExpression: {
            auto* n = static_cast<nodes::NewExpression*>(e);
            if (n->array_size) walk_expr(n->array_size);
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
                if (res.status == SelectStatus::Ok) { x->resolved = res.chosen; assign_typed(e, res.type, VC::RValue); break; }
                SemanticError::no_matching_overload(m_reporter, file_of(x), x->line, "~");
                assign_typed(e, nullptr, VC::RValue);
                break;
            }

            if (t && !is_integral_t(t) && !is_dynamic_t(t)) SemanticError::bad_operand(m_reporter, file_of(x), x->line, "~", type_str(t));
            assign_typed(e, t, VC::RValue);
            break;
        }

        case K::LambdaExpression: {
            auto* l = static_cast<nodes::LambdaExpression*>(e);
            if (l->m_captures) for (auto& c : l->m_captures->m_captures) if (c.init) walk_expr(c.init);

            if (is_generic_lambda(l)) {
                if (auto* ps = l->get_parameters()) for (nodes::FunctionParameter* p : ps->m_params) walk_expr(p->m_initializer);
                assign_typed(e, m_types.closure(l), VC::RValue);
                break;
            }

            Scope* saved = m_current;
            ControlContextGuard ctx(*this);
            if (l->scope) m_current = l->scope;
            std::vector<Type*> param_types;

            if (auto* ps = l->get_parameters()) {
                param_types.reserve(ps->size());
                    
                for (nodes::FunctionParameter* p : ps->m_params) {
                    walk_expr(p->m_initializer);
                    param_types.push_back(m_types.canonicalize(p->get_type()));
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
            assign_typed(e, m_types.function(ret, std::move(param_types), q.has(FQ::Const), rq, q.has(FQ::Noexcept)), VC::RValue);
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
            Type* queried = nullptr;
            q->pack_count = -1;

            if (q->operand && q->op == nodes::TypeQueryExpression::Op::Countof) {
                Symbol* ps = nullptr;
                if (q->operand->is_value() && q->operand->value && q->operand->value->kind == K::Identifier) ps = static_cast<nodes::Identifier*>(q->operand->value)->resolved;
                else if (q->operand->is_type()) ps = q->operand->type.resolved;
                nodes::FunctionParameter* vp = variadic_param_of(ps);
                std::vector<Symbol*> pks;
                if (vp) collect_pack_params(m_types.canonicalize(vp->get_type()), pks);

                if (vp && (vp->expanded_pack || !pks.empty())) {
                    if (vp->expanded_pack) q->pack_count = static_cast<long long>(vp->pack_symbols.size());
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
                if (q->op == QOp::Countof && qv && !qv->is_error() && !qv->is_array() && !is_dynamic_t(qv)
                    && !(qv->is_builtin() && (static_cast<BuiltinType*>(qv)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(qv)->base() == parser_types::PrimitiveType::BaseKind::Text))) {
                    SemanticError::emit(m_reporter, file_of(q), q->line, "Type", "countof requires an array or a string, not '" + type_str(qv) + "'");
                }
                long long size = 0, alignment = 0;
                if (q->op != QOp::Countof && qv && !qv->is_error() && !qv->is_dependent() && !m_eval.layout_of(qv, size, alignment)) {
                    SemanticError::emit(m_reporter, file_of(q), q->line, "Type", std::string(q->op_name()) + " is not defined for '" + type_str(qv) + "' because it has no fixed size");
                }
                assign_typed(e, m_types.builtin(parser_types::PrimitiveType::BaseKind::Int), VC::RValue);
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

            if (Symbol* g = callee_symbol(t->m_template); g && g->kind == SymbolKind::Concept) {
                assign_typed(e, m_types.bool_(), VC::RValue);
                break;
            }

            if (Symbol* g = callee_symbol(t->m_template); g && m_inst) {
                bool ok = true;
                std::vector<Instantiator::Arg> args = m_inst->canon_args(t->m_args, ok);
                    
                if (ok) {
                    if (auto* I = m_inst->instantiate(g, std::move(args), t)) {
                        if (I->decl && I->decl->kind == K::FunctionDeclaration) ensure_typed(I);   // functions: eager
                        result = I->type;                                                          // records: lazy members
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
}

void TypeWalker::check_cast(nodes::CastExpression* c, Type* src, Type* dst) {
    using CK = nodes::CastExpression::CastKind;
    if (!src || !dst) return;
    if (is_dynamic_t(src) || is_dynamic_t(dst)) return;   

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
            if (m_types.strip_cv(src) != m_types.strip_cv(dst)) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
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
    Scope* ms = member_scope_of(recv);
    if (!ms) return;

    for (Symbol* o = ms->find_member("operator"); o; o = o->next_overload) {
        if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
        if (static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() != want) continue;
        EnvGuard g(m_types, env_of_symbol(o));
        CandidateMatch m = match_arguments(shapes_of(o), explicit_args, m_types);
        if (!m.viable) continue;
        m.ranks.insert(m.ranks.begin(), ConversionRank::Exact);   
        matches.push_back(std::move(m)); cands.push_back(o); member.push_back(true);
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

Type* TypeWalker::iterated_element(Type* container) {
    Type* c = container;
    if (c && c->is_reference()) c = static_cast<ReferenceType*>(c)->referent();
    c = m_types.strip_cv(c);
    if (!c) return nullptr;
    if (CoroutineType* g = generator_type(c)) return g->value();
    if (c->is_array()) return static_cast<ArrayType*>(c)->element();
    if (c->is_builtin() && (static_cast<BuiltinType*>(c)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(c)->base() == parser_types::PrimitiveType::BaseKind::Text)) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Char);
    if (is_dynamic_t(c)) return c;
    return nullptr;
}

void TypeWalker::type_for_each(nodes::ForEachStatement* s) {
    Type* ct = type_of(s->m_container);
    if (!ct || ct->is_error()) return;
    Type* el = iterated_element(ct);

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
        ti.canonical = ti.indirection.empty() ? el : m_types.canonicalize(ti);
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

    if (s->kind == SymbolKind::Function && !s->intrinsic && !s->template_decl && s->decl && s->decl->kind == K::FunctionDeclaration) {
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
            if (!v->get_type_info().modifiers.has(modifiers::RawModifiers::Static) && v->symbol) out.push_back(v->symbol);
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

bool TypeWalker::is_generic_lambda(const nodes::LambdaExpression* l) {
    if (l->generic_origin || !l->get_parameters()) return false;
    for (const nodes::FunctionParameter* p : l->get_parameters()->m_params) if (prim_is(p->get_type(), parser_types::PrimitiveType::BaseKind::Auto)) return true;
    return false;
}

nodes::LambdaExpression* TypeWalker::specialize_lambda(nodes::LambdaExpression* gl, const std::vector<Type*>& args, nodes::ASTNode* site, bool quiet) {
    if (!m_inst || !gl->get_parameters()) return nullptr;
    const auto& params = gl->get_parameters()->m_params;
    std::vector<Type*> key;

    for (Type* a : args) {
        if (a && a->is_reference()) a = static_cast<ReferenceType*>(a)->referent();
        key.push_back(m_types.strip_cv(a));
    }

    if (key.size() != params.size()) {
        if (!quiet && site) SemanticError::no_matching_overload(m_reporter, file_of(site), site->line, "lambda");
        return nullptr;
    }

    for (std::size_t i = 0; i < gl->spec_keys.size(); ++i) {
        if (gl->spec_keys[i] != key) continue;
        if (gl->spec_failed[i]) return nullptr;
        return gl->specializations[i];
    }

    for (Type* k : key) if (!k || k->is_error()) return nullptr;
    auto* clone = static_cast<nodes::LambdaExpression*>(gl->clone_into(m_inst->arena()));
    clone->generic_origin = gl;
    Scope* parent = gl->scope ? gl->scope->parent : m_current;
    if (m_inst->build_scopes) m_inst->build_scopes(clone, parent);
    if (m_inst->resolve_names) m_inst->resolve_names(clone, parent);

    for (std::size_t i = 0; i < params.size(); ++i) {
        parser_types::TypeInfo& ti = clone->get_parameters()->m_params[i]->get_type();
        if (!prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) continue;
        Type* t = key[i];
        if (ti.modifiers.has(modifiers::RawModifiers::Const)) t = add_const(t);

        for (const parser_types::IndirectionQualifier& q : ti.indirection) {
            if (q.is_pointer()) t = m_types.pointer(t);
            else t = m_types.reference(t, q.is_rvalue_ref() ? RefQual::RValue : RefQual::LValue);
        }

        ti.canonical = t;
    }

    const std::size_t index = gl->specializations.size();
    gl->specializations.push_back(clone);
    gl->spec_keys.push_back(key);
    gl->spec_failed.push_back(false);
    Scope* saved = m_current;
    m_current = parent;
    DiagnosticTrap trap(m_reporter);
    walk_expr(clone);
    const bool failed = quiet ? trap.discard() : trap.commit();
    m_current = saved;

    if (failed && quiet) {
        gl->specializations.erase(gl->specializations.begin() + static_cast<std::ptrdiff_t>(index));
        gl->spec_keys.erase(gl->spec_keys.begin() + static_cast<std::ptrdiff_t>(index));
        gl->spec_failed.erase(gl->spec_failed.begin() + static_cast<std::ptrdiff_t>(index));
        return nullptr;
    }

    if (failed) {
        gl->spec_failed[index] = true;
        return nullptr;
    }

    return clone;
}

bool TypeWalker::is_pack_spread(const nodes::ASTNode* a) {
    return a && a->kind == K::UnaryExpression && static_cast<const nodes::UnaryExpression*>(a)->op == tokenizing::Token::Kind::Ellipsis;
}

void TypeWalker::expand_pack_arguments(nodes::CallExpression* c) {
    bool any = false;
    for (nodes::ASTNode* a : c->m_arguments) any = any || is_pack_spread(a);
    if (!any) return;
    std::vector<nodes::ASTNode*> out;

    for (nodes::ASTNode* a : c->m_arguments) {
        if (!is_pack_spread(a)) { out.push_back(a); continue; }
        nodes::ASTNode* pattern = static_cast<nodes::UnaryExpression*>(a)->operand;
        Symbol* pack = find_pack_ref(pattern);
        nodes::FunctionParameter* p = variadic_param_of(pack);
        std::vector<Symbol*> pks;
        if (p) collect_pack_params(m_types.canonicalize(p->get_type()), pks);

        if (p && p->expanded_pack && m_inst) {
            for (Symbol* es : p->pack_symbols) {
                nodes::ASTNode* clone = clone_resolved(pattern);
                substitute_symbol(clone, pack, es);
                out.push_back(clone);
            }
            continue;
        }

        if (!p || pks.empty()) SemanticError::emit(m_reporter, file_of(a), a->line, "Type", "only a template parameter pack can be expanded with '...'; pass a runtime variadic parameter as an array instead");
        out.push_back(a);
    }

    c->m_arguments = std::move(out);
}

void TypeWalker::type_fold(nodes::FoldExpression* f) {
    using Form = nodes::FoldExpression::Form;
    using TK = tokenizing::Token::Kind;
    f->lowered = f->sequence = f->first = f->step = f->init = nullptr;
    f->element = f->accumulator = nullptr;
    nodes::ASTNode* pattern = nullptr;
    nodes::ASTNode* init = nullptr;

    if (f->form == Form::UnaryLeft) pattern = f->rhs;
    else if (f->form == Form::UnaryRight) pattern = f->lhs;
    else if (find_pack_ref(f->lhs)) { pattern = f->lhs; init = f->rhs; }
    else { pattern = f->rhs; init = f->lhs; }

    const bool left = f->form == Form::UnaryLeft || (f->form == Form::Binary && pattern == f->rhs);
    Symbol* pack = find_pack_ref(pattern);
    nodes::FunctionParameter* p = variadic_param_of(pack);

    if (!p || !m_inst) {
        walk_expr(f->lhs); walk_expr(f->rhs);
        if (!p) SemanticError::emit(m_reporter, file_of(f), f->line, "Type", "a fold expression needs a variadic parameter to expand");
        assign_typed(f, nullptr, VC::RValue);
        return;
    }

    if (p->expanded_pack) {
        std::vector<nodes::ASTNode*> elems;
        for (Symbol* es : p->pack_symbols) {
            nodes::ASTNode* c = clone_resolved(pattern);
            substitute_symbol(c, pack, es);
            elems.push_back(c);
        }

        nodes::ASTNode* acc = nullptr;

        if (elems.empty()) {
            if (init) acc = init;
            else if (f->op == TK::LogicAnd) acc = make_in<nodes::Literal>(m_inst->arena(), std::string_view("true"), TK::True, f->line);
            else if (f->op == TK::LogicOr) acc = make_in<nodes::Literal>(m_inst->arena(), std::string_view("false"), TK::False, f->line);
            else if (f->op != TK::Comma) SemanticError::emit(m_reporter, file_of(f), f->line, "Type", "a fold over an empty pack needs an initial value");
        } else if (left) {
            acc = init ? binary_node(init, f->op, elems.front(), f->line) : elems.front();
            for (std::size_t i = 1; i < elems.size(); ++i) acc = binary_node(acc, f->op, elems[i], f->line);
        } else {
            acc = init ? binary_node(elems.back(), f->op, init, f->line) : elems.back();
            for (std::size_t i = elems.size() - 1; i-- > 0;) acc = binary_node(elems[i], f->op, acc, f->line);
        }

        if (acc) acc->file_id = f->file_id;
        f->lowered = acc;
        walk_expr(acc);
        assign_typed(f, acc ? type_of(acc) : m_types.void_(), VC::RValue);
        return;
    }

    Type* seq = symbol_type(pack);
    if (!seq || seq->is_dependent() || !m_types.strip_cv(seq)->is_array()) {
        walk_expr(f->lhs); walk_expr(f->rhs);
        assign_typed(f, nullptr, VC::RValue);
        return;
    }

    Type* el = static_cast<ArrayType*>(m_types.strip_cv(seq))->element();
    const std::string tag = std::to_string(m_inst->kept_names());
    Symbol* es = synthetic_symbol("fold#e" + tag, el, f);
    f->sequence = ident_for(pack, f->line);
    walk_expr(f->sequence);
    f->element = es;
    f->right_to_left = !left;
    nodes::ASTNode* first = clone_resolved(pattern);
    substitute_symbol(first, pack, es);
    walk_expr(first);
    f->first = first;

    if (f->op == TK::Comma) {
        walk_expr(init);
        f->init = init;
        assign_typed(f, m_types.void_(), VC::RValue);
        return;
    }

    Type* pt = type_of(first);
    if (pt && pt->is_reference()) pt = static_cast<ReferenceType*>(pt)->referent();
    pt = m_types.strip_cv(pt);
    Symbol* acc = synthetic_symbol("fold#a" + tag, pt, f);
    f->accumulator = acc;
    nodes::ASTNode* again = clone_resolved(pattern);
    substitute_symbol(again, pack, es);
    nodes::ASTNode* step = left ? binary_node(ident_for(acc, f->line), f->op, again, f->line) : binary_node(again, f->op, ident_for(acc, f->line), f->line);
    step->file_id = f->file_id;
    walk_expr(step);
    Type* rt = type_of(step);
    if (rt && rt->is_reference()) rt = static_cast<ReferenceType*>(rt)->referent();
    rt = m_types.strip_cv(rt);

    if (rt && rt != pt) {
        acc->bound_type = rt;
        walk_expr(step);
    }

    f->step = step;

    if (init) {
        walk_expr(init);
        f->init = init;
        if (Type* it = type_of(init); it && rt) check_binding(init, it, rt, f);
    }

    assign_typed(f, rt, VC::RValue);
}

Type* TypeWalker::type_of_literal(nodes::Literal* lit) {
    using BK = parser_types::PrimitiveType::BaseKind;
    using TK = tokenizing::Token::Kind;
    switch (lit->get_kind()) {
        case TK::Integer: {
            ConstValue v = m_eval.eval(lit);
            if (v.is_int()) for (int rank = 0; rank <= 6; ++rank) if (ConstTable::fits(*v.i, bit_width_of_rank(rank), true)) return m_types.builtin_ranked(BK::Int, rank);
            return m_types.builtin(BK::Int);
        }
        case TK::Float:           return m_types.builtin(BK::Float);
        case TK::InfinityKeyword: return m_types.builtin(BK::Float);   
        case TK::String:          return m_types.builtin(BK::String);
        case TK::TextLiteral:     return m_types.builtin(BK::Text);
        case TK::Character:       return m_types.builtin(BK::Char);
        case TK::True:
        case TK::False:           return m_types.bool_();
        case TK::NullptrKeyword:  return m_types.null_();
        case TK::ThisKeyword:     return this_type();
        default:                  return m_types.dynamic_();
    }
}

void TypeWalker::rebase_on_instance(nodes::RecordDeclaration* r) {
    if (!m_inst || !r->has_inherits() || !r->scope) return;
    Type* bt = m_types.strip_cv(m_types.canonicalize(r->get_inherits()));
    if (!bt || !bt->is_record() || bt->is_dependent()) return;
    auto* rt = static_cast<RecordType*>(bt);
    if (!rt->is_instantiation()) return;
    Instantiator::Instantiation* I = m_inst->for_record(rt, r);
    if (!I || !I->sym || !I->sym->inner_scope) return;
    Scope* inst_scope = I->sym->inner_scope;
    Symbol* generic = r->get_inherits().resolved;
    bool replaced = false;

    for (Scope*& b : r->scope->bases) {
        if (b == inst_scope) { replaced = true; break; }
        if (generic && b == generic->inner_scope) { b = inst_scope; replaced = true; break; }
    }

    if (!replaced) r->scope->bases.push_back(inst_scope);
    ensure_typed(I);
}

void TypeWalker::refresh_member(nodes::Identifier* id) {
    Symbol* s = id->resolved;
    if (!s || !s->owner || s->owner->kind != Scope::Kind::Record) return;

    for (Scope* sc = m_current; sc; sc = sc->parent) {
        if (sc->kind != Scope::Kind::Record) continue;
        Symbol* again = sc->find_member(s->name);
        if (again && again != s && again->kind == s->kind) id->resolved = again;
        return;
    }
}

Type* TypeWalker::record_type_of(Symbol* sym) {
    if (m_types.instance_type_hook) if (Type* t = m_types.instance_type_hook(sym)) return t;
    return m_types.record(sym, {}, CV{});
}

Type* TypeWalker::this_type() {
    Type* rec = enclosing_record_type();
    if (rec && rec->is_record() && m_in_const_method) rec = add_const(rec);
    return (rec && rec->is_record()) ? m_types.pointer(rec) : rec;
}

Type* TypeWalker::enclosing_record_type() {
    for (Scope* s = m_current; s; s = s->parent) {       
        if (s->node && s->node->kind == K::RecordDeclaration)
            if (Symbol* sym = static_cast<nodes::RecordDeclaration*>(s->node)->symbol) return record_type_of(sym);
    }

    return m_types.dynamic_();
}

Type* TypeWalker::type_binary(nodes::BinaryExpression* b, Symbol*& resolved_op) {
    resolved_op = nullptr;
    Type* l = type_of(b->left); 
    Type* r = type_of(b->right);
    const tokenizing::Token::Kind op = b->op;
    if (op == tokenizing::Token::Kind::Comma) return r;

    if (record_symbol_of(l) || record_symbol_of(r)) {
        const nodes::OverloadableOperator want = nodes::classify_single_operator(op);
        OpResult res = resolve_operator(want, { l, r });
        b->negate_result = false;
        if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }

        if (op == tokenizing::Token::Kind::NotEqual && res.status == SelectStatus::NoMatch) {
            OpResult eq = resolve_operator(nodes::classify_single_operator(tokenizing::Token::Kind::LogicEqual), { l, r });

            if (eq.status == SelectStatus::Ok) {
                resolved_op = eq.chosen;
                b->negate_result = true;
                return m_types.bool_();
            }
        }

        if (is_logical_op(op) && res.status == SelectStatus::NoMatch) {
            if (l && !is_bool_testable(l)) SemanticError::condition_not_bool(m_reporter, file_of(b->left),  b->left->line,  type_str(l));
            if (r && !is_bool_testable(r)) SemanticError::condition_not_bool(m_reporter, file_of(b->right), b->right->line, type_str(r));
            return m_types.bool_();
        }

        if (op == tokenizing::Token::Kind::Equal && res.status == SelectStatus::NoMatch && record_symbol_of(l)) {
            if (!is_lvalue(b->left)) SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", false);
            else if (type_is_const(l)) SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", true);
            if (r) check_binding(b->right, r, m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l), b);
            return l;
        }

        if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
        else                                       SemanticError::no_matching_overload(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
        return nullptr;
    }

    if (!check_builtin_operands(b, l, r) && !is_assignment_op(op)) {
        return (is_comparison_op(op) || is_equality_op(op)) ? m_types.bool_() : l;
    }

    if (is_assignment_op(op)) {
        if (!known_t(l)) {
        } else if (!is_lvalue(b->left)) {
            SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", false);
        } else if (type_is_const(l)) {
            SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", true);
        }

        if (l && r) {
            using TKind = tokenizing::Token::Kind;
            Type* lv = m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l);
            Type* rv = m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r);
            const bool r_integer = rv && rv->is_builtin() && static_cast<BuiltinType*>(rv)->base() == parser_types::PrimitiveType::BaseKind::Int;
            if (b->op == TKind::Equal) check_binding(b->right, r, l, b);
            else if (lv && lv->is_pointer() && (b->op == TKind::PlusEqual || b->op == TKind::MinusEqual) && (r_integer || is_dynamic_t(rv))) {}
            else if ((b->op == TKind::ShiftLeftEqual || b->op == TKind::ShiftRightEqual) && r_integer) {}
            else if (b->op == TKind::PlusEqual && lv && lv->is_builtin() && (static_cast<BuiltinType*>(lv)->base() == parser_types::PrimitiveType::BaseKind::String || static_cast<BuiltinType*>(lv)->base() == parser_types::PrimitiveType::BaseKind::Text)
                     && rv && rv->is_builtin() && static_cast<BuiltinType*>(rv)->base() == parser_types::PrimitiveType::BaseKind::Char) {}
            else if (!constant_fits(b->right, r, l)) check_convertible(r, l, b);
        }
        return l;                                        
    }

    if (is_logical_op(op)) {                             
        if (l && !is_bool_testable(l)) SemanticError::condition_not_bool(m_reporter, file_of(b->left),  b->left->line,  type_str(l));
        if (r && !is_bool_testable(r)) SemanticError::condition_not_bool(m_reporter, file_of(b->right), b->right->line, type_str(r));
        return m_types.bool_();
    }

    if (is_comparison_op(op) || is_equality_op(op)) return m_types.bool_();
    if (is_dynamic_t(l) || is_dynamic_t(r)) return m_types.dynamic_();   

    if (op == tokenizing::Token::Kind::Plus && l && r) {
        if (operand_class(l) == OperandClass::String) return m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l);
        if (operand_class(r) == OperandClass::String) return m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r);
    }

    if (l && r) {
        if (Type* adapted = adapt_literal(b->right, r, l)) return adapted;
        if (Type* adapted = adapt_literal(b->left, l, r)) return adapted;
    }

    if (op == tokenizing::Token::Kind::Minus && l && r) {
        Type* lp = m_types.strip_cv(l->is_reference() ? static_cast<ReferenceType*>(l)->referent() : l);
        Type* rp = m_types.strip_cv(r->is_reference() ? static_cast<ReferenceType*>(r)->referent() : r);
        if (lp && rp && lp->is_pointer() && rp->is_pointer()) return m_types.builtin(parser_types::PrimitiveType::BaseKind::Int, parser_types::LengthModifier::Long);
    }

    if (l && r) {
        if (rank_conversion(r, l, m_types) != ConversionRank::None) return l;
        if (rank_conversion(l, r, m_types) != ConversionRank::None) return r;
    }

    return l ? l : r;
}

Type* TypeWalker::type_unary(nodes::UnaryExpression* u, Symbol*& resolved_op) {
    using K = tokenizing::Token::Kind;
    resolved_op = nullptr;
    Type* t = type_of(u->operand);

    if (u->op == K::ExclamationMark) {
        if (record_symbol_of(t)) {
            OpResult res = resolve_operator(nodes::OverloadableOperator::LogicalNot, { t });
            if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
        }

        if (t && !is_bool_testable(t)) SemanticError::not_convertible(m_reporter, file_of(u), u->line, type_str(t), "bool");
        return m_types.bool_();
    }

    if (u->op == K::DoublePlus || u->op == K::DoubleMinus) {
        if (record_symbol_of(t)) {
            nodes::OverloadableOperator want = nodes::classify_single_operator(u->op);
            OpResult res = resolve_incdec(t, want, !u->is_prefix());
            if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
            if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
            else                                       SemanticError::no_matching_overload(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
            return nullptr;
        }

        const char* what = (u->op == K::DoublePlus) ? "apply '++' to" : "apply '--' to";
        const char* sym  = (u->op == K::DoublePlus) ? "++" : "--";

        if (!known_t(t))             {}
        else if (!is_lvalue(u->operand))  SemanticError::not_assignable(m_reporter, file_of(u), u->line, what, false);
        else if (type_is_const(t))   SemanticError::not_assignable(m_reporter, file_of(u), u->line, what, true);

        if (t && !is_arithmetic_or_pointer(t) && !is_dynamic_t(t)) SemanticError::bad_operand(m_reporter, file_of(u), u->line, sym, type_str(t));
        return t;
    }

    if (record_symbol_of(t)) {                      
        const nodes::OverloadableOperator want = nodes::classify_single_operator(u->op);
        OpResult res = resolve_operator(want, { t });
        if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
        if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
        else                                       SemanticError::no_matching_overload(m_reporter, file_of(u), u->line, nodes::overloadable_operator_name(want));
        return nullptr;
    }

    if (u->op == K::Minus && t && !is_numeric_t(t) && !is_dynamic_t(t)) SemanticError::bad_operand(m_reporter, file_of(u), u->line, "-", type_str(t));
    return t;
}

Type* TypeWalker::common_type(Type* a, Type* b, nodes::ASTNode* site) {
    if (!a) return b;
    if (!b) return a;
    if (a == b) return a;
    if (is_dynamic_t(a) || is_dynamic_t(b)) return m_types.dynamic_();
    if (rank_conversion(b, a, m_types) != ConversionRank::None) return a;
    if (rank_conversion(a, b, m_types) != ConversionRank::None) return b;
    SemanticError::no_common_type(m_reporter, file_of(site), site->line, type_str(a), type_str(b));
    return a;
}

void TypeWalker::walk_param_defaults(const nodes::FunctionParameters* ps) {
    if (!ps) return;

    for (nodes::FunctionParameter* p : ps->m_params) {
        if (!p || !p->has_initializer()) continue;
        nodes::ASTNode* d = const_cast<nodes::ASTNode*>(p->get_initializer());
        walk_expr(d);
        Type* want = m_types.canonicalize(p->get_type());
        Type* got = type_of(d);
        if (want && got && !is_dynamic_t(want)) check_binding(d, got, want, d);
    }
}

Symbol* TypeWalker::generic_of_instance(Symbol* s) {
    if (!s || !m_inst || s->kind != SymbolKind::Function) return s;
    Instantiator::Instantiation* I = m_inst->owning(s);
    if (!I || I->sym != s || !I->source || !I->source->m_declaration || I->source->m_declaration->kind != K::FunctionDeclaration) return s;
    Symbol* primary = static_cast<nodes::FunctionDeclaration*>(I->source->m_declaration)->symbol;
    if (!primary) return s;
    if (primary->owner) if (Symbol* head = primary->owner->find_local(primary->name)) return head;
    return primary;
}

Type* TypeWalker::type_call(nodes::CallExpression* c, const std::vector<Type*>& args, VC& vc) {
    vc = VC::RValue;
    if (c->m_callee && c->m_callee->kind == K::Identifier) refresh_member(static_cast<nodes::Identifier*>(c->m_callee));
    Symbol* callee_sym = generic_of_instance(callee_symbol(c->m_callee));
    if (callee_sym && callee_sym->intrinsic) return type_intrinsic(c, callee_sym, args);

    if (Type* constructed = constructed_type(c->m_callee, callee_sym, c)) {
        check_not_abstract(constructed, c);
        c->resolved = resolve_construction(constructed, args, c);
        return constructed;
    }

    if (callee_sym && callee_sym->kind == SymbolKind::Function) {
        const std::size_t constraint_failures_before = m_inst ? m_inst->constraint_failures : 0;
        std::vector<CandidateMatch>                 matches;
        std::vector<Symbol*>                        cand_syms;
        std::vector<Instantiator::Instantiation*>   cand_inst;

        for (Symbol* o = callee_sym; o; o = o->next_overload) {
            if (o->template_decl) {                         
                if (!m_inst) continue;
                auto* I = m_inst->instantiate_for_call(o, c->get_template_args(), args, c);
                if (!I) continue;                           
                matches.push_back(match_with_constants(c, I->fn_shapes, args));
                cand_syms.push_back(I->sym);
                cand_inst.push_back(I);
                continue;
            }

            Instantiator::Instantiation* owner = m_inst ? m_inst->owning(o) : nullptr;

            {
                EnvGuard g(m_types, owner ? &owner->env : nullptr);
                matches.push_back(match_with_constants(c, shapes_of(o), args));
            }

            cand_syms.push_back(o);
            cand_inst.push_back(owner);
        }

        Selection sel = select_overload(matches);

        if (sel.status == SelectStatus::NoMatch) {
            if (m_inst && m_inst->constraint_failures != constraint_failures_before) {
                SemanticError::constraints_not_satisfied(m_reporter, file_of(c), c->line, callee_name(c->m_callee), m_last_constraint_failure);
            } else {
                SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee));
            }
            return nullptr;
        }
        if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(c), c->line, callee_name(c->m_callee)); return nullptr; }
        Symbol* chosen = cand_syms[sel.index];
        c->resolved = chosen;
        check_const_call(chosen, c);

        if (Instantiator::Instantiation* I = cand_inst[sel.index]) {
            ensure_typed(I);                                 
            { EnvGuard g(m_types, &I->env); check_arg_bindings(c, shapes_of(chosen)); }
            if (I->type && I->type->is_function()) return decay_ref(async_result(chosen, static_cast<FunctionType*>(I->type)->ret()), vc);
            EnvGuard g(m_types, &I->env);
            return decay_ref(async_result(chosen, chosen->type ? m_types.canonicalize(*chosen->type) : nullptr), vc);
        }

        check_arg_bindings(c, shapes_of(chosen));
        return decay_ref(async_result(chosen, chosen->type ? m_types.canonicalize(*chosen->type) : nullptr), vc);
    }

    Type* callee_type = type_of(c->m_callee);
    c->closure_spec = nullptr;

    if (Type* ct = m_types.strip_cv(callee_type && callee_type->is_reference() ? static_cast<ReferenceType*>(callee_type)->referent() : callee_type); ct && ct->kind() == TypeKind::Closure) {
        auto* gl = static_cast<nodes::LambdaExpression*>(static_cast<ClosureType*>(ct)->lambda());
        nodes::LambdaExpression* spec = specialize_lambda(gl, args, c, false);
        c->closure_spec = spec;
        FunctionType* sf = spec ? as_function_type(type_of(spec)) : nullptr;
        if (!sf) return nullptr;
        for (std::size_t i = 0; i < args.size() && i < sf->params().size(); ++i) if (args[i] && sf->params()[i]) check_binding(c->m_arguments[i], args[i], sf->params()[i], c, false);
        return decay_ref(sf->ret(), vc);
    }

    if (FunctionType* ft = as_function_type(callee_type)) {
        const std::vector<Type*>& params = ft->params();

        if (args.size() != params.size()) {
            SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee));
        } else {
            for (std::size_t i = 0; i < args.size(); ++i) if (args[i] && params[i]) check_binding(c->m_arguments[i], args[i], params[i], c, false);
        }

        return decay_ref(ft->ret(), vc);
    }

    if (record_symbol_of(callee_type)) {
        std::vector<Type*> operands;
        operands.reserve(args.size() + 1);
        operands.push_back(callee_type);
        for (Type* a : args) operands.push_back(a);
        OpResult res = resolve_operator(nodes::OverloadableOperator::Call, operands);
        if (res.status == SelectStatus::Ok)        { c->resolved = res.chosen; return decay_ref(res.type, vc); }
        if (res.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(c), c->line, "()"); }
        else                                       { SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, "()"); }
        return nullptr;
    }

    return callee_type;                                  
}

bool TypeWalker::derives_from(Scope* s, Scope* base, int depth) {
    if (!s || depth > 32) return false;
    for (Scope* b : s->bases) if (b == base || derives_from(b, base, depth + 1)) return true;
    return false;
}

bool TypeWalker::is_friend_of(Scope* record_scope) {
    if (!record_scope || !record_scope->node || record_scope->node->kind != K::RecordDeclaration) return false;
    auto* rd = static_cast<nodes::RecordDeclaration*>(record_scope->node);
    std::string_view fn_name;

    for (auto it = m_returns.rbegin(); it != m_returns.rend(); ++it) {
        if (it->site && it->site->kind == K::FunctionDeclaration) { fn_name = static_cast<nodes::FunctionDeclaration*>(it->site)->get_name(); break; }
    }

    for (const auto& member : rd->get_members()) {
        nodes::ASTNode* n = member.node;
        if (!n) continue;
        using RM = modifiers::RawModifiers;

        if (n->kind == K::FunctionDeclaration) {
            auto* f = static_cast<nodes::FunctionDeclaration*>(n);
            if (f->get_modifiers().has(RM::Friend) && !fn_name.empty() && f->get_name() == fn_name) return true;
        } else if (n->kind == K::RecordDeclaration) {
            auto* r = static_cast<nodes::RecordDeclaration*>(n);
            if (!r->get_modifiers().has(RM::Friend)) continue;
            for (Scope* sc = m_current; sc; sc = sc->parent) {
                if (sc->kind == Scope::Kind::Record && sc->owner_symbol && sc->owner_symbol->name == r->get_name()) return true;
            }
        } else if (n->kind == K::OperatorFunctionDeclaration) {
            if (static_cast<nodes::OperatorFunctionDeclaration*>(n)->get_modifiers().has(RM::Friend)) {
                for (auto it = m_returns.rbegin(); it != m_returns.rend(); ++it) {
                    if (it->site && it->site->kind == K::OperatorFunctionDeclaration) return true;
                }
            }
        }
    }

    return false;
}

void TypeWalker::check_member_access(Symbol* found, nodes::ASTNode* site) {
    if (!found || found->member_access == 0 || !site) return;
    Scope* owner = found->owner;
    if (!owner || owner->kind != Scope::Kind::Record) return;
    const bool is_private = found->member_access == static_cast<std::uint8_t>(nodes::AccessLevel::Private);

    for (Scope* sc = m_current; sc; sc = sc->parent) {
        if (sc->kind != Scope::Kind::Record) continue;
        if (sc == owner) return;
        if (!is_private && derives_from(sc, owner)) return;
    }

    if (is_friend_of(owner)) return;
    std::string_view rec = owner->owner_symbol ? owner->owner_symbol->name : std::string_view{"<record>"};
    SemanticError::inaccessible_member(m_reporter, file_of(site), site->line, found->name, is_private, rec);
}

Type* TypeWalker::type_member_access(nodes::MemberAccessExpression* m) {
    if (m->is_scope()) {
        if (m->m_object && m->m_object->kind == K::TemplateInstantiation && m_inst) {
            Type* ot = m_types.strip_cv(type_of(m->m_object));
            if (ot && ot->is_record() && static_cast<RecordType*>(ot)->is_instantiation()) {
                if (Instantiator::Instantiation* I = m_inst->for_record(static_cast<RecordType*>(ot), m)) {
                    ensure_typed(I);
                    if (I->sym && I->sym->inner_scope) m->resolved = I->sym->inner_scope->find_member(m->get_member());
                    if (!m->resolved) SemanticError::unresolved_name(m_reporter, file_of(m), m->line, m->get_member());
                }
            }
        }
        check_member_access(m->resolved, m);
        return symbol_type(m->resolved);
    }
    Type* obj = type_of(m->m_object);
    if (!obj) return nullptr;
    if (m->is_arrow()) obj = arrow_target(obj);
    Scope* members = nullptr;
    Instantiator::Instantiation* I = nullptr;
    Type* s = m_types.strip_cv(obj);
    if (s && s->is_reference()) s = m_types.strip_cv(static_cast<ReferenceType*>(s)->referent());

    if (s && s->is_record()) {
        auto* rt = static_cast<RecordType*>(s);

        if (rt->is_instantiation() && m_inst) {
            I = m_inst->for_record(rt, m);              
            if (I && I->sym) members = I->sym->inner_scope;
        }

        if (!members && rt->decl()) members = rt->decl()->inner_scope;
    }

    Symbol* found = members ? members->find_member(m->get_member()) : nullptr;
    m->resolved = found;
    check_member_access(found, m);
        
    if (!found) {
        if (obj && !is_dynamic_t(obj) && !obj->is_error()) SemanticError::unresolved_name(m_reporter, file_of(m), m->line, m->get_member());
        return nullptr;
    }

    EnvGuard g(m_types, I ? &I->env : nullptr);          
    Type* ft = symbol_type(found);
    Type* ov = obj && obj->is_reference() ? static_cast<ReferenceType*>(obj)->referent() : obj;
    if (ov && ov->cv().is_const && is_instance_field(found)) ft = add_const(ft);
    return ft;
}

bool TypeWalker::is_instance_field(const Symbol* s) {
    if (!s || s->kind != SymbolKind::Variable || !s->owner || s->owner->kind != Scope::Kind::Record || !s->decl) return false;
    using RM = modifiers::RawModifiers;
    if (s->decl->kind == K::VariableDeclaration) {
        const auto& mods = static_cast<const nodes::VariableDeclaration*>(s->decl)->get_type_info().modifiers;
        return !mods.has(RM::Static) && !mods.has(RM::Mutable);
    }
    if (s->decl->kind == K::ArrayDeclaration) return !static_cast<const nodes::ArrayDeclaration*>(s->decl)->get_array_modifiers().has(RM::Static);
    return false;
}

Type* TypeWalker::add_const(Type* t) {
    if (!t || t->is_reference() || t->is_error()) return t;
    CV c = t->cv();
    c.is_const = true;
    return m_types.with_cv(t, c);
}

bool TypeWalker::is_const_method(const Symbol* fn) {
    if (!fn || !fn->decl) return false;
    using FQ = modifiers::FunctionQualifiers;
    if (fn->decl->kind == K::FunctionDeclaration) {
        auto* f = static_cast<const nodes::FunctionDeclaration*>(fn->decl);
        return f->qualifiers().has(FQ::Const) || f->get_modifiers().has(modifiers::RawModifiers::Static);
    }
    if (fn->decl->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(fn->decl)->qualifiers().has(FQ::Const);
    return true;
}

bool TypeWalker::is_member_function(const Symbol* fn) const {
    return fn && fn->owner && fn->owner->kind == Scope::Kind::Record && fn->decl
        && (fn->decl->kind == K::FunctionDeclaration || fn->decl->kind == K::OperatorFunctionDeclaration);
}

void TypeWalker::check_const_call(Symbol* chosen, nodes::CallExpression* c) {
    if (!is_member_function(chosen) || is_const_method(chosen)) return;
    bool on_const = false;

    if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
        if (m->is_scope()) on_const = m_in_const_method;
        else {
            Type* o = type_of(m->m_object);
            if (o && o->is_reference()) o = static_cast<ReferenceType*>(o)->referent();
            if (o && m->is_arrow() && o->is_pointer()) o = static_cast<PointerType*>(o)->pointee();
            on_const = o && o->cv().is_const;
        }
    } else {
        on_const = m_in_const_method;
    }

    if (on_const) SemanticError::emit(m_reporter, file_of(c), c->line, "Const", "cannot call non-const member function '" + std::string(chosen->name) + "' on a const object");
}

Type* TypeWalker::arrow_target(Type* obj) {
    if (obj && obj->is_pointer()) return static_cast<PointerType*>(obj)->pointee();

    for (int guard = 0; obj && guard < 16; ++guard) {
        Symbol* op = nullptr;
        Type* ret = resolve_member_operator(obj, nodes::OverloadableOperator::Arrow, {}, &op);
        if (!ret) break;
        if (ret->is_pointer()) return static_cast<PointerType*>(ret)->pointee();
        obj = ret;
    }

    return obj;
}

Type* TypeWalker::deduce_auto_declared(const parser_types::TypeInfo& ti, Type* init) {
    Type* base = deduce_auto(init);
    if (!base || ti.indirection.empty()) {
        if (base && ti.modifiers.has(modifiers::RawModifiers::Const)) { CV c = base->cv(); c.is_const = true; base = m_types.with_cv(base, c); }
        return base;
    }

    const parser_types::IndirectionQualifier& last = ti.indirection[ti.indirection.size() - 1];

    if (!last.is_pointer()) {
        Type* referent = init->is_reference() ? static_cast<ReferenceType*>(init)->referent() : init;
        CV c = referent->cv();
        if (ti.modifiers.has(modifiers::RawModifiers::Const)) c.is_const = true;
        referent = m_types.with_cv(m_types.strip_cv(referent), c);
        return m_types.reference(referent, last.is_rvalue_ref() ? RefQual::RValue : RefQual::LValue);
    }

    return base;
}

Type* TypeWalker::deduce_auto(Type* init) {
    if (!init) return nullptr;
    Type* t = init;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    return m_types.strip_cv(t);
}

Type* TypeWalker::decay_ref(Type* t, VC& vc) {
    if (t && t->is_reference()) { vc = VC::LValue; return static_cast<ReferenceType*>(t)->referent(); }
    return t;
}

std::vector<ParamShape> TypeWalker::shapes_of(Symbol* fn) {
    std::vector<ParamShape> out;
    auto* decl = fn->decl;
    const nodes::FunctionParameters* ps = nullptr;

    if (decl && decl->kind == K::FunctionDeclaration) {
        ps = static_cast<nodes::FunctionDeclaration*>(decl)->get_parameters();
    } else if (decl && decl->kind == K::OperatorFunctionDeclaration) {
        ps = static_cast<nodes::OperatorFunctionDeclaration*>(decl)->get_parameters();
    } else if (decl && decl->kind == K::ConstructorDeclaration) {
        ps = static_cast<nodes::ConstructorDeclaration*>(decl)->get_parameters();
    }

    if (!ps) return out;
    out.reserve(ps->size());
    for (const nodes::FunctionParameter* p : ps->m_params) out.push_back(shape_of(p, m_types));
    return out;
}

Symbol* TypeWalker::callee_symbol(nodes::ASTNode* n) {
    if (!n) return nullptr;

    switch (n->kind) {
        case K::Identifier:             return static_cast<nodes::Identifier*>(n)->resolved;
        case K::QualifiedIdentifier:    return static_cast<nodes::QualifiedIdentifier*>(n)->resolved;
        case K::MemberAccessExpression: return static_cast<nodes::MemberAccessExpression*>(n)->resolved;
        default: return nullptr;
    }
}

void TypeWalker::check_convertible(Type* from, Type* to, nodes::ASTNode* site) {
    if (!from || !to) return;
    if (from->is_error() || to->is_error()) return;
    if (is_dynamic_t(from) || is_dynamic_t(to)) return;   
    if (rank_conversion(from, to, m_types) == ConversionRank::None) SemanticError::not_convertible(m_reporter, file_of(site), site->line, type_str(from), type_str(to));
}

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

void TypeWalker::check_binding(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site, bool check_rvalue_refs) {
    if (!from || !to) return;
    if (constant_fits(expr, from, to)) return;

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
    if (args.size() != ps.size()) return false;
    SubstEnv env;

    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (!ps[i]->symbol) return false;
        const TemplateArg& a = args[i];
        if      (a.is_type)  env.types  [ps[i]->symbol] = a.type;
        else if (a.value)    env.values [ps[i]->symbol] = a.value;
        else if (a.fvalue)   env.fvalues[ps[i]->symbol] = a.fvalue;
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

    for (auto& req : r->m_requirements) {
        if (!ok) break;

        switch (req.form) {
            case Form::Simple:
                walk_expr(req.expr);
                break;

            case Form::Compound: {
                walk_expr(req.expr);
                Type* t = type_of(req.expr);
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
                ok = t && !t->is_error() && !contains_error(t);
                break;
            }

            case Form::Nested: {
                std::string ignored;
                ok = constraint_holds(req.expr, ignored);
                break;
            }
        }
    }

    m_current = saved;
    const bool had_errors = trap.discard();
    return ok && !had_errors;
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
    return false;
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

bool TypeWalker::printable(Type* t) {
    if (!t || t->is_error()) return true;
    t = m_types.strip_cv(t);
    if (t->is_pointer() || t->is_null() || t->is_enum()) return true;
    if (!t->is_builtin()) return false;
    return !is_void_t(t);
}

Type* TypeWalker::type_intrinsic(nodes::CallExpression* c, Symbol* sym, const std::vector<Type*>& args) {
    c->resolved = sym;
    using semantics::Intrinsic;

    switch (static_cast<Intrinsic>(sym->intrinsic)) {
        case Intrinsic::Print:
        case Intrinsic::Println:
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (!printable(args[i])) SemanticError::bad_operand(m_reporter, file_of(c), c->line, sym->name, type_str(args[i]));
            }
            return m_types.void_();

        case Intrinsic::Input:
            if (!args.empty()) SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, sym->name);
            return m_types.builtin(parser_types::PrimitiveType::BaseKind::String);

        case Intrinsic::ToString:
            if (args.size() != 1) SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, sym->name);
            else if (!printable(args[0])) SemanticError::bad_operand(m_reporter, file_of(c), c->line, sym->name, type_str(args[0]));
            return m_types.builtin(parser_types::PrimitiveType::BaseKind::String);

        case Intrinsic::ParseInt:
        case Intrinsic::ParseFloat: {
            using BK = parser_types::PrimitiveType::BaseKind;
            Type* a = args.size() == 1 ? m_types.strip_cv(args[0] && args[0]->is_reference() ? static_cast<ReferenceType*>(args[0])->referent() : args[0]) : nullptr;
            const bool ok = a && (is_dynamic_t(a) || (a->is_builtin() && (static_cast<BuiltinType*>(a)->base() == BK::String || static_cast<BuiltinType*>(a)->base() == BK::Text)));
            if (!ok) SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, sym->name);
            return static_cast<Intrinsic>(sym->intrinsic) == Intrinsic::ParseInt ? m_types.builtin(BK::Int, parser_types::LengthModifier::Long) : m_types.builtin(BK::Float);
        }

        default:
            return nullptr;
    }
}

CandidateMatch TypeWalker::match_with_constants(nodes::CallExpression* c, const std::vector<ParamShape>& shapes, const std::vector<Type*>& args) {
    CandidateMatch m = match_arguments(shapes, args, m_types);
    if (m.viable) return m;
    std::vector<Type*> adjusted = args;
    std::vector<bool>  changed(args.size(), false);
    bool any = false;

    for (std::size_t i = 0; i < shapes.size() && i < args.size(); ++i) {
        if (shapes[i].is_pack) break;
        if (!args[i] || !shapes[i].element) continue;
        Type* want = shapes[i].element;
        if (want->is_reference()) {
            auto* rt = static_cast<ReferenceType*>(want);
            if (rt->ref_qual() == RefQual::LValue && !rt->referent()->cv().is_const) continue;
            want = rt->referent();
        }
        if (constant_fits(c->m_arguments[i], args[i], want)) { adjusted[i] = m_types.strip_cv(want); changed[i] = true; any = true; }
    }

    if (!any) return m;
    CandidateMatch a = match_arguments(shapes, adjusted, m_types);
    if (!a.viable) return m;
    for (std::size_t i = 0; i < a.ranks.size() && i < changed.size(); ++i) {
        if (changed[i] && a.ranks[i] < ConversionRank::Conversion) a.ranks[i] = ConversionRank::Conversion;
    }
    return a;
}

void TypeWalker::check_arg_bindings(nodes::CallExpression* c, const std::vector<ParamShape>& shapes) {
    std::size_t i = 0;

    for (const ParamShape& p : shapes) {
        if (p.is_pack || i >= c->m_arguments.size()) break;
        nodes::ASTNode* a = c->m_arguments[i++];
        if (binds_nonconst_lvalue_ref_to_rvalue(a, type_of(a), p.element))
            SemanticError::bad_reference_binding(m_reporter, file_of(a), a->line, type_str(p.element), true);
    }
}

void TypeWalker::check_constant_init(nodes::VariableDeclaration* v, Type* target) {
    using RM = modifiers::RawModifiers;
    const auto& mods = v->get_type_info().modifiers;
    const bool required = mods.has(RM::Constexpr) || mods.has(RM::Constinit);

    if (!v->has_initializer()) {
        if (required) SemanticError::constant_needs_initializer(m_reporter, file_of(v), v->line, mods.has(RM::Constexpr) ? "constexpr" : "constinit", v->get_name());
        return;
    }

    Type* t = target ? m_types.strip_cv(target) : nullptr;
    if (!t || t->is_dependent() || !ConstEvaluator::is_constant_type(t)) return;

    ConstValue raw = eval_checked(v->m_initializer, true);
    if (raw.is_error()) return;

    if (!raw.ok()) {
        if (required) SemanticError::not_a_constant(m_reporter, file_of(v), v->line, fail_message(ConstValue::Fail::NotConstant));
        return;
    }

    ConstValue fitted = m_eval.coerce(raw, t);
    if (fitted.is_error()) SemanticError::not_a_constant(m_reporter, file_of(v), v->line, fail_message(fitted.fail));
}

auto TypeWalker::operand_class(Type* t) -> OperandClass {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t || t->is_error() || t->is_dependent() || is_dynamic_t(t)) return OperandClass::Unknown;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    t = m_types.strip_cv(t);
    if (t->is_pointer() || t->is_array()) return OperandClass::Pointer;
    if (t->is_null()) return OperandClass::Null;
    if (t->is_enum()) return OperandClass::Enum;
    if (!t->is_builtin()) return OperandClass::Unknown;

    switch (static_cast<BuiltinType*>(t)->base()) {
        case BK::Int:    return OperandClass::Int;
        case BK::Char:   return OperandClass::Char;
        case BK::Float:  return OperandClass::Float;
        case BK::Bool:   return OperandClass::Bool;
        case BK::String:
        case BK::Text:   return OperandClass::String;
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

    auto numeric  = [](OC c) { return c == OC::Int || c == OC::Char || c == OC::Float; };
    auto integral = [](OC c) { return c == OC::Int || c == OC::Char; };
    auto same_family = [](OC x, OC y) { return (x == OC::Char) == (y == OC::Char); };
    auto arith    = [&](OC x, OC y) { return numeric(x) && numeric(y) && same_family(x, y); };
    auto bitwise  = [&](OC x, OC y) {
        if (x == OC::Bool || y == OC::Bool) return x == OC::Bool && y == OC::Bool;
        if (x == OC::Enum || y == OC::Enum) return (x == OC::Enum || x == OC::Int) && (y == OC::Enum || y == OC::Int);
        return integral(x) && integral(y) && same_family(x, y);
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
                || (a == OC::Pointer && b == OC::Int) || (a == OC::Int && b == OC::Pointer)
                || (a == OC::String && (b == OC::String || b == OC::Char))
                || (a == OC::Char && b == OC::String);

        case TK::Minus:
            return arith(a, b) || (a == OC::Pointer && (b == OC::Int || b == OC::Pointer));

        case TK::Asterisk:
        case TK::Slash:
        case TK::DoubleAsterisk:
            return arith(a, b);

        case TK::Percent:
            return integral(a) && integral(b) && same_family(a, b);

        case TK::Ampersand:
        case TK::Pipe:
        case TK::Caret:
            return bitwise(a, b);

        case TK::DoubleLessThan:
        case TK::DoubleGreaterThan:
            return (integral(a) || a == OC::Enum) && (integral(b) || b == OC::Enum);

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
    if (builtin_operands_ok(compound_base(b->op), operand_class(l), operand_class(r))) return true;
    SemanticError::bad_binary_operands(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(nodes::classify_single_operator(b->op)), type_str(l), type_str(r));
    return false;
}

void TypeWalker::ensure_conversions(Type* t) {
    if (!t || !t->is_record()) return;
    Type* key = m_types.strip_cv(t);
    if (!m_types.user_conversions().mark_if_new(key)) return;     
    Symbol* decl = static_cast<RecordType*>(key)->decl();
    nodes::ASTNode*     node = decl ? decl->decl : nullptr;       
    if (!node || node->kind != K::RecordDeclaration) return;
    auto* rd = static_cast<nodes::RecordDeclaration*>(node);

    for (const nodes::RecordDeclaration::Member& m : rd->get_members()) {
        if (!m.node || m.node->kind != K::OperatorFunctionDeclaration) continue;
        auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(m.node);
        if (!op->is_conversion()) continue;                       
        UserConversion uc;
        uc.dst       = m_types.strip_cv(m_types.canonicalize(op->get_conversion_type()));
        uc.op        = op->symbol;
        uc.explicit_ = op->qualifiers().has(modifiers::FunctionQualifiers::Explicit);
        m_types.user_conversions().add(key, uc);
    }
}

void TypeWalker::check_operator_signature(nodes::OperatorFunctionDeclaration* od, Symbol* sym) {
    bool is_member = m_current && m_current->node && m_current->node->kind == K::RecordDeclaration;
    nodes::OverloadableOperator op = od->get_overload();
    std::size_t n = od->get_parameters() ? od->get_parameters()->size() : 0;
    const char* why = nullptr;
    if (!operator_arity_ok(op, is_member, n, why)) SemanticError::bad_operator_arity(m_reporter, file_of(od), od->line, nodes::overloadable_operator_name(op), why);
    check_signature_defaults(sym, od->get_parameters(), od);   
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
        EnvGuard g(m_types, env_of_symbol(o));
        matches.push_back(match_arguments(shapes_of(o), extra, m_types));
        cands.push_back(o);
    }

    if (cands.empty()) return nullptr;
    Selection sel = select_overload(matches);
    if (sel.status != SelectStatus::Ok) return nullptr;
    Symbol* pick = cands[sel.index];
    if (chosen) *chosen = pick;
    EnvGuard g(m_types, env_of_symbol(pick));
    return pick->type ? m_types.canonicalize(*pick->type) : nullptr;
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
    Selection sel = select_overload(matches);
    res.status = sel.status;
    if (sel.status != SelectStatus::Ok) return res;
    Symbol* pick   = cands[sel.index];
    res.chosen     = pick;
    res.via_member = member[sel.index];
    EnvGuard g(m_types, env_of_symbol(pick));
    res.type       = pick->type ? m_types.canonicalize(*pick->type) : nullptr;
    return res;
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

    if (Scope* ms = member_scope_of(recv)) for (Symbol* o = ms->find_member("operator"); o; o = o->next_overload) consider(o, true);

    for (Scope* s = m_current; s; s = s->parent) {
        if (s->node && s->node->kind == K::RecordDeclaration) continue;
        for (Symbol* o = s->find_local("operator"); o; o = o->next_overload) consider(o, false);
    }

    OpResult res;
    if (cands.empty()) return res;
    Selection sel = select_overload(matches);
    res.status = sel.status;
    if (sel.status != SelectStatus::Ok) return res;
    Symbol* pick = cands[sel.index];
    res.chosen = pick; res.via_member = member[sel.index];
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

    if (callee_sym && callee_sym->kind == SymbolKind::Type && callee_sym->decl && callee_sym->decl->kind == K::RecordDeclaration && !callee_sym->template_decl) {
        return record_type_of(callee_sym);
    }

    if (callee_sym && callee_sym->kind == SymbolKind::Type && callee_sym->template_decl && call && !call->m_template_args.empty() && m_inst) {
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

    if (!bucket) {
        if (args.size() == 1 && is_copy_source(rec, args[0])) return nullptr;
        if (!args.empty()) SemanticError::no_matching_overload(m_reporter, file_of(site), site->line, "constructor");
        return nullptr;
    }

    std::vector<CandidateMatch> matches;
    std::vector<Symbol*>        cands;

    for (Symbol* o = bucket; o; o = o->next_overload) {
        if (!o->decl || o->decl->kind != K::ConstructorDeclaration) continue;
        matches.push_back(match_arguments(shapes_of(o), args, m_types));
        cands.push_back(o);
    }

    if (cands.empty()) return nullptr;
    Selection sel = select_overload(matches);
    if (sel.status == SelectStatus::NoMatch && args.size() == 1 && is_copy_source(rec, args[0])) return nullptr;
    if (sel.status == SelectStatus::NoMatch)   { SemanticError::no_matching_overload(m_reporter, file_of(site), site->line, "constructor"); return nullptr; }
    if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(site), site->line, "constructor"); return nullptr; }
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

    if (t->is_record()) { return rank_user_defined(t, m_types.bool_(), m_types, true, nullptr) != ConversionRank::None; }
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
            return v == V::Break || v == V::Continue || v == V::Repeat;
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
            for (const nodes::TryCatchStatement* h = t; h; h = h->next_handler) if (breaks_out(h->catch_body)) return true;
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
            if (!always_exits(t->try_body)) return false;
            for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) if (!always_exits(h->catch_body)) return false;
            return true;
        }

        default: return false;
    }
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

ConstValue TypeWalker::eval_checked(nodes::ASTNode* e, bool required) {
    ConstValue v = m_eval.eval(e);
    if (!e) return v;

    if (v.is_error()) {
        if (required) SemanticError::not_a_constant(m_reporter, file_of(e), e->line, fail_message(v.fail));
        return v;
    }

    if (!v.ok()) return v; 

    switch (v.fail) {
        case ConstValue::Fail::Precision:
            SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "constant expression lost precision: " + std::string(fail_message(v.fail)));
            break;

        case ConstValue::Fail::Wrapped:
            SemanticWarning::emit(m_warnings, file_of(e), e->line, "Constant", "constant expression wrapped around its type: " + std::string(fail_message(v.fail)));
            break;

        default:
            break;
    }

    return v;
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
