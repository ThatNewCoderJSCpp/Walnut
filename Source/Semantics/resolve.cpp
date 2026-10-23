#include "Semantics/resolve.hpp"

#include <algorithm>
#include <cctype>

namespace walnut {
namespace semantics {

bool visible_at(const Symbol* sym, const nodes::ASTNode* use) {
    if (!sym || !use) { return false; }
    if (sym->is_hoisted || sym->is_imported) { return true; }
    if (sym->owner && (sym->owner->kind == Scope::Kind::Record || sym->owner->kind == Scope::Kind::Enum)) { return true; }
    if (sym->kind == SymbolKind::Function && sym->owner && (sym->owner->kind == Scope::Kind::Module || sym->owner->kind == Scope::Kind::Namespace)) { return true; }
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

namespace {

std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const char x = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i - 1])));
            const char y = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j - 1])));
            const std::size_t cost = x == y ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

bool suggestable(const Symbol* s) {
    if (!s || s->name.empty()) return false;
    if (s->name == "constructor" || s->name == "destructor") return false;
    if (s->name.find('#') != std::string_view::npos || s->name.rfind("walnut", 0) == 0 || s->name.rfind("__", 0) == 0) return false;
    return true;
}

void collect_members(const Scope* s, std::vector<std::string_view>& out, int depth) {
    if (!s || depth > 16) return;
    for (const Symbol* sym : s->symbols) if (suggestable(sym)) out.push_back(sym->name);
    for (const Scope* b : s->bases) collect_members(b, out, depth + 1);
}

} // namespace

std::string suggest_similar(const Scope* from, std::string_view name, bool members_only) {
    if (name.empty()) return {};
    std::vector<std::string_view> names;

    if (members_only) {
        collect_members(from, names, 0);
    } else {
        for (const Scope* s = from; s; s = s->parent) {
            if (s->kind == Scope::Kind::Record) collect_members(s, names, 0);
            else for (const Symbol* sym : s->symbols) if (suggestable(sym)) names.push_back(sym->name);
        }
    }

    const std::size_t limit = name.size() <= 2 ? 0 : (name.size() <= 4 ? 1 : (name.size() <= 7 ? 2 : 3));
    std::string_view best;
    std::size_t best_d = limit + 1;

    for (std::string_view n : names) {
        if (n == name || (n.size() <= 2 && name.size() > 2)) continue;
        const std::size_t longer = std::max(n.size(), name.size());
        const std::size_t shorter = std::min(n.size(), name.size());
        if (longer - shorter > limit) continue;
        const std::size_t d = edit_distance(n, name);
        if (d < best_d) { best_d = d; best = n; }
    }

    if (best.empty()) return {};
    return "; did you mean '" + std::string(best) + "'?";
}

bool scope_carrier(SymbolKind k) {
    return k == SymbolKind::Namespace || k == SymbolKind::Module || k == SymbolKind::Type || k == SymbolKind::Enum;
}

bool carries_scope(const Symbol* s) {
    if (!s || !s->inner_scope) return false;
    if (s->kind == SymbolKind::Import) return s->import_target && scope_carrier(s->import_target->kind);
    return scope_carrier(s->kind);
}

Symbol* descend(Scope* start, const std::vector<std::string_view>& parts, std::size_t first) {
    if (!start || parts.empty()) { return nullptr; }
    Scope* cur = start;

    for (std::size_t i = first; i + 1 < parts.size(); ++i) {
        Symbol* s = cur->kind == Scope::Kind::Record ? cur->find_member(parts[i]) : cur->find_local(parts[i]);
        if (!carries_scope(s)) { return nullptr; }
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
        if (parts.size() == 1 || !carries_scope(head)) return chain;
        cur = head->inner_scope;
        first = 1;
    }

    for (std::size_t i = first; i < parts.size() && cur; ++i) {
        Symbol* s = cur->kind == Scope::Kind::Record ? cur->find_member(parts[i]) : cur->find_local(parts[i]);
        if (!s) break;
        chain.push_back(s);
        if (i + 1 == parts.size()) break;
        if (!carries_scope(s)) break;
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
    if (!carries_scope(head)) { return nullptr; }
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
