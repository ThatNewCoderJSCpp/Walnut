#ifndef PARSER_BASE_CLASS_HPP
#define PARSER_BASE_CLASS_HPP

#include <stdexcept>
#include "../../Lexer/token_stream.hpp"
#include "../../Common/error_reporter.hpp"

#include "../nodes.hpp"

#include "../Utility/parser_error.hpp"
#include "../Utility/type_checkers.hpp"
#include "../../Common/arena_allocator.hpp"

#include "../modifiers.hpp"

#include <deque>
#include <unordered_set>

namespace walnut {
namespace parsing {

enum class Precedence : int {
    None           = 0,
    Assignment     = 1,   // = += -= *= /= %= **= |= &= ^= <<= >>=
    Ternary        = 2,   // ?:
    LogicOr        = 3,   // ||
    LogicAnd       = 4,   // &&
    BitwiseOr      = 5,   // |
    BitwiseXor     = 6,   // ^
    BitwiseAnd     = 7,   // &
    Equality       = 8,   // == !=
    Comparison     = 9,   // < > <= >=
    Shift          = 10,  // << >>
    Additive       = 11,  // + -
    Multiplicative = 12,  // * / %
    Unary          = 13,  // - ! ~ ++ -- * &
    Power          = 14,  // ** (right associative)
    Postfix        = 15,  // ++ -- [] ()
    Primary        = 16
};

inline int to_int(Precedence p) { return static_cast<int>(p); }

class Parser {
private:
    tokenizing::TokenStream& tokens;
    const tokenizing::Token* m_cur_tok;
    std::size_t              nesting_depth = 0;
    std::uint32_t            m_node_order  = 1;
    bool                     m_in_template_args = false;
    std::size_t              m_pending_close_angle = 0;
    std::unordered_set<std::string_view> m_template_names;
    std::unordered_set<std::string_view> m_concept_names;
    Arena&                   arena;
    ErrorReporter&           reporter;

    struct AngleGuard {
        Parser* self;
        bool prev;
        AngleGuard(Parser* s, bool v) : self(s), prev(s->m_in_template_args) { s->m_in_template_args = v; }
        ~AngleGuard() { self->m_in_template_args = prev; }
        AngleGuard(const AngleGuard&) = delete;
        AngleGuard& operator=(const AngleGuard&) = delete;
    };

    struct QualifiedTypeComponent {
        std::string_view                             name;
        std::vector<parser_types::TemplateArgument*> args;
    };

public:
    Parser(tokenizing::TokenStream& stream, ErrorReporter& rep, Arena& ar) : tokens(stream), m_cur_tok(&tokens.current()), arena(ar), reporter(rep) {}

    nodes::BlockStatement*   parse_program();
    const tokenizing::Token& current_token() const noexcept { return *m_cur_tok; }
    tokenizing::TokenStream& get_token_stream() { return tokens; }
    Arena&                   get_arena() { return arena; }
    ErrorReporter&           get_reporter() { return reporter; }

    void register_concept_name(std::string_view name) { if (!name.empty()) { m_concept_names.insert(name); } }
    bool is_known_concept(std::string_view name) const { return !name.empty() && m_concept_names.count(name) != 0; }

private:
    template<typename T, typename... Args, typename = typename std::enable_if<std::is_base_of<nodes::ASTNode, T>::value>::type>
    T* make(Args&&... args) {
        T* node = arena.alloc<T>(std::forward<Args>(args)...);
        node->file_id = current_token().file_id();
        node->order   = m_node_order++; 
        return node;
    }

    template<typename T, typename... Args, typename = typename std::enable_if<!std::is_base_of<nodes::ASTNode, T>::value>::type, typename = void>  
    T* make(Args&&... args) {
        return arena.alloc<T>(std::forward<Args>(args)...);
    }

    const tokenizing::Token& token_at(std::size_t k) { return k == 0 ? current_token() : lookahead(k); }

    void                     advance();
    void                     synchronize();
    void                     expect(tokenizing::Token::Kind kind);
    void                     expect(tokenizing::Token::Kind kind, const std::string& msg);
    bool                     match(tokenizing::Token::Kind kind);
    void                     check_nesting_depth(const tokenizing::Token& token, const std::string& construct_type) const;
    const tokenizing::Token& peek();
    bool                     concrete_match(tokenizing::Token::Kind kind) const noexcept;
    const tokenizing::Token& lookahead(std::size_t n = 1);
    void                     consume_semicolons();

private:
    void validate_identifier(const tokenizing::Token& token);
    void validate_string_literal(const tokenizing::Token& token);
    void validate_numeric_literal(const tokenizing::Token& token);
    void validate_character_literal(const tokenizing::Token& token);
    void parse_indirection_qualifiers(parser_types::TypeInfo& info);

private:
    std::vector<std::string_view> parse_qualified_name(const char* what, bool* is_global = nullptr);
    std::string                   join_qualified_name(const std::vector<std::string_view>& parts);
    std::string_view              expect_string_literal(const char* what);

private:
    bool               looks_like_user_type_declaration();
    bool               looks_like_special_member();        
    bool               looks_like_operator_function();    
    bool               looks_like_for_each();
    bool               looks_like_declaration();
    bool               fold_ahead();
    nodes::ASTNode*    parse_call_argument();
    bool               looks_like_constrained_param();    
    static  bool is_fold_operator(tokenizing::Token::Kind k);
    static  bool is_indirection_token(tokenizing::Token::Kind k);

private:
    nodes::ASTNode* parse_statement();
    nodes::ASTNode* parse_single_statement();
    nodes::ASTNode* parse_for_loop();
    nodes::ASTNode* parse_for_each_loop();
    nodes::ASTNode* parse_while_loop();
    nodes::ASTNode* parse_do_while_loop();
    nodes::ASTNode* parse_if_statement();
    nodes::ASTNode* parse_switch_statement();
    nodes::ASTNode* parse_try_catch_statement();
    nodes::ASTNode* parse_return_statement();
    nodes::ASTNode* parse_co_return_statement();
    nodes::ASTNode* parse_expression_statement();
    nodes::ASTNode* parse_namespace_declaration();
    nodes::ASTNode* parse_enum_declaration(const modifiers::RawModifiers& mods, std::size_t line);

