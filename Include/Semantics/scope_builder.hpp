#ifndef WALNUT_SEMANTICS_SCOPE_BUILDER_HPP
#define WALNUT_SEMANTICS_SCOPE_BUILDER_HPP

#include <string>
#include <cstdint>

#include "scope.hpp"
#include "prelude.hpp"
#include "symbol.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Common/error_reporter.hpp"
#include "semantic_error.hpp"
#include "modifier_rules.hpp"

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

    Scope* build(nodes::BlockStatement* program);

    void build_into(nodes::ASTNode* node, Scope* parent) {
        Scope* saved = m_current;
        m_current = parent;
        build(node);
        m_current = saved;
    }

private:
    AnalysisContext& m_ctx;
    Arena&           m_arena;
    ErrorReporter&   m_reporter;
    Scope*           m_current = nullptr;
    std::uint64_t    m_order   = 0;  

private:
    Scope* push_scope(Scope::Kind kind) {
        Scope* s  = make_in<Scope>(m_arena, kind, m_current);
        m_current = s;
        return s;
    }

    void pop_scope() { m_current = m_current->parent; }

    Symbol* declare(Scope& scope, std::string_view name, SymbolKind kind, ASTNode* decl, bool hoisted, Visibility vis = Visibility::Global);

    static bool decl_is_hoisted(const ASTNode* n);

    static Visibility decl_visibility(const ASTNode* n);

    static Symbol* member_symbol(nodes::ASTNode* n);

    static bool is_friend_decl(const nodes::ASTNode* n);

    void validate_record_specials(nodes::RecordDeclaration* rec);

    void build(ASTNode* node);

    void build_variable(nodes::VariableDeclaration* var);

    void build_callable_scope(Symbol* sym, const nodes::FunctionParameters* params, nodes::ASTNode* body, const std::vector<nodes::ASTNode*>* init_list = nullptr);

    void declare_parameters(const nodes::FunctionParameters* params);

    Symbol* declare_function(nodes::FunctionDeclaration* fn);

    void build_function_body(nodes::FunctionDeclaration* fn, Symbol* sym);

    void build_function(nodes::FunctionDeclaration* fn) {
        Symbol* sym = declare_function(fn);
        build_function_body(fn, sym);
    }

    void build_operator(nodes::OperatorFunctionDeclaration* op);

    void build_constructor(nodes::ConstructorDeclaration* ctor);

    void build_destructor(nodes::DestructorDeclaration* dtor);

    void build_namespace(nodes::NamespaceDeclaration* ns);

    Symbol* declare_record(nodes::RecordDeclaration* rec);

    void build_record_body(nodes::RecordDeclaration* rec, Symbol* sym);

    void build_record(nodes::RecordDeclaration* rec);

    void build_enum(nodes::EnumDeclaration* en);

    static Scope* entity_inner_scope(nodes::ASTNode* decl);

    void build_record_specialization(nodes::TemplateDeclaration* tmpl, nodes::RecordDeclaration* rec);

    void build_function_specialization(nodes::TemplateDeclaration* tmpl, nodes::FunctionDeclaration* fn);

    void build_template(nodes::TemplateDeclaration* tmpl);

    void build_lambda(nodes::LambdaExpression* lam);

    void build_requires(nodes::RequiresExpression* req);

    void walk_template_args(const std::vector<parser_types::TemplateArgument*>& args) {
        for (auto* a : args) { if (a && a->is_value()) { walk_expr(a->value); } }
    }

    void walk_expr(ASTNode* e);

    static nodes::RecordDeclaration* spec_record_of(nodes::TemplateDeclaration* t);

    static nodes::FunctionDeclaration* spec_function_of(nodes::TemplateDeclaration* t);

    static nodes::TemplateParameter* first_defaulted_param(nodes::TemplateDeclaration* t);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SCOPE_BUILDER_HPP