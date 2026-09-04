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
    static std::string format_token_list(const std::vector<tokenizing::Token::Kind>& expected_tokens) {
        std::stringstream ss;
        if (expected_tokens.empty()) return "";
        if (expected_tokens.size() == 1) { return std::string(tokenizing::Token::name_fast(expected_tokens[0])); }
        
        for (std::size_t i = 0; i < expected_tokens.size(); ++i) {
            if (i > 0) {
                if (i == 1 && expected_tokens.size() == 2) {
                    ss << " or ";
                } else if (i == expected_tokens.size() - 1) {
                    ss << ", or ";
                } else {
                    ss << ", ";
                }
            }
            
            ss << tokenizing::Token::name_fast(expected_tokens[i]);
        }

        return ss.str();
    }

    static std::string build_error_header(const tokenizing::Token& token, const std::string& error_type) {
        std::stringstream ss;
        ss << error_type << " Error on line " << token.line() << ": ";
        return ss.str();
    }

    static std::string format_code_context(const tokenizing::Token& token, const std::string& context) {
        if (context.empty()) return "";
        return "\nContext: " + context + " (line " + std::to_string(token.line()) + ")";
    }

    static std::string format_token_info(const tokenizing::Token& token) {
        std::string lexeme = std::string(token.lexeme());
        if (!lexeme.empty()) { return " '" + lexeme + "'"; }
        return "";
    }

    static ParserError make_and_report(ErrorReporter& reporter, std::size_t line, const std::string& message) {
        reporter.report(ErrorPhase::Parser, line, message);
        return ParserError(message);
    }

    static ParserError make_and_report(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message) {
        reporter.report(ErrorPhase::Parser, token.file_id(), token.line(), message);
        return ParserError(message);
    }

