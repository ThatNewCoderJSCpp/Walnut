#include "Parser/Parsing/validation.hpp"

namespace walnut {
namespace parsing {

void Parser::validate_identifier(const tokenizing::Token& token) {
    std::string_view lexeme = token.lexeme();
    if (lexeme.empty()) { throw ParserError::invalid_identifier_format(reporter, token); }
    if (!std::isalpha(lexeme[0]) && lexeme[0] != '_') { throw ParserError::invalid_identifier_format(reporter, token); }
    
    for (std::size_t i = 1; i < lexeme.length(); ++i) {
        if (!std::isalnum(lexeme[i]) && lexeme[i] != '_') { throw ParserError::invalid_identifier_format(reporter, token); }
    }
}

void Parser::validate_string_literal(const tokenizing::Token& token) {
    using namespace tokenizing;
    EscapeError err = EscapeError::None;
    std::size_t at = 0;

    if (!validate_literal(token.lexeme(), err, &at)) {
        throw ParserError::invalid_escape_sequence(reporter, token, escape_error_text(err), at);
    }
}

void Parser::validate_numeric_literal(const tokenizing::Token& token) {
    const std::string_view lexeme = token.lexeme();
    bool has_digit = false;
    bool has_decimal = false;
    
    for (std::size_t i = 0; i < lexeme.length(); ++i) {
        char c = lexeme[i];

        if (std::isdigit(c)) {
            has_digit = true;
        } else if (c == '.') {
            if (has_decimal) { throw ParserError::invalid_literal(reporter, token, "Multiple decimal points in number"); }
            has_decimal = true;
        } else if (c != '-' || i > 0) {
            throw ParserError::invalid_literal(reporter, token, "Invalid character in number");
        }
    }
    
    if (!has_digit) { throw ParserError::invalid_literal(reporter, token, "Number must contain at least one digit"); }
}

void Parser::validate_character_literal(const tokenizing::Token& token) {
    using namespace tokenizing;
    std::uint32_t cp = 0;
    EscapeError err = EscapeError::None;

    if (!decode_char(token.lexeme(), cp, err)) {
        throw ParserError::invalid_escape_sequence(reporter, token, escape_error_text(err), 0);
    }
}

} // namespace parsing
} // namespace walnut
