#include "Parser/Nodes/requires_expression_node.hpp"

namespace walnut {
namespace nodes {

const char* RequiresExpression::Requirement::form_name() const {
    switch (form) {
        case Form::Simple:   return "simple";
        case Form::Type:     return "type";
        case Form::Compound: return "compound";
        case Form::Nested:   return "nested";
    }
    return "unknown";
}

void RequiresExpression::print(std::ostream& os, std::size_t indent) const {
    print_indent(os, indent);
    os << "RequiresExpression\n";
    if (m_parameters) { m_parameters->print(os, indent + 1); }
    print_indent(os, indent + 1);
    os << "Requirements (" << m_requirements.size() << "):\n";

    for (std::size_t i = 0; i < m_requirements.size(); ++i) {
        const Requirement& r = m_requirements[i];
        print_indent(os, indent + 2);
        os << "[" << i << "] (" << r.form_name() << ")";

        if (r.form == Requirement::Form::Type) {
            os << ": " << r.type << "\n";
            continue;
        }

        if (r.is_noexcept)         { os << " [noexcept]"; }
        if (r.has_type_constraint) { os << " -> " << r.type; }
        os << "\n";
        if (r.expr) { print_node(r.expr, os, indent + 3); }
    }

    if (m_requirements.empty()) {
        print_indent(os, indent + 2);
        os << "<none>\n";
    }
}

ASTNode* RequiresExpression::clone_into(Arena& a) const {
    auto* c = make_in<RequiresExpression>(a, clone_typed(m_parameters, a), line);
    copy_base_to(c);
    c->m_requirements.reserve(m_requirements.size());

    for (const auto& r : m_requirements) {
        Requirement nr;
        nr.form                = r.form;
        nr.expr                = clone_child(r.expr, a);
        nr.type                = r.type.clone_into(a);
        nr.has_type_constraint = r.has_type_constraint;
        nr.is_noexcept         = r.is_noexcept;
        c->m_requirements.push_back(std::move(nr));
    }
        
    return c;
}

} // namespace nodes
} // namespace walnut
