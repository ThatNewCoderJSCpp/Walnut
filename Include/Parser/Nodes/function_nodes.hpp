#ifndef WALNUT_FUNCTION_NODES_HPP
#define WALNUT_FUNCTION_NODES_HPP

#include "expression_nodes.hpp"
#include "operator_kind.hpp"

namespace walnut {
namespace nodes {

struct FunctionParameter : ASTNode {
    static constexpr std::size_t INFINITE_VARIADIC = std::numeric_limits<std::size_t>::max();

    std::string_view m_name;
    parser_types::TypeInfo m_type;
    ASTNode* m_initializer;
    std::size_t m_variadic_length;
    bool m_is_init_in_decl;
    semantics::Symbol* symbol = nullptr; 
    std::vector<semantics::Symbol*> pack_symbols;
    bool expanded_pack = false;

    FunctionParameter(
        std::string_view name,
        const parser_types::TypeInfo& type,
        ASTNode* initializer = nullptr,
        bool is_initialized_in_declaration = false,
        std::size_t variadic_length = 0
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::FunctionParameter; }

    std::string_view get_name() const { return m_name; }
    const parser_types::TypeInfo& get_type() const { return m_type; }
    parser_types::TypeInfo& get_type() { return m_type; }
    const ASTNode* get_initializer() const { return m_initializer; }
    bool has_initializer() const { return m_initializer != nullptr; }

    bool is_init_in_declaration() const { return m_is_init_in_decl; }
    void set_init_in_declaration(bool value) { m_is_init_in_decl = value; }

