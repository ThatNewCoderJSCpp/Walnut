#ifndef WALNUT_SEMA_TYPE_WALKER_HPP
#define WALNUT_SEMA_TYPE_WALKER_HPP

#include <algorithm>
#include <sstream>

#include "type_impl.hpp"
#include "conversion.hpp"
#include "overload.hpp"

#include "scope.hpp"
#include "symbol.hpp"
#include "semantic_error.hpp"
#include "semantic_warning.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/error_reporter.hpp"

#include "instantiator.hpp"

#include "throw_spec.hpp"

namespace walnut {
namespace semantics {

class TypeWalker {
public:
    TypeWalker(
        TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings, Scope* root,
        Instantiator* inst = nullptr, ThrowContext* throws = nullptr
    ) noexcept
        : m_types(types), m_reporter(reporter), m_warnings(warnings), m_root(root), m_current(root)
        , m_inst(inst), m_eval(types), m_throws(throws) {}
    void run(nodes::BlockStatement* program) {
        if (!program) return;
        m_current = m_root;
        for (nodes::ASTNode* s : program->get_statements()) build(s);
    }

private:
    using K = nodes::ASTNode::Kind;
    using VC = nodes::ValueCategory;

    struct OpResult {
        Type*        type       = nullptr;
        SelectStatus status     = SelectStatus::NoMatch;
        Symbol*      chosen     = nullptr;
        bool         via_member = false;
    };

    struct ControlContextGuard {
        TypeWalker& w; unsigned long long int loops, switches;

        explicit ControlContextGuard(TypeWalker& tw) : w(tw), loops(tw.m_loop_depth), switches(tw.m_switch_depth) {
            w.m_loop_depth = w.m_switch_depth = 0;
        }

        ~ControlContextGuard() { w.m_loop_depth = loops; w.m_switch_depth = switches; }
    };

    struct ReturnFrame {
        Type* declared = nullptr;   
        bool  deducing = false;    
        bool  seen     = false;     
        Type* deduced  = nullptr;   

        bool  saw_co           = false; 
        bool  saw_value_return = false;
        nodes::ASTNode* site   = nullptr;
    };

    struct EnvGuard {
        TypeContext& t; bool on;
        EnvGuard(TypeContext& tc, const SubstEnv* e) : t(tc), on(e != nullptr) { if (on) t.push_subst(e); }
        ~EnvGuard() { if (on) t.pop_subst(); }
    };

private:
    TypeContext&                 m_types;
    ErrorReporter&               m_reporter;
    WarningReporter&             m_warnings;
    Scope*                       m_root    = nullptr;
    Scope*                       m_current = nullptr;
    Instantiator*                m_inst    = nullptr;
    ThrowContext*                m_throws  = nullptr;
    ConstEvaluator               m_eval;
    std::vector<ReturnFrame>     m_returns;
    std::vector<nodes::ASTNode*> m_fn_bodies;
    std::size_t                  m_catch_depth  = 0; 
    unsigned long long int       m_loop_depth   = 0;   
    unsigned long long int       m_switch_depth = 0;

private:
    static nodes::ASTNode* mn(const nodes::ASTNode* n) { return const_cast<nodes::ASTNode*>(n); }
    void assign_type(nodes::ASTNode* n, Type* t) { n->expr_type.type = t; ensure_conversions(t); }
    static Type* type_of(const nodes::ASTNode* n) { return n ? n->expr_type.type : nullptr; }
    Scope* inner_of(Symbol* s) const { return s ? s->inner_scope : nullptr; }
    static FileId file_of(const nodes::ASTNode* n) { return n ? n->file_id : FileId{}; }

    Symbol* record_symbol_of(Type* t) {
        if (!t) return nullptr;
        t = m_types.strip_cv(t);
        if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();  
        if (t) t = m_types.strip_cv(t);
        return (t && t->is_record()) ? static_cast<RecordType*>(t)->decl() : nullptr;
    }

