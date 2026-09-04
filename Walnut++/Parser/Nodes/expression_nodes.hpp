#ifndef WALNUT_EXPRESSION_NODES_HPP
#define WALNUT_EXPRESSION_NODES_HPP

#include "main_nodes.hpp"

namespace walnut {
namespace nodes {

struct Literal : ASTNode {
    std::string_view value;
    tokenizing::Token::Kind token_kind;

    Literal(std::string_view val, tokenizing::Token::Kind k, std::uint32_t ln = 0) : ASTNode(Kind::Literal, ln), value(val), token_kind(k) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::Literal; }

    tokenizing::Token::Kind get_kind() const { return token_kind; }
    std::string_view lexeme() const { return tokenizing::Token::name_fast(token_kind); }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "Literal(" << tokenizing::Token::name_fast(token_kind) << "): " << value << "\n";
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<Literal>(a, value, token_kind, line); copy_base_to(c); return c; }
};

struct Identifier : ASTNode {
    std::string_view name;
    semantics::Symbol* resolved = nullptr; 

    Identifier(std::string_view n, std::uint32_t ln = 0) : ASTNode(Kind::Identifier, ln), name(n) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::Identifier; }

    std::string_view get_name() const { return name; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "Identifier: " << name << "\n";
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<Identifier>(a, name, line); copy_base_to(c); return c; }
};

struct QualifiedIdentifier : ASTNode {
    std::vector<std::string_view> m_parts;
    bool m_is_global;
    semantics::Symbol* resolved = nullptr; 

    QualifiedIdentifier(std::uint32_t ln, bool is_global = false) : ASTNode(Kind::QualifiedIdentifier, ln), m_is_global(is_global) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::QualifiedIdentifier; }

    void add_part(std::string_view part) { m_parts.push_back(part); }
    const std::vector<std::string_view>& parts() const { return m_parts; }
    bool is_global() const { return m_is_global; }
    bool is_qualified() const { return m_parts.size() > 1; }
    std::string_view simple_name() const { return m_parts.empty() ? "" : m_parts.back(); }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "QualifiedIdentifier: ";
        if (m_is_global) os << "::";
        for (std::size_t i = 0; i < m_parts.size(); ++i) {
            if (i > 0) os << "::";
            os << m_parts[i];
        }
        os << "\n";
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<QualifiedIdentifier>(a, line, m_is_global); copy_base_to(c); c->m_parts = m_parts; return c; }
};

struct BinaryExpression : ASTNode {
    ASTNode* left;
    tokenizing::Token::Kind op;
    ASTNode* right;
    semantics::Symbol* resolved = nullptr;

    BinaryExpression(ASTNode* l, tokenizing::Token::Kind o, ASTNode* r, std::uint32_t ln) : ASTNode(Kind::BinaryExpression, ln), left(l), op(o), right(r) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BinaryExpression; }

    const ASTNode* get_left() const { return left; }
    const ASTNode* get_right() const { return right; }
    tokenizing::Token::Kind get_operator() const { return op; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "BinaryExpression(" << tokenizing::Token::name_fast(op) << ")\n";
        print_node(left, os, indent + 1);
        print_node(right, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<BinaryExpression>(a, clone_child(left, a), op, clone_child(right, a), line); copy_base_to(c); return c; }
};

struct UnaryExpression : ASTNode {
    ASTNode* operand;
    tokenizing::Token::Kind op;
    bool prefix;
    semantics::Symbol* resolved = nullptr;

    UnaryExpression(ASTNode* expr, tokenizing::Token::Kind o, bool is_prefix, std::uint32_t ln) : ASTNode(Kind::UnaryExpression, ln), operand(expr), op(o), prefix(is_prefix) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::UnaryExpression; }

    const ASTNode* get_operand() const { return operand; }
    tokenizing::Token::Kind get_operator() const { return op; }
    bool is_prefix() const { return prefix; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "UnaryExpression(" << tokenizing::Token::name_fast(op) << ", " << (prefix ? "prefix" : "postfix") << ")\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<UnaryExpression>(a, clone_child(operand, a), op, prefix, line); copy_base_to(c); return c; }
};

struct DereferenceExpression : ASTNode {
    ASTNode* operand;
    semantics::Symbol* resolved = nullptr;

