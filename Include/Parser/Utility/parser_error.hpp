#ifndef PARSER_ERROR_HPP
#define PARSER_ERROR_HPP

#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "../../Lexer/token_macro.hpp"
#include "../../Common/error_reporter.hpp"

namespace walnut {
namespace parsing {

class ParserError : public std::runtime_error {
private:
    static std::string format_token_list(const std::vector<tokenizing::Token::Kind>& expected_tokens);

    static std::string build_error_header(const tokenizing::Token& token, const std::string& error_type) {
        std::stringstream ss;
        ss << error_type << " Error on line " << token.line() << ": ";
        return ss.str();
    }

    static std::string format_code_context(const tokenizing::Token& token, const std::string& context);

    static std::string format_token_info(const tokenizing::Token& token);

    static ParserError make_and_report(ErrorReporter& reporter, std::size_t line, const std::string& message) {
        reporter.report(ErrorPhase::Parser, line, message);
        return ParserError(message);
    }

    static ParserError make_and_report(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message);

public:
    static ParserError unexpected_token(ErrorReporter& reporter, const tokenizing::Token& token, const std::vector<tokenizing::Token::Kind>& expected_tokens, const std::string& context);

    static ParserError unexpected_token(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        std::string_view expected_description,
        const std::string& context
    );

    static ParserError invalid_expression(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message);

    static ParserError missing_token(ErrorReporter& reporter, const tokenizing::Token& token, tokenizing::Token::Kind expected, const std::string& context);

    static ParserError invalid_operator(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& op_type);

    static ParserError max_nesting_exceeded(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& construct_type);

    static ParserError dangling_operator(ErrorReporter& reporter, const tokenizing::Token& token);

    static ParserError invalid_literal(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& details);

    static ParserError mismatched_delimiters(ErrorReporter& reporter, const tokenizing::Token& token, char opening, char closing, const std::string& delimiter_type);

    static ParserError invalid_identifier_format(ErrorReporter& reporter, const tokenizing::Token& token);

    static ParserError unexpected_end_of_input(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& context);

    static ParserError invalid_escape_sequence(
        ErrorReporter& reporter, const tokenizing::Token& token,
        const std::string& why, std::size_t offset = 0
    );

    static ParserError empty_block(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& block_type);

    static ParserError duplicate_type_modifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& identifier_name = "");

    static ParserError duplicate_function_qualifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& func_name = "");

    static ParserError namespace_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& opening_name,
        const std::string& closing_name
    );

    static ParserError function_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& opening_name,
        const std::string& closing_name
    );

    static ParserError record_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& keyword,       
        const std::string& opening_name,
        const std::string& closing_name
    );

    static ParserError ambiguous_template(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& name);

    static ParserError invalid_operator_overload(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::vector<tokenizing::Token::Kind>& op_tokens
    );

    ParserError(const std::string& message) : std::runtime_error(message) {}
    ParserError(const std::size_t line, const std::string& message) : std::runtime_error("Error on line " + std::to_string(line) + ": " + message) {}
    static constexpr std::size_t MAX_NESTING_DEPTH = std::numeric_limits<std::size_t>::max() - 1;
};

} // namespace parsing
} // namespace walnut

#endif // PARSER_ERROR_HPP