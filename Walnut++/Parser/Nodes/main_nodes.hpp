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

namespace walnut {

namespace semantics { struct Symbol; struct Scope; }

namespace nodes {

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
        MultiSubscriptExpression,
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

protected:
    ASTNode(Kind k, std::uint32_t ln = 0) : kind(k), line(ln) {}
};

inline bool is_expression(const ASTNode* n) {
    switch (n->kind) {
        case ASTNode::Kind::Literal:
        case ASTNode::Kind::Identifier:
        case ASTNode::Kind::QualifiedIdentifier:
        case ASTNode::Kind::BinaryExpression:
        case ASTNode::Kind::UnaryExpression:
        case ASTNode::Kind::DereferenceExpression:
        case ASTNode::Kind::ReferenceExpression:
        case ASTNode::Kind::BitwiseNotExpression:
        case ASTNode::Kind::TernaryExpression:
        case ASTNode::Kind::CallExpression:
        case ASTNode::Kind::SubscriptExpression:
        case ASTNode::Kind::MultiSubscriptExpression:
        case ASTNode::Kind::BraceInitializerList:
        case ASTNode::Kind::LambdaExpression:
        case ASTNode::Kind::MemberAccessExpression:
        case ASTNode::Kind::TemplateInstantiation:
        case ASTNode::Kind::TypeQuery:
        case ASTNode::Kind::NewExpression:
        case ASTNode::Kind::DeleteExpression:
        case ASTNode::Kind::NoexceptExpression:
        case ASTNode::Kind::FoldExpression:
        case ASTNode::Kind::AwaitExpression:
        case ASTNode::Kind::BraceConstructExpression:
        case ASTNode::Kind::DiscardExpression:
        case ASTNode::Kind::RequiresExpression:
            return true;
        default:
            return false;
    }
}

inline bool is_statement(const ASTNode* n) {
    switch (n->kind) {
        case ASTNode::Kind::ExpressionStatement:
        case ASTNode::Kind::VariableDeclaration:
        case ASTNode::Kind::BlockStatement:
        case ASTNode::Kind::IfStatement:
        case ASTNode::Kind::ForStatement:
        case ASTNode::Kind::WhileStatement:
        case ASTNode::Kind::FunctionDeclaration:
        case ASTNode::Kind::ReturnStatement:
        case ASTNode::Kind::ArrayDeclaration:
        case ASTNode::Kind::NamespaceDeclaration:
        case ASTNode::Kind::DoWhileStatement:
        case ASTNode::Kind::SwitchStatement:
        case ASTNode::Kind::TryCatchStatement:
        case ASTNode::Kind::EnumDeclaration:
        case ASTNode::Kind::SingleStatement:
        case ASTNode::Kind::UsingDeclaration:
        case ASTNode::Kind::TemplateDeclaration: 
        case ASTNode::Kind::ImportExportDeclaration:
        case ASTNode::Kind::ModuleDeclaration:
        case ASTNode::Kind::OperatorFunctionDeclaration:
        case ASTNode::Kind::ForEachStatement:
        case ASTNode::Kind::CoReturnStatement:
        case ASTNode::Kind::StaticAssertDeclaration:
            return true;
        default:
            return false;
    }
}

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

inline void print_indent(std::ostream& os, std::size_t indent) { for (std::size_t i = 0; i < indent; ++i) os << "    "; }

void print_node(const ASTNode* node, std::ostream& os, std::size_t indent = 0);

} // namespace nodes
} // namespace walnut

#endif // WALNUT_MAIN_NODES_HPP