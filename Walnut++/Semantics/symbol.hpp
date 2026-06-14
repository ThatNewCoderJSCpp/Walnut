#ifndef WALNUT_SEMANTICS_SYMBOL_HPP
#define WALNUT_SEMANTICS_SYMBOL_HPP

#include <string_view>
#include <cstdint>

#include "../Common/small_vector.hpp"   // adjust path to your SmallVector
// Forward-decls keep this header light and avoid pulling the whole AST /
// type subsystem in. Resolution code includes the real headers.
namespace walnut {
    namespace nodes        { struct ASTNode; }
    namespace parser_types { class  TypeInfo; }
}

namespace walnut {
namespace semantics {

enum class SymbolKind : std::uint8_t {
    Variable = 0,
    Parameter,
    Function,
    Type,           // record / class / struct
    TypeAlias,      // using-alias / typedef
    Enum,
    EnumConstant,
    Namespace,
    Module,
    Concept,
    TemplateParam,
    Import
};

enum class Visibility : std::uint8_t {
    Global = 0,
    Local, 
    Hidden
};

struct Scope; 

struct Symbol {
    std::string_view              name;
    SymbolKind                    kind;
    nodes::ASTNode*               decl  = nullptr;   // defining node

    Visibility                    visibility;

    // Points into the decl node. nullptr for Namespace/Module. Return type for function
    const parser_types::TypeInfo* type  = nullptr;
    Scope*                        owner = nullptr;   // scope this lives in

    // The scope this symbol introduces
    Scope*                        inner_scope = nullptr;

    // Intrusive overload chain. nullptr unless this name is overloaded
    Symbol*                       next_overload = nullptr;

    std::uint64_t                 decl_order  = 0;    
    bool                          is_hoisted  = false;

    bool                          is_imported   = false;
    std::string_view              import_source;
    Symbol*                       import_target = nullptr;

    Symbol(std::string_view n, SymbolKind k, nodes::ASTNode* d) noexcept : name(n), kind(k), decl(d) {}
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_SYMBOL_HPP