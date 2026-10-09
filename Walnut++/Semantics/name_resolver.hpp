#ifndef WALNUT_SEMANTICS_NAME_RESOLVER_HPP
#define WALNUT_SEMANTICS_NAME_RESOLVER_HPP

#include <string>
#include <vector>
#include <string_view>
#include <unordered_set>

#include "scope.hpp"
#include "symbol.hpp"
#include "resolve.hpp"          
#include "scope_builder.hpp"    
#include "semantic_error.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Common/error_reporter.hpp"

namespace walnut {
namespace semantics {

class NameResolver {
public:
    explicit NameResolver(AnalysisContext& ctx) noexcept : m_ctx(ctx), m_arena(ctx.arena), m_reporter(ctx.reporter) {}

    void run(nodes::BlockStatement* program, Scope* root = nullptr) {
        m_root    = root ? root : m_ctx.root;
        m_current = m_root;
        if (!program || !m_root) { return; }
        for (nodes::ASTNode* s : program->get_statements()) { build(s); }
    }

    void resolve_into(nodes::ASTNode* node, Scope* scope) {
        if (!node || !scope) { return; }
        Scope* saved_root    = m_root;
        Scope* saved_current = m_current;
        Scope* top = scope;
        while (top->parent) { top = top->parent; }
        m_root    = top;
        m_current = scope;
        const bool saved_quiet = m_quiet;
        m_quiet = true;
        build(node);
        m_quiet   = saved_quiet;
        m_root    = saved_root;
        m_current = saved_current;
    }

private:
    AnalysisContext& m_ctx;
    Arena&           m_arena;
    ErrorReporter&   m_reporter;
    Scope*           m_root    = nullptr;
    Scope*           m_current = nullptr;
    bool             m_quiet   = false;

private:
    static parser_types::TypeInfo& mut(const parser_types::TypeInfo& t) { return const_cast<parser_types::TypeInfo&>(t); }
    static nodes::ASTNode*         mn(const nodes::ASTNode* n)          { return const_cast<nodes::ASTNode*>(n); }

    struct Declaring {
        nodes::VariableDeclaration* var;
        std::size_t                 lambda_depth;
    };

    std::vector<Declaring>                 m_declaring;
    std::vector<nodes::LambdaExpression*>  m_lambdas;
    std::unordered_set<const Symbol*>      m_self_reported;

    Symbol* declaring_symbol(std::string_view name) const {
        if (m_declaring.empty()) return nullptr;
        const Declaring& d = m_declaring.back();
        if (d.lambda_depth != m_lambdas.size() || !d.var->symbol || d.var->get_name() != name) return nullptr;
        return d.var->symbol;
    }

    static bool is_auto_declared(const nodes::VariableDeclaration* v) {
        const auto& ti = v->get_type_info();
        return ti.type && ti.type->is_primitive() && static_cast<parser_types::PrimitiveType*>(ti.type)->base_kind() == parser_types::PrimitiveType::BaseKind::Auto;
    }

    Symbol* self_capture(std::string_view name) const {
        using M = nodes::LambdaCaptureItem::Mode;

        for (auto it = m_declaring.rbegin(); it != m_declaring.rend(); ++it) {
            if (!it->var->symbol || it->var->get_name() != name) continue;
            if (it->lambda_depth >= m_lambdas.size()) return nullptr;
            nodes::LambdaExpression* l = m_lambdas[it->lambda_depth];
            if (!l->m_captures) return nullptr;

            for (const auto& c : l->m_captures->m_captures) {
                if (c.mode == M::ByReference && c.name == name) return it->var->symbol;
                if (c.mode == M::AllByReference) return it->var->symbol;
            }

            return nullptr;
        }

        return nullptr;
    }

    static Symbol* chase(Symbol* s) {
        while (s && s->is_imported && s->import_target) { s = s->import_target; }
        return s;
    }

    static bool is_type_symbol(SymbolKind k) {
        return k == SymbolKind::Type      || k == SymbolKind::Enum ||
               k == SymbolKind::TypeAlias || k == SymbolKind::Concept ||
               k == SymbolKind::TemplateParam;
    }

