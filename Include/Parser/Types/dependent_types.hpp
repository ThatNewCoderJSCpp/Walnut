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

    Type* clone_into(Arena& arena) const override;

    void write_to(std::ostream& os) const override;

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

    Type* clone_into(Arena& arena) const override;

    void write_to(std::ostream& os) const override;

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

    Type* clone_into(Arena& arena) const override;

    void write_to(std::ostream& os) const override;

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

    Type* clone_into(Arena& arena) const override;

    void write_to(std::ostream& os) const override;
};

} // namespace parser_types
} // namespace walnut

#endif // WALNUT_DEPENDENT_TYPES_HPP