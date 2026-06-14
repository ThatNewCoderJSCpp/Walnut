#ifndef WALNUT_FUNCTION_NODES_HPP
#define WALNUT_FUNCTION_NODES_HPP

#include "expression_nodes.hpp"

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

    FunctionParameter(
        std::string_view name,
        const parser_types::TypeInfo& type,
        ASTNode* initializer = nullptr,
        bool is_initialized_in_declaration = false,
        std::size_t variadic_length = 0
    )
        : ASTNode(Kind::FunctionParameter)
        , m_name(name)
        , m_type(type)
        , m_initializer(initializer)
        , m_variadic_length(variadic_length)
        , m_is_init_in_decl(is_initialized_in_declaration)
    {}

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

    void print(std::ostream& os, std::size_t indent) const {
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

    bool remove_at(std::size_t index) {
        if (index >= m_params.size()) return false;
        m_params.erase(m_params.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool remove_by_name(std::string_view name) {
        auto it = std::find_if(m_params.begin(), m_params.end(), [&name](const auto& p) { return p->get_name() == name; });
        
        if (it != m_params.end()) {
            m_params.erase(it);
            return true;
        }

        return false;
    }

    bool remove_last() {
        if (m_params.empty()) return false;
        m_params.pop_back();
        return true;
    }

    void clear() { m_params.clear(); }

    void set_all_init_in_declaration(bool value) {
        for (auto& param : m_params) {
            if (param->has_initializer()) { param->set_init_in_declaration(value); }
        }
    }

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

    std::pair<std::size_t, std::size_t> find_duplicate_indices() const {
        std::unordered_map<std::string_view, std::size_t> seen;

        for (std::size_t i = 0; i < m_params.size(); ++i) {
            std::string_view name = m_params[i]->get_name();
            auto [it, inserted] = seen.emplace(name, i);
            if (!inserted) { return {it->second, i}; }
        }

        return {std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max()};
    }

    bool has_duplicate_names() const {
        auto [first, second] = find_duplicate_indices();
        return first != std::numeric_limits<std::size_t>::max();
    }

    void print(std::ostream& os, std::size_t indent) const {
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
};

struct FunctionDeclaration : ASTNode {
    std::vector<std::string_view> m_name_parts;
    bool m_is_global_qualified = false;
    parser_types::TypeInfo m_return_type;
    modifiers::RawModifiers m_modifiers;
    FunctionParameters* m_parameters;
    ASTNode* m_body;
    modifiers::FunctionQualifiers m_qualifiers;
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
        : ASTNode(Kind::FunctionDeclaration, ln)
        , m_name_parts(std::move(name_parts))
        , m_is_global_qualified(is_global_qualified)
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_parameters(params)
        , m_body(body)
        , m_qualifiers(qualifiers)
    {}

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
        : ASTNode(Kind::FunctionDeclaration, ln)
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_parameters(params)
        , m_body(body)
        , m_qualifiers(qualifiers)
    {
        m_name_parts.push_back(name);
    }

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
    modifiers::FunctionQualifiers qualifiers() const { return m_qualifiers; }

    const FunctionParameter* find_parameter(std::string_view name) const {
        return m_parameters ? m_parameters->find_by_name(name) : nullptr;
    }

    bool has_duplicate_parameter_names() const {
        return m_parameters && m_parameters->has_duplicate_names();
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "FunctionDeclaration\n";
        print_indent(os, indent + 1);
        os << "Name: ";
        if (m_is_global_qualified) os << "::";

        for (std::size_t i = 0; i < m_name_parts.size(); ++i) {
            if (i > 0) os << "::";
            os << m_name_parts[i];
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
};

struct ReturnStatement : ASTNode {
    ASTNode* m_value;

    ReturnStatement(ASTNode* value = nullptr, std::uint32_t ln = 0) : ASTNode(Kind::ReturnStatement, ln), m_value(value) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::ReturnStatement; }

    const ASTNode* get_value() const { return m_value; }
    bool has_value() const { return m_value != nullptr; }
    std::uint32_t get_line() const { return line; }

    void print(std::ostream& os, std::size_t indent) const {
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

    void write_to(std::ostream& os) const {
        switch (mode) {
            case Mode::ByValue:         os << name; break;
            case Mode::ByReference:     os << "&" << name; break;
            case Mode::AllByValue:      os << "="; break;
            case Mode::AllByReference:  os << "&"; break;
            case Mode::This:            os << "this"; break;
            case Mode::ThisByReference: os << "&this"; break;
        }
    }
};

struct LambdaCaptureList : ASTNode {
    std::vector<LambdaCaptureItem> m_captures;

    LambdaCaptureList() : ASTNode(Kind::LambdaCaptureList) {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::LambdaCaptureList; }

    void add(const LambdaCaptureItem& item) { m_captures.push_back(item); }
    const std::vector<LambdaCaptureItem>& captures() const { return m_captures; }
    bool empty() const { return m_captures.empty(); }
    std::size_t size() const { return m_captures.size(); }

    bool has_capture_all() const {
        for (const auto& c : m_captures) {
            if (c.mode == LambdaCaptureItem::Mode::AllByValue || c.mode == LambdaCaptureItem::Mode::AllByReference) return true;
        }

        return false;
    }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "CaptureList [";

        for (std::size_t i = 0; i < m_captures.size(); ++i) {
            if (i > 0) os << ", ";
            m_captures[i].write_to(os);
        }

        os << "]\n";
    }
};

struct LambdaExpression : ASTNode {
    LambdaCaptureList* m_captures;
    FunctionParameters* m_parameters;
    parser_types::TypeInfo m_return_type;
    modifiers::FunctionQualifiers m_quals;
    ASTNode* m_body;
    semantics::Scope* scope = nullptr;

    LambdaExpression(
        LambdaCaptureList* captures,
        FunctionParameters* params,
        const parser_types::TypeInfo& return_type,
        const modifiers::FunctionQualifiers& quals,
        ASTNode* body,
        std::uint32_t ln = 0
    )
        : ASTNode(Kind::LambdaExpression, ln)
        , m_captures(captures)
        , m_parameters(params)
        , m_return_type(return_type)
        , m_quals(quals)
        , m_body(body)
    {}

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

    void print(std::ostream& os, std::size_t indent) const {
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
};

struct OperatorFunctionDeclaration : ASTNode {
    enum class Form : std::uint8_t { Symbol, Conversion };

    Form                                  form;
    std::vector<tokenizing::Token::Kind>  op_tokens;        
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
        : ASTNode(Kind::OperatorFunctionDeclaration, ln)
        , form(Form::Symbol)
        , op_tokens(std::move(ops))
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_qualifiers(qualifiers)
        , m_parameters(params)
        , m_body(body)
    {}

    OperatorFunctionDeclaration(
        const parser_types::TypeInfo& conv_type,
        const parser_types::TypeInfo& return_type,
        const modifiers::RawModifiers& modifiers,
        const modifiers::FunctionQualifiers& qualifiers,
        std::uint32_t ln,
        FunctionParameters* params = nullptr,
        ASTNode* body = nullptr
    )
        : ASTNode(Kind::OperatorFunctionDeclaration, ln)
        , form(Form::Conversion)
        , conversion_type(conv_type)
        , m_return_type(return_type)
        , m_modifiers(modifiers)
        , m_qualifiers(qualifiers)
        , m_parameters(params)
        , m_body(body)
    {}

    static bool classof(const ASTNode* n) { return n->kind == Kind::OperatorFunctionDeclaration; }

    bool is_conversion() const { return form == Form::Conversion; }
    bool is_symbol()     const { return form == Form::Symbol; }
    const std::vector<tokenizing::Token::Kind>& operator_tokens() const { return op_tokens; }
    const parser_types::TypeInfo& get_conversion_type() const { return conversion_type; }
    const parser_types::TypeInfo& get_return_type() const { return m_return_type; }
    const FunctionParameters* get_parameters() const { return m_parameters; }
    bool has_parameters() const { return m_parameters && !m_parameters->empty(); }
    const ASTNode* get_body() const { return m_body; }
    bool has_body() const { return m_body != nullptr; }
    modifiers::FunctionQualifiers qualifiers() const { return m_qualifiers; }
    const modifiers::RawModifiers& get_modifiers() const { return m_modifiers; }

    void print(std::ostream& os, std::size_t indent) const {
        print_indent(os, indent);
        os << "OperatorFunctionDeclaration\n";
        print_indent(os, indent + 1);

        if (form == Form::Conversion) {
            os << "Conversion to: " << conversion_type << "\n";
        } else {
            os << "Operator: ";

            for (std::size_t i = 0; i < op_tokens.size(); ++i) {
                if (i) os << ' ';
                os << tokenizing::Token::name_fast(op_tokens[i]);
            }
            
            os << "\n";
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
};

} // namespace nodes
} // namespace walnut

#endif // WALNUT_FUNCTION_NODES_HPP