    static std::string join(const std::vector<std::string_view>& parts, bool is_global) {
        std::string out;
        if (is_global) { out += "::"; }

        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) { out += "::"; }
            out += std::string(parts[i]);
        }

        return out;
    }

    static Symbol* node_symbol(nodes::ASTNode* n) {
        if (!n) { return nullptr; }

        switch (n->kind) {
            case nodes::ASTNode::Kind::Identifier:
                return static_cast<nodes::Identifier*>(n)->resolved;
            case nodes::ASTNode::Kind::QualifiedIdentifier:
                return static_cast<nodes::QualifiedIdentifier*>(n)->resolved;
            case nodes::ASTNode::Kind::MemberAccessExpression:
                return static_cast<nodes::MemberAccessExpression*>(n)->resolved;
            default:
                return nullptr;
        }
    }

    Scope* inner_of(Symbol* s) const { return s ? s->inner_scope : nullptr; }

    void error_unresolved(const nodes::ASTNode* use, std::string_view name) {
        if (use && !m_quiet) { SemanticError::unresolved_name(m_reporter, use->file_id, use->line, name); }
    }

    void resolve_type(parser_types::TypeInfo& info, const nodes::ASTNode* use, bool allow_value = false) {
        if (info.type && info.type->is_user_defined()) {
            auto* ud = static_cast<parser_types::UserDefinedType*>(info.type);
            Symbol* s = resolve_name(m_current, m_root, ud->parts(), ud->is_global(), use);

            if (s) {
                Symbol* c = chase(s);
                info.resolved = c;
                const bool value_ok = allow_value && c && (c->kind == SymbolKind::Variable || c->kind == SymbolKind::Parameter || c->kind == SymbolKind::EnumConstant);
                if (c && !is_type_symbol(c->kind) && !value_ok) {
                    error_unresolved(use, join(ud->parts(), ud->is_global()));
                }
            } else if (!(ud->parts().size() == 1 && !ud->is_global() && info.template_args.size() == 1 && (ud->name() == "generator" || ud->name() == "task"))) {
                error_unresolved(use, join(ud->parts(), ud->is_global()));
            }
        }
        
        if (info.type && info.type->is_qualified()) resolve_qualified(info, use);

        if (info.type && info.type->is_array()) {
            auto* at = static_cast<parser_types::ArrayType*>(info.type);
            resolve_type(at->element_type(), use);
            if (at->dimension_expr()) walk_expr(const_cast<nodes::ASTNode*>(at->dimension_expr()));
        }

        if (info.type && info.type->is_function_pointer()) {
            auto* fp = static_cast<parser_types::FunctionPointerType*>(info.type);
            for (const parser_types::TypeInfo& pt : fp->param_types()) { resolve_type(const_cast<parser_types::TypeInfo&>(pt), use); }
            resolve_type(const_cast<parser_types::TypeInfo&>(fp->return_type()), use);
        }

        for (parser_types::TemplateArgument* a : info.template_args) { resolve_targ(a, use); }
        if (info.alignment) { resolve_targ(info.alignment, use); }
    }

    void resolve_qualified(parser_types::TypeInfo& info, const nodes::ASTNode* use) {
        auto* qt = static_cast<parser_types::QualifiedType*>(info.type);
        const auto& comps = qt->components();
        std::vector<std::string_view> names;
        std::size_t last_args = comps.size();

        for (std::size_t i = 0; i < comps.size(); ++i) {
            names.push_back(comps[i].name);
            if (comps[i].has_args()) last_args = i;
            for (parser_types::TemplateArgument* a : comps[i].args) resolve_targ(a, use);
        }

        if (last_args == comps.size()) {
            Symbol* s = resolve_name(m_current, m_root, names, qt->is_global(), use);
            if (s && is_type_symbol(chase(s)->kind)) info.resolved = chase(s);
            else error_unresolved(use, join(names, qt->is_global()));
            return;
        }

        std::vector<std::string_view> head(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(last_args + 1));
        Symbol* s = resolve_name(m_current, m_root, head, qt->is_global(), use);
        Symbol* c = s ? chase(s) : nullptr;

        if (!c || c->kind != SymbolKind::Type || last_args + 2 != comps.size()) {
            error_unresolved(use, join(names, qt->is_global()));
            return;
        }

        parser_types::TypeInfo prefix;
        prefix.template_args = comps[last_args].args;
        prefix.has_angle_args = true;
        prefix.resolved = c;
        qt->set_prefix(prefix);
    }

