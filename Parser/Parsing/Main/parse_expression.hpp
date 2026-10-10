#ifndef PARSE_EXPRESSION_HPP
#define PARSE_EXPRESSION_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_expression_statement() {
    nodes::ASTNode* expr = parse_expression();

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::Semicolon,
            "Expression statement must end with a semicolon" 
        );
    }

    consume_semicolons();
    return make<nodes::ExpressionStatement>(expr);
}

nodes::ASTNode* Parser::parse_expression() {
    const std::size_t line = current_token().line();
    nodes::ASTNode* expr = parse_expression_pratt(0);
    expr->line = line;
    return expr;  
}

nodes::ASTNode* Parser::parse_identifier_or_qualified() {
    const std::size_t line_num = current_token().line();
    bool is_global = false;

    if (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        is_global = true;
        advance();

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '::' in qualified name"
            );
        }
    }

    std::string_view first = current_token().lexeme();
    advance();

    if (!is_global && current_token().kind() != tokenizing::Token::Kind::DoubleColon) {
        return make<nodes::Identifier>(first, line_num);
    }

    auto* qualified = make<nodes::QualifiedIdentifier>(line_num, is_global);
    qualified->add_part(first);

    while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        if (lookahead(1).kind() == tokenizing::Token::Kind::LessThan) { break; }  // turbofish: leave '::<' 
        advance();

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '::' in qualified name"
            );
        }

        qualified->add_part(current_token().lexeme());
        advance();
    }

    return qualified;
}

nodes::ASTNode* Parser::parse_primary_base() {
    if (current_token().kind() == tokenizing::Token::Kind::String) { validate_string_literal(current_token()); }
    if (current_token().kind() == tokenizing::Token::Kind::Identifier) { validate_identifier(current_token()); }
    if (is_basic_arithmetic_operator(current_token())) { throw ParserError::dangling_operator(reporter, current_token()); }
    if (current_token().kind() == tokenizing::Token::Kind::RightParen) { throw ParserError::mismatched_delimiters(reporter, current_token(), '(', ')', "parentheses"); }
    if (current_token().kind() == tokenizing::Token::Kind::RightSquare) { throw ParserError::mismatched_delimiters(reporter, current_token(), '[', ']', "brackets"); }
    if (current_token().kind() == tokenizing::Token::Kind::RightCurly) { throw ParserError::mismatched_delimiters(reporter, current_token(), '{', '}', "braces"); }
    return parse_primary(); 
}

nodes::ASTNode* Parser::parse_primary() {
    const std::size_t line_num = current_token().line();
    if (current_token().kind() == tokenizing::Token::Kind::LeftCurly) { return parse_brace_initializer_list(); }
    
    if (current_token().is_one_of(
        tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float, 
        tokenizing::Token::Kind::String, tokenizing::Token::Kind::TextLiteral,
        tokenizing::Token::Kind::Character, tokenizing::Token::Kind::True, tokenizing::Token::Kind::False,
        tokenizing::Token::Kind::NullptrKeyword, tokenizing::Token::Kind::InfinityKeyword,
        tokenizing::Token::Kind::ThisKeyword   
    )) {
        if (current_token().is_one_of(tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float)) {
            validate_numeric_literal(current_token());
        } else if (current_token().is_one_of(tokenizing::Token::Kind::String, tokenizing::Token::Kind::TextLiteral)) {
            validate_string_literal(current_token());
        }
        
        auto literal = make<nodes::Literal>(current_token().lexeme(), current_token().kind(), line_num);
        advance();
        return literal;
    }
    
    if (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        return parse_identifier_or_qualified();
    }

    if (current_token().is_one_of(
        tokenizing::Token::Kind::Identifier,
        tokenizing::Token::Kind::AutoDeclaration,
        tokenizing::Token::Kind::DynamicDeclaration
    )) {
        if (!current_token().is(tokenizing::Token::Kind::Identifier)) {
            std::string_view identifier_name = current_token().lexeme();
            advance();
            return make<nodes::Identifier>(identifier_name, line_num);
        }

        return parse_identifier_or_qualified();
    }
    
    if (match(tokenizing::Token::Kind::LeftParen)) {
        AngleGuard _g(this, false);
        auto expr = parse_expression();

        if (!match(tokenizing::Token::Kind::RightParen)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::RightParen,
                "Unclosed parenthesis in expression"
            );
        }
        
        return expr;
    }
    
    throw ParserError::unexpected_token(
        reporter,
        current_token(),
        {tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float, tokenizing::Token::Kind::String, tokenizing::Token::Kind::TextLiteral, 
        tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::Character, tokenizing::Token::Kind::LeftParen, tokenizing::Token::Kind::True, 
        tokenizing::Token::Kind::False, tokenizing::Token::Kind::NullptrKeyword},
        "While parsing primary expression"
    );
}

bool Parser::fold_ahead() {                 
    using K = tokenizing::Token::Kind;
    int depth = 0;
    constexpr std::size_t MAXK = 8192;

    for (std::size_t k = 0; k < MAXK; ++k) {
        K kind = (k == 0 ? current_token() : lookahead(k)).kind();

        switch (kind) {
            case K::LeftParen: case K::LeftSquare: case K::LeftCurly: ++depth; break;
            case K::RightParen: if (depth == 0) return false; --depth; break;
            case K::RightSquare: case K::RightCurly: --depth; break;
            case K::Ellipsis: if (depth == 0) return true; break;
            case K::Semicolon: case K::End: return false;
            default: break;
        }
    }

    return false;
}

nodes::ASTNode* Parser::parse_fold_expression() {   
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();

    if (match(K::Ellipsis)) {
        K op = current_token().kind();
        if (!is_fold_operator(op)) { throw ParserError::invalid_expression(reporter, current_token(), "Expected a binary operator after '...' in a fold expression"); }
        advance();
        nodes::ASTNode* rhs = parse_expression_pratt(to_int(Precedence::None));
        if (!match(K::RightParen)) { throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close fold expression"); }
        return make<nodes::FoldExpression>(nodes::FoldExpression::Form::UnaryLeft, op, nullptr, rhs, line);
    }

    nodes::ASTNode* lhs = parse_expression_pratt(to_int(Precedence::Unary));
    K op = current_token().kind();
    if (!is_fold_operator(op)) { throw ParserError::invalid_expression(reporter, current_token(), "Expected a binary operator before '...' in a fold expression"); }
    advance();
    if (!match(K::Ellipsis)) { throw ParserError::missing_token(reporter, current_token(), K::Ellipsis, "Expected '...' in fold expression"); }

    if (match(K::RightParen)) {
        return make<nodes::FoldExpression>(nodes::FoldExpression::Form::UnaryRight, op, lhs, nullptr, line);
    }

    K op2 = current_token().kind();
    if (!is_fold_operator(op2)) { throw ParserError::invalid_expression(reporter, current_token(), "Expected a binary operator after '...' in a binary fold"); }
    if (op2 != op) { throw ParserError::invalid_expression(reporter, current_token(), "Both operators in a binary fold must be identical"); }
    advance();
    nodes::ASTNode* rhs = parse_expression_pratt(to_int(Precedence::None));
    if (!match(K::RightParen)) { throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close fold expression"); }
    return make<nodes::FoldExpression>(nodes::FoldExpression::Form::Binary, op, lhs, rhs, line);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_EXPRESSION_HPP