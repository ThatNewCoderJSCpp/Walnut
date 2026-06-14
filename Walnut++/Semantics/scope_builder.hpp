#ifndef WALNUT_SEMANTICS_SCOPE_BUILDER_HPP
#define WALNUT_SEMANTICS_SCOPE_BUILDER_HPP

#include <string>
#include <cstdint>

#include "scope.hpp"
#include "symbol.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Common/error_reporter.hpp"
#include "semantic_error.hpp"

namespace walnut {
namespace semantics {

using nodes::ASTNode;

struct AnalysisContext {
    Arena&         arena;
    ErrorReporter& reporter;
    Scope*         root = nullptr;   

    AnalysisContext(Arena& a, ErrorReporter& r) noexcept : arena(a), reporter(r) {}
};

class ScopeBuilder {
public:
    explicit ScopeBuilder(AnalysisContext& ctx) noexcept : m_ctx(ctx), m_arena(ctx.arena), m_reporter(ctx.reporter) {}

    Scope* build(nodes::BlockStatement* program) {
        m_ctx.root = make_in<Scope>(m_arena, Scope::Kind::Module, nullptr);
        m_current  = m_ctx.root;
        for (ASTNode* stmt : program->get_statements()) { build(stmt); }
        return m_ctx.root;
    }

private:
    AnalysisContext& m_ctx;
    Arena&           m_arena;
    ErrorReporter&   m_reporter;
    Scope*           m_current = nullptr;
    std::uint64_t    m_order   = 0;  

private:
    static constexpr std::uint16_t kValidRecordMods =
        modifiers::RawModifiers::Constexpr | modifiers::RawModifiers::Hoisted |
        modifiers::RawModifiers::Local     | modifiers::RawModifiers::Global  |
        modifiers::RawModifiers::Hidden    | modifiers::RawModifiers::Friend;

    static constexpr std::uint16_t kValidFunctionMods =
        modifiers::RawModifiers::Constexpr | modifiers::RawModifiers::Hoisted |
        modifiers::RawModifiers::Inline    | modifiers::RawModifiers::Hidden  |
        modifiers::RawModifiers::Local     | modifiers::RawModifiers::Global  |
        modifiers::RawModifiers::Extern    | modifiers::RawModifiers::Friend  |
        modifiers::RawModifiers::Static;

    static constexpr std::uint16_t kValidVariableMods =
        modifiers::RawModifiers::Const       | modifiers::RawModifiers::Constexpr |
        modifiers::RawModifiers::Constinit   | modifiers::RawModifiers::Hoisted   |
        modifiers::RawModifiers::Inline      | modifiers::RawModifiers::Static    |
        modifiers::RawModifiers::Hidden      | modifiers::RawModifiers::Local     |
        modifiers::RawModifiers::Global      | modifiers::RawModifiers::Extern    |
        modifiers::RawModifiers::Immutable   | modifiers::RawModifiers::Volatile  |
        modifiers::RawModifiers::ThreadLocal | modifiers::RawModifiers::Mutable   |
        modifiers::RawModifiers::Friend;

    static constexpr std::uint16_t kValidEnumMods =
        modifiers::RawModifiers::Hoisted | modifiers::RawModifiers::Local |
        modifiers::RawModifiers::Global  | modifiers::RawModifiers::Hidden | modifiers::RawModifiers::Inline;

    static constexpr std::uint16_t kVisibilityGroup =
        modifiers::RawModifiers::Global | modifiers::RawModifiers::Local |
        modifiers::RawModifiers::Hidden;

    static constexpr std::uint16_t kMutabilityGroup =
        modifiers::RawModifiers::Const | modifiers::RawModifiers::Mutable |
        modifiers::RawModifiers::Immutable;

    static constexpr std::uint16_t kConstEvalGroup =
        modifiers::RawModifiers::Constexpr | modifiers::RawModifiers::Constinit;

    static constexpr std::uint16_t kLinkageGroup =
        modifiers::RawModifiers::Extern | modifiers::RawModifiers::Static;

    struct ConflictPair { std::uint16_t a, b; };

