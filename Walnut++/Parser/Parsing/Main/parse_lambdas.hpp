#ifndef PARSE_LAMBDA_FUNCTIONS_HPP
#define PARSE_LAMBDA_FUNCTIONS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

void Parser::parse_lambda_capture_item(nodes::LambdaCaptureList* captures) {
    using Mode = nodes::LambdaCaptureItem::Mode;

    if (match(tokenizing::Token::Kind::Equal)) {
        captures->add(nodes::LambdaCaptureItem(Mode::AllByValue));
    } else if (concrete_match(tokenizing::Token::Kind::Ampersand)) {
        advance();

        if (concrete_match(tokenizing::Token::Kind::RightSquare) || concrete_match(tokenizing::Token::Kind::Comma)) {
            captures->add(nodes::LambdaCaptureItem(Mode::AllByReference));
        } else if (current_token().kind() == tokenizing::Token::Kind::ThisKeyword) {
            advance();
            captures->add(nodes::LambdaCaptureItem(Mode::ThisByReference));
        } else if (current_token().kind() == tokenizing::Token::Kind::Identifier) {
            std::string_view name = current_token().lexeme();
            advance();
            captures->add(nodes::LambdaCaptureItem(Mode::ByReference, name));
        } else {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '&' in capture list"
            );
        }
    } else if (current_token().kind() == tokenizing::Token::Kind::ThisKeyword) {
        advance();
        captures->add(nodes::LambdaCaptureItem(Mode::This));
    } else if (current_token().kind() == tokenizing::Token::Kind::Identifier) {
        std::string_view name = current_token().lexeme();
        advance();
        
        if (match(tokenizing::Token::Kind::Equal)) {            
            nodes::ASTNode* init = parse_expression_pratt(to_int(Precedence::Assignment));
            captures->add(nodes::LambdaCaptureItem(Mode::InitByValue, name, init));
        } else {
            captures->add(nodes::LambdaCaptureItem(Mode::ByValue, name));
        }
    } else {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected capture item in lambda capture list"
        );
    }
}

nodes::LambdaCaptureList* Parser::parse_lambda_capture_list() {
    auto captures = make<nodes::LambdaCaptureList>();
    if (concrete_match(tokenizing::Token::Kind::RightSquare)) { return captures; }
    parse_lambda_capture_item(captures);
    while (match(tokenizing::Token::Kind::Comma)) { parse_lambda_capture_item(captures); }
    return captures;
}

nodes::ASTNode* Parser::parse_lambda_expression() {
    const std::size_t line = current_token().line();
    auto captures = parse_lambda_capture_list();
    expect(tokenizing::Token::Kind::RightSquare, "Expected ']' to close lambda capture list");
    auto parameters = parse_function_parameters();
    auto quals = parse_function_qualifiers();
    parser_types::TypeInfo return_type;
    bool has_arrow = match(tokenizing::Token::Kind::SingleRightArrow) || match(tokenizing::Token::Kind::DoubleRightArrow);

    if (has_arrow) {
        return_type = parse_return_type();
    } else if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        return_type.type = parser_types::PrimitiveType::make_dynamic(arena);
    } else {
        throw ParserError::invalid_expression(
            reporter,
            current_token(),
            "Lambda expression requires a body (optional: return type)"
        );
    }

    nodes::ASTNode* body = nullptr;
    if (concrete_match(tokenizing::Token::Kind::LeftCurly)) { body = parse_block(); }
    return make<nodes::LambdaExpression>(captures, parameters, return_type, quals, body, line);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_LAMBDA_FUNCTIONS_HPP