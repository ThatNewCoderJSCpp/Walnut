#include "Parser/Nodes/main_nodes.hpp"

namespace walnut {
namespace nodes {

bool is_expression(const ASTNode* n) {
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
        case ASTNode::Kind::CastExpression:  
        case ASTNode::Kind::CoYieldExpression:
        case ASTNode::Kind::ThrowExpression:
            return true;
        default:
            return false;
    }
}

bool is_statement(const ASTNode* n) {
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
        case ASTNode::Kind::RecordDeclaration:    
        case ASTNode::Kind::ConceptDeclaration:    
            return true;
        default:
            return false;
    }
}

parser_types::TemplateArgument* clone_targ(const parser_types::TemplateArgument* t, Arena& a) {
    if (!t) return nullptr;
    auto* n = make_in<parser_types::TemplateArgument>(a);
    n->form       = t->form;
    n->type       = t->type.clone_into(a);
    n->value      = t->value ? t->value->clone_into(a) : nullptr;
    n->value_repr = t->value_repr;
    n->is_pack    = t->is_pack;
    return n;
}

std::vector<parser_types::TemplateArgument*> clone_targs(const std::vector<parser_types::TemplateArgument*>& src, Arena& a) {
    std::vector<parser_types::TemplateArgument*> out; out.reserve(src.size());
    for (const auto* t : src) out.push_back(clone_targ(t, a));
    return out;
}

} // namespace nodes
} // namespace walnut