    static constexpr ConflictPair kConflictPairs[] = {
        { modifiers::RawModifiers::Constexpr, modifiers::RawModifiers::Mutable     },  
        { modifiers::RawModifiers::Constexpr, modifiers::RawModifiers::Volatile    },  
        { modifiers::RawModifiers::Constexpr, modifiers::RawModifiers::ThreadLocal },  
        { modifiers::RawModifiers::Constexpr, modifiers::RawModifiers::Extern      }, 
        { modifiers::RawModifiers::Constinit, modifiers::RawModifiers::Mutable     },
        { modifiers::RawModifiers::Mutable,   modifiers::RawModifiers::Static      },  
        { modifiers::RawModifiers::Mutable,   modifiers::RawModifiers::ThreadLocal },
        { modifiers::RawModifiers::Mutable,   modifiers::RawModifiers::Extern      },
        { modifiers::RawModifiers::Volatile,  modifiers::RawModifiers::Immutable   },  
    };

private:
    Scope* push_scope(Scope::Kind kind) {
        Scope* s  = make_in<Scope>(m_arena, kind, m_current);
        m_current = s;
        return s;
    }

    void pop_scope() { m_current = m_current->parent; }

    Symbol* declare(Scope& scope, std::string_view name, SymbolKind kind, ASTNode* decl, bool hoisted, Visibility vis = Visibility::Global) {
        Symbol* existing = scope.find_local(name);
        const bool overloadable = existing && kind == SymbolKind::Function && existing->kind == SymbolKind::Function;

        if (existing && !overloadable) {
            SemanticError::redeclaration(m_reporter, decl->file_id, decl->line, name, existing->decl ? existing->decl->file_id : INVALID_FILE, existing->decl ? existing->decl->line : 0);
            return existing;
        }

        Symbol* sym     = make_in<Symbol>(m_arena, name, kind, decl);
        sym->decl_order = m_order++;
        sym->is_hoisted = hoisted;
        sym->visibility = vis;

        if (!existing) {
            scope.declare(sym);                 
        } else {
            Symbol* tail = existing;
            while (tail->next_overload) { tail = tail->next_overload; }
            tail->next_overload = sym;
            sym->owner = &scope;
        }

        return sym;
    }

    static bool decl_is_hoisted(const ASTNode* n) {
        using modifiers::RawModifiers;
        switch (n->kind) {
            case ASTNode::Kind::VariableDeclaration:
                return static_cast<const nodes::VariableDeclaration*>(n)->get_type_info().modifiers.has(RawModifiers::Hoisted);
            case ASTNode::Kind::FunctionDeclaration:
                return static_cast<const nodes::FunctionDeclaration*>(n)->get_modifiers().has(RawModifiers::Hoisted);
            case ASTNode::Kind::RecordDeclaration:
                return static_cast<const nodes::RecordDeclaration*>(n)->get_modifiers().has(RawModifiers::Hoisted);
            case ASTNode::Kind::ArrayDeclaration:
                return static_cast<const nodes::ArrayDeclaration*>(n)->get_array_modifiers().has(RawModifiers::Hoisted);
            case ASTNode::Kind::EnumDeclaration:
                return static_cast<const nodes::EnumDeclaration*>(n)->get_modifiers().has(RawModifiers::Hoisted);
            default:
                return false;
        }
    }

    static Visibility decl_visibility(const ASTNode* n) {
        using modifiers::RawModifiers;
        const RawModifiers* mods = nullptr;

        switch (n->kind) {
            case ASTNode::Kind::VariableDeclaration:
                mods = &static_cast<const nodes::VariableDeclaration*>(n)->get_type_info().modifiers;
                break;
            case ASTNode::Kind::FunctionDeclaration:
                mods = &static_cast<const nodes::FunctionDeclaration*>(n)->get_modifiers();
                break;
            case ASTNode::Kind::OperatorFunctionDeclaration:
                mods = &static_cast<const nodes::OperatorFunctionDeclaration*>(n)->get_modifiers();
                break;
            case ASTNode::Kind::RecordDeclaration:
                mods = &static_cast<const nodes::RecordDeclaration*>(n)->get_modifiers();
                break;
            case ASTNode::Kind::ArrayDeclaration:
                mods = &static_cast<const nodes::ArrayDeclaration*>(n)->get_array_modifiers();
                break;
            case ASTNode::Kind::EnumDeclaration:
                mods = &static_cast<const nodes::EnumDeclaration*>(n)->get_modifiers();
                break;
            default:
                return Visibility::Global;
        }

        if (mods->has(RawModifiers::Global)) { return Visibility::Global; }
        if (mods->has(RawModifiers::Local))  { return Visibility::Local;  }
        if (mods->has(RawModifiers::Hidden)) { return Visibility::Hidden; }
        return Visibility::Global;
    }

