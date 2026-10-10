#include "Parser/Types/dependent_types.hpp"

namespace walnut {
namespace parser_types {

Type* FunctionPointerType::clone_into(Arena& arena) const {
    std::vector<TypeInfo> cloned_params;
    cloned_params.reserve(m_param_types.size());
    for (const auto& p : m_param_types) { cloned_params.push_back(p.clone_into(arena)); }
    return make_in<FunctionPointerType>(arena, std::move(cloned_params), m_return_type.clone_into(arena), m_quals);
}

void FunctionPointerType::write_to(std::ostream& os) const {
    os << "function(";

    for (std::size_t i = 0; i < m_param_types.size(); ++i) {
        if (i > 0) os << ", ";
        m_param_types[i].write_to(os);
    }

    os << ") " << m_quals << " \u2192 ";
    m_return_type.write_to(os);
}

Type* ArrayType::clone_into(Arena& arena) const {
    return make_in<ArrayType>(
        arena,
        m_element_type.clone_into(arena),
        m_dimension,
        m_dimension_expr
    );
}

void ArrayType::write_to(std::ostream& os) const {
    os << "array[";

    if (m_dimension.has_value()) {
        os << m_dimension.value();
    } else if (m_dimension_expr) {
        os << "<dynamic>";
    }
        
    os << "] ";
    m_element_type.write_to(os);
}

Type* VariantType::clone_into(Arena& arena) const {
    std::vector<TypeInfo> cloned;
    cloned.reserve(m_alternatives.size());
    for (const auto& a : m_alternatives) { cloned.push_back(a.clone_into(arena)); }
    return make_in<VariantType>(arena, std::move(cloned));
}

void VariantType::write_to(std::ostream& os) const {
    os << "variant(";

    for (std::size_t i = 0; i < m_alternatives.size(); ++i) {
        if (i > 0) os << ", ";
        m_alternatives[i].write_to(os);
    }

    os << ")";
}

Type* QualifiedType::clone_into(Arena& arena) const {
    auto* copy = make_in<QualifiedType>(arena, m_is_global);

    for (const auto& c : m_components) {
        std::vector<TemplateArgument*> cargs;
        cargs.reserve(c.args.size());

        for (const TemplateArgument* a : c.args) {
            auto* na = make_in<TemplateArgument>(arena);
            na->form       = a->form;
            na->type       = a->type.clone_into(arena);
            na->value      = a->value;
            na->value_repr = a->value_repr;
            na->is_pack    = a->is_pack;
            cargs.push_back(na);
        }

        copy->add_component(c.name, std::move(cargs));
    }

    if (m_has_prefix) copy->set_prefix(m_prefix.clone_into(arena));
    return copy;
}

void QualifiedType::write_to(std::ostream& os) const {
    if (m_is_global) os << "::";

    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (i > 0) os << "::";
        os << m_components[i].name;

        if (m_components[i].has_args()) {
            os << '<';
            for (std::size_t j = 0; j < m_components[i].args.size(); ++j) {
                if (j > 0) os << ", ";
                m_components[i].args[j]->write_to(os);
            }
            os << '>';
        }
    }
}

} // namespace parser_types
} // namespace walnut
