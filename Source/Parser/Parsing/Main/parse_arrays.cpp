#include "Parser/Parsing/Main/parse_arrays.hpp"

namespace walnut {
namespace parsing {

parser_types::TypeInfo Parser::parse_array_type() {
    parser_types::TypeInfo info;
    expect(tokenizing::Token::Kind::ArrayKeyword, "Expected 'array' keyword in array type");
    std::optional<std::size_t> dimension;
    nodes::ASTNode* dimension_expr = nullptr;

    if (match(tokenizing::Token::Kind::LeftSquare)) {
        if (match(tokenizing::Token::Kind::Ellipsis)) {
            // [...] - inferred
        } else if (concrete_match(tokenizing::Token::Kind::RightSquare)) {
            // [] - unsized
        } else if (current_token().kind() == tokenizing::Token::Kind::Integer) {
            std::string size_str(current_token().lexeme());

            try {
                dimension = std::stoull(size_str);
            } catch (...) {
                throw ParserError::invalid_literal(reporter, current_token(), "Array size is too large");
            }

            advance();
        } else {
            dimension_expr = parse_expression();
        }

        if (!match(tokenizing::Token::Kind::RightSquare)) {
            throw ParserError::missing_token(
                reporter, current_token(),
                tokenizing::Token::Kind::RightSquare,
                "Expected ']' after array dimension"
            );
        }

        if (current_token().kind() == tokenizing::Token::Kind::LeftSquare) {
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::ArrayKeyword, tokenizing::Token::Kind::FunctionKeyword},
                "Each 'array' takes a single dimension; for multi-dimensional storage write 'array[...] array[...] T'"
            );
        }
    }

    parser_types::TypeInfo element_type = parse_type_info();
    info.type = make<parser_types::ArrayType>(element_type, dimension, dimension_expr);
    parse_indirection_qualifiers(info);
    return info;
}

nodes::ASTNode* Parser::parse_array_declaration(const modifiers::RawModifiers& array_mods, const std::size_t line_number) {
    if (!match(tokenizing::Token::Kind::ArrayKeyword)) {
        throw ParserError::missing_token(
            reporter, current_token(),
            tokenizing::Token::Kind::ArrayKeyword,
            "Expected 'array' keyword in array declaration"
        );
    }

    parser_types::TemplateArgument* alignment = parse_alignas();
    std::optional<std::size_t> dimension;
    nodes::ASTNode* dimension_expr = nullptr;
    bool has_brackets         = false;
    bool dimension_on_keyword = false;
    bool needs_initializer    = false;

    if (match(tokenizing::Token::Kind::LeftSquare)) {
        dimension_on_keyword = true;
        has_brackets         = true;

        if (match(tokenizing::Token::Kind::Ellipsis)) {
            needs_initializer = true;
        } else if (concrete_match(tokenizing::Token::Kind::RightSquare)) {
            needs_initializer = true;
        } else if (current_token().kind() == tokenizing::Token::Kind::Integer) {
            std::string size_str(current_token().lexeme());

            try {
                dimension = std::stoull(size_str);
            } catch (...) {
                throw ParserError::invalid_literal(reporter, current_token(), "Array size is too large");
            }

            advance();
        } else {
            dimension_expr = parse_expression();
        }

        if (!match(tokenizing::Token::Kind::RightSquare)) {
            throw ParserError::missing_token(
                reporter, current_token(),
                tokenizing::Token::Kind::RightSquare,
                "Expected ']' after array dimension"
            );
        }

        if (current_token().kind() == tokenizing::Token::Kind::LeftSquare) {
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::ArrayKeyword, tokenizing::Token::Kind::FunctionKeyword},
                "Each 'array' takes a single dimension; for multi-dimensional storage write 'array[...] array[...] T'"
            );
        }
    }

    parser_types::TypeInfo element_type = parse_type_info();

    if (!match(tokenizing::Token::Kind::Colon)) {
        throw ParserError::missing_token(
            reporter, current_token(),
            tokenizing::Token::Kind::Colon,
            "Expected ':' between array type and name"
        );
    }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected array name"
        );
    }

    std::string_view array_name = current_token().lexeme();

    if (is_keyword(array_name)) {
        throw ParserError::unexpected_token(
            reporter, current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Array name cannot be a keyword"
        );
    }

    advance();

    if (match(tokenizing::Token::Kind::LeftSquare)) {
        if (dimension_on_keyword) {
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {tokenizing::Token::Kind::Equal, tokenizing::Token::Kind::LeftCurly, tokenizing::Token::Kind::Semicolon},
                "Array dimension already specified on 'array[...]'; it cannot also appear after the name"
            );
        }

        has_brackets = true;

        if (match(tokenizing::Token::Kind::Ellipsis)) {
            needs_initializer = true;
        } else if (concrete_match(tokenizing::Token::Kind::RightSquare)) {
            needs_initializer = true;
        } else if (current_token().kind() == tokenizing::Token::Kind::Integer) {
            std::string size_str(current_token().lexeme());
            
            try {
                dimension = std::stoull(size_str);
            } catch (...) {
                throw ParserError::invalid_literal(reporter, current_token(), "Array size is too large");
            }

            advance();
        } else {
            dimension_expr = parse_expression();
        }

        if (!match(tokenizing::Token::Kind::RightSquare)) {
            throw ParserError::missing_token(
                reporter, current_token(),
                tokenizing::Token::Kind::RightSquare,
                "Expected ']' after array dimension"
            );
        }

        if (current_token().kind() == tokenizing::Token::Kind::LeftSquare) {
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {tokenizing::Token::Kind::Equal, tokenizing::Token::Kind::LeftCurly, tokenizing::Token::Kind::Semicolon},
                "Array declarations only support a single dimension; use a nested 'array[...]' element type for multi-dimensional storage"
            );
        }
    }

    nodes::BraceInitializerList* initializer = nullptr;
    bool has_equals = false;

    if (match(tokenizing::Token::Kind::Equal)) {
        has_equals = true;

        if (!concrete_match(tokenizing::Token::Kind::LeftCurly)) {
            nodes::ASTNode* source = parse_expression();

            if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
                throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Semicolon, "Array declaration must end with a semicolon");
            }

            consume_semicolons();
            parser_types::TypeInfo info;
            info.type = make<parser_types::ArrayType>(element_type, dimension, dimension_expr);
            info.modifiers = array_mods;
            return make<nodes::VariableDeclaration>(info, array_name, source, static_cast<std::uint32_t>(line_number));
        }

        initializer = parse_brace_initializer_list();
    } else if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        initializer = parse_brace_initializer_list();
    }

    if (needs_initializer && !initializer) {
        throw ParserError::missing_token(
            reporter, current_token(),
            tokenizing::Token::Kind::Equal,
            "Array with unspecified size ('[]' or '[...]') requires a brace initializer"
        );
    }

    bool needs_semicolon = true;
    if (initializer && !has_equals) { needs_semicolon = false; }
    if (!has_brackets)              { needs_semicolon = true; }

    if (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        consume_semicolons();
    } else if (needs_semicolon) {
        throw ParserError::missing_token(
            reporter, current_token(),
            tokenizing::Token::Kind::Semicolon,
            "Array declaration must end with a semicolon"
        );
    }

    auto* node = make<nodes::ArrayDeclaration>(
        array_name,
        element_type,
        array_mods,
        line_number,
        dimension,
        dimension_expr,
        initializer
    );

    node->set_alignment(alignment);
    return node;
}

} // namespace parsing
} // namespace walnut