    static bool is_friend_decl(const nodes::ASTNode* n) {
        using K = nodes::ASTNode::Kind;
        using modifiers::RawModifiers;
        switch (n->kind) {
            case K::FunctionDeclaration:
                return static_cast<const nodes::FunctionDeclaration*>(n)->get_modifiers().has(RawModifiers::Friend);
            case K::OperatorFunctionDeclaration:
                return static_cast<const nodes::OperatorFunctionDeclaration*>(n)->get_modifiers().has(RawModifiers::Friend);
            case K::RecordDeclaration:
                return static_cast<const nodes::RecordDeclaration*>(n)->get_modifiers().has(RawModifiers::Friend);
            default: return false;
        }
    }

    static std::string flag_list(std::uint16_t mask, std::size_t& count) {
        std::string list;
        count = 0;

        for (std::uint16_t bit = 1; bit; bit <<= 1) {
            if (mask & bit) {
                if (count) { list += ", "; }
                list += modifiers::RawModifiers::flag_name(static_cast<modifiers::RawModifiers::Flag>(bit));
                ++count;
            }
        }

        return list;
    }

    void check_exclusive(const modifiers::RawModifiers& mods, std::uint16_t group, std::string_view decl_kind, const ASTNode* node) {
        const std::uint16_t present = static_cast<std::uint16_t>(mods.flags() & group);
        if ((present & (present - 1)) == 0) { return; }   
        std::size_t count = 0;
        const std::string list = flag_list(present, count);
        SemanticError::conflicting_modifiers(m_reporter, node->file_id, node->line, decl_kind, list);
    }

    void check_conflicts(const modifiers::RawModifiers& mods, std::string_view decl_kind, const ASTNode* node) {
        for (const auto& p : kConflictPairs) {
            if (mods.has_all(static_cast<std::uint16_t>(p.a | p.b))) {
                std::size_t count = 0;
                const std::string list = flag_list(static_cast<std::uint16_t>(p.a | p.b), count);
                SemanticError::conflicting_modifiers(m_reporter, node->file_id, node->line, decl_kind, list);
            }
        }
    }

    void check_modifiers(const modifiers::RawModifiers& mods, std::uint16_t valid_mask, std::string_view decl_kind, const ASTNode* node) {
        const std::uint16_t bad = static_cast<std::uint16_t>(mods.flags() & ~valid_mask);

        if (bad != 0) {
            std::size_t count = 0;
            const std::string list = flag_list(bad, count);
            SemanticError::invalid_modifiers(m_reporter, node->file_id, node->line, decl_kind, list, count);
        }

        check_exclusive(mods, kVisibilityGroup, decl_kind, node);
        check_exclusive(mods, kMutabilityGroup, decl_kind, node);
        check_exclusive(mods, kConstEvalGroup,  decl_kind, node);
        check_exclusive(mods, kLinkageGroup,    decl_kind, node);

        check_conflicts(mods, decl_kind, node);
    }

