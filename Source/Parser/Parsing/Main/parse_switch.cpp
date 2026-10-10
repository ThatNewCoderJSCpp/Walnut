#include "Parser/Parsing/Main/parse_switch.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_switch_statement() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::SwitchKeyword);
    expect(tokenizing::Token::Kind::LeftParen);

    if (current_token().is_one_of(tokenizing::Token::Kind::RightParen, tokenizing::Token::Kind::End)) {
        throw ParserError::invalid_expression(
            reporter, current_token(), "Expected expression in switch statement"
        );
    }

    nodes::ASTNode* condition = parse_expression();
    expect(tokenizing::Token::Kind::RightParen);
    expect(tokenizing::Token::Kind::LeftCurly);

    auto switch_stmt = make<nodes::SwitchStatement>(condition, line_number);

    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        const std::size_t case_line = current_token().line();
        nodes::ASTNode* case_value = nullptr;

        if (match(tokenizing::Token::Kind::CaseKeyword)) {
            case_value = parse_expression();

            if (!match(tokenizing::Token::Kind::Colon)) {
                throw ParserError::missing_token(
                    reporter,
                    current_token(),
                    tokenizing::Token::Kind::Colon,
                    "Expected ':' after case value"
                );
            }
        } else if (match(tokenizing::Token::Kind::DefaultKeyword)) {
            if (!match(tokenizing::Token::Kind::Colon)) {
                throw ParserError::missing_token(
                    reporter,
                    current_token(),
                    tokenizing::Token::Kind::Colon,
                    "Expected ':' after 'default'"
                );
            }
        } else {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::CaseKeyword, tokenizing::Token::Kind::DefaultKeyword},
                "Expected 'case' or 'default' in switch body"
            );
        }

        auto switch_case = make<nodes::SwitchCase>(case_value, case_line);

        if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
            nodes::BlockStatement* block = parse_block();
            for (auto* stmt : block->get_statements()) { switch_case->add_statement(stmt); }

            if (!block->get_statements().empty()) {
                const auto* last = block->get_statements().back();
                // Cannot easily detect fallthrough inside a block,
                // so check for an explicit fallthrough after the block
            }
        } else {
            while (
                current_token().kind() != tokenizing::Token::Kind::CaseKeyword &&
                current_token().kind() != tokenizing::Token::Kind::DefaultKeyword &&
                current_token().kind() != tokenizing::Token::Kind::RightCurly &&
                current_token().kind() != tokenizing::Token::Kind::End
            ) {
                if (concrete_match(tokenizing::Token::Kind::BreakKeyword)) {
                    switch_case->add_statement(parse_single_statement());
                    break;
                }

                if (match(tokenizing::Token::Kind::FallthroughKeyword)) {
                    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
                        throw ParserError::missing_token(
                            reporter,
                            current_token(),
                            tokenizing::Token::Kind::Semicolon,
                            "Fallthrough statement must end with a semicolon"
                        );
                    }

                    consume_semicolons();
                    switch_case->set_fallthrough(true);
                    break;
                }

                switch_case->add_statement(parse_statement());
            }
        }

        switch_stmt->add_case(switch_case);
    }

    expect(tokenizing::Token::Kind::RightCurly);
    return switch_stmt;
}

} // namespace parsing
} // namespace walnut
