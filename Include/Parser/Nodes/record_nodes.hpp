#ifndef WALNUT_RECORD_NODES_HPP
#define WALNUT_RECORD_NODES_HPP

#include <vector>
#include <string>
#include <string_view>
#include <ostream>
#include "main_nodes.hpp"
#include "../modifiers.hpp"
#include "function_nodes.hpp"
#include "statement_nodes.hpp"

namespace walnut {
namespace nodes {

enum class AccessLevel : std::uint8_t { Public = 0, Private, Protected };

const char* access_name(AccessLevel a);

struct ConstructorDeclaration : ASTNode {
    enum class Special : std::uint8_t { None = 0, Default, Delete };

    FunctionParameters*           m_parameters;
    std::vector<ASTNode*>         m_init_list;   
    BlockStatement*               m_body;
    modifiers::RawModifiers       m_modifiers;
    modifiers::FunctionQualifiers m_qualifiers;
    Special                       m_special;
    semantics::Symbol* symbol = nullptr; 

    ConstructorDeclaration(
        FunctionParameters* params,
        std::vector<ASTNode*> init_list,
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        BlockStatement* body,
        std::uint32_t ln
    )
;

    ConstructorDeclaration(
        FunctionParameters* params,
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::ConstructorDeclaration; }

    const FunctionParameters* get_parameters() const { return m_parameters; }
          FunctionParameters* get_parameters()       { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const std::vector<ASTNode*>& get_init_list() const { return m_init_list; }
    bool has_init_list() const { return !m_init_list.empty(); }
    const ASTNode* get_body() const { return m_body; }
    ASTNode* get_body() { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    const modifiers::RawModifiers& modifiers() const { return m_modifiers; }
    const modifiers::FunctionQualifiers& qualifiers() const { return m_qualifiers; }
          modifiers::FunctionQualifiers& qualifiers()       { return m_qualifiers; }
    bool is_defaulted() const { return m_special == Special::Default; }
    bool is_deleted() const { return m_special == Special::Delete; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct DestructorDeclaration : ASTNode {
    enum class Special : std::uint8_t { None = 0, Default, Delete };

    BlockStatement*               m_body;
    modifiers::RawModifiers       m_modifiers;
    modifiers::FunctionQualifiers m_qualifiers;
    Special                       m_special;
    semantics::Symbol* symbol = nullptr; 

    DestructorDeclaration(
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        BlockStatement* body,
        std::uint32_t ln
    )
;

    DestructorDeclaration(
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::DestructorDeclaration; }

    const ASTNode* get_body() const { return m_body; }
    ASTNode* get_body() { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    const modifiers::RawModifiers& modifiers() const { return m_modifiers; }
    modifiers::FunctionQualifiers qualifiers() const { return m_qualifiers; }
    bool is_defaulted() const { return m_special == Special::Default; }
    bool is_deleted() const { return m_special == Special::Delete; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct RecordDeclaration : ASTNode {
    enum class Form : std::uint8_t { Class = 0, Struct, Union };

    struct Member {
        AccessLevel access;
        ASTNode*    node;
    };

    Form                                         m_form;
    std::vector<std::string_view>                m_name_parts;
    parser_types::TypeInfo                       m_inherits;
    bool                                         m_is_declaration;
    bool                                         m_is_specialization = false;                
    std::vector<parser_types::TemplateArgument*> m_spec_args; 
    std::vector<Member>                          m_members;
    parser_types::TemplateArgument*              m_alignment = nullptr;
    modifiers::RawModifiers                      m_mods;
    semantics::Symbol* symbol = nullptr; 
    semantics::Scope* scope = nullptr;

    RecordDeclaration(Form form, std::vector<std::string_view> name_parts, const modifiers::RawModifiers& mods, const parser_types::TypeInfo& inherits, bool is_declaration, std::uint32_t ln)
;

    RecordDeclaration(Form form, std::string_view name, const modifiers::RawModifiers& mods, const parser_types::TypeInfo& inherits, bool is_declaration, std::uint32_t ln)
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::RecordDeclaration; }

    void add_member(AccessLevel access, ASTNode* node) { m_members.push_back({access, node}); }

    bool is_class()  const { return m_form == Form::Class; }
    bool is_struct() const { return m_form == Form::Struct; }
    bool is_union()  const { return m_form == Form::Union; }

    bool has_inherits() const { return m_inherits.has_type(); }
    const parser_types::TypeInfo& get_inherits() const { return m_inherits; }
    bool is_forward() const { return m_is_declaration; }
    
    const char* form_keyword() const;

    std::string_view get_name() const { return m_name_parts.empty() ? "" : m_name_parts.back(); }
    const std::vector<Member>& get_members() const { return m_members; }

    void set_specialization(std::vector<parser_types::TemplateArgument*> args) {
        m_is_specialization = true;
        m_spec_args = std::move(args);
    }

    bool is_specialization() const { return m_is_specialization; }
    const std::vector<parser_types::TemplateArgument*>& get_spec_args() const { return m_spec_args; }

    void set_alignment(parser_types::TemplateArgument* a) { m_alignment = a; }
    const parser_types::TemplateArgument* get_alignment() const { return m_alignment; }

    const modifiers::RawModifiers& get_modifiers() const { return m_mods; }
    void set_mods(const modifiers::RawModifiers& mods) { m_mods = mods; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_RECORD_NODES_HPP