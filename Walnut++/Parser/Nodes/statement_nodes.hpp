#ifndef WALNUT_STATEMENT_NODES_HPP
#define WALNUT_STATEMENT_NODES_HPP

#include "expression_nodes.hpp"

namespace walnut {
namespace nodes {

struct ExpressionStatement : ASTNode {
    ASTNode* expr;

    ExpressionStatement(ASTNode* e) : ASTNode(Kind::ExpressionStatement), expr(e) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ExpressionStatement; }

    const ASTNode* get_expression() const { return expr; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ExpressionStatement\n";
        print_node(expr, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<ExpressionStatement>(a, clone_child(expr, a)); copy_base_to(c); return c; }
};

struct VariableDeclaration : ASTNode {
    parser_types::TypeInfo m_type_info;
    std::string_view m_name;
    SmallVector<std::string_view, 4> m_bindings;
    ASTNode* m_initializer;
    semantics::Symbol* symbol = nullptr; 
    SmallVector<semantics::Symbol*, 4> binding_symbols;

    VariableDeclaration(const parser_types::TypeInfo& type_info, std::string_view name, ASTNode* init = nullptr, std::uint32_t ln = 0)
        : ASTNode(Kind::VariableDeclaration, ln), m_type_info(type_info), m_name(name), m_initializer(init) {}

    VariableDeclaration(const parser_types::TypeInfo& type_info, SmallVector<std::string_view, 4> bindings, ASTNode* init, std::uint32_t ln = 0)
        : ASTNode(Kind::VariableDeclaration, ln), m_type_info(type_info), m_bindings(std::move(bindings)), m_initializer(init) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::VariableDeclaration; }

    const parser_types::TypeInfo& get_type_info() const { return m_type_info; }
    parser_types::TypeInfo& get_type_info() { return m_type_info; }
    std::string_view get_name() const { return m_name; }
    bool is_structured_binding() const { return !m_bindings.empty(); }
    const SmallVector<std::string_view, 4>& get_bindings() const { return m_bindings; }
    const SmallVector<semantics::Symbol*, 4>& get_binding_symbols() const { return binding_symbols; }
    const ASTNode* get_initializer() const { return m_initializer; }
    bool has_initializer() const { return m_initializer != nullptr; }
    std::uint32_t get_line() const { return line; }
    bool is_pointer() const { return m_type_info.has_pointer(); }
    bool is_reference() const { return m_type_info.has_reference(); }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "VariableDeclaration\n";
        print_indent(os, indent + 1);
        
        if (is_structured_binding()) {
            os << "Bindings: [";
            for (std::size_t i = 0; i < m_bindings.size(); ++i) { if (i) { os << ", "; } os << m_bindings[i]; }
            os << "]";
        } else {
            os << "Name: " << m_name;
        }

        os << "\n";
        print_indent(os, indent + 1);
        os << "Type: " << m_type_info << "\n";
        print_indent(os, indent + 1);
        os << "Initializer:";

        if (!m_initializer) {
            os << " <none>\n";
        } else {
            os << "\n";
            print_node(m_initializer, os, indent + 2);
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        VariableDeclaration* c;
        if (is_structured_binding()) c = make_in<VariableDeclaration>(a, m_type_info.clone_into(a), m_bindings, clone_child(m_initializer, a), line);
        else                         c = make_in<VariableDeclaration>(a, m_type_info.clone_into(a), m_name,     clone_child(m_initializer, a), line);
        copy_base_to(c);
        return c;
    }
};

struct BlockStatement : ASTNode {
    std::vector<ASTNode*> statements;
    semantics::Scope* scope = nullptr;

    BlockStatement() : ASTNode(Kind::BlockStatement) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BlockStatement; }

    void add_statement(ASTNode* stmt) { statements.push_back(stmt); }
    const std::vector<ASTNode*>& get_statements() const { return statements; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "Block\n";
        for (const auto& stmt : statements) { print_node(stmt, os, indent + 1); }
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<BlockStatement>(a); copy_base_to(c); c->statements = clone_list(statements, a); return c; }
};

struct IfBranch : ASTNode {
    ASTNode* condition;
    ASTNode* body;

    IfBranch(ASTNode* cond, ASTNode* b) : ASTNode(Kind::IfBranch), condition(cond), body(b) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::IfBranch; }

    const ASTNode* get_condition() const { return condition; }
    const ASTNode* get_body() const { return body; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "If Branch\n";
        print_indent(os, indent);
        os << "Condition:\n";
        print_node(condition, os, indent + 1);
        print_indent(os, indent);
        os << "Body:\n";
        print_node(body, os, indent + 1);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<IfBranch>(a, clone_child(condition, a), clone_child(body, a)); copy_base_to(c); return c; }
};

struct IfStatement : ASTNode {
    std::vector<IfBranch*> branches;
    ASTNode* else_branch = nullptr;