    void build(ASTNode* node) {
        if (!node) { return; }
        using K = ASTNode::Kind;

        switch (node->kind) {
            case K::NamespaceDeclaration:        build_namespace  (static_cast<nodes::NamespaceDeclaration*>(node));        break;
            case K::TemplateDeclaration:         build_template   (static_cast<nodes::TemplateDeclaration*>(node));         break;
            case K::OperatorFunctionDeclaration: build_operator   (static_cast<nodes::OperatorFunctionDeclaration*>(node)); break;
            case K::ConstructorDeclaration:      build_constructor(static_cast<nodes::ConstructorDeclaration*>(node));      break;
            case K::DestructorDeclaration:       build_destructor (static_cast<nodes::DestructorDeclaration*>(node));       break;
            
            case K::EnumDeclaration: {
                auto* e = static_cast<nodes::EnumDeclaration*>(node);
                check_modifiers(e->get_modifiers(), kValidEnumMods, "enum", e);
                build_enum(e);
                break;
            }

            case K::WhileStatement: {
                auto* w = static_cast<nodes::WhileStatement*>(node);
                walk_expr(w->condition);                                            
                build(w->body);
                break;
            }

            case K::DoWhileStatement: {
                auto* w = static_cast<nodes::DoWhileStatement*>(node);
                build(w->body);
                walk_expr(w->condition);                                           
                break;
            }

            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(node);
                check_modifiers(v->get_type_info().modifiers, kValidVariableMods, "variable", v);
                build_variable(v);
                break;
            }

            case K::FunctionDeclaration: {
                auto* f = static_cast<nodes::FunctionDeclaration*>(node);
                check_modifiers(f->get_modifiers(), kValidFunctionMods, "function", f);
                build_function(f);
                break;
            }

            case K::RecordDeclaration: {
                auto* r = static_cast<nodes::RecordDeclaration*>(node);
                check_modifiers(r->get_modifiers(), kValidRecordMods, "record", r);
                build_record(r);
                break;
            }

            case K::BlockStatement: {
                auto* b = static_cast<nodes::BlockStatement*>(node);
                b->scope = push_scope(Scope::Kind::Block);
                for (ASTNode* s : b->get_statements()) { build(s); }
                pop_scope();
                break;
            }

            case K::IfStatement: {
                auto* s = static_cast<nodes::IfStatement*>(node);

                for (nodes::IfBranch* br : s->branches) {
                    const bool init = br->condition && br->condition->kind == K::VariableDeclaration;
                    if (init) { push_scope(Scope::Kind::Block); }
                    build(br->condition);     
                    build(br->body);         
                    if (init) { pop_scope(); }
                }

                build(s->else_branch);
                break;
            }

            case K::ForStatement: {
                auto* s = static_cast<nodes::ForStatement*>(node);
                push_scope(Scope::Kind::Block);
                build(s->var_init);
                build(s->initializer);
                walk_expr(s->condition);        
                walk_expr(s->increment);        
                build(s->body);
                pop_scope();
                break;
            }

            case K::ForEachStatement: {
                auto* s = static_cast<nodes::ForEachStatement*>(node);
                walk_expr(s->get_container());  
                push_scope(Scope::Kind::Block);

                if (s->is_structured_binding()) {
                    for (std::string_view b : s->get_bindings()) { declare(*m_current, b, SymbolKind::Variable, s, false); }
                } else if (!s->get_variable_name().empty()) {
                    s->symbol = declare(*m_current, s->get_variable_name(), SymbolKind::Variable, s, false);
                }

                build(static_cast<ASTNode*>(s->get_body()));
                pop_scope();
                break;
            }

            case K::SwitchStatement: {
                auto* s = static_cast<nodes::SwitchStatement*>(node);
                push_scope(Scope::Kind::Block);
                walk_expr(s->get_condition());

                for (nodes::SwitchCase* c : s->get_cases()) {
                    walk_expr(c->value);       
                    for (ASTNode* stmt : c->get_body()) { build(stmt); }
                }

                pop_scope();
                break;
            }

            case K::TryCatchStatement: {
                auto* s = static_cast<nodes::TryCatchStatement*>(node);
                build(static_cast<nodes::BlockStatement*>(s->get_try_body()));
                // The catch parameter belongs to the catch body's scope
                push_scope(Scope::Kind::Block);
                if (s->is_typed_catch() && !s->get_catch_name().empty()) { declare(*m_current, s->get_catch_name(), SymbolKind::Variable, s, false); }
                
                if (nodes::BlockStatement* cb = s->get_catch_body()) { 
                    cb->scope = m_current;  
                    for (ASTNode* stmt : static_cast<nodes::BlockStatement*>(cb)->get_statements()) { build(stmt); }
                }
                
                pop_scope();
                break;
            }

            case K::UsingDeclaration: {
                auto* u = static_cast<nodes::UsingDeclaration*>(node);

                if (u->is_alias() || u->is_typedef()) {
                    u->symbol = declare(*m_current, u->name, SymbolKind::TypeAlias, u, false);
                } else if (u->is_directive()) {
                    m_current->using_directives.push_back(u);
                }
                
                break;
            }

            case K::ConceptDeclaration: {
                auto* c = static_cast<nodes::ConceptDeclaration*>(node);
                c->symbol = declare(*m_current, c->get_name(), SymbolKind::Concept, c, false);
                walk_expr(c->constraint);       
                break;
            }

            case K::ModuleDeclaration: {
                auto* m = static_cast<nodes::ModuleDeclaration*>(node);
                Symbol* sym      = declare(*m_current, m->get_name(), SymbolKind::Module, m, false);
                m->symbol        = sym;
                Scope* ms        = push_scope(Scope::Kind::Module);
                sym->inner_scope = ms;
                for (const auto& it : m->get_items()) { if (it.decl) { build(it.decl); } }
                pop_scope();
                break;
            }

            case K::ImportExportDeclaration: {
                auto* ie = static_cast<nodes::ImportExportDeclaration*>(node);
                for (const auto& it : ie->get_items()) { if (it.decl) { build(it.decl); }}
                break;
            }

            case K::ExpressionStatement:
                walk_expr(static_cast<nodes::ExpressionStatement*>(node)->expr);
                break;

            case K::ReturnStatement:
                walk_expr(static_cast<nodes::ReturnStatement*>(node)->m_value);
                break;

            case K::CoReturnStatement:
                walk_expr(static_cast<nodes::CoReturnStatement*>(node)->m_value);   
                break;

            case K::StaticAssertDeclaration:
                walk_expr(static_cast<nodes::StaticAssertDeclaration*>(node)->constraint);
                break;

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(node);
                check_modifiers(a->get_array_modifiers(), kValidVariableMods, "array", a);
                const bool hoisted = a->get_array_modifiers().has(modifiers::RawModifiers::Hoisted);
                a->symbol = declare(*m_current, a->get_name(), SymbolKind::Variable, a, hoisted, decl_visibility(a));
                a->symbol->type = &a->get_element_type();   
                walk_expr(a->m_dimension_expr);
                walk_expr(a->m_initializer);                
                break;
            }