public:
    static ParserError unexpected_token(ErrorReporter& reporter, const tokenizing::Token& token, const std::vector<tokenizing::Token::Kind>& expected_tokens, const std::string& context) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Unexpected " << tokenizing::Token::name_fast(token.kind()) << format_token_info(token);
        if (!expected_tokens.empty()) { ss << "\nExpected: " << format_token_list(expected_tokens); }
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError unexpected_token(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        std::string_view expected_description,
        const std::string& context
    ) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Unexpected " << tokenizing::Token::name_fast(token.kind()) << format_token_info(token);
        ss << "\nExpected: " << expected_description;
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_expression(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& message) {
        std::stringstream ss;
        ss << build_error_header(token, "Expression");
        ss << message;
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError missing_token(ErrorReporter& reporter, const tokenizing::Token& token, tokenizing::Token::Kind expected, const std::string& context) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Missing " << tokenizing::Token::name_fast(expected);
        ss << format_code_context(token, context);
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_operator(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& op_type) {
        std::stringstream ss;
        ss << build_error_header(token, "Operator");
        ss << "Invalid " << op_type << " operator" << format_token_info(token);
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError max_nesting_exceeded(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& construct_type) {
        std::stringstream ss;
        ss << build_error_header(token, "Nesting");
        ss << "Maximum nesting depth exceeded for " << construct_type;
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError dangling_operator(ErrorReporter& reporter, const tokenizing::Token& token) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Dangling operator" << format_token_info(token);
        ss << "\nOperators must have operands on both sides";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_literal(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& details) {
        std::stringstream ss;
        ss << build_error_header(token, "Literal");
        ss << "Invalid literal value" << format_token_info(token);
        if (!details.empty()) { ss << "\n" << details; }
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError mismatched_delimiters(ErrorReporter& reporter, const tokenizing::Token& token, char opening, char closing, const std::string& delimiter_type) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Mismatched " << delimiter_type << ": found '" << closing << "' but expected '" << opening << "'";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_identifier_format(ErrorReporter& reporter, const tokenizing::Token& token) {
        std::stringstream ss;
        ss << build_error_header(token, "Identifier");
        ss << "Invalid identifier format" << format_token_info(token);
        ss << "\nIdentifiers must:\n";
        ss << "- Start with a letter or underscore\n";
        ss << "- Contain only letters, numbers, and underscores\n";
        ss << "- Not be a reserved keyword";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError unexpected_end_of_input(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& context) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Unexpected end of input while parsing " << context;
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_escape_sequence(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& sequence) {
        std::stringstream ss;
        ss << build_error_header(token, "String");
        ss << "Invalid escape sequence '\\" << sequence << "'";
        ss << "\nValid escape sequences are: \\n, \\t, \\r, \\\", \\\', \\\\";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError empty_block(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& block_type) {
        std::stringstream ss;
        ss << build_error_header(token, "Structure");
        ss << "Empty " << block_type << " block\n";
        ss << "Block must contain at least one statement";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError duplicate_type_modifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& identifier_name = "") {
        std::stringstream ss;
        ss << build_error_header(token, "Modifier Duplication");
        ss << "Excess type modifier '" << tokenizing::Token::name_fast(token.kind()) << "'";
        if (!identifier_name.empty()) { ss << " on identifier '" << identifier_name << "'"; }
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError duplicate_function_qualifiers(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& func_name = "") {
        std::stringstream ss;
        ss << build_error_header(token, "Function Qualifier Duplication");
        ss << "Excess function qualifier '" << tokenizing::Token::name_fast(token.kind()) << "'";
        if (!func_name.empty()) { ss << " on function '" << func_name << "'"; }
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError namespace_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& opening_name,
        const std::string& closing_name
    ) {
        std::stringstream ss;
        ss << build_error_header(token, "Namespace");
        ss << "Closing namespace name '" << closing_name << "' does not match opening name '" << opening_name << "'";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError function_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& opening_name,
        const std::string& closing_name
    ) {
        std::stringstream ss;
        ss << build_error_header(token, "Function");
        ss << "Closing function name '" << closing_name << "' does not match opening name '" << opening_name << "'";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError record_mismatch(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::string& keyword,       
        const std::string& opening_name,
        const std::string& closing_name
    ) {
        std::stringstream ss;
        ss << build_error_header(token, "Type");
        ss << "Closing " << keyword << " name '" << closing_name << "' does not match opening name '" << opening_name << "'";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError ambiguous_template(ErrorReporter& reporter, const tokenizing::Token& token, const std::string& name) {
        std::stringstream ss;
        ss << build_error_header(token, "Syntax");
        ss << "Ambiguous '<' after '" << name << "'";
        ss << format_token_info(token);
        ss << "\nThis could be a template instantiation or a comparison, and '" << name << "' is not a known template.";
        ss << "\nDisambiguate with turbofish (e.g. '" << name << "::<...>') or parenthesize the comparison.";
        return make_and_report(reporter, token, ss.str());
    }

    static ParserError invalid_operator_overload(
        ErrorReporter& reporter,
        const tokenizing::Token& token,
        const std::vector<tokenizing::Token::Kind>& op_tokens
    ) {
        std::string spelling;

        for (std::size_t i = 0; i < op_tokens.size(); ++i) {
            if (i) spelling += ' ';
            spelling += tokenizing::Token::name_fast(op_tokens[i]);
        }
        
        std::stringstream ss;
        ss << build_error_header(token, "Operator");
        ss << "'" << spelling << "' is not an overloadable operator";
        return make_and_report(reporter, token, ss.str());
    }

    ParserError(const std::string& message) : std::runtime_error(message) {}
    ParserError(const std::size_t line, const std::string& message) : std::runtime_error("Error on line " + std::to_string(line) + ": " + message) {}
    static constexpr std::size_t MAX_NESTING_DEPTH = std::numeric_limits<std::size_t>::max() - 1;
};

} // namespace parsing
} // namespace walnut

#endif // PARSER_ERROR_HPP