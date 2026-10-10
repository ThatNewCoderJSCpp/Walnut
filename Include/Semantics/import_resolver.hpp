#ifndef WALNUT_SEMANTICS_IMPORT_RESOLVER_HPP
#define WALNUT_SEMANTICS_IMPORT_RESOLVER_HPP

#include <vector>
#include <string_view>

#include "scope.hpp"
#include "symbol.hpp"
#include "scope_builder.hpp"   

#include "../Parser/nodes.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Common/error_reporter.hpp"
#include "semantic_error.hpp"

namespace walnut {
namespace semantics {

using nodes::ASTNode;

// Outbound visibility record
// Consumed by cross-file linking pass
// Does not introduce a locally resolvable symbol
struct ExportEntry {
    nodes::ImportExportItem::Kind kind;
    std::vector<std::string_view> public_parts;     // name as importers see it
    bool                          public_global = false;
    std::vector<std::string_view> origin_parts;     // where it came from in this file
    bool                          origin_global = false;
    std::string_view              source;
    bool                          has_source   = false;
    nodes::ASTNode*               decl         = nullptr;  // for export function
    std::string_view              module_owner;            // for module NAME {...}
};

class ImportResolver {
public:
    explicit ImportResolver(AnalysisContext& ctx) noexcept : m_ctx(ctx), m_arena(ctx.arena), m_reporter(ctx.reporter) {}

    void run(nodes::BlockStatement* program);

    const std::vector<ExportEntry>& exports() const noexcept { return m_exports; }

private:
    AnalysisContext&         m_ctx;
    Arena&                   m_arena;
    ErrorReporter&           m_reporter;
    std::vector<ExportEntry> m_exports;

private:
    Scope* namespace_path_parent(Scope* base, const std::vector<std::string_view>& parts, ASTNode* origin);

    void declare_import(Scope* target, std::string_view name, SymbolKind kind, ASTNode* origin, std::string_view source);

    void import_item(const nodes::ImportExportItem& it, Scope* root, ASTNode* origin);

    void export_item(const nodes::ImportExportItem& it, std::string_view module_owner);

    void process_import_export(nodes::ImportExportDeclaration* decl, Scope* root, std::string_view module_owner);

    void process_module(nodes::ModuleDeclaration* mod) { for (const auto& it : mod->get_items()) { export_item(it, mod->get_name()); }}

    void forbid_nested(const ASTNode* node);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_IMPORT_RESOLVER_HPP