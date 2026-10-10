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
    Symbol* owner_symbol = nullptr;
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
    Symbol* declare(Symbol* sym);

    // Walk the scope chain outward. Unqualified names only
    Symbol* resolve(std::string_view n) const;

    Symbol* find_member(std::string_view n) const;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SCOPE_HPP