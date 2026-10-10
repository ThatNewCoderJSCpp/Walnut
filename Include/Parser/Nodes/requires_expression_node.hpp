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

        const char* form_name() const;
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

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_REQUIRES_EXPRESSION_NODE_HPP