#ifndef PARSE_IF_STATEMENT_HPP
#define PARSE_IF_STATEMENT_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

bool Parser::looks_like_declaration() {
    using K = tokenizing::Token::Kind;
    K k = current_token().kind();
    if (k == K::Colon) { return true; }                   
    if (is_variable_modifier(k)) { return true; }          
    if (is_base_type_token(k) || k == K::FunctionKeyword || k == K::ArrayKeyword || k == K::VariantKeyword) { return true; }  
    if (k == K::Identifier || k == K::DoubleColon) { return looks_like_user_type_declaration(); }
    return false;
}

nodes::ASTNode* Parser::parse_if_statement() {
    using K = tokenizing::Token::Kind;
    expect(K::IfKeyword);

    auto parse_clause = [&](nodes::IfStatement* stmt) {
        expect(K::LeftParen, "Expected '(<condition>)' for if statement");
        const std::size_t cline = current_token().line();
        nodes::ASTNode* cond = looks_like_declaration() ? parse_variable_declaration({}, cline, false) : parse_expression();
        expect(K::RightParen, "Expected ')' to end the if condition)");
        expect(K::ThenKeyword, "Expected 'then' to indicated if body");
        stmt->add_branch(make<nodes::IfBranch>(cond, parse_block()));
    };

    nodes::IfStatement* if_statement = make<nodes::IfStatement>();
    parse_clause(if_statement);

    while (match(K::ElseKeyword)) {
        if (match(K::IfKeyword)) {
            parse_clause(if_statement);
        } else {
            expect(K::ThenKeyword, "Expected 'then' to indicate if body");
            if_statement->set_else(parse_block());
            break;
        }
    }

    return if_statement;
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_IF_STATEMENT_HPP