#ifndef PARSE_USING_HPP
#define PARSE_USING_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_using_declaration() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::UsingKeyword, "Expected 'using' keyword");

    if (match(tokenizing::Token::Kind::NamespaceKeyword)) {
        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected namespace name after 'using namespace'"
            );
        }

        std::vector<std::string_view> parts;
        parts.push_back(current_token().lexeme());
        advance();

        while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
            advance();
            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter, current_token(),
                    {tokenizing::Token::Kind::Identifier},
                    "Expected identifier after '::' in qualified name"
                );
            }

            parts.push_back(current_token().lexeme());
            advance();
        }

        if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(
                reporter, current_token(), tokenizing::Token::Kind::Semicolon,
                "using-namespace directive must end with a semicolon"
            );
        }

        consume_semicolons();
        return make<nodes::UsingDeclaration>(std::move(parts), line_number);
    }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::NamespaceKeyword},
            "Expected an alias name or 'namespace' after 'using'"
        );
    }

    std::string_view name = current_token().lexeme();

    if (is_keyword(name)) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Alias name cannot be a keyword"
        );
    }

    advance();

    if (!match(tokenizing::Token::Kind::Equal)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Equal,
            "Expected '=' in using-alias declaration"
        );
    }

    parser_types::TypeInfo type_info = parse_type_info();

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Semicolon,
            "using-alias declaration must end with a semicolon"
        );
    }

    consume_semicolons();
    return make<nodes::UsingDeclaration>(name, type_info, line_number);
}

nodes::ASTNode* Parser::parse_typedef_declaration() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::TypedefKeyword, "Expected 'typedef' keyword");
    parser_types::TypeInfo type_info = parse_type_info();

    if (!match(tokenizing::Token::Kind::Colon)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Colon,
            "typedef requires a colon between the type and the new name"
        );
    }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier},
            "typedef requires a name"
        );
    }

    std::string_view name = current_token().lexeme();

    if (is_keyword(name)) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier},
            "typedef name cannot be a keyword"
        );
    }

    advance();

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Semicolon,
            "typedef declaration must end with a semicolon"
        );
    }
    
    consume_semicolons();
    return make<nodes::UsingDeclaration>(name, type_info, line_number);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_USING_HPP