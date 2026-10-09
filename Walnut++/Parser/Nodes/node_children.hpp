#ifndef WALNUT_AST_NODE_CHILDREN_HPP
#define WALNUT_AST_NODE_CHILDREN_HPP

#include "../nodes.hpp"

namespace walnut {
namespace nodes {

template <bool IncludeTemplateArgValues = false, typename Fn>
void for_each_child(ASTNode* n, Fn&& fn) {
    using K = ASTNode::Kind;
    if (!n) return;

    const auto yield = [&fn](ASTNode* c) { if (c) fn(c); };

    const auto yield_list = [&](const auto& list) {
        for (auto* c : list) if (c) fn(static_cast<ASTNode*>(c));
    };

    const auto yield_targs = [&](const std::vector<parser_types::TemplateArgument*>& args) {
        if constexpr (IncludeTemplateArgValues) {
            for (auto* a : args) if (a && a->is_value() && a->value) fn(a->value);
        } else {
            (void)args;
        }
    };

    const auto yield_params = [&](FunctionParameters* ps) {
        if (!ps) return;
        for (FunctionParameter* p : *ps) if (p) fn(static_cast<ASTNode*>(p));
    };

    switch (n->kind) {

        case K::Literal:
        case K::Identifier:
        case K::QualifiedIdentifier:
        case K::SingleStatement:           
        case K::TemplateParameter:        
            break;

        case K::UnaryExpression:       yield(static_cast<UnaryExpression*>(n)->operand);       break;
        case K::DereferenceExpression: yield(static_cast<DereferenceExpression*>(n)->operand); break;
        case K::ReferenceExpression:   yield(static_cast<ReferenceExpression*>(n)->operand);   break;
        case K::BitwiseNotExpression:  yield(static_cast<BitwiseNotExpression*>(n)->operand);  break;
        case K::CastExpression:        yield(static_cast<CastExpression*>(n)->operand);        break;
        case K::DeleteExpression:      yield(static_cast<DeleteExpression*>(n)->operand);      break;
        case K::NoexceptExpression:    yield(static_cast<NoexceptExpression*>(n)->operand);    break;
        case K::ThrowExpression:       yield(static_cast<ThrowExpression*>(n)->operand);       break;
        case K::DiscardExpression:     yield(static_cast<DiscardExpression*>(n)->operand);     break;
        case K::AwaitExpression:       yield(static_cast<AwaitExpression*>(n)->operand);       break;
        case K::CoYieldExpression:     yield(static_cast<CoYieldExpression*>(n)->operand);     break;
        case K::MemberAccessExpression:yield(static_cast<MemberAccessExpression*>(n)->m_object);break;

        // ---- multi-operand expressions ------------------------------------
        case K::BinaryExpression: {
            auto* b = static_cast<BinaryExpression*>(n);
            yield(b->left); yield(b->right);
            break;
        }

        case K::TernaryExpression: {
            auto* t = static_cast<TernaryExpression*>(n);
            yield(t->condition); yield(t->true_branch); yield(t->false_branch);
            break;
        }

        case K::SubscriptExpression: {
            auto* s = static_cast<SubscriptExpression*>(n);
            yield(s->m_array); yield(s->m_index);
            break;
        }

        case K::CallExpression: {
            auto* c = static_cast<CallExpression*>(n);
            yield(c->m_callee);
            yield_targs(c->m_template_args);
            yield_list(c->m_arguments);
            break;
        }

        case K::BraceInitializerList:
            yield_list(static_cast<BraceInitializerList*>(n)->m_elements);
            break;

        case K::BraceConstructExpression: {
            auto* b = static_cast<BraceConstructExpression*>(n);
            yield(b->m_callee); yield(b->m_init);
            break;
        }

        case K::NewExpression: {
            auto* e = static_cast<NewExpression*>(n);
            yield(e->array_size);
            yield_list(e->args);
            break;
        }

        case K::TemplateInstantiation: {
            auto* t = static_cast<TemplateInstantiation*>(n);
            yield(t->m_template);
            yield_targs(t->m_args);
            break;
        }

        case K::TypeQuery: {
            // sizeof / typeof / decltype / alignof over a type-or-value operand.
            auto* q = static_cast<TypeQueryExpression*>(n);
            if constexpr (IncludeTemplateArgValues) {
                if (q->operand && q->operand->is_value() && q->operand->value) fn(q->operand->value);
            }
            break;
        }

        case K::FoldExpression: {
            auto* f = static_cast<FoldExpression*>(n);
            yield(f->lhs); yield(f->rhs);
            break;
        }

        case K::RequiresExpression: {
            auto* r = static_cast<RequiresExpression*>(n);
            yield_params(r->m_parameters);
            for (auto& req : r->m_requirements) yield(req.expr);
            break;
        }

        case K::LambdaExpression: {
            // The body is a structural child, but it does not RUN here. Visitors
            // that model execution must treat this kind as a barrier.
            auto* l = static_cast<LambdaExpression*>(n);
            yield_params(l->m_parameters);
            yield(l->m_body);
            break;
        }

        // ---- statements ----------------------------------------------------
        case K::ExpressionStatement:
            yield(static_cast<ExpressionStatement*>(n)->expr);
            break;

        case K::BlockStatement:
            yield_list(static_cast<BlockStatement*>(n)->get_statements());
            break;

        case K::IfBranch: {
            auto* b = static_cast<IfBranch*>(n);
            yield(b->condition); yield(b->body);
            break;
        }

        case K::IfStatement: {
            auto* s = static_cast<IfStatement*>(n);
            yield_list(s->branches);
            yield(s->else_branch);
            break;
        }

        case K::ForStatement: {
            auto* s = static_cast<ForStatement*>(n);
            yield(s->var_init); yield(s->initializer);
            yield(s->condition); yield(s->increment);
            yield(s->body);
            break;
        }

        case K::ForEachStatement: {
            auto* s = static_cast<ForEachStatement*>(n);
            yield(s->m_container); yield(s->m_body);
            break;
        }

        case K::WhileStatement: {
            auto* s = static_cast<WhileStatement*>(n);
            yield(s->condition); yield(s->body);
            break;
        }

        case K::DoWhileStatement: {
            auto* s = static_cast<DoWhileStatement*>(n);
            yield(s->body); yield(s->condition);      // body first — it runs first
            break;
        }

        case K::SwitchCase: {
            auto* c = static_cast<SwitchCase*>(n);
            yield(c->value);
            yield_list(c->body);
            break;
        }

        case K::SwitchStatement: {
            auto* s = static_cast<SwitchStatement*>(n);
            yield(s->condition);
            yield_list(s->cases);
            break;
        }

        case K::TryCatchStatement: {
            auto* t = static_cast<TryCatchStatement*>(n);
            yield(t->try_body); yield(t->catch_body);
            break;
        }

        case K::ReturnStatement:
            yield(static_cast<ReturnStatement*>(n)->m_value);
            break;

        case K::CoReturnStatement:
            yield(static_cast<CoReturnStatement*>(n)->m_value);
            break;

        case K::StaticAssertDeclaration:
            yield(static_cast<StaticAssertDeclaration*>(n)->constraint);
            break;

        case K::ConceptDeclaration:
            yield(static_cast<ConceptDeclaration*>(n)->constraint);
            break;

        // ---- declarations --------------------------------------------------
        case K::VariableDeclaration:
            yield(static_cast<VariableDeclaration*>(n)->m_initializer);
            break;

        case K::ArrayDeclaration: {
            auto* a = static_cast<ArrayDeclaration*>(n);
            yield(a->m_dimension_expr); yield(a->m_initializer);
            break;
        }

        case K::FunctionParameter:
            yield(static_cast<FunctionParameter*>(n)->m_initializer);
            break;

        case K::FunctionParameters:
            yield_params(static_cast<FunctionParameters*>(n));
            break;

        case K::FunctionDeclaration: {
            auto* f = static_cast<FunctionDeclaration*>(n);
            yield_params(f->get_parameters());
            yield(f->get_body());
            break;
        }

        case K::OperatorFunctionDeclaration: {
            auto* f = static_cast<OperatorFunctionDeclaration*>(n);
            yield_params(f->get_parameters());
            yield(f->get_body());
            break;
        }

        case K::ConstructorDeclaration: {
            auto* c = static_cast<ConstructorDeclaration*>(n);
            yield_params(c->get_parameters());
            yield_list(c->get_init_list());          // member/base initializers run first
            yield(c->get_body());
            break;
        }

        case K::DestructorDeclaration:
            yield(static_cast<DestructorDeclaration*>(n)->get_body());
            break;

        case K::RecordDeclaration:
            for (const auto& m : static_cast<RecordDeclaration*>(n)->get_members()) yield(m.node);
            break;

        case K::EnumValue:
            yield(static_cast<EnumValue*>(n)->initializer);
            break;

        case K::EnumDeclaration:
            yield_list(static_cast<EnumDeclaration*>(n)->values);
            break;

        case K::NamespaceDeclaration:
            yield(static_cast<NamespaceDeclaration*>(n)->m_body);
            break;

        case K::UsingDeclaration: {
            auto* u = static_cast<UsingDeclaration*>(n);
            if (u->is_alias()) yield(u->aliased_expr);
            break;
        }

        case K::TemplateDeclaration: {
            auto* t = static_cast<TemplateDeclaration*>(n);
            for (TemplateParameter* p : t->params()) if (p) yield(p->m_default_value);
            yield(t->m_declaration);
            break;
        }

        case K::ModuleDeclaration:
            for (const auto& it : static_cast<ModuleDeclaration*>(n)->get_items()) yield(it.decl);
            break;

        case K::ImportExportDeclaration:
            for (const auto& it : static_cast<ImportExportDeclaration*>(n)->get_items()) yield(it.decl);
            break;
    }
}

template <bool IncludeTemplateArgValues = false, typename Fn>
void for_each_child(const ASTNode* n, Fn&& fn) {
    for_each_child<IncludeTemplateArgValues>(const_cast<ASTNode*>(n),
        [&fn](ASTNode* c) { fn(static_cast<const ASTNode*>(c)); });
}

template <bool IncludeTemplateArgValues = false, typename Fn>
void walk_tree(ASTNode* n, Fn&& fn) {
    if (!n) return;

    if constexpr (std::is_same_v<decltype(fn(n)), bool>) {
        if (!fn(n)) return;
    } else {
        fn(n);
    }

    for_each_child<IncludeTemplateArgValues>(n, [&](ASTNode* c) {
        walk_tree<IncludeTemplateArgValues>(c, fn);
    });
}

} // namespace nodes
} // namespace walnut

#endif