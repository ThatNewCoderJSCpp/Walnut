#ifndef PARSE_CONTROL_FLOW_HPP
#define PARSE_CONTROL_FLOW_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_single_statement() {
    const std::size_t line_number = current_token().line();
    using V = nodes::SingleStatement::Variant;
    V variant;
    
    switch (current_token().kind()) {
        case tokenizing::Token::Kind::BreakKeyword:       variant = V::Break;       break;
        case tokenizing::Token::Kind::ContinueKeyword:    variant = V::Continue;    break;
        case tokenizing::Token::Kind::FallthroughKeyword: variant = V::Fallthrough; break;
        case tokenizing::Token::Kind::RepeatKeyword:      variant = V::Repeat;      break;
        default:
            throw ParserError::unexpected_token(
                reporter, current_token(),
                { tokenizing::Token::Kind::BreakKeyword,
                  tokenizing::Token::Kind::ContinueKeyword,
                  tokenizing::Token::Kind::FallthroughKeyword,
                  tokenizing::Token::Kind::RepeatKeyword },
                "Expected a single-keyword statement"
            );
    }

    nodes::SingleStatement node_variant = variant;  
    advance();

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) &&
        !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter, current_token(),
            tokenizing::Token::Kind::Semicolon,
            std::string(node_variant.variant_name()) + " statement must end with a semicolon"
        );
    }

    consume_semicolons();
    return make<nodes::SingleStatement>(variant, line_number);
}

nodes::ASTNode* Parser::parse_do_while_loop() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::DoKeyword);
    nodes::ASTNode* body = parse_statement();
    
    if (!match(tokenizing::Token::Kind::WhileKeyword)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::WhileKeyword,
            "Expected 'while' after do-loop body"
        );
    }

    expect(tokenizing::Token::Kind::LeftParen);

    if (current_token().is_one_of(tokenizing::Token::Kind::RightParen, tokenizing::Token::Kind::End)) {
        throw ParserError::invalid_expression(
            reporter, current_token(), "Expected condition in do-while loop"
        );
    }

    nodes::ASTNode* condition = parse_expression();
    expect(tokenizing::Token::Kind::RightParen);

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) &&  !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::Semicolon,
            "Do-while statement must end with a semicolon"
        );
    }

    consume_semicolons();
    return make<nodes::DoWhileStatement>(body, condition, line_number);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_CONTROL_FLOW_HPP