    IfStatement() : ASTNode(Kind::IfStatement) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::IfStatement; }

    void add_branch(IfBranch* branch) { branches.push_back(branch); }
    void set_else(ASTNode* else_body) { else_branch = else_body; }
    const std::vector<IfBranch*>& get_branches() const { return branches; }
    const ASTNode* get_else() const { return else_branch; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "IfStatement\n";

        for (std::size_t i = 0; i < branches.size(); ++i) {
            print_indent(os, indent + 1);
            os << (i == 0 ? "If" : "Else If") << " Branch:\n";
            branches[i]->print(os, indent + 2);
        }

        if (else_branch) {
            print_indent(os, indent + 1);
            os << "Else Branch:\n";
            print_node(else_branch, os, indent + 2);
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<IfStatement>(a);
        copy_base_to(c);
        c->branches    = clone_typed_list(branches, a);
        c->else_branch = clone_child(else_branch, a);
        return c;
    }
};

struct NamespaceDeclaration : ASTNode {
    std::vector<std::string_view> m_name_parts;
    BlockStatement* m_body;
    semantics::Symbol* symbol = nullptr; 

    NamespaceDeclaration(
        std::vector<std::string_view> name_parts,
        BlockStatement* body,
        std::uint32_t ln
    )
        : ASTNode(Kind::NamespaceDeclaration, ln)
        , m_name_parts(std::move(name_parts))
        , m_body(body)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::NamespaceDeclaration; }

    const std::vector<std::string_view>& name_parts() const { return m_name_parts; }
    std::string_view simple_name() const { return m_name_parts.empty() ? "" : m_name_parts.back(); }
    bool is_qualified() const { return m_name_parts.size() > 1; }
    const BlockStatement* get_body() const { return m_body; }
    BlockStatement* get_body() { return m_body; }
    std::uint32_t get_line() const { return line; }

    std::string full_name() const {
        std::string result;

        for (std::size_t i = 0; i < m_name_parts.size(); ++i) {
            if (i > 0) result += "::";
            result += m_name_parts[i];
        }

        return result;
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "NamespaceDeclaration\n";
        print_indent(os, indent + 1);
        os << "Name: " << full_name() << "\n";
        print_indent(os, indent + 1);
        os << "Body:";

        if (!m_body) { 
            os << " <none>\n"; 
        } else {
            os << "\n";
            m_body->print(os, indent + 2);
        }
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<NamespaceDeclaration>(a, m_name_parts, clone_typed(m_body, a), line); copy_base_to(c); return c; }
};

struct ForStatement : ASTNode {
    ASTNode* initializer = nullptr;
    ASTNode* condition = nullptr;
    ASTNode* increment = nullptr;
    ASTNode* body = nullptr;
    ASTNode* var_init = nullptr;
    semantics::Scope* scope = nullptr;
    bool has_var_init;

    ForStatement(ASTNode* init, ASTNode* cond, ASTNode* inc, ASTNode* b)
        : ASTNode(Kind::ForStatement)
        , initializer(init), condition(cond), increment(inc)
        , body(b), has_var_init(false) {}