            default:
                if (nodes::is_expression(node)) { walk_expr(node); }
                break;
        }
    }

    void build_variable(nodes::VariableDeclaration* var) {
        const bool hoisted = var->get_type_info().modifiers.has(modifiers::RawModifiers::Hoisted);
        const Visibility vis = decl_visibility(var);

        if (var->is_structured_binding()) {
            for (std::string_view name : var->get_bindings()) {
                var->binding_symbols.push_back(declare(*m_current, name, SymbolKind::Variable, var, hoisted, vis));
            }
            // per-binding element types are derived during type-checking, left null here
        } else {
            var->symbol = declare(*m_current, var->get_name(), SymbolKind::Variable, var, hoisted, vis);
            var->symbol->type = &var->get_type_info();
        }

        walk_expr(var->m_initializer);
    }

    void build_callable_scope(Symbol* sym, const nodes::FunctionParameters* params, nodes::ASTNode* body, const std::vector<nodes::ASTNode*>* init_list = nullptr) {
        Scope* fs = push_scope(Scope::Kind::Function);
        if (sym) { sym->inner_scope = fs; }
        declare_parameters(params);
        if (init_list) { for (nodes::ASTNode* e : *init_list) { walk_expr(e); }}

        if (body) {
            if (body->kind == ASTNode::Kind::BlockStatement) {
                auto* block = static_cast<nodes::BlockStatement*>(body);
                block->scope = fs;
                for (ASTNode* s : block->get_statements()) { build(s); }
            } else {
                build(body);
            }
        }

        pop_scope();
    }

    void declare_parameters(const nodes::FunctionParameters* params) {
        if (!params) { return; }

        for (nodes::FunctionParameter* p : *params) {
            if (!p->get_name().empty()) {
                p->symbol = declare(*m_current, p->get_name(), SymbolKind::Parameter, p, false);
                p->symbol->type = &p->get_type();
            }

            walk_expr(p->m_initializer);  
        }
    }

    Symbol* declare_function(nodes::FunctionDeclaration* fn) {
        Symbol* sym = declare(*m_current, fn->get_name(), SymbolKind::Function, fn, decl_is_hoisted(fn), decl_visibility(fn));
        sym->type  = &fn->get_return_type();
        fn->symbol = sym;
        return sym;
    }

