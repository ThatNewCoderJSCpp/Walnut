#ifndef PARSE_NAMES_HPP
#define PARSE_NAMES_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

std::vector<std::string_view> Parser::parse_qualified_name(const char* what, bool* is_global) {
    using K = tokenizing::Token::Kind;
    if (is_global) { *is_global = match(K::DoubleColon); }

    if (current_token().kind() != K::Identifier) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, what);
    }
    
    if (is_keyword(current_token().lexeme())) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Name cannot be a keyword");
    }

    std::vector<std::string_view> parts;
    parts.push_back(current_token().lexeme());
    advance();

    while (current_token().kind() == K::DoubleColon) {
        advance();

        if (current_token().kind() != K::Identifier) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected identifier after '::' in qualified name");
        }

        parts.push_back(current_token().lexeme());
        advance();
    }

    return parts;
}

std::string Parser::join_qualified_name(const std::vector<std::string_view>& parts) {
    std::string out;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "::";
        out.append(parts[i].data(), parts[i].size());
    }

    return out;
}

std::string_view Parser::expect_string_literal(const char* what) {
    using K = tokenizing::Token::Kind;

    if (current_token().kind() != K::String) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::String}, what);
    }

    validate_string_literal(current_token());
    std::string_view s = current_token().lexeme();
    advance();
    return s;
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_NAMES_HPP