    ForStatement(ASTNode* var_initializer, std::nullptr_t, ASTNode* cond, ASTNode* inc, ASTNode* b)
        : ASTNode(Kind::ForStatement)
        , condition(cond), increment(inc), body(b)
        , var_init(var_initializer), has_var_init(true) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ForStatement; }

    const ASTNode* get_initializer() const { return initializer; }
    const ASTNode* get_var_initializer() const { return var_init; }
    const ASTNode* get_condition() const { return condition; }
    const ASTNode* get_increment() const { return increment; }
    const ASTNode* get_body() const { return body; }
    const semantics::Scope* get_scope() const { return scope; }
    semantics::Scope* get_scope() { return scope; }
    void set_scope(semantics::Scope* s) { scope = s; }
    bool has_variable_initializer() const { return has_var_init; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ForStatement\n";
        print_indent(os, indent + 1);
        os << "Initializer:\n";

        if (has_var_init) {
            print_node(var_init, os, indent + 2);
        } else if (initializer) {
            print_node(initializer, os, indent + 2);
        } else {
            print_indent(os, indent + 2);
            os << "<empty>\n";
        }

        print_indent(os, indent + 1);
        os << "Condition:\n";

        if (condition) {
            print_node(condition, os, indent + 2);
        } else {
            print_indent(os, indent + 2);
            os << "<empty>\n";
        }

        print_indent(os, indent + 1);
        os << "Increment:\n";

        if (increment) {
            print_node(increment, os, indent + 2);
        } else {
            print_indent(os, indent + 2);
            os << "<empty>\n";
        }

        print_indent(os, indent + 1);
        os << "Body:\n";
        print_node(body, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override {
        ForStatement* c;
        if (has_var_init) c = make_in<ForStatement>(a, clone_child(var_init, a), nullptr, clone_child(condition, a), clone_child(increment, a), clone_child(body, a));
        else              c = make_in<ForStatement>(a, clone_child(initializer, a), clone_child(condition, a), clone_child(increment, a), clone_child(body, a));
        copy_base_to(c);
        return c;
    }
};

struct ForEachStatement : ASTNode {
    parser_types::TypeInfo  m_element_type;
    modifiers::RawModifiers m_modifiers;
    std::string_view        m_var_name;
    SmallVector<std::string_view, 4> m_bindings;
    ASTNode*                m_container;
    ASTNode*                m_body;
    semantics::Symbol* symbol = nullptr; 

    ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, std::string_view var_name, ASTNode* container, ASTNode* body, std::uint32_t ln = 0)
        : ASTNode(Kind::ForEachStatement, ln), m_element_type(element_type), m_modifiers(mods)
        , m_var_name(var_name), m_container(container), m_body(body) {}

    ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, SmallVector<std::string_view, 4> bindings, ASTNode* container, ASTNode* body, std::uint32_t ln = 0)
        : ASTNode(Kind::ForEachStatement, ln), m_element_type(element_type), m_modifiers(mods)
        , m_bindings(std::move(bindings)), m_container(container), m_body(body) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ForEachStatement; }

    const parser_types::TypeInfo&  get_element_type()  const { return m_element_type; }
    parser_types::TypeInfo&        get_element_type()        { return m_element_type; }
    const modifiers::RawModifiers& get_modifiers()     const { return m_modifiers;    }
    std::string_view               get_variable_name() const { return m_var_name;     }
    bool is_structured_binding() const { return !m_bindings.empty(); }
    const SmallVector<std::string_view, 4>& get_bindings() const { return m_bindings; }
    const ASTNode*                 get_container()     const { return m_container;    }
    ASTNode*                       get_container()           { return m_container;    }
    const ASTNode*                 get_body()          const { return m_body;         }
    ASTNode*                       get_body()                { return m_body;         }
    std::uint32_t                  get_line()          const { return line;           }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ForEachStatement\n";
        print_indent(os, indent + 1);

        if (is_structured_binding()) {
            os << "Bindings: [";
            for (std::size_t i = 0; i < m_bindings.size(); ++i) { if (i) { os << ", "; } os << m_bindings[i]; }
            os << "]\n";
        } else {
            os << "Variable: " << m_var_name << "\n";
        }

        print_indent(os, indent + 1);
        os << "Type: " << m_element_type << "\n";
        print_indent(os, indent + 1);
        os << "Modifiers: " << m_modifiers << "\n";
        print_indent(os, indent + 1);
        os << "Container:\n";
        print_node(m_container, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Body:\n";
        print_node(m_body, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override {
        ForEachStatement* c;
        if (is_structured_binding()) c = make_in<ForEachStatement>(a, m_element_type.clone_into(a), m_modifiers, m_bindings, clone_child(m_container, a), clone_child(m_body, a), line);
        else                         c = make_in<ForEachStatement>(a, m_element_type.clone_into(a), m_modifiers, m_var_name, clone_child(m_container, a), clone_child(m_body, a), line);
        copy_base_to(c);
        return c;
    }
};

struct WhileStatement : ASTNode {
    ASTNode* body;
    ASTNode* condition;

    WhileStatement(ASTNode* b, ASTNode* c) : ASTNode(Kind::WhileStatement), body(b), condition(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::WhileStatement; }

    const ASTNode* get_body() const { return body; }
    const ASTNode* get_condition() const { return condition; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "WhileStatement\n";
        print_indent(os, indent + 1);
        os << "Condition:\n";
        print_node(condition, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Body:\n";
        print_node(body, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<WhileStatement>(a, clone_child(body, a), clone_child(condition, a)); copy_base_to(c); return c; }
};

struct ArrayDeclaration : ASTNode {
    std::string_view m_name;
    parser_types::TypeInfo m_element_type;
    modifiers::RawModifiers m_array_modifiers;
    std::optional<std::size_t> m_dimension;
    ASTNode* m_dimension_expr;
    BraceInitializerList* m_initializer;
    parser_types::TemplateArgument* m_alignment = nullptr;
    semantics::Symbol* symbol = nullptr; 

    ArrayDeclaration(
        std::string_view name,
        const parser_types::TypeInfo& element_type,
        const modifiers::RawModifiers& array_mods,
        std::uint32_t ln,
        std::optional<std::size_t> dimension = std::nullopt,
        ASTNode* dimension_expr = nullptr,
        BraceInitializerList* initializer = nullptr
    )
        : ASTNode(Kind::ArrayDeclaration, ln)
        , m_name(name)
        , m_element_type(element_type)
        , m_array_modifiers(array_mods)
        , m_dimension(dimension)
        , m_dimension_expr(dimension_expr)
        , m_initializer(initializer)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ArrayDeclaration; }

    std::string_view get_name() const { return m_name; }
    const parser_types::TypeInfo& get_element_type() const { return m_element_type; }
    parser_types::TypeInfo& get_element_type() { return m_element_type; }
    const modifiers::RawModifiers& get_array_modifiers() const { return m_array_modifiers; }
    modifiers::RawModifiers& get_array_modifiers() { return m_array_modifiers; }

    std::optional<std::size_t> get_dimension() const { return m_dimension; }
    const ASTNode* get_dimension_expr() const { return m_dimension_expr; }

    void set_alignment(parser_types::TemplateArgument* a) { m_alignment = a; }
    const parser_types::TemplateArgument* get_alignment() const { return m_alignment; }

    bool has_static_dimension()   const { return m_dimension.has_value(); }
    bool has_dynamic_dimension()  const { return m_dimension_expr != nullptr; }
    bool has_inferred_dimension() const { return !m_dimension.has_value() && m_dimension_expr == nullptr; }

    bool has_initializer() const { return m_initializer != nullptr; }
    const BraceInitializerList* get_initializer() const { return m_initializer; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ArrayDeclaration\n";
        print_indent(os, indent + 1);
        os << "Name: " << m_name << "\n";
        print_indent(os, indent + 1);
        os << "Element Type: " << m_element_type << "\n";
        print_indent(os, indent + 1);
        os << "Array Modifiers: ";

        if (!m_array_modifiers.is_modified()) {
            os << "<none>\n";
        } else {
            os << m_array_modifiers << "\n";
        }

        print_indent(os, indent + 1);
        os << "Alignas: " << (m_alignment ? (m_alignment->is_type() ? "<type>" : "<value>") : "<none>") << "\n";
        print_indent(os, indent + 1);
        os << "Dimension: ";

        if (m_dimension.has_value()) {
            os << m_dimension.value() << "\n";
        } else if (m_dimension_expr) {
            os << "<dynamic>\n";
            print_node(m_dimension_expr, os, indent + 2);
        } else {
            os << "<inferred>\n";
        }

        print_indent(os, indent + 1);
        os << "Initializer:";
        
        if (!m_initializer) { 
            os << " <none>\n"; 
        } else {
            os << "\n";
            m_initializer->print(os, indent + 2);
        } 
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<ArrayDeclaration>(
            a, m_name, 
            m_element_type.clone_into(a), 
            m_array_modifiers, 
            line,
            m_dimension, 
            clone_child(m_dimension_expr, a), 
            clone_typed(m_initializer, a)
        );
        
        copy_base_to(c);
        c->m_alignment = clone_targ(m_alignment, a);
        return c;
    }
};

struct SingleStatement : ASTNode {
    enum class Variant : std::uint8_t { Break = 0, Continue, Fallthrough, Repeat };

    Variant variant;

    SingleStatement(Variant v, std::uint32_t ln = 0) : ASTNode(Kind::SingleStatement, ln), variant(v) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::SingleStatement; }

    const char* variant_name() const {
        switch (variant) {
            case Variant::Break:       return "break";
            case Variant::Continue:    return "continue";
            case Variant::Fallthrough: return "fallthrough";
            case Variant::Repeat:      return "repeat";
        }
        return "unknown";
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "SingleStatement (" << variant_name() << ")\n";
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<SingleStatement>(a, variant, line); copy_base_to(c); return c; }
};

struct DoWhileStatement : ASTNode {
    ASTNode* body;
    ASTNode* condition;

    DoWhileStatement(ASTNode* b, ASTNode* c, std::uint32_t ln = 0) : ASTNode(Kind::DoWhileStatement, ln), body(b), condition(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::DoWhileStatement; }

    const ASTNode* get_body() const { return body; }
    const ASTNode* get_condition() const { return condition; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "DoWhileStatement\n";
        print_indent(os, indent + 1);
        os << "Body:\n";
        print_node(body, os, indent + 2);
        print_indent(os, indent + 1);
        os << "Condition:\n";
        print_node(condition, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<DoWhileStatement>(a, clone_child(body, a), clone_child(condition, a), line); copy_base_to(c); return c; }
};

struct SwitchCase : ASTNode {
    ASTNode* value; // nullptr for default case
    std::vector<ASTNode*> body;
    bool has_fallthrough;
    semantics::Symbol* symbol = nullptr; 

    SwitchCase(ASTNode* val, std::uint32_t ln = 0) : ASTNode(Kind::SwitchCase, ln), value(val), has_fallthrough(false) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::SwitchCase; }

    bool is_default() const { return value == nullptr; }
    const ASTNode* get_value() const { return value; }
    const std::vector<ASTNode*>& get_body() const { return body; }
    bool falls_through() const { return has_fallthrough; }

    void add_statement(ASTNode* stmt) { body.push_back(stmt); }
    void set_fallthrough(bool ft) { has_fallthrough = ft; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << (is_default() ? "DefaultCase" : "SwitchCase") << "\n";

        if (value) {
            print_indent(os, indent + 1);
            os << "Value:\n";
            print_node(value, os, indent + 2);
        }

        print_indent(os, indent + 1);
        os << "Body:\n";
        for (const auto& stmt : body) { print_node(stmt, os, indent + 2); }
        print_indent(os, indent + 1);
        os << "Fallthrough: " << (has_fallthrough ? "true" : "false") << "\n";
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<SwitchCase>(a, clone_child(value, a), line);
        copy_base_to(c);
        c->body = clone_list(body, a);
        c->has_fallthrough = has_fallthrough;
        return c;
    }
};

struct SwitchStatement : ASTNode {
    ASTNode* condition;
    std::vector<SwitchCase*> cases;
    semantics::Scope* scope = nullptr;

    SwitchStatement(ASTNode* cond, std::uint32_t ln = 0) : ASTNode(Kind::SwitchStatement, ln), condition(cond) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::SwitchStatement; }

    const ASTNode* get_condition() const { return condition; }
    ASTNode* get_condition() { return condition; }
    const std::vector<SwitchCase*>& get_cases() const { return cases; }
    const semantics::Scope* get_scope() const { return scope; }
    semantics::Scope* get_scope() { return scope; }
    void set_scope(semantics::Scope* s) { scope = s; }
    void add_case(SwitchCase* c) { cases.push_back(c); }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "SwitchStatement\n";
        print_indent(os, indent + 1);
        os << "Condition:\n";
        print_node(condition, os, indent + 2);
        for (const auto& c : cases) { c->print(os, indent + 1); }
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<SwitchStatement>(a, clone_child(condition, a), line);
        copy_base_to(c);
        c->cases = clone_typed_list(cases, a);
        return c;
    }
};