    DereferenceExpression(ASTNode* expr, std::uint32_t ln) : ASTNode(Kind::DereferenceExpression, ln), operand(expr) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::DereferenceExpression; }

    const ASTNode* get_operand() const { return operand; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "DereferenceExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<DereferenceExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct ReferenceExpression : ASTNode {
    ASTNode* operand;

    ReferenceExpression(ASTNode* expr, std::uint32_t ln) : ASTNode(Kind::ReferenceExpression, ln), operand(expr) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ReferenceExpression; }

    const ASTNode* get_operand() const { return operand; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ReferenceExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<ReferenceExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct BitwiseNotExpression : ASTNode {
    ASTNode* operand;

    BitwiseNotExpression(ASTNode* expr, std::uint32_t ln = 0) : ASTNode(Kind::BitwiseNotExpression, ln), operand(expr) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BitwiseNotExpression; }

    const ASTNode* get_operand() const { return operand; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "BitwiseNotExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<BitwiseNotExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct TernaryExpression : ASTNode {
    ASTNode* condition;
    ASTNode* true_branch;
    ASTNode* false_branch;

    TernaryExpression(ASTNode* cond, ASTNode* t, ASTNode* f, std::uint32_t ln) : ASTNode(Kind::TernaryExpression, ln), condition(cond), true_branch(t), false_branch(f) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TernaryExpression; }

    const ASTNode* get_condition() const { return condition; }
    const ASTNode* get_true_branch() const { return true_branch; }
    const ASTNode* get_false_branch() const { return false_branch; }

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<TernaryExpression>(a, clone_child(condition, a), clone_child(true_branch, a), clone_child(false_branch, a), line); copy_base_to(c); return c; }
};

struct CallExpression : ASTNode {
    ASTNode* m_callee;
    std::vector<ASTNode*> m_arguments;
    std::vector<parser_types::TemplateArgument*> m_template_args;
    semantics::Symbol* resolved = nullptr;

    CallExpression(ASTNode* callee, std::uint32_t ln = 0) : ASTNode(Kind::CallExpression, ln), m_callee(callee) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::CallExpression; }

    void add_argument(ASTNode* arg) { m_arguments.push_back(arg); }
    const ASTNode* get_callee() const { return m_callee; }
    const std::vector<ASTNode*>& get_arguments() const { return m_arguments; }
    std::size_t argument_count() const { return m_arguments.size(); }
    void set_template_args(std::vector<parser_types::TemplateArgument*> a) { m_template_args = std::move(a); }
    const std::vector<parser_types::TemplateArgument*>& get_template_args() const { return m_template_args; }
    bool has_template_args() const { return !m_template_args.empty(); }

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<CallExpression>(a, clone_child(m_callee, a), line);
        copy_base_to(c);
        c->m_arguments     = clone_list(m_arguments, a);
        c->m_template_args = clone_targs(m_template_args, a);
        return c;
    }
};

struct SubscriptExpression : ASTNode {
    ASTNode* m_array;
    ASTNode* m_index;
    semantics::Symbol* resolved = nullptr;

    SubscriptExpression(ASTNode* array, ASTNode* index, std::uint32_t ln) : ASTNode(Kind::SubscriptExpression, ln), m_array(array), m_index(index) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::SubscriptExpression; }

    const ASTNode* get_array() const { return m_array; }
    const ASTNode* get_index() const { return m_index; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "SubscriptExpression\n";
        print_indent(os, indent + 1);
        os << "Array:\n";
        print_node(m_array, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Index:\n";
        print_node(m_index, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<SubscriptExpression>(a, clone_child(m_array, a), clone_child(m_index, a), line); copy_base_to(c); return c; }
};

struct BraceInitializerList : ASTNode {
    std::vector<ASTNode*> m_elements;

    BraceInitializerList(std::uint32_t ln = 0) : ASTNode(Kind::BraceInitializerList, ln) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BraceInitializerList; }

    void add_element(ASTNode* element) { m_elements.push_back(element); }
    const std::vector<ASTNode*>& get_elements() const { return m_elements; }
    std::size_t size() const { return m_elements.size(); }
    bool empty() const { return m_elements.empty(); }

    const ASTNode* operator[](std::size_t index) const {
        return index < m_elements.size() ? m_elements[index] : nullptr;
    }

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<BraceInitializerList>(a, line); copy_base_to(c); c->m_elements = clone_list(m_elements, a); return c; }
};

struct MemberAccessExpression : ASTNode {
    enum class Op : std::uint8_t { Dot = 0, Arrow, Scope };

    ASTNode*         m_object;
    std::string_view m_member;
    Op               m_op;
    semantics::Symbol* resolved = nullptr; 

