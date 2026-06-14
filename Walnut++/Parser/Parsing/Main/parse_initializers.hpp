#ifndef PARSE_INITIALIZERS_HPP
#define PARSE_INITIALIZERS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::BraceInitializerList* Parser::parse_brace_initializer_list() {
    AngleGuard _g(this, false);
    const std::size_t line_number = current_token().line();
    auto initializer = make<nodes::BraceInitializerList>(line_number);
    
    if (!match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::LeftCurly,
            "Expected '{' to start initializer list"
        );
    }
    
    if (match(tokenizing::Token::Kind::RightCurly)) { return initializer; }
    initializer->add_element(parse_expression());
    
    while (match(tokenizing::Token::Kind::Comma)) {
        if (concrete_match(tokenizing::Token::Kind::RightCurly)) { break; }
        initializer->add_element(parse_expression());
    }
    
    if (!match(tokenizing::Token::Kind::RightCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::RightCurly,
            "Expected '}' to end initializer list"
        );
    }
    
    return initializer;
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_INITIALIZERS_HPP