struct TryCatchStatement : ASTNode {
    BlockStatement* try_body;
    std::string_view catch_name;         
    parser_types::TypeInfo catch_type;  
    bool has_typed_catch;                
    BlockStatement* catch_body;

    TryCatchStatement(
        BlockStatement* tb,
        std::string_view cn,
        const parser_types::TypeInfo& ct,
        bool typed,
        BlockStatement* cb,
        std::uint32_t ln = 0
    )
        : ASTNode(Kind::TryCatchStatement, ln)
        , try_body(tb), catch_name(cn), catch_type(ct)
        , has_typed_catch(typed), catch_body(cb)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TryCatchStatement; }

    const BlockStatement* get_try_body() const { return try_body; }
    BlockStatement* get_try_body() { return try_body; }
    const BlockStatement* get_catch_body() const { return catch_body; }
    BlockStatement* get_catch_body() { return catch_body; }
    std::string_view get_catch_name() const { return catch_name; }
    const parser_types::TypeInfo& get_catch_type() const { return catch_type; }
    bool is_typed_catch() const { return has_typed_catch; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "TryCatchStatement\n";
        print_indent(os, indent + 1);
        os << "Try Body:\n";
        try_body->print(os, indent + 2);
        print_indent(os, indent + 1);
        os << "Catch";

        if (has_typed_catch) {
            os << " (" << catch_type << " " << catch_name << "):\n";
        } else {
            os << " (" << catch_name << "):\n";
        }

        catch_body->print(os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<TryCatchStatement>(a, clone_typed(try_body, a), catch_name, catch_type.clone_into(a), has_typed_catch, clone_typed(catch_body, a), line);
        copy_base_to(c);
        return c;
    }
};