    MemberAccessExpression(ASTNode* object, std::string_view member, Op op, std::uint32_t ln) : ASTNode(Kind::MemberAccessExpression, ln), m_object(object), m_member(member), m_op(op) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::MemberAccessExpression; }

    const ASTNode* get_object() const { return m_object; }
    std::string_view get_member() const { return m_member; }
    Op   get_op()   const { return m_op; }
    bool is_dot()   const { return m_op == Op::Dot; }
    bool is_arrow() const { return m_op == Op::Arrow; }
    bool is_scope() const { return m_op == Op::Scope; }

    const char* op_spelling() const {
        switch (m_op) { case Op::Dot: return "."; case Op::Arrow: return "\u2192"; case Op::Scope: return "::"; }
        return "?";
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "MemberAccessExpression (" << op_spelling() << ")\n";
        print_indent(os, indent + 1);
        os << "Object:\n";
        print_node(m_object, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Member: " << m_member << "\n";
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<MemberAccessExpression>(a, clone_child(m_object, a), m_member, m_op, line); copy_base_to(c); return c; }
};

struct CastExpression : ASTNode {
    enum class CastKind : std::uint8_t { Static = 0, Dynamic, Reinterpret, Const, Bit };

    CastKind                        cast_kind;
    parser_types::TypeInfo          target;   
    ASTNode*                        operand;
    semantics::Symbol*              conversion = nullptr;

    CastExpression(CastKind ck, const parser_types::TypeInfo& tgt, ASTNode* op, std::uint32_t ln = 0) : ASTNode(Kind::CastExpression, ln), cast_kind(ck), target(tgt), operand(op) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::CastExpression; }

    CastKind get_cast_kind() const { return cast_kind; }
    const parser_types::TypeInfo& get_target() const { return target; }
    const ASTNode* get_operand() const { return operand; }
    semantics::Symbol* get_conversion() const { return conversion; }

    void set_conversion(semantics::Symbol* conv) { conversion = conv; }

    const char* cast_name() const {
        switch (cast_kind) {
            case CastKind::Static:      return "static_cast";
            case CastKind::Dynamic:     return "dynamic_cast";
            case CastKind::Reinterpret: return "reinterpret_cast";
            case CastKind::Const:       return "const_cast";
            case CastKind::Bit:         return "bit_cast";
        }
        return "unknown_cast";
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "CastExpression (" << cast_name() << ")\n";
        print_indent(os, indent + 1);
        os << "Target: " << target << "\n";
        print_indent(os, indent + 1);
        os << "Operand:\n";
        print_node(operand, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<CastExpression>(a, cast_kind, target.clone_into(a), clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct TemplateInstantiation : ASTNode {
    ASTNode*                                      m_template; 
    std::vector<parser_types::TemplateArgument*>  m_args;

    TemplateInstantiation(ASTNode* tmpl, std::vector<parser_types::TemplateArgument*> args, std::uint32_t ln = 0) : ASTNode(Kind::TemplateInstantiation, ln), m_template(tmpl), m_args(std::move(args)) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TemplateInstantiation; }

    const ASTNode* get_template() const { return m_template; }
    const std::vector<parser_types::TemplateArgument*>& get_args() const { return m_args; }

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<TemplateInstantiation>(a, clone_child(m_template, a), clone_targs(m_args, a), line); copy_base_to(c); return c; }
};

struct TypeQueryExpression : ASTNode {
    enum class Op : std::uint8_t { Sizeof = 0, Countof, Typeid, Typeof, Alignof, Decltype };

    Op                              op;
    parser_types::TemplateArgument* operand;  

    TypeQueryExpression(Op o, parser_types::TemplateArgument* operand_, std::uint32_t ln = 0) : ASTNode(Kind::TypeQuery, ln), op(o), operand(operand_) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TypeQuery; }

    Op get_op() const { return op; }
    const parser_types::TemplateArgument* get_operand() const { return operand; }

    const char* op_name() const {
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

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<TypeQueryExpression>(a, op, clone_targ(operand, a), line); copy_base_to(c); return c; }
};

struct NewExpression : ASTNode {
    parser_types::TypeInfo type;
    std::vector<ASTNode*>  args;        
    ASTNode*               array_size;  
    bool                   is_array;
    semantics::Symbol*     ctor  = nullptr;
    semantics::Symbol*     alloc = nullptr;

    NewExpression(const parser_types::TypeInfo& t, std::vector<ASTNode*> a, ASTNode* size, bool arr, std::uint32_t ln = 0) : ASTNode(Kind::NewExpression, ln), type(t), args(std::move(a)),array_size(size), is_array(arr) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::NewExpression; }

    void print(std::ostream& os, std::size_t indent) const {
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

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<NewExpression>(a, type.clone_into(a), clone_list(args, a), clone_child(array_size, a), is_array, line); copy_base_to(c); return c; }
};