    nodes::ASTNode* parse_variable_declaration(
        const modifiers::RawModifiers& mods,
        std::size_t line_number,
        bool require_semicolon = false
    );

    nodes::ASTNode* parse_function_declaration(
        const modifiers::RawModifiers& mods,
        std::size_t line_number
    );

    nodes::ASTNode* parse_array_declaration(
        const modifiers::RawModifiers& mods,
        std::size_t line_number
    );

    nodes::ASTNode* parse_using_declaration();
    nodes::ASTNode* parse_typedef_declaration();
    nodes::ASTNode* parse_static_assert();

private:
    nodes::ASTNode* parse_expression();
    nodes::ASTNode* parse_primary_base();
    nodes::ASTNode* parse_primary();
    nodes::ASTNode* parse_expression_pratt(int min_precedence);
    nodes::ASTNode* parse_prefix();
    nodes::ASTNode* parse_atom();
    nodes::ASTNode* parse_infix(nodes::ASTNode* left, tokenizing::Token::Kind op, Precedence prec);
    nodes::ASTNode* parse_identifier_or_qualified();
    nodes::ASTNode* parse_cast_expression();
    nodes::ASTNode* parse_new_expression();
    nodes::ASTNode* parse_delete_expression();
    nodes::ASTNode* parse_noexcept_expression();
    nodes::ASTNode* parse_fold_expression();
    nodes::ASTNode* parse_discard_expression();
    nodes::ASTNode* parse_requires_expression();
    nodes::ASTNode* parse_throw_expression();

    nodes::ASTNode* parse_type_query_expression();
    SmallVector<std::string_view, 4> parse_binding_names();

private:
    parser_types::TypeInfo   parse_type_info();
    parser_types::TypeInfo   parse_type_only();
    parser_types::TypeInfo   parse_return_type();
    parser_types::TypeInfo   parse_function_pointer_type();
    parser_types::TypeInfo   parse_array_type();
    parser_types::TypeInfo   parse_variant_type();
    modifiers::RawModifiers  parse_raw_modifiers();

private:
    nodes::FunctionParameter*     parse_function_parameter();
    nodes::FunctionParameters*    parse_function_parameters();
    modifiers::FunctionQualifiers parse_function_qualifiers();
    nodes::ASTNode*               parse_operator_function(const modifiers::RawModifiers& mods, std::size_t line_number);

private:
    void                                   parse_lambda_capture_item(nodes::LambdaCaptureList* captures);
    nodes::LambdaCaptureList*              parse_lambda_capture_list();
    nodes::ASTNode*                        parse_lambda_expression();
    nodes::RequiresExpression::Requirement parse_requirement();

private:
    nodes::ASTNode* parse_record_declaration(const modifiers::RawModifiers& mods, std::size_t line_number);
    nodes::ASTNode* parse_constructor_declaration(const modifiers::RawModifiers& mods, std::size_t line_number);
    nodes::ASTNode* parse_destructor_declaration(const modifiers::RawModifiers& mods, std::size_t line_number);

private:
    nodes::ASTNode*                              parse_template_declaration();
    nodes::ASTNode*                              parse_template_value_expr();
    nodes::TemplateParameter*                    parse_template_parameter();
    std::vector<parser_types::TemplateArgument*> parse_template_argument_list_body();
    parser_types::TemplateArgument*              parse_alignas();
    parser_types::TemplateArgument*       parse_type_or_value_operand(bool force_type = false);

    std::size_t             scan_paired(std::size_t open_off, tokenizing::Token::Kind open, tokenizing::Token::Kind close);
    std::size_t             scan_angle_close(std::size_t open_off, bool* leftover = nullptr);
    bool                    template_call_ahead();
    bool                    looks_like_type_argument(bool allow_paren_terminator = false);
    bool                    match_template_close();
    bool                    template_scope_ahead();
    bool                    template_value_ahead();
    bool                    template_ambiguous_ahead();
    bool                    is_known_template(const nodes::ASTNode* n) const;
    void                    register_template_name(std::string_view name);
    static std::string_view template_probe_key(const nodes::ASTNode* n);

private:
    nodes::ASTNode* parse_concept_declaration();

private:
    nodes::ASTNode*         parse_module_declaration();
    nodes::ASTNode*         parse_import_export(nodes::ImportExportDeclaration::Direction dir);
    nodes::ImportExportItem parse_import_export_item(bool is_import);

private:
    nodes::BlockStatement*       parse_block(bool require_body = true);
    nodes::BraceInitializerList* parse_brace_initializer_list();
};

} // namespace parsing
} // namespace walnut

#endif // PARSER_BASE_CLASS_HPP