struct EnumValue : ASTNode {
    std::string_view name;
    ASTNode* initializer; 
    semantics::Symbol* symbol = nullptr; 

    EnumValue(std::string_view n, ASTNode* init = nullptr, std::uint32_t ln = 0) : ASTNode(Kind::EnumValue, ln), name(n), initializer(init) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::EnumValue; }

    std::string_view get_name() const { return name; }
    const ASTNode* get_initializer() const { return initializer; }
    bool has_initializer() const { return initializer != nullptr; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "EnumValue\n";
        print_indent(os, indent + 1);
        os << "Name: " << name << "\n";
        print_indent(os, indent + 1);
        os << "Initializer:";

        if (!initializer) {
            os << " <none>\n"; 
        } else {
            os << "\n";
            print_node(initializer, os, indent + 2);
        }
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<EnumValue>(a, name, clone_child(initializer, a), line); copy_base_to(c); return c; }
};

struct EnumDeclaration : ASTNode {
    std::string_view name;
    parser_types::TypeInfo underlying_type; 
    bool has_underlying_type;
    std::vector<EnumValue*> values;
    modifiers::RawModifiers m_modifiers;
    semantics::Symbol* symbol = nullptr; 

    EnumDeclaration(
        std::string_view n,
        std::uint32_t ln = 0
    )
        : ASTNode(Kind::EnumDeclaration, ln)
        , name(n)
        , has_underlying_type(false)
    {}

    EnumDeclaration(
        std::string_view n,
        const parser_types::TypeInfo& ut,
        std::uint32_t ln = 0
    )
        : ASTNode(Kind::EnumDeclaration, ln)
        , name(n)
        , underlying_type(ut)
        , has_underlying_type(true)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::EnumDeclaration; }

    std::string_view get_name() const { return name; }
    bool has_type() const { return has_underlying_type; }
    const parser_types::TypeInfo& get_underlying_type() const { return underlying_type; }
    const std::vector<EnumValue*>& get_values() const { return values; }
    const modifiers::RawModifiers& get_modifiers() const { return m_modifiers; }
    modifiers::RawModifiers&       get_modifiers()       { return m_modifiers; }
    void set_modifiers(const modifiers::RawModifiers& m) { m_modifiers = m; }

    void add_value(EnumValue* v) { values.push_back(v); }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "EnumDeclaration\n";
        print_indent(os, indent + 1);
        os << "Name: " << name << "\n";
        print_indent(os, indent + 1);
        os << "Underlying Type: ";

        if (has_underlying_type) {
            os << underlying_type << "\n";
        } else {
            os << "<default>\n";
        }

        print_indent(os, indent + 1);
        os << "Modifiers: " << m_modifiers << "\n";
        print_indent(os, indent + 1);
        os << "Values:";

        if (values.empty()) { 
            os << " <none>\n"; 
        } else {
            os << "\n";
            for (auto* v : values) { v->print(os, indent + 2); }
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        EnumDeclaration* c;
        if (has_underlying_type) c = make_in<EnumDeclaration>(a, name, underlying_type.clone_into(a), line);
        else                     c = make_in<EnumDeclaration>(a, name, line);
        copy_base_to(c);
        c->m_modifiers = m_modifiers;
        c->values      = clone_typed_list(values, a);
        return c;
    }
};

