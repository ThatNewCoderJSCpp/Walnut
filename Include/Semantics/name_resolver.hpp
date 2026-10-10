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

    void run(nodes::BlockStatement* program, Scope* root = nullptr);

    void resolve_into(nodes::ASTNode* node, Scope* scope);

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

    Symbol* declaring_symbol(std::string_view name) const;

    static bool is_auto_declared(const nodes::VariableDeclaration* v);

    Symbol* self_capture(std::string_view name) const;

    static Symbol* chase(Symbol* s) {
        while (s && s->is_imported && s->import_target) { s = s->import_target; }
        return s;
    }

    static bool is_type_symbol(SymbolKind k);

    static std::string join(const std::vector<std::string_view>& parts, bool is_global);

    static Symbol* node_symbol(nodes::ASTNode* n);

    Scope* inner_of(Symbol* s) const { return s ? s->inner_scope : nullptr; }

    void error_unresolved(const nodes::ASTNode* use, std::string_view name);

    void resolve_type(parser_types::TypeInfo& info, const nodes::ASTNode* use, bool allow_value = false);

    void resolve_qualified(parser_types::TypeInfo& info, const nodes::ASTNode* use);

    void resolve_targ(parser_types::TemplateArgument* a, const nodes::ASTNode* use);

    void resolve_spec_args(nodes::ASTNode* decl);

    void resolve_targs(std::vector<parser_types::TemplateArgument*>& args, const nodes::ASTNode* use) {
        for (parser_types::TemplateArgument* a : args) { resolve_targ(a, use); }
    }

    void walk_expr(nodes::ASTNode* e);

    void build(nodes::ASTNode* node);

    void resolve_params(nodes::FunctionParameters* params);

    void link_base(nodes::RecordDeclaration* rec);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_NAME_RESOLVER_HPP