    void collect_conversions(nodes::ASTNode* n) {
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

    void register_conversion(nodes::OperatorFunctionDeclaration* op) {
        if (!op->is_conversion()) return;  
        Symbol* owner = enclosing_record_symbol(op);
        if (!owner) return;                
        Type* src = m_types.record(owner, {}, CV{});           
        Type* dst = m_types.canonicalize(op->get_conversion_type());
        if (!dst) return;
        UserConversion uc;
        uc.dst       = m_types.strip_cv(dst);
        uc.op        = op->symbol;
        uc.explicit_ = op->qualifiers().has(modifiers::FunctionQualifiers::Explicit);
        m_types.user_conversions().add(m_types.strip_cv(src), uc);
    }

    void ensure_typed(Instantiator::Instantiation* I) {
        if (!I || I->typed || !I->decl) return;
        I->typed = true;                       
        m_types.push_subst(&I->env);
        Scope* saved = m_current;
        m_current = I->scope;
        build(I->decl);                        
        m_current = saved;
        m_types.pop_subst();
        if (m_inst) m_inst->refresh_function_type(I);   
    }

    Symbol* enclosing_record_symbol(nodes::ASTNode* /*op*/) const {
        for (Scope* s = m_current; s; s = s->parent) {
            if (s->node && s->node->kind == K::RecordDeclaration && s->owner_symbol) return s->owner_symbol;
        }
        return nullptr;
    }

    void assign_typed(nodes::ASTNode* n, Type* t, VC vc) {
        n->expr_type.type = t;
        n->expr_type.vc   = vc;
        ensure_conversions(t);
    }

    void check_condition(nodes::ASTNode* expr) {
        if (!expr) return;
        Type* t = type_of(expr);
        if (t && !is_bool_testable(t)) SemanticError::condition_not_bool(m_reporter, file_of(expr), expr->line, type_str(t));
    }

    void check_signature_defaults(Symbol* fn, const nodes::FunctionParameters* ps, nodes::ASTNode* site) {
        if (!ps) return;
        std::vector<ParamShape> shapes;
        shapes.reserve(ps->size());
        for (const nodes::FunctionParameter* p : ps->m_params) shapes.push_back(shape_of(p, m_types));
        std::size_t bi = 0, bj = 0;
        if (!signature_defaults_ok(shapes, bi, bj)) SemanticError::ambiguous_defaults(m_reporter, file_of(site), site->line, fn ? fn->name : "<function>");
    }

    void build(nodes::ASTNode* node) {
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

                if (prim_is(ti, parser_types::PrimitiveType::BaseKind::Auto)) {
                    if (!init) { SemanticError::auto_needs_initializer(m_reporter, file_of(v), v->line, v->get_name()); break; }
                    ti.canonical = deduce_auto(init);           
                    break;
                }

                Type* declared = m_types.canonicalize(ti);
                if (declared && init && !is_dynamic_t(declared))  check_convertible(init, declared, v);
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(node);
                walk_expr(a->m_dimension_expr);
                walk_expr(a->m_initializer);
                break;
            }

            case K::FunctionDeclaration: {
                auto* fn = static_cast<nodes::FunctionDeclaration*>(node);
                check_signature_defaults(fn->symbol, fn->get_parameters(), fn);
                Scope* saved = m_current;
                ControlContextGuard ctx(*this);
                if (Scope* fs = inner_of(fn->symbol)) m_current = fs;
                push_return(fn->get_return_type(), fn);
                if (fn->has_body()) build(mn(fn->get_body()));
                pop_return(fn->get_return_type());
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
                push_return(op->get_return_type(), op);
                if (op->has_body()) build(mn(op->get_body()));
                pop_return(op->get_return_type());
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
                for (nodes::ASTNode* e : ct->get_init_list()) walk_expr(e);
                push_return(m_types.void_(), ct);
                if (ct->has_body()) build(mn(ct->get_body()));
                pop_return_fixed();
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
                    if (v->initializer && is_int_type(base)) { if (Type* it = type_of(v->initializer)) check_convertible(it, base, v); }
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

                if (want) {
                    Type* val = type_of(rs->get_value());

                    if (!rs->has_value()) {
                        if (!(is_void_t(want) || is_dynamic_t(want))) SemanticError::missing_return_value(m_reporter, file_of(rs), rs->line, type_str(want));
                    } else if (is_void_t(want)) {
                        if (val && !(is_void_t(val) || is_dynamic_t(val))) SemanticError::return_value_in_void(m_reporter, file_of(rs), rs->line);
                    } else if (val) {
                        check_convertible(val, want, rs);
                    }
                }

                break;
            }

            case K::CoReturnStatement: {
                auto* cr = static_cast<nodes::CoReturnStatement*>(node);
                walk_expr(cr->m_value);
                note_coroutine(cr, "co_return");
                // Operand is validated against the promise's return_value/return_void,
                // NOT the function's declared return type
                // deferred to lowering
                break;
            }

            case K::ExpressionStatement:
                walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
                break;

            case K::IfStatement: {
                auto* s = static_cast<nodes::IfStatement*>(node);

                for (nodes::IfBranch* br : s->get_branches()) {
                    nodes::ASTNode* cond = br->condition;

                    if (cond && cond->kind == K::VariableDeclaration) {
                        build(cond);                             
                        auto* v = static_cast<nodes::VariableDeclaration*>(cond);
                        Type* tested = m_types.canonicalize(v->get_type_info());   
                        if (tested && !is_bool_testable(tested)) SemanticError::condition_not_bool(m_reporter, file_of(v), v->line, type_str(tested));
                    } else {
                        walk_expr(cond);
                        check_condition(cond);
                    }

                    build(br->body);
                }

                build(mn(s->get_else()));
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
                build(s->get_catch_body());
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

    void walk_children(nodes::ASTNode* node) {
        if (node->kind == K::ExpressionStatement) walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
    }

    void walk_expr(nodes::ASTNode* e) {
        if (!e) return;
        switch (e->kind) {
            case K::Literal: assign_type(e, type_of_literal(static_cast<nodes::Literal*>(e))); break;

            case K::Identifier: {
                auto* id = static_cast<nodes::Identifier*>(e);
                assign_typed(e, symbol_type(id->resolved), symbol_is_object(id->resolved) ? VC::LValue : VC::RValue);
                break;
            }

            case K::QualifiedIdentifier: {
                auto* q = static_cast<nodes::QualifiedIdentifier*>(e);
                assign_typed(e, symbol_type(q->resolved), symbol_is_object(q->resolved) ? VC::LValue : VC::RValue);
                break;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<nodes::BinaryExpression*>(e);
                walk_expr(b->left); walk_expr(b->right);
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
                walk_expr(r->operand);
                if (!is_addressable(r->operand)) SemanticError::not_addressable(m_reporter, file_of(r), r->line);
                Type* t = type_of(r->operand);
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
                std::vector<Type*> args;
                args.reserve(c->m_arguments.size());
                for (nodes::ASTNode* a : c->m_arguments) { walk_expr(a); args.push_back(type_of(a)); }
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
                assign_typed(c, dst, VC::RValue);
                break;
            }

            case K::NewExpression: {
                auto* n = static_cast<nodes::NewExpression*>(e);
                if (n->array_size) walk_expr(n->array_size);
                for (nodes::ASTNode* a : n->args) walk_expr(a);
                Type* allocated = m_types.canonicalize(n->type);

                if (n->is_array) {                                  
                    Type* sz = type_of(n->array_size);
                    if (sz && !is_integral_t(sz) && !is_dynamic_t(sz)) SemanticError::bad_operand(m_reporter, file_of(n), n->line, "new[] size", type_str(sz));
                } else if (Symbol* rec = record_symbol_of(allocated)) {   
                    std::vector<Type*> args;
                    args.reserve(n->args.size());
                    for (nodes::ASTNode* a : n->args) args.push_back(type_of(a));
                    n->ctor = resolve_constructor(rec, args, n);     
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
                if (t && !is_integral_t(t) && !is_dynamic_t(t) && !record_symbol_of(t)) SemanticError::bad_operand(m_reporter, file_of(x), x->line, "~", type_str(t));
                assign_typed(e, t, VC::RValue);
                break;
            }

            case K::LambdaExpression: {
                auto* l = static_cast<nodes::LambdaExpression*>(e);
                if (l->m_captures) for (auto& c : l->m_captures->m_captures) if (c.init) walk_expr(c.init);
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
                m_current = saved;
                using FQ = modifiers::FunctionQualifiers;
                const auto& q = l->get_qualifiers();
                RefQual rq = q.has(FQ::RValueRef) ? RefQual::RValue : q.has(FQ::LValueRef) ? RefQual::LValue : RefQual::None;
                assign_typed(e, m_types.function(ret, std::move(param_types), q.has(FQ::Const), rq, q.has(FQ::Noexcept)), VC::RValue);
                break;
            }

            case K::AwaitExpression: {
                auto* a = static_cast<nodes::AwaitExpression*>(e);
                walk_expr(a->operand);
                note_coroutine(a, "await");
                // Type as dynamic to avoid errors later
                assign_typed(e, m_types.dynamic_(), VC::RValue);
                break;
            }

            case K::CoYieldExpression: {
                auto* y = static_cast<nodes::CoYieldExpression*>(e);
                walk_expr(y->operand);
                note_coroutine(y, "co_yield");
                // result type resolved at lowering
                assign_typed(e, m_types.dynamic_(), VC::RValue);
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

                if (q->operand) {
                    if (q->operand->is_value()) {
                        walk_expr(q->operand->value);
                        queried = type_of(q->operand->value);
                    } else if (q->operand->is_type()) {          
                        queried = m_types.canonicalize(q->operand->type);  
                    }                                           
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
                            constructed = m_types.record(s, {}, CV{});
                        } else {
                            constructed = symbol_type(s);
                        }
                    }
                }

                if (Symbol* rec = record_symbol_of(constructed)) {
                    if (rec->inner_scope && rec->inner_scope->find_member("constructor")) {
                        std::vector<Type*> args;

                        if (b->m_init) {
                            args.reserve(b->m_init->m_elements.size());
                            for (nodes::ASTNode* el : b->m_init->m_elements) args.push_back(type_of(el));
                        }

                        resolve_constructor(rec, args, b);  
                    }
                }

                assign_typed(e, constructed, VC::RValue);
                break;
            }

            case K::TemplateInstantiation: {
                auto* t = static_cast<nodes::TemplateInstantiation*>(e);
                walk_expr(t->m_template);
                Type* result = nullptr;

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

            case K::FoldExpression: {
                auto* f = static_cast<nodes::FoldExpression*>(e);
                walk_expr(f->lhs); walk_expr(f->rhs);
                // result type determined at instantiation
                assign_typed(e, nullptr, VC::RValue);
                break;
            }

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

    void check_cast(nodes::CastExpression* c, Type* src, Type* dst) {
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
                if (!record_symbol_of(src) || !record_symbol_of(dst)) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
                break;
            }
            case CK::Bit: {                                    
                if (src->is_reference() || dst->is_reference()) SemanticError::invalid_cast(m_reporter, file_of(c), c->line, c->cast_name(), type_str(src), type_str(dst));
                break;
            }
        }
    }

    void collect_member_ops(
        Type* recv, nodes::OverloadableOperator want,
        const std::vector<Type*>& explicit_args,
        std::vector<CandidateMatch>& matches,
        std::vector<Symbol*>& cands, std::vector<bool>& member
    ) {
        Symbol* rec = record_symbol_of(recv);
        if (!rec || !rec->inner_scope) return;

        for (Symbol* o = rec->inner_scope->find_member("operator"); o; o = o->next_overload) {
            if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
            if (static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() != want) continue;
            CandidateMatch m = match_arguments(shapes_of(o), explicit_args, m_types);
            if (!m.viable) continue;
            m.ranks.insert(m.ranks.begin(), ConversionRank::Exact);   
            matches.push_back(std::move(m)); cands.push_back(o); member.push_back(true);
        }
    }

    void collect_free_ops(
        nodes::OverloadableOperator want, const std::vector<Type*>& operands,
        std::vector<CandidateMatch>& matches,
        std::vector<Symbol*>& cands, std::vector<bool>& member
    ) {
        for (Scope* s = m_current; s; s = s->parent) {
            if (s->node && s->node->kind == K::RecordDeclaration) continue;   
            
            for (Symbol* o = s->find_local("operator"); o; o = o->next_overload) {   
                if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
                if (static_cast<nodes::OperatorFunctionDeclaration*>(o->decl)->get_overload() != want) continue;
                CandidateMatch m = match_arguments(shapes_of(o), operands, m_types);
                if (!m.viable) continue;
                matches.push_back(std::move(m)); cands.push_back(o); member.push_back(false);
            }
        }
    }

    void push_return(Type* declared, nodes::ASTNode* site) {
        ReturnFrame f;
        f.declared = declared;
        f.site     = site;
        m_returns.push_back(f);
    }

    void push_return(const parser_types::TypeInfo& rt, nodes::ASTNode* site) {
        ReturnFrame f;

        if (prim_is(rt, parser_types::PrimitiveType::BaseKind::Auto)) {
            f.deducing = true;
        } else {
            f.declared = m_types.canonicalize(rt);
        }
        
        f.site = site;
        m_returns.push_back(f);
    }

    void contribute_return(Type* contrib, nodes::ASTNode* site) {
        if (m_returns.empty()) return;
        ReturnFrame& f = m_returns.back();
        if (!f.deducing || !contrib) return;
        if (!f.seen) { f.deduced = contrib; f.seen = true; }
        else         { f.deduced = common_type(f.deduced, contrib, site); }
    }

    void pop_return_fixed() { m_returns.pop_back(); }

    Type* pop_return(const parser_types::TypeInfo& rt) {
        ReturnFrame f = m_returns.back();
        m_returns.pop_back();

        if (f.saw_co) {
            if (f.saw_value_return && f.site) SemanticError::coroutine_mixed_return(m_reporter, file_of(f.site), f.site->line);
            if (f.deducing && f.site) SemanticError::coroutine_deduced_return(m_reporter, file_of(f.site), f.site->line);
            return f.declared;
        }

        if (!f.deducing) return f.declared;
        Type* result = f.seen ? f.deduced : m_types.void_();
        const_cast<parser_types::TypeInfo&>(rt).canonical = result;
        return result;
    }

    void note_coroutine(nodes::ASTNode* site, std::string_view kw) {
        if (m_returns.empty()) {
            SemanticError::coroutine_outside_function(m_reporter, file_of(site), site->line, kw);
            return;
        }
        m_returns.back().saw_co = true;
    }

    Type* symbol_type(Symbol* s) {
        if (!s) return nullptr;

        if (s->kind == SymbolKind::EnumConstant) {                
            Symbol* en = s->owner ? s->owner->owner_symbol : nullptr;
            return en ? m_types.enum_(en, CV{}) : nullptr;
        }

        if (!s->type) return nullptr;
        return m_types.canonicalize(*s->type);
    }

    Type* type_of_literal(nodes::Literal* lit) {
        using BK = parser_types::PrimitiveType::BaseKind;
        using TK = tokenizing::Token::Kind;
        switch (lit->get_kind()) {
            case TK::Integer:         return m_types.builtin(BK::Int);
            case TK::Float:           return m_types.builtin(BK::Float);
            case TK::InfinityKeyword: return m_types.builtin(BK::Float);   
            case TK::String:          return m_types.builtin(BK::String);
            case TK::TextLiteral:     return m_types.builtin(BK::Text);
            case TK::Character:       return m_types.builtin(BK::Char);
            case TK::True:
            case TK::False:           return m_types.bool_();
            case TK::NullptrKeyword:  return m_types.null_();
            case TK::ThisKeyword:     return enclosing_record_type();
            default:                  return m_types.dynamic_();
        }
    }

    Type* enclosing_record_type() {
        for (Scope* s = m_current; s; s = s->parent) {       
            if (s->node && s->node->kind == K::RecordDeclaration)
                if (Symbol* sym = static_cast<nodes::RecordDeclaration*>(s->node)->symbol) return m_types.record(sym, {}, CV{});
        }

        return m_types.dynamic_();
    }

    Type* type_binary(nodes::BinaryExpression* b, Symbol*& resolved_op) {
        resolved_op = nullptr;
        Type* l = type_of(b->left); 
        Type* r = type_of(b->right);
        const tokenizing::Token::Kind op = b->op;

        if (record_symbol_of(l) || record_symbol_of(r)) {
            const nodes::OverloadableOperator want = nodes::classify_single_operator(op);
            OpResult res = resolve_operator(want, { l, r });
            if (res.status == SelectStatus::Ok) { resolved_op = res.chosen; return res.type; }
            if (res.status == SelectStatus::Ambiguous) SemanticError::ambiguous_call(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
            else                                       SemanticError::no_matching_overload(m_reporter, file_of(b), b->line, nodes::overloadable_operator_name(want));
            return nullptr;
        }

        if (is_assignment_op(op)) {
            if (!is_lvalue(b->left)) {
                SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", false);
            } else if (type_is_const(l)) {
                SemanticError::not_assignable(m_reporter, file_of(b), b->line, "assign to", true);
            }

            if (l && r) check_convertible(r, l, b);
            return l;                                        
        }

        if (is_logical_op(op)) {                             
            if (l && !is_bool_testable(l)) SemanticError::condition_not_bool(m_reporter, file_of(b->left),  b->left->line,  type_str(l));
            if (r && !is_bool_testable(r)) SemanticError::condition_not_bool(m_reporter, file_of(b->right), b->right->line, type_str(r));
            return m_types.bool_();
        }

        if (is_comparison_op(op) || is_equality_op(op)) return m_types.bool_();
        if (is_dynamic_t(l) || is_dynamic_t(r)) return m_types.dynamic_();   

        if (l && r) {
            if (rank_conversion(r, l, m_types) != ConversionRank::None) return l;
            if (rank_conversion(l, r, m_types) != ConversionRank::None) return r;
        }

        return l ? l : r;
    }

    Type* type_unary(nodes::UnaryExpression* u, Symbol*& resolved_op) {
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

            if (!is_lvalue(u->operand))  SemanticError::not_assignable(m_reporter, file_of(u), u->line, what, false);
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

    Type* common_type(Type* a, Type* b, nodes::ASTNode* site) {
        if (!a) return b;
        if (!b) return a;
        if (a == b) return a;
        if (rank_conversion(b, a, m_types) != ConversionRank::None) return a;
        if (rank_conversion(a, b, m_types) != ConversionRank::None) return b;
        SemanticError::no_common_type(m_reporter, file_of(site), site->line, type_str(a), type_str(b));
        return a;
    }

    Type* type_call(nodes::CallExpression* c, const std::vector<Type*>& args, VC& vc) {
        vc = VC::RValue;
        Symbol* callee_sym = callee_symbol(c->m_callee);

        if (callee_sym && callee_sym->kind == SymbolKind::Function) {
            std::vector<CandidateMatch>                 matches;
            std::vector<Symbol*>                        cand_syms;
            std::vector<Instantiator::Instantiation*>   cand_inst;

            for (Symbol* o = callee_sym; o; o = o->next_overload) {
                if (o->template_decl) {                         
                    if (!m_inst) continue;
                    auto* I = m_inst->instantiate_for_call(o, c->get_template_args(), args, c);
                    if (!I) continue;                           
                    matches.push_back(match_arguments(I->fn_shapes, args, m_types));
                    cand_syms.push_back(I->sym);
                    cand_inst.push_back(I);
                    continue;
                }

                Instantiator::Instantiation* owner = m_inst ? m_inst->owning(o) : nullptr;

                {
                    EnvGuard g(m_types, owner ? &owner->env : nullptr);
                    matches.push_back(match_arguments(shapes_of(o), args, m_types));
                }

                cand_syms.push_back(o);
                cand_inst.push_back(owner);
            }

            Selection sel = select_overload(matches);
            if (sel.status == SelectStatus::NoMatch)   { SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee)); return nullptr; }
            if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(c), c->line, callee_name(c->m_callee)); return nullptr; }
            Symbol* chosen = cand_syms[sel.index];
            c->resolved = chosen;

            if (Instantiator::Instantiation* I = cand_inst[sel.index]) {
                ensure_typed(I);                                 
                if (I->type && I->type->is_function()) return decay_ref(static_cast<FunctionType*>(I->type)->ret(), vc);
                EnvGuard g(m_types, &I->env);
                return decay_ref(chosen->type ? m_types.canonicalize(*chosen->type) : nullptr, vc);
            }

            return decay_ref(chosen->type ? m_types.canonicalize(*chosen->type) : nullptr, vc);
        }

        Type* callee_type = type_of(c->m_callee);

        if (FunctionType* ft = as_function_type(callee_type)) {
            const std::vector<Type*>& params = ft->params();

            if (args.size() != params.size()) {
                SemanticError::no_matching_overload(m_reporter, file_of(c), c->line, callee_name(c->m_callee));
            } else {
                for (std::size_t i = 0; i < args.size(); ++i) if (args[i] && params[i]) check_convertible(args[i], params[i], c);
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

    Type* type_member_access(nodes::MemberAccessExpression* m) {
        if (m->is_scope()) return symbol_type(m->resolved);
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
        
        if (!found) {
            if (obj && !is_dynamic_t(obj) && !obj->is_error()) SemanticError::unresolved_name(m_reporter, file_of(m), m->line, m->get_member());
            return nullptr;
        }

        EnvGuard g(m_types, I ? &I->env : nullptr);          
        return symbol_type(found);
    }

    Type* arrow_target(Type* obj) {
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

    Type* deduce_auto(Type* init) {
        if (!init) return nullptr;
        Type* t = init;
        if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        return m_types.strip_cv(t);
    }

    Type* decay_ref(Type* t, VC& vc) {
        if (t && t->is_reference()) { vc = VC::LValue; return static_cast<ReferenceType*>(t)->referent(); }
        return t;
    }

    std::vector<ParamShape> shapes_of(Symbol* fn) {
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

    Symbol* callee_symbol(nodes::ASTNode* n) {
        if (!n) return nullptr;

        switch (n->kind) {
            case K::Identifier:             return static_cast<nodes::Identifier*>(n)->resolved;
            case K::QualifiedIdentifier:    return static_cast<nodes::QualifiedIdentifier*>(n)->resolved;
            case K::MemberAccessExpression: return static_cast<nodes::MemberAccessExpression*>(n)->resolved;
            default: return nullptr;
        }
    }

    void check_convertible(Type* from, Type* to, nodes::ASTNode* site) {
        if (!from || !to) return;
        if (from->is_error() || to->is_error()) return;
        if (is_dynamic_t(from) || is_dynamic_t(to)) return;   
        if (rank_conversion(from, to, m_types) == ConversionRank::None) SemanticError::not_convertible(m_reporter, file_of(site), site->line, type_str(from), type_str(to));
    }

    void ensure_conversions(Type* t) {
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

    void check_operator_signature(nodes::OperatorFunctionDeclaration* od, Symbol* sym) {
        bool is_member = m_current && m_current->node && m_current->node->kind == K::RecordDeclaration;
        nodes::OverloadableOperator op = od->get_overload();
        std::size_t n = od->get_parameters() ? od->get_parameters()->size() : 0;
        const char* why = nullptr;
        if (!operator_arity_ok(op, is_member, n, why)) SemanticError::bad_operator_arity(m_reporter, file_of(od), od->line, nodes::overloadable_operator_name(op), why);
        check_signature_defaults(sym, od->get_parameters(), od);   
    }

    Type* resolve_member_operator(Type* recv, nodes::OverloadableOperator wanted, const std::vector<Type*>& extra, Symbol** chosen = nullptr) {
        if (chosen) *chosen = nullptr;
        Symbol* rec = record_symbol_of(recv);
        if (!rec || !rec->inner_scope) return nullptr;
        Symbol* bucket = rec->inner_scope->find_member("operator");
        std::vector<CandidateMatch> matches;
        std::vector<Symbol*>        cands;

        for (Symbol* o = bucket; o; o = o->next_overload) {
            if (!o->decl || o->decl->kind != K::OperatorFunctionDeclaration) continue;
            auto* od = static_cast<nodes::OperatorFunctionDeclaration*>(o->decl);
            if (od->get_overload() != wanted) continue;
            matches.push_back(match_arguments(shapes_of(o), extra, m_types));
            cands.push_back(o);
        }

        if (cands.empty()) return nullptr;
        Selection sel = select_overload(matches);
        if (sel.status != SelectStatus::Ok) return nullptr;
        Symbol* pick = cands[sel.index];
        if (chosen) *chosen = pick;
        return pick->type ? m_types.canonicalize(*pick->type) : nullptr;
    }

    OpResult resolve_operator(nodes::OverloadableOperator want, const std::vector<Type*>& operands) {
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
        res.type       = pick->type ? m_types.canonicalize(*pick->type) : nullptr;
        return res;
    }

    OpResult resolve_incdec(Type* recv, nodes::OverloadableOperator want, bool postfix) {
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

        if (Symbol* rec = record_symbol_of(recv))
            if (rec->inner_scope) for (Symbol* o = rec->inner_scope->find_member("operator"); o; o = o->next_overload) consider(o, true);

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

    Symbol* resolve_constructor(Symbol* rec, const std::vector<Type*>& args, nodes::ASTNode* site) {
        if (!rec || !rec->inner_scope) return nullptr;
        Symbol* bucket = rec->inner_scope->find_member("constructor");   

        if (!bucket) {
            // no user-declared constructors: only implicit default is valid
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
        if (sel.status == SelectStatus::NoMatch)   { SemanticError::no_matching_overload(m_reporter, file_of(site), site->line, "constructor"); return nullptr; }
        if (sel.status == SelectStatus::Ambiguous) { SemanticError::ambiguous_call(m_reporter, file_of(site), site->line, "constructor"); return nullptr; }
        return cands[sel.index];
    }

    Symbol* find_alloc_operator(Type* in_record, nodes::OverloadableOperator want) {
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

    FunctionType* as_function_type(Type* t) {
        if (!t) return nullptr;
        t = m_types.strip_cv(t);
        if (t->is_pointer()) t = m_types.strip_cv(static_cast<PointerType*>(t)->pointee());
        return (t && t->is_function()) ? static_cast<FunctionType*>(t) : nullptr;
    }

    static bool is_lvalue(const nodes::ASTNode* n) {
        return n && n->expr_type.vc == VC::LValue;
    }

    static bool symbol_is_object(Symbol* s) {
        return s && (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter);
    }

    bool is_addressable(nodes::ASTNode* n) {
        if (is_lvalue(n)) return true;
        Symbol* s = callee_symbol(n);                 
        return s && s->kind == SymbolKind::Function;
    }

    bool type_is_const(Type* t) const { return t && t->cv().is_const; }   

    static bool is_dynamic_t(Type* t) {
        return t && t->is_builtin() && static_cast<BuiltinType*>(t)->is_dynamic();
    }

    static bool is_void_t(Type* t) {
        return t && t->is_builtin() && static_cast<BuiltinType*>(t)->is_void();
    }

    static bool is_int_type(Type* t) {
        return t && t->is_builtin() && static_cast<BuiltinType*>(t)->base() == parser_types::PrimitiveType::BaseKind::Int;
    }

    bool is_switchable(Type* t) {
        return t && (is_integral_t(t) || t->is_enum());
    }

    static bool is_numeric_t(Type* t) {
        if (!t || !t->is_builtin()) return false;
        using BK = parser_types::PrimitiveType::BaseKind;

        switch (static_cast<BuiltinType*>(t)->base()) {
            case BK::Int: case BK::Float: case BK::Char: return true;
            default: return false;
        }
    }

    static bool is_integral_t(Type* t) {
        if (!t || !t->is_builtin()) return false;
        using BK = parser_types::PrimitiveType::BaseKind;

        switch (static_cast<BuiltinType*>(t)->base()) {
            case BK::Int: case BK::Char: case BK::Bool: return true;
            default: return false;
        }
    }

    bool is_arithmetic_or_pointer(Type* t) { return is_numeric_t(t) || (t && t->is_pointer()); }

    bool is_bool_testable(Type* t) {
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

    static bool prim_is(const parser_types::TypeInfo& ti, parser_types::PrimitiveType::BaseKind bk) {
        if (!ti.type || !ti.type->is_primitive()) return false;
        return static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == bk;
    }

    static bool is_single(const nodes::ASTNode* n, nodes::SingleStatement::Variant v) {
        return n && n->kind == K::SingleStatement && static_cast<const nodes::SingleStatement*>(n)->variant == v;
    }

    static bool terminates(const nodes::ASTNode* s) {
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

    static bool case_terminates(const nodes::SwitchCase* c) {
        const auto& body = c->body;
        return !body.empty() && terminates(body.back());
    }

    static bool marked_fallthrough(const nodes::SwitchCase* c) {
        using V = nodes::SingleStatement::Variant;
        if (c->has_fallthrough) return true;                      
        return !c->body.empty() && is_single(c->body.back(), V::Fallthrough);
    }

    bool operator_arity_ok(nodes::OverloadableOperator op, bool is_member, std::size_t n, const char*& why) {
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

    ConstValue eval_checked(nodes::ASTNode* e, bool required = false) {
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

    static std::string type_str(Type* t) {
        return type_str_of(t);
    }

    static std::string_view callee_name(nodes::ASTNode* n) {
        if (!n) return "<callee>";
        switch (n->kind) {
            case K::Identifier:             return static_cast<nodes::Identifier*>(n)->get_name();
            case K::QualifiedIdentifier:    return static_cast<nodes::QualifiedIdentifier*>(n)->simple_name();
            case K::MemberAccessExpression: return static_cast<nodes::MemberAccessExpression*>(n)->get_member();
            default: return "<callee>";
        }
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_WALKER_HPP