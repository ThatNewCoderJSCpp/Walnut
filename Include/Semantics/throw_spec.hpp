#ifndef WALNUT_SEMANTICS_THROW_SPEC_HPP
#define WALNUT_SEMANTICS_THROW_SPEC_HPP

#include "type_impl.hpp"
#include "conversion.hpp"
#include "symbol.hpp"
#include "../Parser/nodes.hpp"
#include "../Common/error_reporter.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace semantics {

namespace tdetail {
    inline nodes::ASTNode* mn(const nodes::ASTNode* n) {
        return const_cast<nodes::ASTNode*>(n);
    }
}

struct FunctionLike {
    nodes::ASTNode*                      decl   = nullptr;
    Symbol*                              symbol = nullptr;
    nodes::ASTNode*                      body   = nullptr;
    const modifiers::FunctionQualifiers* quals  = nullptr;  
    std::string_view                     name;

    bool valid() const { return decl != nullptr; }
};

FunctionLike as_function_like(nodes::ASTNode* n);

enum class ThrowSpec : std::uint8_t { NoThrow = 0, MayThrow, Unknown };

struct ThrowInfo {
    bool               unknown = false;
    std::vector<Type*> thrown;

    ThrowSpec spec() const;

    bool nothrow() const { return spec() == ThrowSpec::NoThrow; }

    void add(Type* t) {
        if (!t) { unknown = true; return; }
        for (Type* e : thrown) if (e == t) return;
        thrown.push_back(t);
    }

    void merge(const ThrowInfo& o) {
        unknown = unknown || o.unknown;
        for (Type* t : o.thrown) add(t);
    }
};

class ThrowAnalyzer {
public:
    explicit ThrowAnalyzer(TypeContext& types) : m_types(types) {}

    ThrowSpec declared_or_inferred(Symbol* fn);

    const ThrowInfo& of_function_body(Symbol* fn);

    ThrowInfo of_expr(nodes::ASTNode* e);

    ThrowInfo of_stmt(nodes::ASTNode* s);

private:
    bool caught_by(Type* thrown, Type* catch_type) const;

    static bool declared_noexcept(Symbol* fn);

    static nodes::ASTNode* function_body(Symbol* fn);

    Type* type_of(nodes::ASTNode* e) const { return e ? e->expr_type.type : nullptr; }

    TypeContext& m_types;
    std::unordered_map<Symbol*, ThrowInfo> m_cache;
    std::unordered_set<Symbol*>            m_active;
};

struct ThrowContext {
    explicit ThrowContext(TypeContext& types) : analyzer(types) {}

    ThrowAnalyzer                analyzer;
    std::vector<nodes::ASTNode*> pending;  
};

void check_noexcept_contracts(ThrowContext& ctx, TypeContext& types, ErrorReporter& reporter);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_THROW_SPEC_HPP