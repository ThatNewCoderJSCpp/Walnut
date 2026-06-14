#ifndef PARSE_LOOP_STATEMENTS_HPP
#define PARSE_LOOP_STATEMENTS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_for_loop() {
    expect(tokenizing::Token::Kind::ForKeyword);
    expect(tokenizing::Token::Kind::LeftParen);
    nodes::ASTNode* initializer_expr = nullptr;
    nodes::ASTNode* initializer_stmt = nullptr;
    nodes::ASTNode* condition = nullptr;
    nodes::ASTNode* increment = nullptr;
    bool has_var_init = false;
    
    if (current_token().kind() == tokenizing::Token::Kind::DoubleSemicolon) {
        advance();
        if (current_token().kind() != tokenizing::Token::Kind::RightParen) { increment = parse_expression(); }
        expect(tokenizing::Token::Kind::RightParen);
        nodes::ASTNode* body = parse_statement();
        return make<nodes::ForStatement>(static_cast<nodes::ASTNode*>(nullptr), nullptr, increment, body);
    }
    
    if (current_token().kind() == tokenizing::Token::Kind::Semicolon || current_token().kind() == tokenizing::Token::Kind::Comma) {
        advance();
    } else {
        if (is_declaration_token(current_token()) || is_variable_modifier(current_token().kind())) {
            std::size_t line = current_token().line();
            modifiers::RawModifiers mods = parse_raw_modifiers();
            initializer_stmt = parse_variable_declaration(mods, line);
            has_var_init = true;
        } else {
            initializer_expr = parse_expression();
        }

        if (match(tokenizing::Token::Kind::DoubleSemicolon)) {
            if (current_token().kind() != tokenizing::Token::Kind::RightParen) { increment = parse_expression(); }
            expect(tokenizing::Token::Kind::RightParen);
            nodes::ASTNode* body = parse_statement();

            if (has_var_init) {
                return make<nodes::ForStatement>(initializer_stmt, nullptr, increment, body);
            } else {
                return make<nodes::ForStatement>(initializer_expr, nullptr, increment, body);
            }
        }
        
        if (!match(tokenizing::Token::Kind::Semicolon) && !match(tokenizing::Token::Kind::Comma)) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::Comma},
                "Expected separator after for loop initializer"
            );
        }
    }
    
    if (current_token().kind() == tokenizing::Token::Kind::Semicolon || current_token().kind() == tokenizing::Token::Kind::Comma) {
        advance();
    } else {
        condition = parse_expression();
        
        if (!match(tokenizing::Token::Kind::Semicolon) && !match(tokenizing::Token::Kind::Comma)) {
            throw ParserError::unexpected_token(
                reporter, 
                current_token(),
                {tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::Comma},
                "Expected separator after for loop condition"
            );
        }
    }
    
    if (current_token().kind() != tokenizing::Token::Kind::RightParen) { increment = parse_expression(); }
    expect(tokenizing::Token::Kind::RightParen);
    nodes::ASTNode* body = parse_statement();

    if (has_var_init) {
        return make<nodes::ForStatement>(initializer_stmt, nullptr, condition, increment, body);
    } else {
        return make<nodes::ForStatement>(initializer_expr, condition, increment, body);
    }
}

nodes::ASTNode* Parser::parse_while_loop() {
    expect(tokenizing::Token::Kind::WhileKeyword);
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(<condition>)' for while loop");

    if (current_token().is_one_of(tokenizing::Token::Kind::RightParen, tokenizing::Token::Kind::End)) {
        throw ParserError::invalid_expression(reporter, current_token(), "Expected condition in while loop");
    } 

    nodes::ASTNode* condition = parse_expression();
    expect(tokenizing::Token::Kind::RightParen, "Expected ')' to finish condition of while loop");
    nodes::ASTNode* body = parse_statement();
    return make<nodes::WhileStatement>(body, condition);
}

nodes::ASTNode* Parser::parse_for_each_loop() {
    std::size_t line = current_token().line();
    expect(tokenizing::Token::Kind::ForKeyword);
    expect(tokenizing::Token::Kind::LeftParen);
    modifiers::RawModifiers mods = parse_raw_modifiers();
    parser_types::TypeInfo element_type = parse_type_info();
    expect(tokenizing::Token::Kind::Colon, "Expected colon after type in for-each loop");  

    if (concrete_match(tokenizing::Token::Kind::LeftSquare)) {
        SmallVector<std::string_view, 4> bindings = parse_binding_names();
        expect(tokenizing::Token::Kind::InKeyword, "Expected 'in' to indicate a for-each loop");

        if (current_token().is_one_of(tokenizing::Token::Kind::RightParen, tokenizing::Token::Kind::End)) {
            throw ParserError::invalid_expression(reporter, current_token(), "Expected container expression in for-each loop");
        }
        
        nodes::ASTNode* container = parse_expression();
        expect(tokenizing::Token::Kind::RightParen);
        nodes::ASTNode* body = parse_statement();
        return make<nodes::ForEachStatement>(element_type, mods, std::move(bindings), container, body, static_cast<std::uint32_t>(line));
    }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected loop variable name in for-each loop"
        );
    }

    std::string_view var_name = current_token().lexeme();
    advance();
    expect(tokenizing::Token::Kind::InKeyword, "Expected 'in' to indicate a for-each loop");           

    if (current_token().is_one_of(tokenizing::Token::Kind::RightParen, tokenizing::Token::Kind::End)) {
        throw ParserError::invalid_expression(reporter, current_token(), "Expected container expression in for-each loop");
    }

    nodes::ASTNode* container = parse_expression();
    expect(tokenizing::Token::Kind::RightParen);
    nodes::ASTNode* body = parse_statement();

    return make<nodes::ForEachStatement>(
        element_type, mods, var_name, container, body,
        static_cast<std::uint32_t>(line)
    );
}

bool Parser::looks_like_for_each() {
    using K = tokenizing::Token::Kind;
    if (lookahead(1).kind() != K::LeftParen) { return false; }
    int paren = 1, square = 0, curly = 0;

    for (std::size_t i = 2; ; ++i) {
        const tokenizing::Token& t = lookahead(i);

        switch (t.kind()) {
            case K::End: return false;
            case K::LeftParen:   ++paren;  break;
            case K::LeftSquare:  ++square; break;
            case K::LeftCurly:   ++curly;  break;
            case K::RightSquare: --square; break;
            case K::RightCurly:  --curly;  break;
            case K::RightParen:
                if (--paren == 0) { return false; }   
                break;
            case K::Semicolon:
            case K::DoubleSemicolon:
            case K::Comma:
                if (paren == 1 && square == 0 && curly == 0) { return false; }  
                break;
            case K::InKeyword:
                if (paren == 1 && square == 0 && curly == 0) { return true; }
                break;
            default: break;
        }
    }
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_LOOP_STATEMENTS_HPP