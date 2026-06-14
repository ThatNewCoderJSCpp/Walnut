#ifndef WALNUT_REQUIRES_EXPRESSION_NODE_HPP
#define WALNUT_REQUIRES_EXPRESSION_NODE_HPP

#include "function_nodes.hpp"

namespace walnut {
namespace nodes {

struct RequiresExpression : ASTNode {
    struct Requirement {
        enum class Form : std::uint8_t { Simple = 0, Type, Compound, Nested };

        Form                   form = Form::Simple;
        ASTNode*               expr = nullptr;        
        parser_types::TypeInfo type;                  
        bool                   has_type_constraint = false;  
        bool                   is_noexcept         = false; 

        const char* form_name() const {
            switch (form) {
                case Form::Simple:   return "simple";
                case Form::Type:     return "type";
                case Form::Compound: return "compound";
                case Form::Nested:   return "nested";
            }
            return "unknown";
        }
    };

    FunctionParameters*      m_parameters;   
    std::vector<Requirement> m_requirements;
    semantics::Scope*        scope = nullptr;   

    RequiresExpression(FunctionParameters* params, std::uint32_t ln = 0) : ASTNode(Kind::RequiresExpression, ln), m_parameters(params) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::RequiresExpression; }

    void add_requirement(Requirement r) { m_requirements.push_back(std::move(r)); }
    const FunctionParameters* get_parameters() const { return m_parameters; }
    FunctionParameters* get_parameters() { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const std::vector<Requirement>& get_requirements() const { return m_requirements; }
    std::vector<Requirement>& get_requirements() { return m_requirements; }

    void print(std::ostream& os, std::size_t indent) const {
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
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_REQUIRES_EXPRESSION_NODE_HPP