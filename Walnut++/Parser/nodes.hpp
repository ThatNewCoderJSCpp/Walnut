#ifndef WALNUT_AST_NODES_HPP
#define WALNUT_AST_NODES_HPP

#include "Nodes/main_nodes.hpp"
#include "Nodes/expression_nodes.hpp"
#include "Nodes/statement_nodes.hpp"
#include "Nodes/function_nodes.hpp"
#include "Nodes/record_nodes.hpp"
#include "Nodes/requires_expression_node.hpp"

namespace walnut {
namespace nodes {

inline void print_node(const ASTNode* node, std::ostream& os, std::size_t indent) {
    if (!node) return;

    switch (node->kind) {
        case ASTNode::Kind::Literal:                     node_cast_unchecked<Literal>(node)->print(os, indent);                     break;
        case ASTNode::Kind::Identifier:                  node_cast_unchecked<Identifier>(node)->print(os, indent);                  break;
        case ASTNode::Kind::QualifiedIdentifier:         node_cast_unchecked<QualifiedIdentifier>(node)->print(os, indent);         break;
        case ASTNode::Kind::BinaryExpression:            node_cast_unchecked<BinaryExpression>(node)->print(os, indent);            break;
        case ASTNode::Kind::UnaryExpression:             node_cast_unchecked<UnaryExpression>(node)->print(os, indent);             break;
        case ASTNode::Kind::DereferenceExpression:       node_cast_unchecked<DereferenceExpression>(node)->print(os, indent);       break;
        case ASTNode::Kind::ReferenceExpression:         node_cast_unchecked<ReferenceExpression>(node)->print(os, indent);         break;
        case ASTNode::Kind::BitwiseNotExpression:        node_cast_unchecked<BitwiseNotExpression>(node)->print(os, indent);        break;
        case ASTNode::Kind::TernaryExpression:           node_cast_unchecked<TernaryExpression>(node)->print(os, indent);           break;
        case ASTNode::Kind::CallExpression:              node_cast_unchecked<CallExpression>(node)->print(os, indent);              break;
        case ASTNode::Kind::SubscriptExpression:         node_cast_unchecked<SubscriptExpression>(node)->print(os, indent);         break;
        case ASTNode::Kind::BraceInitializerList:        node_cast_unchecked<BraceInitializerList>(node)->print(os, indent);        break;
        case ASTNode::Kind::ExpressionStatement:         node_cast_unchecked<ExpressionStatement>(node)->print(os, indent);         break;
        case ASTNode::Kind::VariableDeclaration:         node_cast_unchecked<VariableDeclaration>(node)->print(os, indent);         break;
        case ASTNode::Kind::BlockStatement:              node_cast_unchecked<BlockStatement>(node)->print(os, indent);              break;
        case ASTNode::Kind::IfBranch:                    node_cast_unchecked<IfBranch>(node)->print(os, indent);                    break;
        case ASTNode::Kind::IfStatement:                 node_cast_unchecked<IfStatement>(node)->print(os, indent);                 break;
        case ASTNode::Kind::ForStatement:                node_cast_unchecked<ForStatement>(node)->print(os, indent);                break;
        case ASTNode::Kind::WhileStatement:              node_cast_unchecked<WhileStatement>(node)->print(os, indent);              break;
        case ASTNode::Kind::FunctionParameter:           node_cast_unchecked<FunctionParameter>(node)->print(os, indent);           break;
        case ASTNode::Kind::FunctionParameters:          node_cast_unchecked<FunctionParameters>(node)->print(os, indent);          break;
        case ASTNode::Kind::FunctionDeclaration:         node_cast_unchecked<FunctionDeclaration>(node)->print(os, indent);         break;
        case ASTNode::Kind::ReturnStatement:             node_cast_unchecked<ReturnStatement>(node)->print(os, indent);             break;
        case ASTNode::Kind::ArrayDeclaration:            node_cast_unchecked<ArrayDeclaration>(node)->print(os, indent);            break;
        case ASTNode::Kind::NamespaceDeclaration:        node_cast_unchecked<NamespaceDeclaration>(node)->print(os, indent);        break;
        case ASTNode::Kind::LambdaExpression:            node_cast_unchecked<LambdaExpression>(node)->print(os, indent);            break;
        case ASTNode::Kind::DoWhileStatement:            node_cast_unchecked<DoWhileStatement>(node)->print(os, indent);            break;
        case ASTNode::Kind::SwitchCase:                  node_cast_unchecked<SwitchCase>(node)->print(os, indent);                  break;
        case ASTNode::Kind::SwitchStatement:             node_cast_unchecked<SwitchStatement>(node)->print(os, indent);             break;
        case ASTNode::Kind::TryCatchStatement:           node_cast_unchecked<TryCatchStatement>(node)->print(os, indent);           break;
        case ASTNode::Kind::RecordDeclaration:           node_cast_unchecked<RecordDeclaration>(node)->print(os, indent);           break;
        case ASTNode::Kind::ConstructorDeclaration:      node_cast_unchecked<ConstructorDeclaration>(node)->print(os, indent);      break;
        case ASTNode::Kind::DestructorDeclaration:       node_cast_unchecked<DestructorDeclaration>(node)->print(os, indent);       break;
        case ASTNode::Kind::MemberAccessExpression:      node_cast_unchecked<MemberAccessExpression>(node)->print(os, indent);      break;
        case ASTNode::Kind::SingleStatement:             node_cast_unchecked<SingleStatement>(node)->print(os, indent);             break;
        case ASTNode::Kind::UsingDeclaration:            node_cast_unchecked<UsingDeclaration>(node)->print(os, indent);            break;
        case ASTNode::Kind::TemplateDeclaration:         node_cast_unchecked<TemplateDeclaration>(node)->print(os, indent);         break;
        case ASTNode::Kind::TemplateParameter:           node_cast_unchecked<TemplateParameter>(node)->print(os, indent);           break;
        case ASTNode::Kind::CastExpression:              node_cast_unchecked<CastExpression>(node)->print(os, indent);              break;
        case ASTNode::Kind::ImportExportDeclaration:     node_cast_unchecked<ImportExportDeclaration>(node)->print(os, indent);     break;
        case ASTNode::Kind::OperatorFunctionDeclaration: node_cast_unchecked<OperatorFunctionDeclaration>(node)->print(os, indent); break;
        case ASTNode::Kind::TypeQuery:                   node_cast_unchecked<TypeQueryExpression>(node)->print(os, indent);         break;
        case ASTNode::Kind::EnumDeclaration:             node_cast_unchecked<EnumDeclaration>(node)->print(os, indent);             break;
        case ASTNode::Kind::EnumValue:                   node_cast_unchecked<EnumValue>(node)->print(os, indent);                   break;
        case ASTNode::Kind::TemplateInstantiation:       node_cast_unchecked<TemplateInstantiation>(node)->print(os, indent);       break;
        case ASTNode::Kind::ForEachStatement:            node_cast_unchecked<ForEachStatement>(node)->print(os, indent);            break;
        case ASTNode::Kind::NewExpression:               node_cast_unchecked<NewExpression>(node)->print(os, indent);               break;
        case ASTNode::Kind::DeleteExpression:            node_cast_unchecked<DeleteExpression>(node)->print(os, indent);            break;
        case ASTNode::Kind::NoexceptExpression:          node_cast_unchecked<NoexceptExpression>(node)->print(os, indent);          break;
        case ASTNode::Kind::FoldExpression:              node_cast_unchecked<FoldExpression>(node)->print(os, indent);              break;
        case ASTNode::Kind::AwaitExpression:             node_cast_unchecked<AwaitExpression>(node)->print(os, indent);             break;
        case ASTNode::Kind::CoYieldExpression:           node_cast_unchecked<CoYieldExpression>(node)->print(os, indent);           break;
        case ASTNode::Kind::CoReturnStatement:           node_cast_unchecked<CoReturnStatement>(node)->print(os, indent);           break;
        case ASTNode::Kind::ModuleDeclaration:           node_cast_unchecked<ModuleDeclaration>(node)->print(os, indent);           break;
        case ASTNode::Kind::ConceptDeclaration:          node_cast_unchecked<ConceptDeclaration>(node)->print(os, indent);          break;
        case ASTNode::Kind::BraceConstructExpression:    node_cast_unchecked<BraceConstructExpression>(node)->print(os, indent);    break;
        case ASTNode::Kind::StaticAssertDeclaration:     node_cast_unchecked<StaticAssertDeclaration>(node)->print(os, indent);     break;
        case ASTNode::Kind::DiscardExpression:           node_cast_unchecked<DiscardExpression>(node)->print(os, indent);           break;
        case ASTNode::Kind::RequiresExpression:          node_cast_unchecked<RequiresExpression>(node)->print(os, indent);          break;
    }
}

} // namespace nodes
} // namespace walnut

#endif // WALNUT_AST_NODES_HPP