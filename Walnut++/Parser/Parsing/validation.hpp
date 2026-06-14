#ifndef PARSER_VALIDATION_HPP
#define PARSER_VALIDATION_HPP

#include "base.hpp"

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
    std::string_view lexeme = token.lexeme();
    std::size_t pos = 0;

    while ((pos = lexeme.find('\\', pos)) != std::string::npos) {
        if (pos + 1 >= lexeme.length()) { throw ParserError::invalid_escape_sequence(reporter, token, ""); }
        const char escape_char = lexeme[pos + 1];

        if (escape_char != 'n' && 
            escape_char != 't' && 
            escape_char != 'r' && 
            escape_char != '"' && 
            escape_char != '\'' && 
            escape_char != '\\'
        ) {
            throw ParserError::invalid_escape_sequence(reporter, token, std::string(1, escape_char));
        }
        pos += 2;
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

} // namespace parsing
} // namespace walnut

#endif // PARSER_VALIDATION_HPP