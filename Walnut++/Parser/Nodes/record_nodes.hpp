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

inline const char* access_name(AccessLevel a) {
    switch (a) {
        case AccessLevel::Public:    return "public";
        case AccessLevel::Private:   return "private";
        case AccessLevel::Protected: return "protected";
    }
    return "unknown";
}

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
        : ASTNode(Kind::ConstructorDeclaration, ln)
        , m_parameters(params)
        , m_init_list(std::move(init_list))
        , m_body(body)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(Special::None)
    {}

    ConstructorDeclaration(
        FunctionParameters* params,
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
    )
        : ASTNode(Kind::ConstructorDeclaration, ln)
        , m_parameters(params)
        , m_body(nullptr)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(special)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ConstructorDeclaration; }

    const FunctionParameters* get_parameters() const { return m_parameters; }
          FunctionParameters* get_parameters()       { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const std::vector<ASTNode*>& get_init_list() const { return m_init_list; }
    bool has_init_list() const { return !m_init_list.empty(); }
    const ASTNode* get_body() const { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    const modifiers::RawModifiers& modifiers() const { return m_modifiers; }
    modifiers::FunctionQualifiers qualifiers() const { return m_qualifiers; }
    bool is_defaulted() const { return m_special == Special::Default; }
    bool is_deleted() const { return m_special == Special::Delete; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "ConstructorDeclaration\n";
        print_indent(os, indent + 1);
        os << "Modifiers: " << m_modifiers << "\n";
        print_indent(os, indent + 1);
        os << "Qualifiers: " << m_qualifiers << "\n";
        if (m_parameters) { m_parameters->print(os, indent + 1); }
        print_indent(os, indent + 1);
        os << "Initializer list:";

        if (m_init_list.empty()) {
            os << " <none>\n";
        } else {
            os << "\n";
            for (auto* e : m_init_list) { print_node(e, os, indent + 2); }
        }

        print_indent(os, indent + 1);
        if (is_defaulted())      { os << "Body: = default\n"; }
        else if (is_deleted())   { os << "Body: = delete\n"; }
        else if (m_body)         { os << "Body:\n"; m_body->print(os, indent + 2); }
        else                     { os << "Body: <none>\n"; }
    }

    ASTNode* clone_into(Arena& a) const override {
        ConstructorDeclaration* c;

        if (m_special == Special::None) {
            c = make_in<ConstructorDeclaration>(a, clone_typed(m_parameters, a), clone_list(m_init_list, a), m_modifiers, m_qualifiers, clone_typed(m_body, a), line);
        } else {
            c = make_in<ConstructorDeclaration>(a, clone_typed(m_parameters, a), m_modifiers, m_qualifiers, m_special, line);
        }

        copy_base_to(c);
        return c;
    }
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
        : ASTNode(Kind::DestructorDeclaration, ln)
        , m_body(body)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(Special::None)
    {}

    DestructorDeclaration(
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
    )
        : ASTNode(Kind::DestructorDeclaration, ln)
        , m_body(nullptr)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(special)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::DestructorDeclaration; }

    const ASTNode* get_body() const { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    const modifiers::RawModifiers& modifiers() const { return m_modifiers; }
    modifiers::FunctionQualifiers qualifiers() const { return m_qualifiers; }
    bool is_defaulted() const { return m_special == Special::Default; }
    bool is_deleted() const { return m_special == Special::Delete; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "DestructorDeclaration\n";
        print_indent(os, indent + 1);
        os << "Modifiers: " << m_modifiers << "\n";
        print_indent(os, indent + 1);
        os << "Qualifiers: " << m_qualifiers << "\n";
        print_indent(os, indent + 1);
        if (is_defaulted())    { os << "Body: = default\n"; }
        else if (is_deleted()) { os << "Body: = delete\n"; }
        else if (m_body)       { os << "Body:\n"; m_body->print(os, indent + 2); }
        else                   { os << "Body: <none>\n"; }
    }

    ASTNode* clone_into(Arena& a) const override {
        DestructorDeclaration* c;
        if (m_special == Special::None) c = make_in<DestructorDeclaration>(a, m_modifiers, m_qualifiers, clone_typed(m_body, a), line);
        else                            c = make_in<DestructorDeclaration>(a, m_modifiers, m_qualifiers, m_special, line);
        copy_base_to(c);
        return c;
    }
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
        : ASTNode(Kind::RecordDeclaration, ln)
        , m_form(form), m_name_parts(std::move(name_parts)), m_mods(mods)
        , m_inherits(inherits), m_is_declaration(is_declaration) {}

    RecordDeclaration(Form form, std::string_view name, const modifiers::RawModifiers& mods, const parser_types::TypeInfo& inherits, bool is_declaration, std::uint32_t ln)
        : ASTNode(Kind::RecordDeclaration, ln)
        , m_form(form), m_inherits(inherits), m_is_declaration(is_declaration), m_mods(mods)
    { m_name_parts.push_back(name); }

    static bool classof(const ASTNode* n) { return n->kind == Kind::RecordDeclaration; }

    void add_member(AccessLevel access, ASTNode* node) { m_members.push_back({access, node}); }

    bool is_class()  const { return m_form == Form::Class; }
    bool is_struct() const { return m_form == Form::Struct; }
    bool is_union()  const { return m_form == Form::Union; }

    bool has_inherits() const { return m_inherits.has_type(); }
    const parser_types::TypeInfo& get_inherits() const { return m_inherits; }
    bool is_forward() const { return m_is_declaration; }
    
    const char* form_keyword() const {
        switch (m_form) {
            case Form::Class:  return "class";
            case Form::Struct: return "struct";
            case Form::Union:  return "union";
        }
        return "class";
    }

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

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "RecordDeclaration (" << form_keyword() << (m_is_declaration ? ", declaration" : "") << (m_is_specialization ? ", specialization" : "") << ")\n";
        print_indent(os, indent + 1);
        os << "Name: ";

        for (std::size_t i = 0; i < m_name_parts.size(); ++i) {
            if (i > 0) os << "::";
            os << m_name_parts[i];
        }

        os << "\n";
        os << "Modifiers: " << m_mods << "\n";

        if (m_is_specialization) {                                             
            print_indent(os, indent + 1);
            os << "Specialization Args (" << m_spec_args.size() << "):";

            if (m_spec_args.empty()) {
                os << " <none>\n";
            } else {
                os << "\n";
                
                for (std::size_t i = 0; i < m_spec_args.size(); ++i) {
                    print_indent(os, indent + 2);
                    os << "[" << i << "]:";
                    const auto* a = m_spec_args[i];

                    if (a->is_type()) {
                        os << " " << a->type << (a->is_pack ? " ..." : "") << "\n";
                    } else {
                        os << "\n";
                        print_node(a->value, os, indent + 3);
                        if (a->is_pack) { print_indent(os, indent + 3); os << "...(pack)\n"; }
                    }
                }
            }
        }                                                                      

        print_indent(os, indent + 1);
        os << "Alignas: " << (m_alignment ? (m_alignment->is_type() ? "<type>" : "<value>") : "<none>") << "\n";
        print_indent(os, indent + 1);
        os << "Inherits: ";
        
        if (has_inherits()) {
            os << m_inherits;
        } else {
            os << "<none>";
        }

        os << "\n";
        if (m_is_declaration) { return; }
        print_indent(os, indent + 1);
        os << "Members:";

        if (m_members.empty()) {
            os << " <none>";
            return;
        } 

        os << "\n";

        for (const auto& m : m_members) {
            print_indent(os, indent + 2);
            os << "[" << access_name(m.access) << "]\n";
            print_node(m.node, os, indent + 3);
        }
    }

    ASTNode* clone_into(Arena& a) const override {
        auto* c = make_in<RecordDeclaration>(a, m_form, m_name_parts, m_mods, m_inherits.clone_into(a), m_is_declaration, line);
        copy_base_to(c);
        c->m_is_specialization = m_is_specialization;
        c->m_spec_args         = clone_targs(m_spec_args, a);
        c->m_alignment         = clone_targ(m_alignment, a);
        c->m_members.reserve(m_members.size());
        for (const auto& m : m_members) c->m_members.push_back({ m.access, clone_child(m.node, a) });
        return c;
    }
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_RECORD_NODES_HPP