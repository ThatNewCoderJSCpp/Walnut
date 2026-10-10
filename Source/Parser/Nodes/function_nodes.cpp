#include "Parser/Nodes/function_nodes.hpp"

namespace walnut {
namespace nodes {

FunctionParameter::FunctionParameter(
        std::string_view name,
        const parser_types::TypeInfo& type,
        ASTNode* initializer,
        bool is_initialized_in_declaration,
        std::size_t variadic_length 
) : ASTNode(Kind::FunctionParameter)
        , m_name(name)
        , m_type(type)
        , m_initializer(initializer)
        , m_variadic_length(variadic_length)
        , m_is_init_in_decl(is_initialized_in_declaration)
    {}

void FunctionParameter::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "Parameter: " << m_name << "\n";
    print_indent(os, indent + 1);
    os << "Type: " << m_type;

    if (is_variadic()) {
        os << "...";
        if (!is_infinite_variadic()) { os << "[" << m_variadic_length << "]"; }
    }

    os << "\n";

    if (has_initializer()) {
        print_indent(os, indent + 1);
        os << "Default" << (m_is_init_in_decl ? " (in declaration)" : "") << ":\n";
        print_node(m_initializer, os, indent + 2);
    }
}

ASTNode* FunctionParameter::clone_into(Arena& a) const {
    auto* c = make_in<FunctionParameter>(a, m_name, m_type.clone_into(a), clone_child(m_initializer, a), m_is_init_in_decl, m_variadic_length);
    copy_base_to(c);
    return c;
}

bool FunctionParameters::remove_at(std::size_t index) {
    if (index >= m_params.size()) return false;
    m_params.erase(m_params.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool FunctionParameters::remove_by_name(std::string_view name) {
    auto it = std::find_if(m_params.begin(), m_params.end(), [&name](const auto& p) { return p->get_name() == name; });
        
    if (it != m_params.end()) {
        m_params.erase(it);
        return true;
    }

    return false;
}

void FunctionParameters::set_all_init_in_declaration(bool value) {
    for (auto& param : m_params) {
        if (param->has_initializer()) { param->set_init_in_declaration(value); }
    }
}

std::pair<std::size_t, std::size_t> FunctionParameters::find_duplicate_indices() const {
    std::unordered_map<std::string_view, std::size_t> seen;

    for (std::size_t i = 0; i < m_params.size(); ++i) {
        std::string_view name = m_params[i]->get_name();
        auto [it, inserted] = seen.emplace(name, i);
        if (!inserted) { return {it->second, i}; }
    }

    return {std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max()};
}

bool FunctionParameters::has_duplicate_names() const {
    auto [first, second] = find_duplicate_indices();
    return first != std::numeric_limits<std::size_t>::max();
}

void FunctionParameters::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "Parameters (" << size() << ")\n";

    for (std::size_t i = 0; i < m_params.size(); ++i) {
        print_indent(os, indent + 1);
        os << "[" << i << "]:\n";
        m_params[i]->print(os, indent + 2);
    }

    if (empty()) {
        print_indent(os, indent + 1);
        os << "<none>\n";
    }
}

ASTNode* FunctionParameters::clone_into(Arena& a) const { auto* c = make_in<FunctionParameters>(a); copy_base_to(c); c->m_params = clone_typed_list(m_params, a); return c; }

FunctionDeclaration::FunctionDeclaration(
        std::vector<std::string_view> name_parts,
        bool is_global_qualified,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params,
        ASTNode* body 
) : ASTNode(Kind::FunctionDeclaration, ln)
        , m_name_parts(std::move(name_parts))
        , m_is_global_qualified(is_global_qualified)
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_parameters(params)
        , m_body(body)
        , m_qualifiers(qualifiers)
    {}

FunctionDeclaration::FunctionDeclaration(
    std::string_view name,
    const parser_types::TypeInfo& return_type,
    const modifiers::RawModifiers& modifiers,
    const modifiers::FunctionQualifiers& qualifiers,
    std::uint32_t ln,
    FunctionParameters* params,
    ASTNode* body,
    bool is_const_qualified 
) : ASTNode(Kind::FunctionDeclaration, ln)
    , m_return_type(return_type)
    , m_modifiers(modifiers)
    , m_parameters(params)
    , m_body(body)
    , m_qualifiers(qualifiers)
{
    m_name_parts.push_back(name);
}

void FunctionDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "FunctionDeclaration\n";
    print_indent(os, indent + 1);
    os << "Name: ";
    if (m_is_global_qualified) os << "::";

    for (std::size_t i = 0; i < m_name_parts.size(); ++i) {
        if (i > 0) os << "::";
        os << m_name_parts[i];
    }

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
        
    os << "\n";
    print_indent(os, indent + 1);
    os << "Returns: " << m_return_type << "\n";
    print_indent(os, indent + 1);
    os << "Modifiers: ";

    if (!m_modifiers.is_modified()) {
        os << "<none>\n";
    } else {
        os << m_modifiers << "\n";
    }

    print_indent(os, indent + 1);
    os << "Qualifiers: ";

    if (!m_qualifiers.is_qualified()) {
        os << "<none>\n";
    } else {
        os << m_qualifiers << "\n";
    }

    if (m_parameters) { m_parameters->print(os, indent + 1); }
    print_indent(os, indent + 1);
    os << "Body:\n";

    if (m_body) {
        print_node(m_body, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<none>\n";
    }
}

ASTNode* FunctionDeclaration::clone_into(Arena& a) const {
    auto* c = make_in<FunctionDeclaration>(
        a, m_name_parts, 
        m_is_global_qualified, 
        m_return_type.clone_into(a),        
        m_modifiers, m_qualifiers, line,
        clone_typed(m_parameters, a), 
        clone_child(m_body, a)
    );

    c->m_is_specialization = m_is_specialization;
    c->m_spec_args         = clone_targs(m_spec_args, a);
    copy_base_to(c);
    return c;
}

void ReturnStatement::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "ReturnStatement\n";
    print_indent(os, indent + 1);
    os << "Value:\n";

    if (m_value) {
        print_node(m_value, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<void>\n";
    }
}

void LambdaCaptureItem::write_to(std::ostream& os) const {
    switch (mode) {
        case Mode::ByValue:         os << name; break;
        case Mode::ByReference:     os << "&" << name; break;
        case Mode::AllByValue:      os << "="; break;
        case Mode::AllByReference:  os << "&"; break;
        case Mode::This:            os << "this"; break;
        case Mode::ThisByReference: os << "&this"; break;
    }
}

bool LambdaCaptureList::has_capture_all() const {
    for (const auto& c : m_captures) {
        if (c.mode == LambdaCaptureItem::Mode::AllByValue || c.mode == LambdaCaptureItem::Mode::AllByReference) return true;
    }

    return false;
}

void LambdaCaptureList::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "CaptureList [";

    for (std::size_t i = 0; i < m_captures.size(); ++i) {
        if (i > 0) os << ", ";
        m_captures[i].write_to(os);
    }

    os << "]\n";
}

ASTNode* LambdaCaptureList::clone_into(Arena& a) const {
    auto* c = make_in<LambdaCaptureList>(a);
    copy_base_to(c);
    c->m_captures.reserve(m_captures.size());
    for (const auto& it : m_captures) c->m_captures.push_back(LambdaCaptureItem(it.mode, it.name, clone_child(it.init, a)));
    return c;
}

LambdaExpression::LambdaExpression(
        LambdaCaptureList* captures,
        FunctionParameters* params,
        const parser_types::TypeInfo& return_type,
        const modifiers::FunctionQualifiers& quals,
        ASTNode* body,
        std::uint32_t ln 
) : ASTNode(Kind::LambdaExpression, ln)
        , m_captures(captures)
        , m_parameters(params)
        , m_return_type(return_type)
        , m_quals(quals)
        , m_body(body)
    {}

void LambdaExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "LambdaExpression\n";
    if (m_captures) { m_captures->print(os, indent + 1); }
    print_indent(os, indent + 1);
    os << "Returns: " << m_return_type << "\n";
    print_indent(os, indent + 1);
    os << "Qualifiers: " << m_quals << "\n";
    if (m_parameters) { m_parameters->print(os, indent + 1); }
    print_indent(os, indent + 1);
    os << "Body:\n";

