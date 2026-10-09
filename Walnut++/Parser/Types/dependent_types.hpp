#ifndef WALNUT_DEPENDENT_TYPES_HPP
#define WALNUT_DEPENDENT_TYPES_HPP

#include "type_info.hpp"

namespace walnut {

namespace nodes { struct ASTNode; }

namespace parser_types {

class FunctionPointerType : public Type {
private:
    std::vector<TypeInfo> m_param_types;   
    TypeInfo m_return_type;
    modifiers::FunctionQualifiers m_quals;

public:
    FunctionPointerType(
        std::vector<TypeInfo> param_types,
        const TypeInfo& return_type,
        const modifiers::FunctionQualifiers& quals
    )
        : m_param_types(std::move(param_types))
        , m_return_type(return_type)
        , m_quals(quals)
    {}

    Kind kind() const override { return Kind::FunctionPointer; }

    Type* clone_into(Arena& arena) const override {
        std::vector<TypeInfo> cloned_params;
        cloned_params.reserve(m_param_types.size());
        for (const auto& p : m_param_types) { cloned_params.push_back(p.clone_into(arena)); }
        return make_in<FunctionPointerType>(arena, std::move(cloned_params), m_return_type.clone_into(arena), m_quals);
    }

    void write_to(std::ostream& os) const override {
        os << "function(";

        for (std::size_t i = 0; i < m_param_types.size(); ++i) {
            if (i > 0) os << ", ";
            m_param_types[i].write_to(os);
        }

        os << ") " << m_quals << " \u2192 ";
        m_return_type.write_to(os);
    }

    const std::vector<TypeInfo>& param_types() const { return m_param_types; }
    std::size_t param_count() const { return m_param_types.size(); }
    const TypeInfo& return_type() const { return m_return_type; }
    TypeInfo& return_type() { return m_return_type; }
    const modifiers::FunctionQualifiers& qualifiers() const { return m_quals; }

    const TypeInfo& param_type(std::size_t i) const { return m_param_types[i]; }

    static FunctionPointerType* make(
        Arena& arena,
        std::vector<TypeInfo> param_types,
        const TypeInfo& return_type,
        const modifiers::FunctionQualifiers& quals
    ) {
        return make_in<FunctionPointerType>(arena, std::move(param_types), return_type, quals);
    }
};

class ArrayType : public Type {
private:
    TypeInfo                   m_element_type;
    std::optional<std::size_t> m_dimension;
    nodes::ASTNode*            m_dimension_expr;

public:
    ArrayType(
        const TypeInfo& element_type,
        std::optional<std::size_t> dimension = std::nullopt,
        nodes::ASTNode* dimension_expr = nullptr
    )
        : m_element_type(element_type)
        , m_dimension(dimension)
        , m_dimension_expr(dimension_expr)
    {}

    Kind kind() const override { return Kind::Array; }

    Type* clone_into(Arena& arena) const override {
        return make_in<ArrayType>(
            arena,
            m_element_type.clone_into(arena),
            m_dimension,
            m_dimension_expr
        );
    }

    void write_to(std::ostream& os) const override {
        os << "array[";

        if (m_dimension.has_value()) {
            os << m_dimension.value();
        } else if (m_dimension_expr) {
            os << "<dynamic>";
        }
        
        os << "] ";
        m_element_type.write_to(os);
    }

    const TypeInfo& element_type() const { return m_element_type; }
    TypeInfo&       element_type()       { return m_element_type; }

    std::optional<std::size_t> dimension()      const { return m_dimension; }
    const nodes::ASTNode*      dimension_expr() const { return m_dimension_expr; }

    bool has_static_dimension()  const { return m_dimension.has_value(); }
    bool has_dynamic_dimension() const { return m_dimension_expr != nullptr; }
    bool is_unsized() const { return !m_dimension.has_value() && m_dimension_expr == nullptr; }

    static ArrayType* make(
        Arena& arena,
        const TypeInfo& element_type,
        std::optional<std::size_t> dimension = std::nullopt,
        nodes::ASTNode* dimension_expr = nullptr
    ) {
        return make_in<ArrayType>(arena, element_type, dimension, dimension_expr);
    }
};

class VariantType : public Type {
private:
    std::vector<TypeInfo> m_alternatives;   

public:
    explicit VariantType(std::vector<TypeInfo> alternatives) : m_alternatives(std::move(alternatives)) {}

    Kind kind() const override { return Kind::Variant; }

    Type* clone_into(Arena& arena) const override {
        std::vector<TypeInfo> cloned;
        cloned.reserve(m_alternatives.size());
        for (const auto& a : m_alternatives) { cloned.push_back(a.clone_into(arena)); }
        return make_in<VariantType>(arena, std::move(cloned));
    }

    void write_to(std::ostream& os) const override {
        os << "variant(";

        for (std::size_t i = 0; i < m_alternatives.size(); ++i) {
            if (i > 0) os << ", ";
            m_alternatives[i].write_to(os);
        }

        os << ")";
    }

    const std::vector<TypeInfo>& alternatives() const { return m_alternatives; }
    std::size_t count() const { return m_alternatives.size(); }
    const TypeInfo& alternative(std::size_t i) const { return m_alternatives[i]; }

    static VariantType* make(Arena& arena, std::vector<TypeInfo> alternatives) {
        return make_in<VariantType>(arena, std::move(alternatives));
    }
};

class QualifiedType : public Type {
public:
    struct Component {
        std::string_view name;
        std::vector<TemplateArgument*> args;   
        bool has_args() const { return !args.empty(); }
    };

private:
    std::vector<Component> m_components;
    bool m_is_global = false;
    TypeInfo m_prefix;
    bool m_has_prefix = false;

public:
    explicit QualifiedType(bool is_global = false) : m_is_global(is_global) {}

    void set_prefix(const TypeInfo& prefix) { m_prefix = prefix; m_has_prefix = true; }
    bool has_prefix() const { return m_has_prefix; }
    const TypeInfo& prefix() const { return m_prefix; }
    TypeInfo& prefix() { return m_prefix; }

    Kind kind() const override { return Kind::Qualified; }

    void add_component(std::string_view name, std::vector<TemplateArgument*> args = {}) {
        m_components.push_back(Component{ name, std::move(args) });
    }

    const std::vector<Component>& components() const { return m_components; }
    bool is_global() const { return m_is_global; }

    std::string_view name() const {
        return m_components.empty() ? std::string_view{} : m_components.back().name;
    }

    Type* clone_into(Arena& arena) const override {
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

    void write_to(std::ostream& os) const override {
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
};

} // namespace parser_types
} // namespace walnut

#endif // WALNUT_DEPENDENT_TYPES_HPP