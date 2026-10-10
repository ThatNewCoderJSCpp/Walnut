#ifndef PARSE_DECLARATORY_OPS_HPP
#define PARSE_DECLARATORY_OPS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_variable_declaration(const modifiers::RawModifiers& mods, const std::size_t line_number, bool require_semicolon) {
    parser_types::TypeInfo type_info;

    if (concrete_match(tokenizing::Token::Kind::Colon)) {
        type_info.type = parser_types::PrimitiveType::make_auto(arena);
    } else {
        type_info = parse_type_info();
    }

    type_info.modifiers = mods;
    const bool is_dynamic = type_info.type && type_info.type->is_primitive() && static_cast<parser_types::PrimitiveType*>(type_info.type)->is_dynamic();

    if (!is_dynamic && !match(tokenizing::Token::Kind::Colon)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::Colon,
            "Variable declaration requires a colon after type"
        );
    } else if (is_dynamic) {
        match(tokenizing::Token::Kind::Colon);
    }

    if (concrete_match(tokenizing::Token::Kind::LeftSquare)) {
        SmallVector<std::string_view, 4> bindings = parse_binding_names();
        nodes::ASTNode* initializer = nullptr;

        if (match(tokenizing::Token::Kind::Equal)) {
            try { initializer = parse_expression(); }
            catch (const ParserError&) { throw ParserError::invalid_expression(reporter, current_token(), "Invalid initializer expression"); }
        }

        if (require_semicolon) {
            if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
                throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Semicolon, "Variable declaration must end with a semicolon");
            }
            consume_semicolons();
        }
        
        return make<nodes::VariableDeclaration>(type_info, std::move(bindings), initializer, line_number);
    }
    
    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Variable declaration requires an identifier"
        );
    }
    
    std::string_view name = current_token().lexeme();
    expect(tokenizing::Token::Kind::Identifier);

    if (is_keyword(name)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Variable name cannot be a keyword"
        );
    }
    
    nodes::ASTNode* initializer = nullptr;
    bool is_initialized = false;

    if (match(tokenizing::Token::Kind::Equal)) {
        try {
            initializer = parse_expression();
            is_initialized = true;
        } catch (const ParserError& e) {
            throw ParserError::invalid_expression(
                reporter,
                current_token(),
                "Invalid initializer expression"
            );
        }
    } 
    
    if (require_semicolon) {
        if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::Semicolon,
                "Variable declaration must end with a semicolon"
            );
        }

        consume_semicolons();
    }

    return make<nodes::VariableDeclaration>(type_info, name, initializer, line_number);
}

} // namespace paarsing
} // namespace walnut

#endif // PARSE_DECLARATORY_OPS_HPP