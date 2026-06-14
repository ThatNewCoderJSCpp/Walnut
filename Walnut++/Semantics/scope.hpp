#ifndef WALNUT_SEMANTICS_SCOPE_HPP
#define WALNUT_SEMANTICS_SCOPE_HPP

#include "symbol.hpp"

namespace walnut {
namespace semantics {

struct Scope {
    enum class Kind : std::uint8_t {
        Module = 0,
        Namespace,
        Function,
        Block,
        Record,
        Enum,
        Template,
        Lambda
    };

    Kind   kind;
    Scope* parent = nullptr;
    nodes::ASTNode* node = nullptr; // for lambdas

    SmallVector<Symbol*, 8> symbols;
    SmallVector<nodes::ASTNode*, 4> using_directives;
    SmallVector<Scope*, 2> bases; 
    SmallVector<Scope*, 4> children; // for lambdas

    explicit Scope(Kind k, Scope* p = nullptr) noexcept : kind(k), parent(p) { if (p) { p->children.push_back(this); }}

public:
    // Local-only lookup (no parent walk)
    Symbol* find_local(std::string_view n) const {
        for (Symbol* s : symbols) { if (s->name == n) { return s; } }
        return nullptr;
    }

    // Insert into THIS scope 
    // Returns the existing symbol on a name clash 
    // Returns nullptr on a clean insert
    Symbol* declare(Symbol* sym) {
        if (Symbol* existing = find_local(sym->name)) { return existing; }
        sym->owner = this;
        symbols.push_back(sym);
        return nullptr;
    }

    // Walk the scope chain outward. Unqualified names only
    Symbol* resolve(std::string_view n) const {
        for (const Scope* s = this; s; s = s->parent) { if (Symbol* found = s->find_local(n)) { return found; }}
        return nullptr;
    }

    Symbol* find_member(std::string_view n) const {
        if (Symbol* s = find_local(n)) { return s; }
        for (Scope* b : bases) { if (b) { if (Symbol* s = b->find_member(n)) { return s; }}}
        return nullptr;
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SCOPE_HPP