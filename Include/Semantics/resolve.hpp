#ifndef WALNUT_SEMANTICS_RESOLVE_HPP
#define WALNUT_SEMANTICS_RESOLVE_HPP

#include <vector>
#include <string_view>

#include "scope.hpp"
#include "symbol.hpp"

#include "../Parser/nodes.hpp"

namespace walnut {
namespace semantics {

bool visible_at(const Symbol* sym, const nodes::ASTNode* use);

Symbol* resolve_qualified(Scope* root, const std::vector<std::string_view>& parts);

bool scope_carrier(SymbolKind k);

Symbol* descend(Scope* start, const std::vector<std::string_view>& parts, std::size_t first);

Symbol* resolve_lexical_visible(Scope* from, std::string_view name, const nodes::ASTNode* use);

Symbol* resolve_name(
    Scope* from, Scope* root,
    const std::vector<std::string_view>& parts,
    bool is_global, const nodes::ASTNode* use
);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_RESOLVE_HPP