struct UsingDeclaration : ASTNode {
    enum class Variant : std::uint8_t { Alias = 0, Typedef, NamespaceDirective, NamespaceAlias };

    Variant variant;
    std::string_view name;
    ASTNode* aliased_expr = nullptr;
    parser_types::TypeInfo aliased_type;
    std::vector<std::string_view> target_parts;
    bool target_global = false;
    semantics::Symbol* symbol = nullptr; 

    UsingDeclaration(std::string_view n, ASTNode* expr, std::uint32_t ln = 0) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::Alias), name(n), aliased_expr(expr) {}
    UsingDeclaration(std::string_view n, const parser_types::TypeInfo& type, std::uint32_t ln = 0) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::Typedef), name(n), aliased_type(type) {}
    UsingDeclaration(std::vector<std::string_view> parts, std::uint32_t ln = 0) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::NamespaceDirective), target_parts(std::move(parts)) {}
    UsingDeclaration(std::string_view n, std::vector<std::string_view> parts, bool global, std::uint32_t ln = 0) : ASTNode(Kind::UsingDeclaration, ln), variant(Variant::NamespaceAlias), name(n), target_parts(std::move(parts)), target_global(global) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::UsingDeclaration; }

    bool is_alias()     const { return variant == Variant::Alias; }
    bool is_typedef()   const { return variant == Variant::Typedef; }
    bool is_directive() const { return variant == Variant::NamespaceDirective; }
    bool is_namespace_alias() const { return variant == Variant::NamespaceAlias; }

    const char* variant_name() const {
        switch (variant) {
            case Variant::Alias:              return "using-alias";
            case Variant::Typedef:            return "typedef";
            case Variant::NamespaceDirective: return "using-namespace";
            case Variant::NamespaceAlias:     return "namespace-alias";
        }

        return "unknown";
    }

    std::string target_name() const {
        std::string result = target_global ? "::" : "";

        for (std::size_t i = 0; i < target_parts.size(); ++i) {
            if (i > 0) result += "::";
            result += target_parts[i];
        }

        return result;
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "UsingDeclaration (" << variant_name() << ")\n";

        switch (variant) {
            case Variant::Alias:
                print_indent(os, indent + 1); os << "Name: " << name << "\n";
                print_indent(os, indent + 1); os << "Aliases:\n";
                print_node(aliased_expr, os, indent + 2);
                break;
            case Variant::Typedef:
                print_indent(os, indent + 1); os << "Name: " << name << "\n";
                print_indent(os, indent + 1); os << "Aliases: " << aliased_type << "\n";
                break;
            case Variant::NamespaceDirective:
                print_indent(os, indent + 1); os << "Target: " << target_name() << "\n";
                break;
            case Variant::NamespaceAlias:
                print_indent(os, indent + 1); os << "Name: " << name << "\n";
                print_indent(os, indent + 1); os << "Target: " << target_name() << "\n";
                break;
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        UsingDeclaration* c;

        switch (variant) {
            case Variant::Alias:          c = make_in<UsingDeclaration>(a, name, clone_child(aliased_expr, a), line); break;
            case Variant::Typedef:        c = make_in<UsingDeclaration>(a, name, aliased_type.clone_into(a), line);   break;
            case Variant::NamespaceAlias: c = make_in<UsingDeclaration>(a, name, target_parts, target_global, line);  break;
            default:                      c = make_in<UsingDeclaration>(a, target_parts, line);                       break;
        }

        c->target_global = target_global;
        copy_base_to(c);
        return c;
    }
};

struct TemplateParameter : ASTNode {
    enum class Form : std::uint8_t { Type, NonType };

    Form m_form = Form::Type;
    std::string_view m_name;            
    bool m_is_pack = false;           

    parser_types::TypeInfo m_type;          
    parser_types::TypeInfo m_default_type; 
    bool m_has_default_type = false;
    ASTNode* m_default_value = nullptr;    
    semantics::Symbol* symbol = nullptr; 

    bool                   m_is_constrained = false;   
    parser_types::TypeInfo m_constraint; 

    explicit TemplateParameter(std::uint32_t ln = 0) : ASTNode(Kind::TemplateParameter, ln) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TemplateParameter; }

    bool is_type_param()        const { return m_form == Form::Type; }
    bool is_non_type_param()    const { return m_form == Form::NonType; }
    std::string_view get_name() const { return m_name; }
    bool is_pack()              const { return m_is_pack; }
    bool is_constrained()       const { return m_is_constrained; }
    const parser_types::TypeInfo& get_constraint() const { return m_constraint; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "TemplateParameter (" << (is_type_param() ? "type" : "non-type") << ")\n";

        if (is_type_param()) {
            print_indent(os, indent + 1);
            if (m_is_constrained) { os << "constraint: " << m_constraint << (m_is_pack ? " ..." : "") << "\n"; }
            else                  { os << "typename"     << (m_is_pack ? " ..." : "") << "\n"; }
        } else {
            print_indent(os, indent + 1);
            os << "Type: " << m_type << (m_is_pack ? " ..." : "") << "\n";
        }

        print_indent(os, indent + 1);
        os << "Name: " << (m_name.empty() ? "<unnamed>" : m_name) << "\n";
        print_indent(os, indent + 1);
        os << "Default: ";

        if (is_type_param() && m_has_default_type) {
            os << m_default_type << "\n";
        } else if (is_non_type_param() && m_default_value) {
            os << "\n";
            print_node(m_default_value, os, indent + 2);
        } else {
            os << "<none>\n";
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<TemplateParameter>(a, line);
        copy_base_to(c);
        c->m_form             = m_form;
        c->m_name             = m_name;
        c->m_is_pack          = m_is_pack;
        c->m_type             = m_type.clone_into(a);
        c->m_default_type     = m_default_type.clone_into(a);
        c->m_has_default_type = m_has_default_type;
        c->m_default_value    = clone_child(m_default_value, a);
        c->m_is_constrained   = m_is_constrained;
        c->m_constraint       = m_constraint.clone_into(a);
        return c;
    }
};