    void build_function_body(nodes::FunctionDeclaration* fn, Symbol* sym) {
        if (!fn->has_body()) { return; }
        build_callable_scope(sym, fn->get_parameters(), fn->get_body());
    }

    void build_function(nodes::FunctionDeclaration* fn) {
        Symbol* sym = declare_function(fn);
        build_function_body(fn, sym);
    }

    void build_operator(nodes::OperatorFunctionDeclaration* op) {
        // Operators resolve by signature later
        // The lookup name is a shared overload bucket.
        Symbol* sym = declare(*m_current, "operator", SymbolKind::Function, op, false, decl_visibility(op));
        sym->type  = &op->get_return_type();
        op->symbol = sym;
        if (!op->has_body()) { return; }
        build_callable_scope(sym, op->get_parameters(), const_cast<nodes::ASTNode*>(op->get_body()));
    }

    void build_constructor(nodes::ConstructorDeclaration* ctor) {
        // No external lookup name (would clash with the type/members)
        Symbol* sym = make_in<Symbol>(m_arena, std::string_view{"constructor"}, SymbolKind::Function, ctor);
        sym->decl_order = m_order++;
        ctor->symbol = sym;
        if (!ctor->has_body()) { return; }
        build_callable_scope(sym, ctor->get_parameters(), const_cast<nodes::ASTNode*>(ctor->get_body()), &ctor->get_init_list());
    }

    void build_destructor(nodes::DestructorDeclaration* dtor) {
        Symbol* sym = make_in<Symbol>(m_arena, std::string_view{"destructor"}, SymbolKind::Function, dtor);
        sym->decl_order = m_order++;
        dtor->symbol = sym;
        if (!dtor->has_body()) { return; }
        build_callable_scope(sym, nullptr, const_cast<nodes::ASTNode*>(dtor->get_body()));
    }

    void build_namespace(nodes::NamespaceDeclaration* ns) {
        Scope*  saved = m_current;
        Symbol* sym   = nullptr;

        for (std::string_view part : ns->name_parts()) {
            sym = m_current->find_local(part);

            if (sym && sym->kind == SymbolKind::Namespace) {
                m_current = sym->inner_scope;            
            } else if (sym) {
                SemanticError::redeclaration(m_reporter, ns->file_id, ns->line, part, sym->decl ? sym->decl->file_id : INVALID_FILE, sym->decl ? sym->decl->line : 0);
                m_current = saved;
                return;                                  
            } else {
                sym = make_in<Symbol>(m_arena, part, SymbolKind::Namespace, ns);
                sym->decl_order = m_order++;
                m_current->declare(sym);
                Scope* ns_scope = make_in<Scope>(m_arena, Scope::Kind::Namespace, m_current);
                sym->inner_scope = ns_scope;
                m_current = ns_scope;
            }
        }

        ns->symbol = sym;   
        if (ns->get_body()) { for (ASTNode* s : ns->get_body()->get_statements()) { build(s); }}
        m_current = saved;
    }

    Symbol* declare_record(nodes::RecordDeclaration* rec) {
        Symbol* sym = declare(*m_current, rec->get_name(), SymbolKind::Type, rec, decl_is_hoisted(rec), decl_visibility(rec));
        rec->symbol = sym;
        return sym;
    }

    void build_record_body(nodes::RecordDeclaration* rec, Symbol* sym) {
        Scope* rs        = push_scope(Scope::Kind::Record);
        sym->inner_scope = rs;

        for (const auto& member : rec->get_members()) {
            if (is_friend_decl(member.node)) { continue; }
            build(member.node);
        }

        pop_scope();
    }

    void build_record(nodes::RecordDeclaration* rec) {
        Symbol* sym = declare_record(rec);
        build_record_body(rec, sym);
    }

    void build_enum(nodes::EnumDeclaration* en) {
        Symbol* sym      = declare(*m_current, en->get_name(), SymbolKind::Enum, en, false);
        en->symbol       = sym;
        Scope* es        = push_scope(Scope::Kind::Enum);
        sym->inner_scope = es;
        for (nodes::EnumValue* v : en->get_values()) { v->symbol = declare(*es, v->get_name(), SymbolKind::EnumConstant, v, false); }
        pop_scope();
    }

