#include "Parser/Parsing/Main/parse_try_catch.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_try_catch_statement() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::TryKeyword);
    nodes::BlockStatement* try_body = parse_block();

    if (!match(tokenizing::Token::Kind::CatchKeyword)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::CatchKeyword,
            "Expected 'catch' after try block"
        );
    }

    nodes::TryCatchStatement* head = nullptr;
    nodes::TryCatchStatement* tail = nullptr;
    bool saw_catch_all = false;

    do {
        if (saw_catch_all) {
            throw ParserError::unexpected_token(
                reporter, current_token(), {tokenizing::Token::Kind::LeftCurly},
                "'catch (...)' must be the last handler of a try statement"
            );
        }

        expect(tokenizing::Token::Kind::LeftParen);
        std::string_view catch_name;
        parser_types::TypeInfo catch_type;
        bool has_typed_catch = false;

        if (match(tokenizing::Token::Kind::Ellipsis)) {
            catch_name = "...";
            catch_type.type = parser_types::PrimitiveType::make_auto(arena);
            has_typed_catch = false;
            saw_catch_all = true;
        } else {
            catch_type = parse_type_info();
            has_typed_catch = true;

            if (!match(tokenizing::Token::Kind::Colon)) {
                throw ParserError::missing_token(
                    reporter, current_token(), tokenizing::Token::Kind::Colon,
                    "Expected ':' between type and variable name in catch"
                );
            }

            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter, current_token(), {tokenizing::Token::Kind::Identifier},
                    "Expected variable name in catch clause"
                );
            }

            catch_name = current_token().lexeme();
            advance();
        }

        expect(tokenizing::Token::Kind::RightParen);
        nodes::BlockStatement* catch_body = parse_block(false);
        auto* handler = make<nodes::TryCatchStatement>(head ? nullptr : try_body, catch_name, catch_type, has_typed_catch, catch_body, line_number);
        if (!head) head = handler;
        else tail->next_handler = handler;
        tail = handler;
    } while (match(tokenizing::Token::Kind::CatchKeyword));

    return head;
}

} // namespace parsing
} // namespace walnut