    void resolve_targ(parser_types::TemplateArgument* a, const nodes::ASTNode* use) {
        if (!a) { return; }
        if (a->is_type()) { resolve_type(a->type, use, !a->is_pack && a->type.indirection.empty() && a->type.template_args.empty()); }
        else              { walk_expr(a->value); }
    }

    void resolve_spec_args(nodes::ASTNode* decl) {
        if (!decl) { return; }

        if (decl->kind == nodes::ASTNode::Kind::RecordDeclaration) {
            auto* r = static_cast<nodes::RecordDeclaration*>(decl);
            if (r->is_specialization()) { resolve_targs(const_cast<std::vector<parser_types::TemplateArgument*>&>(r->get_spec_args()), r); }
        } else if (decl->kind == nodes::ASTNode::Kind::FunctionDeclaration) {
            auto* f = static_cast<nodes::FunctionDeclaration*>(decl);
            if (f->is_specialization()) { resolve_targs(const_cast<std::vector<parser_types::TemplateArgument*>&>(f->get_spec_args()), f); }
        }
    }

    void resolve_targs(std::vector<parser_types::TemplateArgument*>& args, const nodes::ASTNode* use) {
        for (parser_types::TemplateArgument* a : args) { resolve_targ(a, use); }
    }

    void walk_expr(nodes::ASTNode* e) {
        if (!e) { return; }
        using K = nodes::ASTNode::Kind;

        switch (e->kind) {
            case K::Literal:
                break;

            case K::Identifier: {
                auto* id = static_cast<nodes::Identifier*>(e);
                if (Symbol* self = self_capture(id->name)) {
                    id->resolved = self;
                    if (!m_quiet && self->decl && self->decl->kind == K::VariableDeclaration && is_auto_declared(static_cast<nodes::VariableDeclaration*>(self->decl)) && m_self_reported.insert(self).second) {
                        SemanticError::emit(m_reporter, id->file_id, id->line, "Capture", "'" + std::string(id->name) + "' is used inside its own initializer, so it needs an explicit function type instead of 'auto'");
                    }
                    break;
                }
                Symbol* s = resolve_lexical_visible(m_current, id->name, id);
                if (s) { id->resolved = chase(s); }
                else   { error_unresolved(id, id->name); }
                break;
            }

            case K::QualifiedIdentifier: {
                auto* q = static_cast<nodes::QualifiedIdentifier*>(e);
                Symbol* s = resolve_name(m_current, m_root, q->parts(), q->is_global(), q);
                if (s) { q->resolved = chase(s); }
                else   { error_unresolved(q, join(q->parts(), q->is_global())); }
                break;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<nodes::BinaryExpression*>(e);
                walk_expr(b->left); walk_expr(b->right);
                break;
            }

            case K::UnaryExpression:
                walk_expr(static_cast<nodes::UnaryExpression*>(e)->operand);
                break;
            case K::DereferenceExpression:
                walk_expr(static_cast<nodes::DereferenceExpression*>(e)->operand);
                break;
            case K::ReferenceExpression:
                walk_expr(static_cast<nodes::ReferenceExpression*>(e)->operand);
                break;
            case K::BitwiseNotExpression:
                walk_expr(static_cast<nodes::BitwiseNotExpression*>(e)->operand);
                break;
            case K::NoexceptExpression:
                walk_expr(static_cast<nodes::NoexceptExpression*>(e)->operand);
                break;
            case K::AwaitExpression:
                walk_expr(static_cast<nodes::AwaitExpression*>(e)->operand);
                break;
            case K::CoYieldExpression:
                walk_expr(static_cast<nodes::CoYieldExpression*>(e)->operand);
                break;
            case K::DeleteExpression:
                walk_expr(static_cast<nodes::DeleteExpression*>(e)->operand);
                break;
            case K::DiscardExpression:
                walk_expr(static_cast<nodes::DiscardExpression*>(e)->operand);
                break;

            case K::TernaryExpression: {
                auto* t = static_cast<nodes::TernaryExpression*>(e);
                walk_expr(t->condition); walk_expr(t->true_branch); walk_expr(t->false_branch);
                break;
            }

            case K::CallExpression: {
                auto* c = static_cast<nodes::CallExpression*>(e);
                walk_expr(c->m_callee);                   
                for (nodes::ASTNode* a : c->m_arguments) { walk_expr(a); }
                resolve_targs(c->m_template_args, c);     
                break;
            }

            case K::SubscriptExpression: {
                auto* s = static_cast<nodes::SubscriptExpression*>(e);
                walk_expr(s->m_array); walk_expr(s->m_index);
                break;
            }

            case K::BraceInitializerList: {
                auto* b = static_cast<nodes::BraceInitializerList*>(e);
                for (nodes::ASTNode* el : b->m_elements) { walk_expr(el); }
                break;
            }

            case K::BraceConstructExpression: {
                auto* b = static_cast<nodes::BraceConstructExpression*>(e);
                walk_expr(b->m_callee);
                walk_expr(b->m_init);
                break;
            }

            case K::MemberAccessExpression: {
                auto* m = static_cast<nodes::MemberAccessExpression*>(e);
                walk_expr(m->m_object);

                if (m->m_op == nodes::MemberAccessExpression::Op::Scope) {
                    Symbol* base = chase(node_symbol(m->m_object));

                    if (base && base->inner_scope && scope_carrier(base->kind)) {
                        if (Symbol* found = base->inner_scope->find_member(m->m_member)) {
                            m->resolved = chase(found);
                        } else {
                            error_unresolved(m, m->m_member);
                        }
                    }
                }
                
                break;
            }

            case K::CastExpression: {
                auto* c = static_cast<nodes::CastExpression*>(e);
                resolve_type(mut(c->get_target()), c);
                walk_expr(c->operand);
                break;
            }

            case K::TemplateInstantiation: {
                auto* t = static_cast<nodes::TemplateInstantiation*>(e);
                walk_expr(t->m_template);
                resolve_targs(t->m_args, t);
                break;
            }

            case K::TypeQuery: {
                auto* q = static_cast<nodes::TypeQueryExpression*>(e);
                resolve_targ(q->operand, q);
                break;
            }

            case K::NewExpression: {
                auto* n = static_cast<nodes::NewExpression*>(e);
                resolve_type(n->type, n);
                walk_expr(n->array_size);
                for (nodes::ASTNode* a : n->args) { walk_expr(a); }
                break;
            }

            case K::FoldExpression: {
                auto* f = static_cast<nodes::FoldExpression*>(e);
                walk_expr(f->lhs); walk_expr(f->rhs);
                break;
            }

            case K::RequiresExpression: {
                auto* r = static_cast<nodes::RequiresExpression*>(e);
                Scope* saved = m_current;
                if (r->scope) { m_current = r->scope; }   
                
                if (auto* params = r->get_parameters()) {
                    for (nodes::FunctionParameter* p : *params) {
                        resolve_type(mut(p->get_type()), p);
                        walk_expr(p->m_initializer);
                    }
                }

                using ReqForm = nodes::RequiresExpression::Requirement::Form;

                for (auto& req : r->get_requirements()) {
                    walk_expr(req.expr);

                    if (req.form == ReqForm::Type || req.has_type_constraint) {
                        resolve_type(req.type, r);
                    }
                }

                m_current = saved;
                break;
            }

            case K::LambdaExpression: {
                auto* l = static_cast<nodes::LambdaExpression*>(e);
                
                if (l->m_captures) {
                    using M = nodes::LambdaCaptureItem::Mode;

                    for (auto& c : l->m_captures->m_captures) {
                        if (c.init) { walk_expr(c.init); }

                        if ((c.mode == M::ByValue || c.mode == M::ByReference) && !c.name.empty()) {
                            if (Symbol* self = declaring_symbol(c.name)) {
                                c.resolved = self;
                                if (c.mode == M::ByValue && !m_quiet) SemanticError::emit(m_reporter, l->file_id, l->line, "Capture", "a lambda can only capture the variable it initializes by reference ('&" + std::string(c.name) + "')");
                                else if (is_auto_declared(m_declaring.back().var) && !m_quiet && m_self_reported.insert(self).second) SemanticError::emit(m_reporter, l->file_id, l->line, "Capture", "'" + std::string(c.name) + "' captures itself, so it needs an explicit function type instead of 'auto'");
                            } else if (Symbol* s = resolve_lexical_visible(m_current, c.name, l)) {
                                c.resolved = chase(s);
                            } else {
                                error_unresolved(l, c.name);
                            }
                        }
                    }
                }

                Scope* saved = m_current;
                if (l->scope) { m_current = l->scope; }   
                m_lambdas.push_back(l);
                struct PopLambda { std::vector<nodes::LambdaExpression*>& v; ~PopLambda() { v.pop_back(); } } pop_lambda{ m_lambdas };
                resolve_type(l->get_return_type(), l);

                if (auto* params = l->get_parameters()) {
                    for (nodes::FunctionParameter* p : *params) {
                        resolve_type(mut(p->get_type()), p);
                        walk_expr(p->m_initializer);
                    }
                }

                if (l->m_body) { build(l->m_body); }
                m_current = saved;
                break;
            }

            case K::ThrowExpression:
                walk_expr(static_cast<nodes::ThrowExpression*>(e)->operand);
                break;

            default:
                break;
        }
    }

