#ifndef PARSE_STATIC_ASSERT_HPP
#define PARSE_STATIC_ASSERT_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_static_assert() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::StaticAssertKeyword);
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(<condition>, \"message\")' for static assert");
    nodes::ASTNode* cond = parse_expression();
    expect(tokenizing::Token::Kind::Comma, "Expected comma to seperate static_assert condition and message");
    
    if (!current_token().is(tokenizing::Token::Kind::String)) {
        throw ParserError::unexpected_token(reporter, current_token(), {tokenizing::Token::Kind::String}, "A string message is required in static_assert");
    }

    std::string_view message = current_token().lexeme();
    advance();
    expect(tokenizing::Token::Kind::RightParen, "Expected closing ')' to end static_assert");
    return make<nodes::StaticAssertDeclaration>(message, cond, line_number);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_STATIC_ASSERT_HPP