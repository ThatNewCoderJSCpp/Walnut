#include "Semantics/resolve.hpp"

namespace walnut {
namespace semantics {

bool visible_at(const Symbol* sym, const nodes::ASTNode* use) {
    if (!sym || !use) { return false; }
    if (sym->is_hoisted || sym->is_imported) { return true; }
    if (sym->owner && (sym->owner->kind == Scope::Kind::Record || sym->owner->kind == Scope::Kind::Enum)) { return true; }
    const nodes::ASTNode* decl = sym->decl;
    if (!decl) { return true; }
    if (decl->kind == nodes::ASTNode::Kind::TryCatchStatement || decl->kind == nodes::ASTNode::Kind::ForEachStatement) { return true; }
    if (decl->file_id != use->file_id) { return true; }
    if (decl->order != 0 && use->order != 0) { return decl->order <= use->order; }
    return decl->line <= use->line;
}

Symbol* resolve_qualified(Scope* root, const std::vector<std::string_view>& parts) {
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

bool scope_carrier(SymbolKind k) {
    return k == SymbolKind::Namespace || k == SymbolKind::Module || k == SymbolKind::Type || k == SymbolKind::Enum;
}

Symbol* descend(Scope* start, const std::vector<std::string_view>& parts, std::size_t first) {
    if (!start || parts.empty()) { return nullptr; }
    Scope* cur = start;

    for (std::size_t i = first; i + 1 < parts.size(); ++i) {
        Symbol* s = cur->kind == Scope::Kind::Record ? cur->find_member(parts[i]) : cur->find_local(parts[i]);
        if (!s || !s->inner_scope || !scope_carrier(s->kind)) { return nullptr; }
        cur = s->inner_scope;
    }

    return cur->kind == Scope::Kind::Record ? cur->find_member(parts.back()) : cur->find_local(parts.back());
}

Symbol* resolve_lexical_visible(Scope* from, std::string_view name, const nodes::ASTNode* use) {
    for (Scope* s = from; s; s = s->parent) {
        Symbol* sym = s->kind == Scope::Kind::Record ? s->find_member(name) : s->find_local(name);
        if (!sym) { continue; }
        if (visible_at(sym, use)) { return sym; }

        if (sym->inner_scope) {
            for (Scope* p = from; p; p = p->parent) { if (p == sym->inner_scope) { return sym; }}
        }
    }

    for (Scope* s = from; s; s = s->parent) {
        for (nodes::ASTNode* d : s->using_directives) {
            if (!d || d->kind != nodes::ASTNode::Kind::UsingDeclaration) { continue; }
            Symbol* ns = static_cast<nodes::UsingDeclaration*>(d)->symbol;
            if (!ns || !ns->inner_scope) { continue; }
            if (Symbol* sym = ns->inner_scope->find_local(name)) { return sym; }
        }
    }

    return nullptr;
}

std::vector<Symbol*> resolve_path(
    Scope* from, Scope* root,
    const std::vector<std::string_view>& parts,
    bool is_global, const nodes::ASTNode* use
) {
    std::vector<Symbol*> chain;
    if (parts.empty()) return chain;
    Scope* cur = nullptr;
    std::size_t first = 0;

    if (is_global) {
        cur = root;
    } else {
        if (!from) return chain;
        Symbol* head = resolve_lexical_visible(from, parts[0], use);
        if (!head) return chain;
        chain.push_back(head);
        if (parts.size() == 1 || !head->inner_scope || !scope_carrier(head->kind)) return chain;
        cur = head->inner_scope;
        first = 1;
    }

    for (std::size_t i = first; i < parts.size() && cur; ++i) {
        Symbol* s = cur->kind == Scope::Kind::Record ? cur->find_member(parts[i]) : cur->find_local(parts[i]);
        if (!s) break;
        chain.push_back(s);
        if (i + 1 == parts.size()) break;
        if (!s->inner_scope || !scope_carrier(s->kind)) break;
        cur = s->inner_scope;
    }

    return chain;
}

namespace {

bool is_container(const Scope* s) {
    return s && (s->kind == Scope::Kind::Namespace || s->kind == Scope::Kind::Module);
}

const Scope* container_of(const Scope* s) {
    while (s && !is_container(s)) s = s->parent;
    return s;
}

bool is_file_root(const Scope* s) {
    return s && (!s->parent || !s->parent->parent);
}

std::string scope_path(const Scope* s) {
    std::vector<std::string_view> parts;
    for (; s && !is_file_root(s); s = s->parent) if (s->owner_symbol) parts.push_back(s->owner_symbol->name);
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!out.empty()) out += "::"; out += std::string(*it); }
    return out;
}

}

std::string visibility_violation(const Symbol* sym, const Scope* from, const nodes::ASTNode* use) {
    if (!sym || !sym->decl || !use || sym->visibility == Visibility::Global) return {};
    const bool other_file = sym->decl->file_id != use->file_id;
    const std::string name(sym->name);

    if (sym->visibility == Visibility::Local) {
        if (!other_file) return {};
        return "'" + name + "' is local, so it is never exported and cannot be used through an import from another file";
    }

    const Scope* declared_in = sym->kind == SymbolKind::Namespace && sym->inner_scope ? sym->inner_scope : container_of(sym->owner);
    if (!declared_in) return {};

    if (is_file_root(declared_in)) {
        if (!other_file) return {};
        return "'" + name + "' is hidden at the top level of its file, so only that file can use it";
    }

    const Scope* allowed = container_of(declared_in->parent);
    for (const Scope* s = from; s; s = s->parent) if (s == allowed) return {};

    const std::string where = scope_path(declared_in);
    const std::string around = is_file_root(allowed) ? std::string("its own file") : "'" + scope_path(allowed) + "' and the namespaces inside it";
    if (sym->kind == SymbolKind::Namespace) return "namespace '" + where + "' is hidden, so it can only be used inside " + around;
    return "'" + name + "' is hidden in '" + where + "', so it can only be used inside " + around;
}

Symbol* resolve_name(
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
    if (Symbol* found = descend(head->inner_scope, parts, 1)) { return found; }
    if (head->kind != SymbolKind::Namespace) { return nullptr; }

    for (Scope* s = from; s; s = s->parent) {
        if (s->kind == Scope::Kind::Record) { continue; }
        Symbol* other = s->find_local(parts[0]);
        if (!other || other == head || other->kind != SymbolKind::Namespace || !other->inner_scope) { continue; }
        if (Symbol* found = descend(other->inner_scope, parts, 1)) { return found; }
    }

    return nullptr;
}

} // namespace semantics
} // namespace walnut
