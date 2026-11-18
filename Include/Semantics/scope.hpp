#ifndef WALNUT_SEMANTICS_SCOPE_HPP
#define WALNUT_SEMANTICS_SCOPE_HPP

#include "symbol.hpp"
#include <unordered_map>

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
    ~Scope() { delete m_index; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    static constexpr std::size_t index_threshold = 16;

public:
    // Local-only lookup (no parent walk)
    Symbol* find_local(std::string_view n) const {
        if (symbols.size() < index_threshold) {
            for (Symbol* s : symbols) { if (s->name == n) { return s; } }
            return nullptr;
        }
        refresh_index();
        auto it = m_index->find(n);
        return it == m_index->end() ? nullptr : it->second;
    }

    // Insert into THIS scope 
    // Returns the existing symbol on a name clash 
    // Returns nullptr on a clean insert
    Symbol* declare(Symbol* sym);

    // Walk the scope chain outward. Unqualified names only
    Symbol* resolve(std::string_view n) const;

    Symbol* find_member(std::string_view n) const;
    void member_owners(std::string_view n, std::vector<const Scope*>& out, int depth = 0) const;

private:
    mutable std::unordered_map<std::string_view, Symbol*>* m_index = nullptr;
    mutable std::size_t m_indexed = 0;
    mutable Symbol* m_first = nullptr;

    void refresh_index() const {
        if (!m_index) m_index = new std::unordered_map<std::string_view, Symbol*>();
        if (m_indexed > symbols.size() || (m_indexed > 0 && symbols[0] != m_first)) {
            m_index->clear();
            m_indexed = 0;
        }
        if (m_indexed == 0) m_index->reserve(symbols.size() * 2);
        for (; m_indexed < symbols.size(); ++m_indexed) m_index->emplace(symbols[m_indexed]->name, symbols[m_indexed]);
        m_first = symbols[0];
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SCOPE_HPP