    void build(nodes::ASTNode* node) {
        if (!node) { return; }
        using K = nodes::ASTNode::Kind;
        
        switch (node->kind) {
            case K::BlockStatement: {
                auto* b = static_cast<nodes::BlockStatement*>(node);
                Scope* saved = m_current;
                if (b->scope) { m_current = b->scope; }
                for (nodes::ASTNode* s : b->get_statements()) { build(s); }
                m_current = saved;
                break;
            }

            case K::ExpressionStatement:
                walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
                break;

            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(node);
                resolve_type(mut(v->get_type_info()), v);
                m_declaring.push_back(Declaring{ v, m_lambdas.size() });
                walk_expr(v->m_initializer);
                m_declaring.pop_back();
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(node);
                resolve_type(a->m_element_type, a);
                walk_expr(a->m_dimension_expr);
                walk_expr(a->m_initializer);
                break;
            }

            case K::ReturnStatement:
                walk_expr(static_cast<nodes::ReturnStatement*>(node)->m_value);
                break;
            case K::CoReturnStatement:
                walk_expr(static_cast<nodes::CoReturnStatement*>(node)->m_value);
                break;

            case K::IfStatement: {
                auto* s = static_cast<nodes::IfStatement*>(node);

                for (nodes::IfBranch* br : s->get_branches()) {
                    walk_expr(br->condition);
                    build(br->body);
                }

                build(mn(s->get_else()));
                break;
            }

            case K::ForStatement: {
                auto* s = static_cast<nodes::ForStatement*>(node);
                Scope* saved = m_current;
                if (s->scope) { m_current = s->scope; }  
                build(s->var_init);
                build(s->initializer);
                walk_expr(s->condition);
                walk_expr(s->increment);
                build(s->body);
                m_current = saved;
                break;
            }

            case K::ForEachStatement: {
                auto* s = static_cast<nodes::ForEachStatement*>(node);
                resolve_type(s->m_element_type, s);
                walk_expr(s->m_container);
                build(s->m_body);  
                break;
            }

            case K::WhileStatement: {
                auto* s = static_cast<nodes::WhileStatement*>(node);
                walk_expr(mn(s->get_condition()));
                build(mn(s->get_body()));
                break;
            }

            case K::DoWhileStatement: {
                auto* s = static_cast<nodes::DoWhileStatement*>(node);
                build(mn(s->get_body()));
                walk_expr(mn(s->get_condition()));
                break;
            }

            case K::SwitchStatement: {
                auto* s = static_cast<nodes::SwitchStatement*>(node);
                Scope* saved = m_current;
                if (s->scope) { m_current = s->scope; }   
                walk_expr(s->get_condition());

                for (nodes::SwitchCase* c : s->get_cases()) {
                    walk_expr(c->value);
                    for (nodes::ASTNode* stmt : c->body) { build(stmt); }
                }

                m_current = saved;
                break;
            }

            case K::TryCatchStatement: {
                auto* s = static_cast<nodes::TryCatchStatement*>(node);
                build(s->get_try_body());

                for (nodes::TryCatchStatement* h = s; h; h = h->next_handler) {
                    if (h->is_typed_catch()) { resolve_type(h->catch_type, h); }
                    build(h->get_catch_body());
                }
                break;
            }

            case K::FunctionDeclaration: {
                auto* fn = static_cast<nodes::FunctionDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* fs = inner_of(fn->symbol)) { m_current = fs; }
                resolve_type(fn->get_return_type(), fn);
                resolve_params(fn->get_parameters());
                if (fn->has_body()) { build(fn->get_body()); }
                m_current = saved;
                break;
            }

