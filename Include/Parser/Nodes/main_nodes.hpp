#ifndef WALNUT_MAIN_NODES_HPP
#define WALNUT_MAIN_NODES_HPP

#include <vector>
#include <string>
#include <optional>
#include <limits>
#include <unordered_map>
#include <algorithm>
#include "../Types/type_util.hpp"
#include "../Types/type_info.hpp"
#include "../Types/dependent_types.hpp"
#include "../modifiers.hpp"
#include "../../Common/arena_allocator.hpp"
#include "../../Common/small_vector.hpp"
#include "../../Common/foundation.hpp"

namespace walnut {

namespace semantics { struct Symbol; struct Scope; struct Type;}

namespace nodes {

enum class ValueCategory : std::uint8_t { LValue = 0, RValue };

struct ExprType { 
    semantics::Type* type = nullptr; 
    ValueCategory vc = ValueCategory::RValue; 
    bool typed() const { return type != nullptr; }
};

struct ASTNode {
    enum class Kind : std::uint8_t {
        // Expressions 
        Literal = 0,
        Identifier,
        QualifiedIdentifier,
        BinaryExpression,
        UnaryExpression,
        DereferenceExpression,
        ReferenceExpression,
        BitwiseNotExpression,
        TernaryExpression,
        CallExpression,
        SubscriptExpression,
        BraceInitializerList,
        LambdaExpression,
        MemberAccessExpression,
        CastExpression,
        TemplateInstantiation,
        TypeQuery,
        NewExpression,
        DeleteExpression,
        NoexceptExpression,
        FoldExpression,
        AwaitExpression,
        CoYieldExpression,
        RequiresExpression,
        BraceConstructExpression,
        DiscardExpression,
        ThrowExpression,

        // Statements 
        ExpressionStatement,
        VariableDeclaration,
        BlockStatement,
        IfStatement,
        ForStatement,
        WhileStatement,
        FunctionDeclaration,
        ReturnStatement,
        ArrayDeclaration,
        NamespaceDeclaration,
        DoWhileStatement,
        SwitchCase,
        SwitchStatement,
        TryCatchStatement,
        EnumDeclaration,
        RecordDeclaration,
        SingleStatement,
        UsingDeclaration,
        TemplateDeclaration,
        ImportExportDeclaration,
        ModuleDeclaration,
        OperatorFunctionDeclaration,
        ForEachStatement,
        CoReturnStatement,
        ConceptDeclaration,
        StaticAssertDeclaration,

        // Auxiliary 
        IfBranch,
        FunctionParameter,
        FunctionParameters,
        LambdaCaptureList,
        EnumValue,
        ConstructorDeclaration,
        DestructorDeclaration,
        TemplateParameter
    };

    Kind kind;
    std::uint32_t line;
    std::uint32_t file_id = static_cast<std::uint32_t>(-1);
    std::uint32_t order   = 0; 
    ExprType      expr_type;

    virtual ASTNode* clone_into(Arena& arena) const = 0;

protected:
    void copy_base_to(ASTNode* c) const { 
        c->line = line; 
        c->file_id = file_id; 
        c->order = order; 
    }

    ASTNode(Kind k, std::uint32_t ln = 0) : kind(k), line(ln) {}
};

bool is_expression(const ASTNode* n);

bool is_statement(const ASTNode* n);

template<typename T>
bool node_isa(const ASTNode* n) { return n && T::classof(n); }

template<typename T>
T* node_cast(ASTNode* n) {
    return node_isa<T>(n) ? static_cast<T*>(n) : nullptr;
}

template<typename T>
const T* node_cast(const ASTNode* n) {
    return node_isa<T>(n) ? static_cast<const T*>(n) : nullptr;
}

template<typename T>
T* node_cast_unchecked(ASTNode* n) { return static_cast<T*>(n); }

template<typename T>
const T* node_cast_unchecked(const ASTNode* n) { return static_cast<const T*>(n); }

inline ASTNode* clone_child(const ASTNode* n, Arena& a) { return n ? n->clone_into(a) : nullptr; }

template <typename Vec>
inline Vec clone_list(const Vec& src, Arena& a) {
    Vec out; 
    out.reserve(src.size());
    for (const auto* n : src) out.push_back(n ? n->clone_into(a) : nullptr);
    return out;
}

template <typename T>
inline T* clone_typed(const T* n, Arena& a) { return n ? static_cast<T*>(n->clone_into(a)) : nullptr; }

template <typename T>
inline std::vector<T*> clone_typed_list(const std::vector<T*>& src, Arena& a) {
    std::vector<T*> out; out.reserve(src.size());
    for (const T* n : src) out.push_back(n ? static_cast<T*>(n->clone_into(a)) : nullptr);
    return out;
}

parser_types::TemplateArgument* clone_targ(const parser_types::TemplateArgument* t, Arena& a);

 std::vector<parser_types::TemplateArgument*>
clone_targs(const std::vector<parser_types::TemplateArgument*>& src, Arena& a);

inline void print_indent(std::ostream& os, std::size_t indent) { for (std::size_t i = 0; i < indent; ++i) os << "    "; }

void print_node(const ASTNode* node, std::ostream& os, std::size_t indent = 0);

} // namespace nodes
} // namespace walnut

#endif // WALNUT_MAIN_NODES_HPP