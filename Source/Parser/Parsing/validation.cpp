#include "Parser/Parsing/validation.hpp"
#include "Lexer/numeric_literal.hpp"

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
    const tokenizing::NumericLiteral lit = tokenizing::parse_numeric_literal(token.lexeme());
    if (!lit.error.empty()) throw ParserError::invalid_literal(reporter, token, lit.error);
    if (lit.digits.empty()) throw ParserError::invalid_literal(reporter, token, "Number must contain at least one digit");
}

std::size_t Parser::literal_size(const tokenizing::Token& token) {
    validate_numeric_literal(token);
    const tokenizing::NumericLiteral lit = tokenizing::parse_numeric_literal(token.lexeme());
    if (lit.is_float || lit.has_suffix) throw ParserError::invalid_literal(reporter, token, "A size must be a plain whole number");

    try {
        return static_cast<std::size_t>(std::stoull(lit.digits, nullptr, lit.base));
    } catch (...) {
        throw ParserError::invalid_literal(reporter, token, "Size is too large");
    }
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
