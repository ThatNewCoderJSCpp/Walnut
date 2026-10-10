#include "Parser/Nodes/expression_nodes.hpp"

namespace walnut {
namespace nodes {

void Literal::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "Literal(" << tokenizing::Token::name_fast(token_kind) << "): " << value << "\n";
}

void QualifiedIdentifier::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "QualifiedIdentifier: ";
    if (m_is_global) os << "::";
    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        if (i > 0) os << "::";
        os << m_parts[i];
    }
    os << "\n";
}

ASTNode* QualifiedIdentifier::clone_into(Arena& a) const { auto* c = make_in<QualifiedIdentifier>(a, line, m_is_global); copy_base_to(c); c->m_parts = m_parts; return c; }

void BinaryExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "BinaryExpression(" << tokenizing::Token::name_fast(op) << ")\n";
    print_node(left, os, indent + 1);
    print_node(right, os, indent + 1);
}

ASTNode* BinaryExpression::clone_into(Arena& a) const { auto* c = make_in<BinaryExpression>(a, clone_child(left, a), op, clone_child(right, a), line); copy_base_to(c); return c; }

void UnaryExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "UnaryExpression(" << tokenizing::Token::name_fast(op) << ", " << (prefix ? "prefix" : "postfix") << ")\n";
    print_node(operand, os, indent + 1);
}

ASTNode* UnaryExpression::clone_into(Arena& a) const { auto* c = make_in<UnaryExpression>(a, clone_child(operand, a), op, prefix, line); copy_base_to(c); return c; }

ASTNode* DereferenceExpression::clone_into(Arena& a) const { auto* c = make_in<DereferenceExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }

ASTNode* ReferenceExpression::clone_into(Arena& a) const { auto* c = make_in<ReferenceExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }

ASTNode* BitwiseNotExpression::clone_into(Arena& a) const { auto* c = make_in<BitwiseNotExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }

void TernaryExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TernaryExpression\n";
    print_indent(os, indent + 1);
    os << "Condition:\n";
    print_node(condition, os, indent + 2);
    print_indent(os, indent + 1);
    os << "True Branch:\n";
    print_node(true_branch, os, indent + 2);
    print_indent(os, indent + 1);
    os << "False Branch:\n";
    print_node(false_branch, os, indent + 2);
}

ASTNode* TernaryExpression::clone_into(Arena& a) const { auto* c = make_in<TernaryExpression>(a, clone_child(condition, a), clone_child(true_branch, a), clone_child(false_branch, a), line); copy_base_to(c); return c; }

void CallExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "CallExpression\n";
    print_indent(os, indent + 1);
    os << "Callee:\n";
    print_node(m_callee, os, indent + 2);

    if (!m_template_args.empty()) {
        print_indent(os, indent + 1);
        os << "Template Args (" << m_template_args.size() << "):\n";

        for (std::size_t i = 0; i < m_template_args.size(); ++i) {
            print_indent(os, indent + 2);
            os << "[" << i << "]:";
            const auto* a = m_template_args[i];
                
            if (a->is_type()) {
                os << " " << a->type << (a->is_pack ? " ..." : "") << "\n";
            } else {
                os << "\n";
                print_node(a->value, os, indent + 3);
                if (a->is_pack) { print_indent(os, indent + 3); os << "...(pack)\n"; }
            }
        }
    }

    print_indent(os, indent + 1);
    os << "Arguments (" << m_arguments.size() << "):\n";

    if (m_arguments.empty()) {
        print_indent(os, indent + 2);
        os << "<none>\n";
    } else {
        for (std::size_t i = 0; i < m_arguments.size(); ++i) {
            print_indent(os, indent + 2);
            os << "[" << i << "]:\n";
            print_node(m_arguments[i], os, indent + 3);
        }
    }
}

ASTNode* CallExpression::clone_into(Arena& a) const {
    auto* c = make_in<CallExpression>(a, clone_child(m_callee, a), line);
    copy_base_to(c);
    c->m_arguments     = clone_list(m_arguments, a);
    c->m_template_args = clone_targs(m_template_args, a);
    return c;
}

void SubscriptExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "SubscriptExpression\n";
    print_indent(os, indent + 1);
    os << "Array:\n";
    print_node(m_array, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Index:\n";
    print_node(m_index, os, indent + 2);
}

ASTNode* SubscriptExpression::clone_into(Arena& a) const { auto* c = make_in<SubscriptExpression>(a, clone_child(m_array, a), clone_child(m_index, a), line); copy_base_to(c); return c; }

void BraceInitializerList::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "BraceInitializerList (" << m_elements.size() << " elements)\n";

    for (std::size_t i = 0; i < m_elements.size(); ++i) {
        print_indent(os, indent + 1);
        os << "[" << i << "]:\n";
        print_node(m_elements[i], os, indent + 2);
    }

    if (m_elements.empty()) {
        print_indent(os, indent + 1);
        os << "<empty>\n";
    }
}

ASTNode* BraceInitializerList::clone_into(Arena& a) const { auto* c = make_in<BraceInitializerList>(a, line); copy_base_to(c); c->m_elements = clone_list(m_elements, a); return c; }

const char* MemberAccessExpression::op_spelling() const {
    switch (m_op) { case Op::Dot: return "."; case Op::Arrow: return "\u2192"; case Op::Scope: return "::"; }
    return "?";
}

void MemberAccessExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "MemberAccessExpression (" << op_spelling() << ")\n";
    print_indent(os, indent + 1);
    os << "Object:\n";
    print_node(m_object, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Member: " << m_member << "\n";
}

ASTNode* MemberAccessExpression::clone_into(Arena& a) const { auto* c = make_in<MemberAccessExpression>(a, clone_child(m_object, a), m_member, m_op, line); copy_base_to(c); return c; }

const char* CastExpression::cast_name() const {
    switch (cast_kind) {
        case CastKind::Static:      return "static_cast";
        case CastKind::Dynamic:     return "dynamic_cast";
        case CastKind::Reinterpret: return "reinterpret_cast";
        case CastKind::Const:       return "const_cast";
        case CastKind::Bit:         return "bit_cast";
    }
    return "unknown_cast";
}

void CastExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "CastExpression (" << cast_name() << ")\n";
    print_indent(os, indent + 1);
    os << "Target: " << target << "\n";
    print_indent(os, indent + 1);
    os << "Operand:\n";
    print_node(operand, os, indent + 2);
}

ASTNode* CastExpression::clone_into(Arena& a) const { auto* c = make_in<CastExpression>(a, cast_kind, target.clone_into(a), clone_child(operand, a), line); copy_base_to(c); return c; }

void TemplateInstantiation::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TemplateInstantiation\n";
    print_indent(os, indent + 1);
    os << "Template:\n";
    print_node(m_template, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Args (" << m_args.size() << "):\n";

    for (std::size_t i = 0; i < m_args.size(); ++i) {
        print_indent(os, indent + 2);
        os << "[" << i << "]:";
        const auto* a = m_args[i];
            
        if (a->is_type()) {
            os << " " << a->type << (a->is_pack ? " ..." : "") << "\n";
        } else {
            os << "\n";
            print_node(a->value, os, indent + 3);
            if (a->is_pack) { print_indent(os, indent + 3); os << "...(pack)\n"; }
        }
    }
}

ASTNode* TemplateInstantiation::clone_into(Arena& a) const { auto* c = make_in<TemplateInstantiation>(a, clone_child(m_template, a), clone_targs(m_args, a), line); copy_base_to(c); return c; }

const char* TypeQueryExpression::op_name() const {
    switch (op) {
        case Op::Sizeof:   return "sizeof";
        case Op::Countof:  return "countof";
        case Op::Typeid:   return "typeid";
        case Op::Typeof:   return "typeof";
        case Op::Alignof:  return "alignof";
        case Op::Decltype: return "decltype";
    }
    return "unknown";
}

void TypeQueryExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "TypeQueryExpression (" << op_name() << ")\n";
    print_indent(os, indent + 1);

    if (operand && operand->is_type()) {
        os << "Type: " << operand->type << (operand->is_pack ? " ..." : "") << "\n";
    } else if (operand) {
        os << "Operand:\n";
        print_node(operand->value, os, indent + 2);
    } else {
        os << "Operand: <none>\n";
    }
}

