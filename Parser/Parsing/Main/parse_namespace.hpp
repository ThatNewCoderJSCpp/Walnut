#ifndef PARSE_NAMESPACE_HPP
#define PARSE_NAMESPACE_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_namespace_declaration() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::NamespaceKeyword, "Expected 'namespace' keyword");

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected namespace name"
        );
    }

    std::vector<std::string_view> name_parts;
    name_parts.push_back(current_token().lexeme());
    advance();

    if (match(tokenizing::Token::Kind::Equal)) {
        const bool target_global = match(tokenizing::Token::Kind::DoubleColon);

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected a namespace name after '=' in a namespace alias"
            );
        }

        std::vector<std::string_view> target_parts;
        target_parts.push_back(current_token().lexeme());
        advance();

        while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
            advance();

            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    {tokenizing::Token::Kind::Identifier},
                    "Expected identifier after '::' in namespace alias target"
                );
            }

            target_parts.push_back(current_token().lexeme());
            advance();
        }

        if (!match(tokenizing::Token::Kind::Semicolon)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::Semicolon,
                "Expected ';' after namespace alias"
            );
        }

        return make<nodes::UsingDeclaration>(
            name_parts.front(),
            std::move(target_parts),
            target_global,
            static_cast<std::uint32_t>(line_number)
        );
    }

    while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        advance(); // consume ::

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '::' in namespace name"
            );
        }

        name_parts.push_back(current_token().lexeme());
        advance();
    }

    if (!match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::LeftCurly,
            "Expected '{' after namespace name"
        );
    }

    auto* body = make<nodes::BlockStatement>();

    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        body->add_statement(parse_statement());
    }

    if (!match(tokenizing::Token::Kind::RightCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::RightCurly,
            "Expected '}' to close namespace body"
        );
    }

    if (!match(tokenizing::Token::Kind::EndKeyword)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::EndKeyword,
            "Expected 'end' keyword to close namespace"
        );
    }

    if (!match(tokenizing::Token::Kind::NamespaceKeyword)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::NamespaceKeyword,
            "Expected 'namespace' after 'end'"
        );
    }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected namespace name after 'end namespace'"
        );
    }

    std::vector<std::string_view> closing_parts;
    closing_parts.push_back(current_token().lexeme());
    advance();

    while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        advance();

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '::' in closing namespace name"
            );
        }

        closing_parts.push_back(current_token().lexeme());
        advance();
    }

    if (closing_parts.size() != name_parts.size()) {
        std::string opening_name, closing_name;

        for (std::size_t i = 0; i < name_parts.size(); ++i) {
            if (i > 0) opening_name += "::";
            opening_name += name_parts[i];
        }

        for (std::size_t i = 0; i < closing_parts.size(); ++i) {
            if (i > 0) closing_name += "::";
            closing_name += closing_parts[i];
        }

        throw ParserError::namespace_mismatch(reporter, current_token(), opening_name, closing_name);
    }

    for (std::size_t i = 0; i < name_parts.size(); ++i) {
        if (name_parts[i] != closing_parts[i]) {
            std::string opening_name, closing_name;

            for (std::size_t j = 0; j < name_parts.size(); ++j) {
                if (j > 0) opening_name += "::";
                opening_name += name_parts[j];
            }

            for (std::size_t j = 0; j < closing_parts.size(); ++j) {
                if (j > 0) closing_name += "::";
                closing_name += closing_parts[j];
            }

            throw ParserError::namespace_mismatch(reporter, current_token(), opening_name, closing_name);
        }
    }

    return make<nodes::NamespaceDeclaration>(
        std::move(name_parts),
        body,
        line_number
    );
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_NAMESPACE_HPP