struct TemplateDeclaration : ASTNode {
    std::vector<TemplateParameter*> m_params;
    ASTNode* m_declaration = nullptr;   
    bool m_is_empty_template = false;
    ASTNode* m_requires_clause = nullptr;
    semantics::Scope* scope = nullptr;

    explicit TemplateDeclaration(std::uint32_t ln = 0) : ASTNode(Kind::TemplateDeclaration, ln) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::TemplateDeclaration; }

    void add_param(TemplateParameter* p) { m_params.push_back(p); }
    void set_declaration(ASTNode* d) { m_declaration = d; }
    void set_empty_template(bool v) { m_is_empty_template = v; }
    void set_requires_clause(ASTNode* c) { m_requires_clause = c; }
    const std::vector<TemplateParameter*>& params() const { return m_params; }
    const ASTNode* get_declaration() const { return m_declaration; }
    const ASTNode* get_requires_clause() const { return m_requires_clause; }
    bool has_requires_clause() const { return m_requires_clause != nullptr; }
    bool is_empty_template() const { return m_is_empty_template; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "TemplateDeclaration" << (m_is_empty_template ? " (template <>)" : "") << "\n";
        print_indent(os, indent + 1);
        os << "Parameters:";

        if (m_params.empty()) {
            os << (m_is_empty_template ? " <empty>\n" : " <none>\n");
        } else {
            os << "\n";
            for (auto* p : m_params) { p->print(os, indent + 2); }
        }

        print_indent(os, indent + 1); os << "Requires:";
        
        if (m_requires_clause) {
            os << "\n";
            print_node(m_requires_clause, os, indent + 2);
        } else {
            os << " <none>\n";
        }

        print_indent(os, indent + 1);
        os << "Declaration:\n";
        print_node(m_declaration, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<TemplateDeclaration>(a, line);
        copy_base_to(c);
        c->m_params           = clone_typed_list(m_params, a);
        c->m_declaration      = clone_child(m_declaration, a);
        c->m_is_empty_template = m_is_empty_template;
        c->m_requires_clause  = clone_child(m_requires_clause, a);
        return c;
    }
};

struct ImportExportItem {
    enum class Kind : std::uint8_t { Name, Function, Namespace, Module, File, This };

    Kind                          kind = Kind::Name;
    std::vector<std::string_view> target_parts;       
    std::uint32_t                 line = 0;
    bool                          target_global = false;
    bool                          has_source = false;
    std::string_view              source;             
    bool                          has_alias = false;
    std::vector<std::string_view> alias_parts;
    bool                          alias_global = false;
    bool                          is_template = false;
    ASTNode*                      decl = nullptr;      

    static const char* kind_name(Kind k) {
        switch (k) {
            case Kind::Name:      return "name";
            case Kind::Function:  return "function";
            case Kind::Namespace: return "namespace";
            case Kind::Module:    return "module";
            case Kind::File:      return "file";
            case Kind::This:      return "this";
        }
        return "unknown";
    }
};

inline void print_import_export_item(std::ostream& os, const ImportExportItem& it, std::size_t indent) {
    auto write_qualified = [&os](bool glob, const std::vector<std::string_view>& parts) {
        if (glob) { os << "::"; }
        for (std::size_t i = 0; i < parts.size(); ++i) { if (i) { os << "::"; } os << parts[i]; }
    };

    print_indent(os, indent);
    os << "Item (" << ImportExportItem::kind_name(it.kind) << ")" << (it.is_template ? " [template]" : "") << ":\n";

    switch (it.kind) {
        case ImportExportItem::Kind::Function:
            print_node(it.decl, os, indent + 1);
            break;
        case ImportExportItem::Kind::Module:
            print_indent(os, indent + 1); os << "Name: " << it.source << "\n";
            break;
        case ImportExportItem::Kind::File:
            print_indent(os, indent + 1); os << "Path: " << it.source << "\n";
            print_indent(os, indent + 1); os << "As: "; write_qualified(it.alias_global, it.alias_parts); os << "\n";
            break;
        case ImportExportItem::Kind::This:
            print_indent(os, indent + 1); os << "&this\n";
            break;
        default: 
            print_indent(os, indent + 1); os << "Target: "; write_qualified(it.target_global, it.target_parts); os << "\n";
            if (it.has_source) { print_indent(os, indent + 1); os << "From: " << it.source << "\n"; }
            if (it.has_alias)  { print_indent(os, indent + 1); os << "As: "; write_qualified(it.alias_global, it.alias_parts); os << "\n"; }
            break;
    }
}