    std::size_t variadic_length() const { return m_variadic_length; }
    bool is_variadic() const { return m_variadic_length != 0; }
    bool is_infinite_variadic() const { return m_variadic_length == INFINITE_VARIADIC; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct FunctionParameters : ASTNode {
    std::vector<FunctionParameter*> m_params;

    FunctionParameters() : ASTNode(Kind::FunctionParameters) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::FunctionParameters; }

    std::size_t size() const { return m_params.size(); }
    bool empty() const { return m_params.empty(); }

    const FunctionParameter* operator[](std::size_t index) const {
        return index < m_params.size() ? m_params[index] : nullptr;
    }

    FunctionParameter* operator[](std::size_t index) {
        return index < m_params.size() ? m_params[index] : nullptr;
    }

    auto begin() const { return m_params.begin(); }
    auto end() const { return m_params.end(); }
    auto begin() { return m_params.begin(); }
    auto end() { return m_params.end(); }

    FunctionParameters& add(FunctionParameter* param) {
        m_params.push_back(param);
        return *this;
    }

    bool remove_at(std::size_t index);

    bool remove_by_name(std::string_view name);

    bool remove_last() {
        if (m_params.empty()) return false;
        m_params.pop_back();
        return true;
    }

    void clear() { m_params.clear(); }

    void set_all_init_in_declaration(bool value);

    bool has_variadic() const {
        for (const auto& p : m_params) { if (p->is_variadic()) return true; }
        return false;
    }

    bool has_defaults() const {
        for (const auto& p : m_params) { if (p->has_initializer()) return true; }
        return false;
    }

    const FunctionParameter* find_by_name(std::string_view name) const {
        for (const auto& p : m_params) { if (p->get_name() == name) return p; }
        return nullptr;
    }

    std::pair<std::size_t, std::size_t> find_duplicate_indices() const;

    bool has_duplicate_names() const;

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct FunctionDeclaration : ASTNode {
    std::vector<std::string_view> m_name_parts;
    bool m_is_global_qualified = false;
    parser_types::TypeInfo m_return_type;
    modifiers::RawModifiers m_modifiers;
    FunctionParameters* m_parameters;
    ASTNode* m_body;
    modifiers::FunctionQualifiers m_qualifiers;
    std::vector<parser_types::TemplateArgument*> m_spec_args;
    bool                                         m_is_specialization = false;
    semantics::Symbol* symbol = nullptr; 

    FunctionDeclaration(
        std::vector<std::string_view> name_parts,
        bool is_global_qualified,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params = nullptr,
        ASTNode* body = nullptr
    )
;

    FunctionDeclaration(
        std::string_view name,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params = nullptr,
        ASTNode* body = nullptr,
        bool is_const_qualified = false
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::FunctionDeclaration; }

    std::string_view get_name() const {
        return m_name_parts.empty() ? "" : m_name_parts.back();
    }

    const std::vector<std::string_view>& name_parts() const { return m_name_parts; }
    bool is_global_qualified() const { return m_is_global_qualified; }
    bool is_qualified() const { return m_name_parts.size() > 1 || m_is_global_qualified; }

    std::uint32_t get_line() const { return line; }
    const parser_types::TypeInfo& get_return_type() const { return m_return_type; }
    parser_types::TypeInfo& get_return_type() { return m_return_type; }
    const modifiers::RawModifiers& get_modifiers() const { return m_modifiers; }
    modifiers::RawModifiers& get_modifiers() { return m_modifiers; }
    const FunctionParameters* get_parameters() const { return m_parameters; }
    FunctionParameters* get_parameters() { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const ASTNode* get_body() const { return m_body; }
    ASTNode* get_body() { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    bool is_specialization() const { return m_is_specialization; }

    const modifiers::FunctionQualifiers& qualifiers() const { return m_qualifiers; }
          modifiers::FunctionQualifiers& qualifiers()       { return m_qualifiers; }

    const std::vector<parser_types::TemplateArgument*>& get_spec_args() const { return m_spec_args; }
          std::vector<parser_types::TemplateArgument*>& get_spec_args()       { return m_spec_args; }

    const FunctionParameter* find_parameter(std::string_view name) const {
        return m_parameters ? m_parameters->find_by_name(name) : nullptr;
    }

    void set_specialization(std::vector<parser_types::TemplateArgument*> args) {
        m_is_specialization = true;
        m_spec_args = std::move(args);
    }

    bool has_duplicate_parameter_names() const {
        return m_parameters && m_parameters->has_duplicate_names();
    }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct ReturnStatement : ASTNode {
    ASTNode* m_value;

    ReturnStatement(ASTNode* value = nullptr, std::uint32_t ln = 0) : ASTNode(Kind::ReturnStatement, ln), m_value(value) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ReturnStatement; }

    const ASTNode* get_value() const { return m_value; }
    ASTNode* get_value() { return m_value; }
    bool has_value() const { return m_value != nullptr; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override { auto* c = make_in<ReturnStatement>(a, clone_child(m_value, a), line); copy_base_to(c); return c; }
};

struct LambdaCaptureItem {
    enum class Mode : std::uint8_t {
        ByValue = 0,      // x
        ByReference,      // &x
        AllByValue,       // =
        AllByReference,   // &
        This,             // this
        ThisByReference,  // &this
        InitByValue       // x = <expr>
    };

    Mode mode;
    std::string_view name;  // empty for AllByValue / AllByReference
    ASTNode* init = nullptr;
    semantics::Symbol* resolved = nullptr;
    semantics::Symbol* symbol   = nullptr;

    LambdaCaptureItem(Mode m, std::string_view n = {}, ASTNode* initializer = nullptr) : mode(m), name(n), init(initializer) {}

    void write_to(std::ostream& os) const;
};

struct LambdaCaptureList : ASTNode {
    std::vector<LambdaCaptureItem> m_captures;

    LambdaCaptureList() : ASTNode(Kind::LambdaCaptureList) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::LambdaCaptureList; }

    void add(const LambdaCaptureItem& item) { m_captures.push_back(item); }
    const std::vector<LambdaCaptureItem>& captures() const { return m_captures; }
    bool empty() const { return m_captures.empty(); }
    std::size_t size() const { return m_captures.size(); }

    bool has_capture_all() const;

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct LambdaExpression : ASTNode {
    LambdaCaptureList* m_captures;
    FunctionParameters* m_parameters;
    parser_types::TypeInfo m_return_type;
    modifiers::FunctionQualifiers m_quals;
    ASTNode* m_body;
    semantics::Scope* scope = nullptr;
    LambdaExpression* generic_origin = nullptr;
    std::vector<LambdaExpression*> specializations;
    std::vector<std::vector<semantics::Type*>> spec_keys;
    std::vector<bool> spec_failed;

    LambdaExpression(
        LambdaCaptureList* captures,
        FunctionParameters* params,
        const parser_types::TypeInfo& return_type,
        const modifiers::FunctionQualifiers& quals,
        ASTNode* body,
        std::uint32_t ln = 0
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::LambdaExpression; }

    const LambdaCaptureList* get_captures() const { return m_captures; }
    bool has_captures() const { return m_captures && !m_captures->empty(); }
    const FunctionParameters* get_parameters() const { return m_parameters; }
    FunctionParameters* get_parameters() { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const parser_types::TypeInfo& get_return_type() const { return m_return_type; }
    parser_types::TypeInfo& get_return_type() { return m_return_type; }
    const modifiers::FunctionQualifiers& get_qualifiers() const { return m_quals; }
    const ASTNode* get_body() const { return m_body; }
    bool has_body() const { return m_body != nullptr; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

struct OperatorFunctionDeclaration : ASTNode {
    enum class Form : std::uint8_t { Symbol, Conversion };

    Form                                  form;
    std::vector<tokenizing::Token::Kind>  op_tokens;
    OverloadableOperator                  overload = OverloadableOperator::None;        
    parser_types::TypeInfo                conversion_type;  

    parser_types::TypeInfo                m_return_type;
    modifiers::RawModifiers               m_modifiers;
    modifiers::FunctionQualifiers         m_qualifiers;
    FunctionParameters*                   m_parameters;
    ASTNode*                              m_body;
    semantics::Symbol* symbol = nullptr; 

    OperatorFunctionDeclaration(
        std::vector<tokenizing::Token::Kind> ops,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params = nullptr,
        ASTNode* body = nullptr
    )
;

    OperatorFunctionDeclaration(
        const parser_types::TypeInfo& conv_type,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params = nullptr,
        ASTNode* body = nullptr
    )
;

    static bool classof(const ASTNode* n) { return n->kind == Kind::OperatorFunctionDeclaration; }

    bool is_conversion() const { return form == Form::Conversion; }
    bool is_symbol()     const { return form == Form::Symbol; }
    const std::vector<tokenizing::Token::Kind>& operator_tokens() const { return op_tokens; }
    const parser_types::TypeInfo& get_conversion_type() const { return conversion_type; }
    const parser_types::TypeInfo& get_return_type() const { return m_return_type; }
    const FunctionParameters* get_parameters() const { return m_parameters; }
          FunctionParameters* get_parameters()       { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const ASTNode* get_body() const { return m_body; }
    ASTNode* get_body() { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    const modifiers::FunctionQualifiers& qualifiers() const { return m_qualifiers; }
          modifiers::FunctionQualifiers& qualifiers()       { return m_qualifiers; }
    const modifiers::RawModifiers& get_modifiers() const { return m_modifiers; }
    OverloadableOperator get_overload() const { return overload; }

    void print(std::ostream& os, std::size_t indent) const;

    ASTNode* clone_into(Arena& a) const override;
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_FUNCTION_NODES_HPP