    static Scope* entity_inner_scope(nodes::ASTNode* decl) {
        if (!decl) { return nullptr; }
        using K = ASTNode::Kind;
        switch (decl->kind) {
            case K::RecordDeclaration:   { auto* r = static_cast<nodes::RecordDeclaration*>(decl);   return r->symbol ? r->symbol->inner_scope : nullptr; }
            case K::FunctionDeclaration: { auto* f = static_cast<nodes::FunctionDeclaration*>(decl); return f->symbol ? f->symbol->inner_scope : nullptr; }
            default: return nullptr;
        }
    }

    void build_template(nodes::TemplateDeclaration* tmpl) {
        nodes::ASTNode* decl = tmpl->m_declaration;
        using K = ASTNode::Kind;
        Symbol* entity        = nullptr;
        bool    deferred_body = false;

        switch (decl ? decl->kind : K::Literal) {
            case K::RecordDeclaration:   entity = declare_record  (static_cast<nodes::RecordDeclaration*>(decl));   deferred_body = true; break;
            case K::FunctionDeclaration: entity = declare_function(static_cast<nodes::FunctionDeclaration*>(decl)); deferred_body = true; break;
            default:                     build(decl); break;
        }

        Scope* params = push_scope(Scope::Kind::Template);
        tmpl->scope = params;

        for (nodes::TemplateParameter* p : tmpl->params()) {
            if (!p->get_name().empty()) {
                p->symbol = declare(*params, p->get_name(), SymbolKind::TemplateParam, p, true);
            }

            walk_expr(p->m_default_value);   
        }

        walk_expr(tmpl->m_requires_clause);

        if (deferred_body) {
            switch (decl->kind) {
                case K::RecordDeclaration:   build_record_body  (static_cast<nodes::RecordDeclaration*>(decl),   entity); break;
                case K::FunctionDeclaration: build_function_body(static_cast<nodes::FunctionDeclaration*>(decl), entity); break;
                default: break;
            }
        }

        pop_scope();
    }

    void build_lambda(nodes::LambdaExpression* lam) {
        using M = nodes::LambdaCaptureItem::Mode;

        if (lam->m_captures) {
            for (auto& c : lam->m_captures->m_captures) { if (c.init) { walk_expr(c.init); } }
        }

        Scope* ls  = push_scope(Scope::Kind::Lambda);
        ls->node   = lam;
        lam->scope = ls;

        if (lam->m_captures) {
            for (auto& c : lam->m_captures->m_captures) {
                switch (c.mode) {
                    case M::ByValue:
                    case M::ByReference:
                    case M::InitByValue:
                        if (!c.name.empty()) { c.symbol = declare(*m_current, c.name, SymbolKind::Variable, lam, false); }
                        break;
                    default: break;  // This / ThisByReference / AllByValue / AllByReference introduce no name 
                }
            }
        }

        if (lam->m_parameters) {
            for (nodes::FunctionParameter* p : *lam->m_parameters) {
                if (p->get_name().empty()) { continue; }
                p->symbol = declare(*m_current, p->get_name(), SymbolKind::Parameter, p, false);
                p->symbol->type = &p->get_type();
            }
        }

        if (lam->m_body) {
            if (lam->m_body->kind == ASTNode::Kind::BlockStatement) {
                auto* block  = static_cast<nodes::BlockStatement*>(lam->m_body);
                block->scope = ls;
                for (ASTNode* s : block->get_statements()) { build(s); }
            } else {
                walk_expr(lam->m_body);   // expression-bodied lambda
            }
        }

        pop_scope();
    }

    void build_requires(nodes::RequiresExpression* req) {
        Scope* rs  = push_scope(Scope::Kind::Function);
        rs->node   = req;
        req->scope = rs;

        if (req->m_parameters) {
            for (nodes::FunctionParameter* p : *req->m_parameters) {
                if (p->get_name().empty()) { continue; }
                p->symbol = declare(*m_current, p->get_name(), SymbolKind::Parameter, p, false);
                p->symbol->type = &p->get_type();
            }
        }

        for (auto& r : req->m_requirements) { walk_expr(r.expr); }
        pop_scope();
    }

    void walk_template_args(const std::vector<parser_types::TemplateArgument*>& args) {
        for (auto* a : args) { if (a && a->is_value()) { walk_expr(a->value); } }
    }

