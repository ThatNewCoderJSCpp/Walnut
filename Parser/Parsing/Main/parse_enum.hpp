#ifndef PARSE_ENUM_HPP
#define PARSE_ENUM_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_enum_declaration(const modifiers::RawModifiers& mods, std::size_t line_number) {
    expect(tokenizing::Token::Kind::EnumKeyword, "Expected 'enum' keyword");

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected enum name"
        );
    }

    std::string_view enum_name = current_token().lexeme();

    if (is_keyword(enum_name)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Enum name cannot be a keyword"
        );
    }

    advance();
    bool has_underlying = false;
    parser_types::TypeInfo underlying_type;

    if (match(tokenizing::Token::Kind::Colon)) {
        underlying_type = parse_type_only();
        has_underlying  = true;
    }

    if (!match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::LeftCurly,
            "Expected '{' to start enum body"
        );
    }

    nodes::EnumDeclaration* decl = has_underlying ? make<nodes::EnumDeclaration>(enum_name, underlying_type, line_number) : make<nodes::EnumDeclaration>(enum_name, line_number);
    decl->set_modifiers(mods);

    if (!concrete_match(tokenizing::Token::Kind::RightCurly)) {
        do {
            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    {tokenizing::Token::Kind::Identifier},
                    "Expected enumerator name"
                );
            }

            std::string_view value_name = current_token().lexeme();
            const std::size_t value_line = current_token().line();

            if (is_keyword(value_name)) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    {tokenizing::Token::Kind::Identifier},
                    "Enumerator name cannot be a keyword"
                );
            }

            advance();
            nodes::ASTNode* value_init = nullptr;

            if (match(tokenizing::Token::Kind::Equal)) {
                try {
                    value_init = parse_expression();
                } catch (const ParserError&) {
                    throw ParserError::invalid_expression(
                        reporter,
                        current_token(),
                        "Invalid enumerator initializer expression"
                    );
                }
            }

            decl->add_value(make<nodes::EnumValue>(value_name, value_init, value_line));
        } while (match(tokenizing::Token::Kind::Comma) && !concrete_match(tokenizing::Token::Kind::RightCurly));
    }

    if (!match(tokenizing::Token::Kind::RightCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::RightCurly,
            "Expected '}' to close enum body"
        );
    }

    return decl;
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_ENUM_HPP