struct ImportExportDeclaration : ASTNode {
    enum class Direction : std::uint8_t { Import, Export };

    Direction                      direction;
    bool                           is_block;    
    std::vector<ImportExportItem>  items;

    ImportExportDeclaration(Direction dir, bool block, std::uint32_t ln = 0) : ASTNode(Kind::ImportExportDeclaration, ln), direction(dir), is_block(block) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ImportExportDeclaration; }

    bool is_import() const { return direction == Direction::Import; }
    bool is_export() const { return direction == Direction::Export; }
    void add_item(ImportExportItem item) { items.push_back(std::move(item)); }
    const std::vector<ImportExportItem>& get_items() const { return items; }

    void print(std::ostream& os, std::size_t indent) const {
        auto write_qualified = [&os](bool glob, const std::vector<std::string_view>& parts) {
            if (glob) os << "::";

            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) os << "::";
                os << parts[i];
            }
        };

        print_indent(os, indent);
        os << (is_import() ? "ImportDeclaration" : "ExportDeclaration") << (is_block ? " (block)" : "") << "\n";
        for (const auto& it : items) { for (const auto& it : items) { print_import_export_item(os, it, indent + 1); }}
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<ImportExportDeclaration>(a, direction, is_block, line);
        copy_base_to(c);
        c->items.reserve(items.size());
        for (const auto& it : items) { ImportExportItem ni = it; ni.decl = clone_child(it.decl, a); c->items.push_back(std::move(ni)); }
        return c;
    }
};

struct ModuleDeclaration : ASTNode {
    std::string_view              name;
    std::vector<ImportExportItem> items;
    semantics::Symbol* symbol = nullptr; 

    ModuleDeclaration(std::string_view n, std::uint32_t ln = 0) : ASTNode(Kind::ModuleDeclaration, ln), name(n) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ModuleDeclaration; }

    void add_item(ImportExportItem it) { items.push_back(std::move(it)); }
    const std::vector<ImportExportItem>& get_items() const { return items; }
    std::string_view get_name() const { return name; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ModuleDeclaration\n";
        print_indent(os, indent + 1); os << "Name: " << name << "\n";
        print_indent(os, indent + 1); os << "Items (" << items.size() << "):";
        if (items.empty()) { os << " <none>\n"; return; }
        os << "\n";
        for (const auto& it : items) { print_import_export_item(os, it, indent + 2); }
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<ModuleDeclaration>(a, name, line);
        copy_base_to(c);
        c->items.reserve(items.size());
        for (const auto& it : items) { ImportExportItem ni = it; ni.decl = clone_child(it.decl, a); c->items.push_back(std::move(ni)); }
        return c;
    }
};

struct CoReturnStatement : ASTNode {
    ASTNode* m_value;

    CoReturnStatement(ASTNode* value = nullptr, std::uint32_t ln = 0) : ASTNode(Kind::CoReturnStatement, ln), m_value(value) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::CoReturnStatement; }

    const ASTNode* get_value() const { return m_value; }
    bool has_value() const { return m_value != nullptr; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "CoReturnStatement\n";
        if (m_value) { print_node(m_value, os, indent + 1); }
        else { print_indent(os, indent + 1); os << "<void>\n"; }
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<CoReturnStatement>(a, clone_child(m_value, a), line); copy_base_to(c); return c; }
};

struct ConceptDeclaration : ASTNode {
    std::string_view name;
    ASTNode*         constraint; 
    semantics::Symbol* symbol = nullptr;   

    ConceptDeclaration(std::string_view n, ASTNode* c, std::uint32_t ln = 0) : ASTNode(Kind::ConceptDeclaration, ln), name(n), constraint(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ConceptDeclaration; }

    std::string_view get_name() const { return name; }
    const ASTNode* get_constraint() const { return constraint; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ConceptDeclaration\n";
        print_indent(os, indent + 1); 
        os << "Name: " << name << "\n";
        print_indent(os, indent + 1); 
        os << "Constraint:\n";
        print_node(constraint, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<ConceptDeclaration>(a, name, clone_child(constraint, a), line); copy_base_to(c); return c; }
};

struct StaticAssertDeclaration : ASTNode {
    std::string_view message;
    ASTNode*         constraint; 

    StaticAssertDeclaration(std::string_view m, ASTNode* c, std::uint32_t ln = 0) : ASTNode(Kind::StaticAssertDeclaration, ln), message(m), constraint(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::StaticAssertDeclaration; }

    std::string_view get_message() const { return message; }
    const ASTNode* get_constraint() const { return constraint; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "StaticAssert\n";
        print_indent(os, indent + 1); 
        os << "Message: " << message << "\n";
        print_indent(os, indent + 1); 
        os << "Constraint:\n";
        print_node(constraint, os, indent + 2);
    }

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<StaticAssertDeclaration>(a, message, clone_child(constraint, a), line); copy_base_to(c); return c; }
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_STATEMENT_NODES_HPP