            case K::OperatorFunctionDeclaration: {
                auto* op = static_cast<nodes::OperatorFunctionDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* fs = inner_of(op->symbol)) { m_current = fs; }
                resolve_type(mut(op->get_return_type()), op);
                if (op->is_conversion()) { resolve_type(mut(op->get_conversion_type()), op); }
                resolve_params(op->get_parameters());
                if (op->has_body()) { build(mn(op->get_body())); }
                m_current = saved;
                break;
            }

            case K::ConstructorDeclaration: {
                auto* ct = static_cast<nodes::ConstructorDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* fs = inner_of(ct->symbol)) { m_current = fs; }
                resolve_params(ct->get_parameters());
                for (nodes::ASTNode* e : ct->get_init_list()) { walk_expr(e); }
                if (ct->has_body()) { build(mn(ct->get_body())); }
                m_current = saved;
                break;
            }

            case K::DestructorDeclaration: {
                auto* dt = static_cast<nodes::DestructorDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* fs = inner_of(dt->symbol)) { m_current = fs; }
                if (dt->has_body()) { build(mn(dt->get_body())); }
                m_current = saved;
                break;
            }

            case K::RecordDeclaration: {
                auto* rec = static_cast<nodes::RecordDeclaration*>(node);
                
                if (rec->has_inherits()) {
                    Scope* outer = m_current;
                    if (Scope* rs = inner_of(rec->symbol); rs && rs->parent) { m_current = rs->parent; }
                    resolve_type(const_cast<parser_types::TypeInfo&>(rec->get_inherits()), rec);
                    m_current = outer;
                    link_base(rec);
                }

                Scope* saved = m_current;
                if (Scope* rs = inner_of(rec->symbol)) { m_current = rs; }
                for (const auto& member : rec->get_members()) { build(member.node); }
                m_current = saved;
                break;
            }

            case K::EnumDeclaration: {
                auto* en = static_cast<nodes::EnumDeclaration*>(node);

                if (en->has_type()) {
                    resolve_type(const_cast<parser_types::TypeInfo&>(en->get_underlying_type()), en);
                }

                Scope* saved = m_current;
                if (Scope* es = inner_of(en->symbol)) { m_current = es; }
                for (nodes::EnumValue* v : en->get_values()) { walk_expr(v->initializer); }
                m_current = saved;
                break;
            }

            case K::NamespaceDeclaration: {
                auto* ns = static_cast<nodes::NamespaceDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* nsc = inner_of(ns->symbol)) { m_current = nsc; }

                if (auto* body = ns->get_body()) {
                    for (nodes::ASTNode* s : body->get_statements()) { build(s); }
                }

                m_current = saved;
                break;
            }

            case K::ModuleDeclaration: {
                auto* m = static_cast<nodes::ModuleDeclaration*>(node);
                Scope* saved = m_current;
                if (Scope* ms = inner_of(m->symbol)) { m_current = ms; }
                for (const auto& it : m->get_items()) { if (it.decl) { build(it.decl); } }
                m_current = saved;
                break;
            }

            case K::ImportExportDeclaration: {
                auto* ie = static_cast<nodes::ImportExportDeclaration*>(node);
                for (const auto& it : ie->get_items()) { if (it.decl) { build(it.decl); } }
                break;
            }

            case K::TemplateDeclaration: {
                auto* t = static_cast<nodes::TemplateDeclaration*>(node);
                Scope* saved = m_current;
                if (t->scope) { m_current = t->scope; }

                for (nodes::TemplateParameter* p : t->m_params) {
                    if (p->is_non_type_param())  resolve_type(mut(p->m_type), p);
                    if (p->m_has_default_type)   resolve_type(mut(p->m_default_type), p);
                    if (p->is_constrained())     resolve_type(mut(p->m_constraint), p);
                    walk_expr(p->m_default_value);
                }

                walk_expr(t->m_requires_clause);
                resolve_spec_args(t->m_declaration);

                if (t->m_declaration && t->m_declaration->kind == K::ConceptDeclaration) {
                    walk_expr(static_cast<nodes::ConceptDeclaration*>(t->m_declaration)->constraint);
                    m_current = saved;
                    break;
                }

                m_current = saved;
                build(t->m_declaration);
                break;
            }

            case K::UsingDeclaration: {
                auto* u = static_cast<nodes::UsingDeclaration*>(node);

                if (u->is_alias()) {
                    walk_expr(u->aliased_expr);                       // using X = <expr>
                } else if (u->is_typedef()) {
                    resolve_type(u->aliased_type, u);                 // typedef <type> X
                } else if (u->is_namespace_alias()) {                 // namespace X = a::b;
                    Symbol* target = resolve_name(m_current, m_root, u->target_parts, u->target_global, u);

                    if (!target) {
                        error_unresolved(u, u->target_name());
                    } else {
                        Symbol* c = chase(target);

                        if (!c || c->kind != SymbolKind::Namespace) {
                            if (!m_quiet) SemanticError::not_a_namespace(m_reporter, u->file_id, u->line, u->target_name());
                        } else if (u->symbol) {
                            u->symbol->inner_scope   = c->inner_scope;
                            u->symbol->import_target = c;
                        }
                    }
                } else if (u->is_directive()) {                       // using namespace a::b;
                    Symbol* target = resolve_name(m_current, m_root, u->target_parts, u->target_global, u);
                    if (!target) {
                        error_unresolved(u, u->target_name());
                    } else if (Symbol* c = chase(target); c && c->kind == SymbolKind::Namespace) {
                        u->symbol = c;
                    } else if (!m_quiet) {
                        SemanticError::not_a_namespace(m_reporter, u->file_id, u->line, u->target_name());
                    }
                }

                break;
            }

            case K::ConceptDeclaration:
                walk_expr(static_cast<nodes::ConceptDeclaration*>(node)->constraint);
                break;
            case K::StaticAssertDeclaration:
                walk_expr(static_cast<nodes::StaticAssertDeclaration*>(node)->constraint);
                break;

            default:
                if (nodes::is_expression(node)) { walk_expr(node); }
                break;
        }
    }

    void resolve_params(nodes::FunctionParameters* params) {
        if (!params) { return; }

        for (nodes::FunctionParameter* p : *params) {
            resolve_type(mut(p->get_type()), p);
            walk_expr(p->m_initializer);
        }
    }

    void link_base(nodes::RecordDeclaration* rec) {
        Symbol* base = rec->get_inherits().resolved;
        if (!base || !base->inner_scope) { return; }
        Scope* rs = inner_of(rec->symbol);
        if (!rs) { return; }
        for (Scope* b : rs->bases) { if (b == base->inner_scope) { return; } }
        rs->bases.push_back(base->inner_scope);
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_NAME_RESOLVER_HPP