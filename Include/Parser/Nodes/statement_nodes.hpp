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
    semantics::Type* binding_source = nullptr;
    bool binding_by_ref = false;

    VariableDeclaration(const parser_types::TypeInfo& type_info, std::string_view name, ASTNode* init = nullptr, std::uint32_t ln = 0)
;

    VariableDeclaration(const parser_types::TypeInfo& type_info, SmallVector<std::string_view, 4> bindings, ASTNode* init, std::uint32_t ln = 0)
;

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct BlockStatement : ASTNode {
    std::vector<ASTNode*> statements;
    semantics::Scope* scope = nullptr;

    BlockStatement() : ASTNode(Kind::BlockStatement) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::BlockStatement; }

    void add_statement(ASTNode* stmt) { statements.push_back(stmt); }
    const std::vector<ASTNode*>& get_statements() const { return statements; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct IfBranch : ASTNode {
    ASTNode* condition;
    ASTNode* body;
    bool     is_constexpr   = false;
    int      constant_value = -1;

    IfBranch(ASTNode* cond, ASTNode* b) : ASTNode(Kind::IfBranch), condition(cond), body(b) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::IfBranch; }

    const ASTNode* get_condition() const { return condition; }
    const ASTNode* get_body() const { return body; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::NamespaceDeclaration; }

    const std::vector<std::string_view>& name_parts() const { return m_name_parts; }
    std::string_view simple_name() const { return m_name_parts.empty() ? "" : m_name_parts.back(); }
    bool is_qualified() const { return m_name_parts.size() > 1; }
    const BlockStatement* get_body() const { return m_body; }
    BlockStatement* get_body() { return m_body; }
    std::uint32_t get_line() const { return line; }

    std::string full_name() const;

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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
;

    ForStatement(ASTNode* var_initializer, std::nullptr_t, ASTNode* cond, ASTNode* inc, ASTNode* b)
;

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct ForEachStatement : ASTNode {
    parser_types::TypeInfo  m_element_type;
    modifiers::RawModifiers m_modifiers;
    std::string_view        m_var_name;
    SmallVector<std::string_view, 4> m_bindings;
    ASTNode*                m_container;
    ASTNode*                m_body;
    semantics::Symbol* symbol = nullptr; 
    SmallVector<semantics::Symbol*, 4> binding_symbols;
    semantics::Type* binding_source = nullptr;
    bool binding_by_ref = false;

    ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, std::string_view var_name, ASTNode* container, ASTNode* body, std::uint32_t ln = 0)
;

    ForEachStatement(const parser_types::TypeInfo& element_type, const modifiers::RawModifiers& mods, SmallVector<std::string_view, 4> bindings, ASTNode* container, ASTNode* body, std::uint32_t ln = 0)
;

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct WhileStatement : ASTNode {
    ASTNode* body;
    ASTNode* condition;

    WhileStatement(ASTNode* b, ASTNode* c) : ASTNode(Kind::WhileStatement), body(b), condition(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::WhileStatement; }

    const ASTNode* get_body() const { return body; }
    const ASTNode* get_condition() const { return condition; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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
;

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct SingleStatement : ASTNode {
    enum class Variant : std::uint8_t { Break = 0, Continue, Fallthrough, Repeat };

    Variant variant;

    SingleStatement(Variant v, std::uint32_t ln = 0) : ASTNode(Kind::SingleStatement, ln), variant(v) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::SingleStatement; }

    const char* variant_name() const;

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct TryCatchStatement : ASTNode {
    BlockStatement* try_body;
    std::string_view catch_name;         
    parser_types::TypeInfo catch_type;  
    bool has_typed_catch;                
    BlockStatement* catch_body;
    TryCatchStatement* next_handler = nullptr;

    TryCatchStatement(
        BlockStatement* tb,
        std::string_view cn,
        const parser_types::TypeInfo& ct,
        bool typed,
        BlockStatement* cb,
        std::uint32_t ln = 0
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::TryCatchStatement; }

    const BlockStatement* get_try_body() const { return try_body; }
    BlockStatement* get_try_body() { return try_body; }
    const BlockStatement* get_catch_body() const { return catch_body; }
    BlockStatement* get_catch_body() { return catch_body; }
    std::string_view get_catch_name() const { return catch_name; }
    const parser_types::TypeInfo& get_catch_type() const { return catch_type; }
    bool is_typed_catch() const { return has_typed_catch; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::EnumDeclaration; }

    std::string_view get_name() const { return name; }
    bool has_type() const { return has_underlying_type; }
    const parser_types::TypeInfo& get_underlying_type() const { return underlying_type; }
    const std::vector<EnumValue*>& get_values() const { return values; }
    const modifiers::RawModifiers& get_modifiers() const { return m_modifiers; }
    modifiers::RawModifiers&       get_modifiers()       { return m_modifiers; }
    void set_modifiers(const modifiers::RawModifiers& m) { m_modifiers = m; }

    void add_value(EnumValue* v) { values.push_back(v); }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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
    UsingDeclaration(std::string_view n, const parser_types::TypeInfo& type, std::uint32_t ln = 0);
    UsingDeclaration(std::vector<std::string_view> parts, std::uint32_t ln = 0);
    UsingDeclaration(std::string_view n, std::vector<std::string_view> parts, bool global, std::uint32_t ln = 0);

    static bool classof(const ASTNode* n) { return n->kind == Kind::UsingDeclaration; }

    bool is_alias()     const { return variant == Variant::Alias; }
    bool is_typedef()   const { return variant == Variant::Typedef; }
    bool is_directive() const { return variant == Variant::NamespaceDirective; }
    bool is_namespace_alias() const { return variant == Variant::NamespaceAlias; }

    const char* variant_name() const;

    std::string target_name() const;

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    static const char* kind_name(Kind k);
};

void print_import_export_item(std::ostream& os, const ImportExportItem& it, std::size_t indent);

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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct CoReturnStatement : ASTNode {
    ASTNode* m_value;

    CoReturnStatement(ASTNode* value = nullptr, std::uint32_t ln = 0) : ASTNode(Kind::CoReturnStatement, ln), m_value(value) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::CoReturnStatement; }

    const ASTNode* get_value() const { return m_value; }
    bool has_value() const { return m_value != nullptr; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct ConceptDeclaration : ASTNode {
    std::string_view name;
    ASTNode*         constraint; 
    semantics::Symbol* symbol = nullptr;   

    ConceptDeclaration(std::string_view n, ASTNode* c, std::uint32_t ln = 0) : ASTNode(Kind::ConceptDeclaration, ln), name(n), constraint(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ConceptDeclaration; }

    std::string_view get_name() const { return name; }
    const ASTNode* get_constraint() const { return constraint; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct StaticAssertDeclaration : ASTNode {
    std::string_view message;
    ASTNode*         constraint; 

    StaticAssertDeclaration(std::string_view m, ASTNode* c, std::uint32_t ln = 0) : ASTNode(Kind::StaticAssertDeclaration, ln), message(m), constraint(c) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::StaticAssertDeclaration; }

    std::string_view get_message() const { return message; }
    const ASTNode* get_constraint() const { return constraint; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_STATEMENT_NODES_HPP