ASTNode* TypeQueryExpression::clone_into(Arena& a) const { auto* c = make_in<TypeQueryExpression>(a, op, clone_targ(operand, a), line); copy_base_to(c); return c; }

NewExpression::NewExpression(const parser_types::TypeInfo& t, std::vector<ASTNode*> a, ASTNode* size, bool arr, std::uint32_t ln) : ASTNode(Kind::NewExpression, ln), type(t), args(std::move(a)),array_size(size), is_array(arr) {}

void NewExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "NewExpression" << (is_array ? " [array]" : "") << "\n";
    print_indent(os, indent + 1);
    os << "Type: " << type << "\n";

    if (is_array) {
        print_indent(os, indent + 1);
        os << "Size:\n";
        print_node(array_size, os, indent + 2);
    }

    print_indent(os, indent + 1);
    os << "Args (" << args.size() << "):";

    if (args.empty()) { 
        os << " <none>\n"; 
    } else {
        os << "\n";
            
        for (std::size_t i = 0; i < args.size(); ++i) {
            print_indent(os, indent + 2);
            os << "[" << i << "]:\n";
            print_node(args[i], os, indent + 3);
        }
    }
}

ASTNode* NewExpression::clone_into(Arena& a) const { auto* c = make_in<NewExpression>(a, type.clone_into(a), clone_list(args, a), clone_child(array_size, a), is_array, line); copy_base_to(c); return c; }

void DeleteExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "DeleteExpression" << (is_array ? "[]" : "") << "\n";
    print_node(operand, os, indent + 1);
}

ASTNode* DeleteExpression::clone_into(Arena& a) const { auto* c = make_in<DeleteExpression>(a, clone_child(operand, a), is_array, line); copy_base_to(c); return c; }

ASTNode* NoexceptExpression::clone_into(Arena& a) const { auto* c = make_in<NoexceptExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }

void FoldExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    const char* fn = form == Form::UnaryLeft ? "unary-left" : form == Form::UnaryRight ? "unary-right" : "binary";
    os << "FoldExpression (" << fn << ", op " << tokenizing::Token::name_fast(op) << ")\n";
    if (lhs) { print_indent(os, indent + 1); os << "Left:\n";  print_node(lhs, os, indent + 2); }
    print_indent(os, indent + 1); os << "...\n";
    if (rhs) { print_indent(os, indent + 1); os << "Right:\n"; print_node(rhs, os, indent + 2); }
}

ASTNode* FoldExpression::clone_into(Arena& a) const { auto* c = make_in<FoldExpression>(a, form, op, clone_child(lhs, a), clone_child(rhs, a), line); copy_base_to(c); return c; }

ASTNode* CoYieldExpression::clone_into(Arena& a) const { auto* c = make_in<CoYieldExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }

void BraceConstructExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "BraceConstructExpression\n";
    print_indent(os, indent + 1);
    os << "Callee:\n";
    print_node(m_callee, os, indent + 2);
    print_indent(os, indent + 1);
    os << "Initializer:\n";
    print_node(m_init, os, indent + 2);
}

ASTNode* BraceConstructExpression::clone_into(Arena& a) const { auto* c = make_in<BraceConstructExpression>(a, clone_child(m_callee, a), clone_typed(m_init, a), line); copy_base_to(c); return c; }

const char* DiscardExpression::discard_name() const {
    switch (kind_) {
        case DiscardKind::Const:     return "discard_const";
        case DiscardKind::Nodiscard: return "discard_nodiscard";
    }
    return "unknown_discard";
}

void DiscardExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "DiscardExpression (" << discard_name() << ")\n";
    print_node(operand, os, indent + 1);
}

ASTNode* DiscardExpression::clone_into(Arena& a) const { auto* c = make_in<DiscardExpression>(a, kind_, clone_child(operand, a), line); copy_base_to(c); return c; }

void ThrowExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ThrowExpression" << (is_rethrow() ? " (rethrow)" : "") << "\n";
    if (operand) print_node(operand, os, indent + 1);
}

} // namespace nodes
} // namespace walnut
