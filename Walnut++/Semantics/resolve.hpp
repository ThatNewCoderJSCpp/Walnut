#ifndef WALNUT_SEMANTICS_RESOLVE_HPP
#define WALNUT_SEMANTICS_RESOLVE_HPP

#include <vector>
#include <string_view>

#include "scope.hpp"
#include "symbol.hpp"

#include "../Parser/nodes.hpp"

namespace walnut {
namespace semantics {

inline bool visible_at(const Symbol* sym, const nodes::ASTNode* use) {
    if (!sym || !use) { return false; }
    if (sym->is_hoisted || sym->is_imported) { return true; }
    if (sym->owner && (sym->owner->kind == Scope::Kind::Record || sym->owner->kind == Scope::Kind::Enum)) { return true; }
    const nodes::ASTNode* decl = sym->decl;
    if (!decl) { return true; }
    if (decl->file_id != use->file_id) { return true; }
    if (decl->order != 0 && use->order != 0) { return decl->order <= use->order; }
    return decl->line <= use->line;
}

inline Symbol* resolve_qualified(Scope* root, const std::vector<std::string_view>& parts) {
    if (!root || parts.empty()) { return nullptr; }
    Scope* cur = root;

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        Symbol* s = cur->find_local(parts[i]);
        if (!s || !s->inner_scope) { return nullptr; }
        if (s->kind != SymbolKind::Namespace && s->kind != SymbolKind::Module) { return nullptr; }
        cur = s->inner_scope;
    }

    return cur->find_local(parts.back());
}

inline bool scope_carrier(SymbolKind k) {
    return k == SymbolKind::Namespace || k == SymbolKind::Module || k == SymbolKind::Type || k == SymbolKind::Enum;
}

inline Symbol* descend(Scope* start, const std::vector<std::string_view>& parts, std::size_t first) {
    if (!start || parts.empty()) { return nullptr; }
    Scope* cur = start;

    for (std::size_t i = first; i + 1 < parts.size(); ++i) {
        Symbol* s = cur->find_local(parts[i]);
        if (!s || !s->inner_scope || !scope_carrier(s->kind)) { return nullptr; }
        cur = s->inner_scope;
    }

    return cur->find_local(parts.back());
}

inline Symbol* resolve_lexical_visible(Scope* from, std::string_view name, const nodes::ASTNode* use) {
    for (Scope* s = from; s; s = s->parent) {
        Symbol* sym = s->find_local(name);
        if (!sym) { continue; }
        if (visible_at(sym, use)) { return sym; }

        if (sym->inner_scope) {
            for (Scope* p = from; p; p = p->parent) { if (p == sym->inner_scope) { return sym; }}
        }
    }

    return nullptr;
}

inline Symbol* resolve_name(
    Scope* from, Scope* root,
    const std::vector<std::string_view>& parts,
    bool is_global, const nodes::ASTNode* use
) {
    if (parts.empty()) { return nullptr; }
    if (is_global) { return descend(root, parts, 0); }
    if (!from) { return nullptr; }
    Symbol* head = resolve_lexical_visible(from, parts[0], use);
    if (parts.size() == 1) { return head; }
    if (!head || !head->inner_scope || !scope_carrier(head->kind)) { return nullptr; }
    return descend(head->inner_scope, parts, 1);
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_RESOLVE_HPP