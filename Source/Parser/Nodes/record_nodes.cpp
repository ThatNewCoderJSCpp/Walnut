#include "Parser/Nodes/record_nodes.hpp"

namespace walnut {
namespace nodes {

const char* access_name(AccessLevel a) {
    switch (a) {
        case AccessLevel::Public:    return "public";
        case AccessLevel::Private:   return "private";
        case AccessLevel::Protected: return "protected";
    }
    return "unknown";
}

ConstructorDeclaration::ConstructorDeclaration(
        FunctionParameters* params,
        std::vector<ASTNode*> init_list,
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        BlockStatement* body,
        std::uint32_t ln
) : ASTNode(Kind::ConstructorDeclaration, ln)
        , m_parameters(params)
        , m_init_list(std::move(init_list))
        , m_body(body)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(Special::None)
    {}

ConstructorDeclaration::ConstructorDeclaration(
        FunctionParameters* params,
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
) : ASTNode(Kind::ConstructorDeclaration, ln)
        , m_parameters(params)
        , m_body(nullptr)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(special)
    {}

void ConstructorDeclaration::print(std::ostream& os, std::size_t indent) const {
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

ASTNode* ConstructorDeclaration::clone_into(Arena& a) const {
    ConstructorDeclaration* c;

    if (m_special == Special::None) {
        c = make_in<ConstructorDeclaration>(a, clone_typed(m_parameters, a), clone_list(m_init_list, a), m_modifiers, m_qualifiers, clone_typed(m_body, a), line);
    } else {
        c = make_in<ConstructorDeclaration>(a, clone_typed(m_parameters, a), m_modifiers, m_qualifiers, m_special, line);
    }

    copy_base_to(c);
    return c;
}

DestructorDeclaration::DestructorDeclaration(
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        BlockStatement* body,
        std::uint32_t ln
) : ASTNode(Kind::DestructorDeclaration, ln)
        , m_body(body)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(Special::None)
    {}

DestructorDeclaration::DestructorDeclaration(
        const modifiers::RawModifiers& mods,
        const modifiers::FunctionQualifiers& quals,
        Special special,
        std::uint32_t ln
) : ASTNode(Kind::DestructorDeclaration, ln)
        , m_body(nullptr)
        , m_modifiers(mods)
        , m_qualifiers(quals)
        , m_special(special)
    {}

void DestructorDeclaration::print(std::ostream& os, std::size_t indent) const {
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

ASTNode* DestructorDeclaration::clone_into(Arena& a) const {
    DestructorDeclaration* c;
    if (m_special == Special::None) c = make_in<DestructorDeclaration>(a, m_modifiers, m_qualifiers, clone_typed(m_body, a), line);
    else                            c = make_in<DestructorDeclaration>(a, m_modifiers, m_qualifiers, m_special, line);
    copy_base_to(c);
    return c;
}

RecordDeclaration::RecordDeclaration(Form form, std::vector<std::string_view> name_parts, const modifiers::RawModifiers& mods, const parser_types::TypeInfo& inherits, bool is_declaration, std::uint32_t ln) : ASTNode(Kind::RecordDeclaration, ln)
        , m_form(form), m_name_parts(std::move(name_parts)), m_mods(mods)
        , m_inherits(inherits), m_is_declaration(is_declaration) {}

RecordDeclaration::RecordDeclaration(Form form, std::string_view name, const modifiers::RawModifiers& mods, const parser_types::TypeInfo& inherits, bool is_declaration, std::uint32_t ln) : ASTNode(Kind::RecordDeclaration, ln)
        , m_form(form), m_inherits(inherits), m_is_declaration(is_declaration), m_mods(mods)
    { m_name_parts.push_back(name); }

const char* RecordDeclaration::form_keyword() const {
    switch (m_form) {
        case Form::Class:  return "class";
        case Form::Struct: return "struct";
        case Form::Union:  return "union";
    }
    return "class";
}

void RecordDeclaration::print(std::ostream& os, std::size_t indent) const {
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

ASTNode* RecordDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<RecordDeclaration>(a, m_form, m_name_parts, m_mods, m_inherits.clone_into(a), m_is_declaration, line);
    copy_base_to(c);
    c->m_is_specialization = m_is_specialization;
    c->m_spec_args         = clone_targs(m_spec_args, a);
    c->m_alignment         = clone_targ(m_alignment, a);
    c->m_members.reserve(m_members.size());
    for (const auto& m : m_members) c->m_members.push_back({ m.access, clone_child(m.node, a) });
    return c;
}

} // namespace nodes
} // namespace walnut
