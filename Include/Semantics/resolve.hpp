#ifndef WALNUT_SEMANTICS_RESOLVE_HPP
#define WALNUT_SEMANTICS_RESOLVE_HPP

#include <vector>
#include <string>
#include <string_view>

#include "scope.hpp"
#include "symbol.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/source_manager.hpp"

namespace walnut {
namespace semantics {

bool visible_at(const Symbol* sym, const nodes::ASTNode* use);

Symbol* resolve_qualified(Scope* root, Span<std::string_view const> parts);

bool scope_carrier(SymbolKind k);

Symbol* descend(Scope* start, Span<std::string_view const> parts, std::size_t first);

Symbol* resolve_lexical_visible(Scope* from, std::string_view name, const nodes::ASTNode* use);

Symbol* resolve_name(
    Scope* from, Scope* root,
    Span<std::string_view const> parts,
    bool is_global, const nodes::ASTNode* use
);

std::vector<Symbol*> resolve_path(
    Scope* from, Scope* root,
    Span<std::string_view const> parts,
    bool is_global, const nodes::ASTNode* use
);

std::string visibility_violation(const Symbol* sym, const Scope* from, const nodes::ASTNode* use);
std::string visibility_violation(const Symbol* sym, const Scope* from, FileId use_file);
bool visible_from(const Symbol* sym, const Scope* from, FileId use_file);

std::string suggest_similar(const Scope* from, std::string_view name, bool members_only);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_RESOLVE_HPP