    if (m_body) {
        print_node(m_body, os, indent + 2);
    } else {
        print_indent(os, indent + 2);
        os << "<none>\n";
    }
}

ASTNode* LambdaExpression::clone_into(Arena& a) const {
    auto* c = make_in<LambdaExpression>(
        a, 
        clone_typed(m_captures, a), 
        clone_typed(m_parameters, a),
        m_return_type.clone_into(a), 
        m_quals, 
        clone_child(m_body, a), 
        line
    );

    copy_base_to(c);
    return c;
}

OperatorFunctionDeclaration::OperatorFunctionDeclaration(
        std::vector<tokenizing::Token::Kind> ops,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params,
        ASTNode* body 
) : ASTNode(Kind::OperatorFunctionDeclaration, ln)
        , form(Form::Symbol)
        , op_tokens(std::move(ops))
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_qualifiers(qualifiers)
        , m_parameters(params)
        , m_body(body)
    {}

OperatorFunctionDeclaration::OperatorFunctionDeclaration(
        const parser_types::TypeInfo& conv_type,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params,
        ASTNode* body 
) : ASTNode(Kind::OperatorFunctionDeclaration, ln)
        , form(Form::Conversion)
        , conversion_type(conv_type)
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_qualifiers(qualifiers)
        , m_parameters(params)
        , m_body(body)
    {}

void OperatorFunctionDeclaration::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "OperatorFunctionDeclaration\n";
    print_indent(os, indent + 1);

    if (form == Form::Conversion) {
        os << "Conversion to: " << conversion_type << "\n";
    } else {
        os << "Operator: " << overloadable_operator_name(overload) << "\n";
    }

    print_indent(os, indent + 1);
    os << "Returns: " << m_return_type << "\n";
    print_indent(os, indent + 1);
    os << "Modifiers: " << m_modifiers << "\n";
    if (m_parameters) { m_parameters->print(os, indent + 1); }
    print_indent(os, indent + 1);
    os << "Body:\n";
    if (m_body) { print_node(m_body, os, indent + 2); }
    else        { print_indent(os, indent + 2); os << "<none>\n"; }
}

ASTNode* OperatorFunctionDeclaration::clone_into(Arena& a) const {
    OperatorFunctionDeclaration* c;

    if (form == Form::Conversion) {
        c = make_in<OperatorFunctionDeclaration>(
            a, conversion_type.clone_into(a), 
            m_return_type.clone_into(a),
            m_modifiers, 
            m_qualifiers, 
            line, 
            clone_typed(m_parameters, a), 
            clone_child(m_body, a)
        );
    } else {
        c = make_in<OperatorFunctionDeclaration>(
            a, 
            op_tokens, 
            m_return_type.clone_into(a),
            m_modifiers, 
            m_qualifiers, 
            line, 
            clone_typed(m_parameters, a), 
            clone_child(m_body, a)
        );
    }

    copy_base_to(c);
    c->overload = overload;
    return c;
}

} // namespace nodes
} // namespace walnut