    void walk_expr(ASTNode* e) {
        if (!e) { return; }
        using K = ASTNode::Kind;

        switch (e->kind) {
            case K::LambdaExpression: build_lambda(static_cast<nodes::LambdaExpression*>(e)); break;

            case K::BinaryExpression: {
                auto* b = static_cast<nodes::BinaryExpression*>(e);
                walk_expr(b->left); walk_expr(b->right);
                break;
            }

            case K::TernaryExpression: {
                auto* t = static_cast<nodes::TernaryExpression*>(e);
                walk_expr(t->condition); walk_expr(t->true_branch); walk_expr(t->false_branch);
                break;
            }

            case K::CallExpression: {
                auto* c = static_cast<nodes::CallExpression*>(e);
                walk_expr(c->m_callee);
                for (ASTNode* a : c->m_arguments) { walk_expr(a); }
                walk_template_args(c->m_template_args);
                break;
            }

            case K::SubscriptExpression: {
                auto* s = static_cast<nodes::SubscriptExpression*>(e);
                walk_expr(s->m_array); walk_expr(s->m_index);
                break;
            }

            case K::BraceInitializerList: {
                auto* b = static_cast<nodes::BraceInitializerList*>(e);
                for (ASTNode* el : b->m_elements) { walk_expr(el); }
                break;
            }

            case K::BraceConstructExpression: {
                auto* b = static_cast<nodes::BraceConstructExpression*>(e);
                walk_expr(b->m_callee);
                walk_expr(b->m_init);
                break;
            }

            case K::MemberAccessExpression: walk_expr(static_cast<nodes::MemberAccessExpression*>(e)->m_object); break;
            case K::CastExpression:         walk_expr(static_cast<nodes::CastExpression*>(e)->operand);          break;

            case K::TemplateInstantiation: {
                auto* t = static_cast<nodes::TemplateInstantiation*>(e);
                walk_expr(t->m_template);
                walk_template_args(t->m_args);
                break;
            }

            case K::NewExpression: {
                auto* n = static_cast<nodes::NewExpression*>(e);
                walk_expr(n->array_size);
                for (ASTNode* a : n->args) { walk_expr(a); }
                break;
            }

            case K::FoldExpression: {
                auto* f = static_cast<nodes::FoldExpression*>(e);
                walk_expr(f->lhs); walk_expr(f->rhs);
                break;
            }

            case K::TypeQuery: {
                auto* q = static_cast<nodes::TypeQueryExpression*>(e);
                if (q->operand && q->operand->is_value()) { walk_expr(q->operand->value); }
                break;
            }

            // single-operand wrappers
            case K::UnaryExpression:       walk_expr(static_cast<nodes::UnaryExpression*>(e)->operand);       break;
            case K::DereferenceExpression: walk_expr(static_cast<nodes::DereferenceExpression*>(e)->operand); break;
            case K::ReferenceExpression:   walk_expr(static_cast<nodes::ReferenceExpression*>(e)->operand);   break;
            case K::BitwiseNotExpression:  walk_expr(static_cast<nodes::BitwiseNotExpression*>(e)->operand);  break;
            case K::AwaitExpression:       walk_expr(static_cast<nodes::AwaitExpression*>(e)->operand);       break;
            case K::CoYieldExpression:     walk_expr(static_cast<nodes::CoYieldExpression*>(e)->operand);     break;
            case K::DeleteExpression:      walk_expr(static_cast<nodes::DeleteExpression*>(e)->operand);      break;
            case K::NoexceptExpression:    walk_expr(static_cast<nodes::NoexceptExpression*>(e)->operand);    break;

            case K::RequiresExpression: build_requires(static_cast<nodes::RequiresExpression*>(e)); break;

            case K::MultiSubscriptExpression: {
                auto* m = static_cast<nodes::MultiSubscriptExpression*>(e);
                walk_expr(m->m_array);
                for (ASTNode* i : m->m_indices) { walk_expr(i); }
                break;
            }

            case K::DiscardExpression: walk_expr(static_cast<nodes::DiscardExpression*>(e)->operand); break;

            // Leaves: Literal, Identifier, QualifiedIdentifier.
            default: break;
        }
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SCOPE_BUILDER_HPP