struct DeleteExpression : ASTNode {
    ASTNode* operand;
    bool     is_array;
    semantics::Symbol* dealloc  = nullptr;

    DeleteExpression(ASTNode* operand_, bool arr, std::uint32_t ln = 0) : ASTNode(Kind::DeleteExpression, ln), operand(operand_), is_array(arr) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::DeleteExpression; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "DeleteExpression" << (is_array ? "[]" : "") << "\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<DeleteExpression>(a, clone_child(operand, a), is_array, line); copy_base_to(c); return c; }
};

struct NoexceptExpression : ASTNode {
    ASTNode* operand;

    NoexceptExpression(ASTNode* operand_, std::uint32_t ln = 0) : ASTNode(Kind::NoexceptExpression, ln), operand(operand_) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::NoexceptExpression; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "NoexceptExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<NoexceptExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct FoldExpression : ASTNode {
    enum class Form : std::uint8_t { UnaryLeft = 0, UnaryRight, Binary };

    Form                    form;
    tokenizing::Token::Kind op;
    ASTNode*                lhs;   
    ASTNode*                rhs;   

    FoldExpression(Form f, tokenizing::Token::Kind o, ASTNode* l, ASTNode* r, std::uint32_t ln = 0) : ASTNode(Kind::FoldExpression, ln), form(f), op(o), lhs(l), rhs(r) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::FoldExpression; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        const char* fn = form == Form::UnaryLeft ? "unary-left" : form == Form::UnaryRight ? "unary-right" : "binary";
        os << "FoldExpression (" << fn << ", op " << tokenizing::Token::name_fast(op) << ")\n";
        if (lhs) { print_indent(os, indent + 1); os << "Left:\n";  print_node(lhs, os, indent + 2); }
        print_indent(os, indent + 1); os << "...\n";
        if (rhs) { print_indent(os, indent + 1); os << "Right:\n"; print_node(rhs, os, indent + 2); }
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<FoldExpression>(a, form, op, clone_child(lhs, a), clone_child(rhs, a), line); copy_base_to(c); return c; }
};

struct AwaitExpression : ASTNode {
    ASTNode* operand;

    AwaitExpression(ASTNode* operand_, std::uint32_t ln = 0) : ASTNode(Kind::AwaitExpression, ln), operand(operand_) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::AwaitExpression; }

    const ASTNode* get_operand() const { return operand; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "AwaitExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<AwaitExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct CoYieldExpression : ASTNode {
    ASTNode* operand;

    CoYieldExpression(ASTNode* operand_, std::uint32_t ln = 0) : ASTNode(Kind::CoYieldExpression, ln), operand(operand_) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::CoYieldExpression; }

    const ASTNode* get_operand() const { return operand; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "CoYieldExpression\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<CoYieldExpression>(a, clone_child(operand, a), line); copy_base_to(c); return c; }
};

struct BraceConstructExpression : ASTNode {
    ASTNode*              m_callee;
    BraceInitializerList* m_init;

    BraceConstructExpression(ASTNode* callee, BraceInitializerList* init, std::uint32_t ln = 0)
        : ASTNode(Kind::BraceConstructExpression, ln), m_callee(callee), m_init(init) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BraceConstructExpression; }

    const ASTNode* get_callee() const { return m_callee; }
    const BraceInitializerList* get_init() const { return m_init; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "BraceConstructExpression\n";
        print_indent(os, indent + 1);
        os << "Callee:\n";
        print_node(m_callee, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Initializer:\n";
        print_node(m_init, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<BraceConstructExpression>(a, clone_child(m_callee, a), clone_typed(m_init, a), line); copy_base_to(c); return c; }
};

struct DiscardExpression : ASTNode {
    enum class DiscardKind : std::uint8_t { Const = 0, Nodiscard };

    DiscardKind kind_;
    ASTNode*    operand;

    DiscardExpression(DiscardKind dk, ASTNode* operand_, std::uint32_t ln = 0) : ASTNode(Kind::DiscardExpression, ln), kind_(dk), operand(operand_) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::DiscardExpression; }

    DiscardKind get_discard_kind() const { return kind_; }
    const ASTNode* get_operand() const { return operand; }

    const char* discard_name() const {
        switch (kind_) {
            case DiscardKind::Const:     return "discard_const";
            case DiscardKind::Nodiscard: return "discard_nodiscard";
        }
        return "unknown_discard";
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "DiscardExpression (" << discard_name() << ")\n";
        print_node(operand, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<DiscardExpression>(a, kind_, clone_child(operand, a), line); copy_base_to(c); return c; }
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_EXPRESSION_NODES_HPP