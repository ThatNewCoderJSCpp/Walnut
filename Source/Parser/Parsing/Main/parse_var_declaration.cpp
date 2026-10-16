#include "Parser/Parsing/Main/parse_var_declaration.hpp"

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
    const tokenizing::Token pack_token = current_token();
    const bool pack_field = match(tokenizing::Token::Kind::Ellipsis);
    const bool is_dynamic = type_info.type && type_info.type->is_primitive() && static_cast<parser_types::PrimitiveType*>(type_info.type)->is_dynamic();

    if (!is_dynamic && !match(tokenizing::Token::Kind::Colon)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::Colon,
            "A variable is declared as 'type: name', with a colon after the type, as in 'int: count = 0;'"
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
    
    nodes::VariableDeclaration* first = parse_declarator(type_info, line_number);

    if (pack_field) {
        if (first->has_initializer() || current_token().kind() == tokenizing::Token::Kind::Comma) {
            throw ParserError::invalid_declaration(reporter, pack_token, "A pack of fields ('Ts...: name;') declares one field per type in the pack, so it can't have an initializer or share its declaration");
        }
        first->pack_field = true;
    }

    if (require_semicolon) {
        while (match(tokenizing::Token::Kind::Comma)) {
            m_spliced.push_back(parse_declarator(type_info.clone_into(arena), line_number));
        }

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

    return first;
}

nodes::VariableDeclaration* Parser::parse_declarator(const parser_types::TypeInfo& type_info, std::size_t line_number) {
    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "A variable needs a name after the colon, as in 'int: count = 0;'"
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

    std::vector<parser_types::TemplateArgument*> spec_args;
    bool specialization = false;

    if (concrete_match(tokenizing::Token::Kind::LessThan)) {
        advance();
        spec_args = parse_template_argument_list_body();
        specialization = true;
    }

    nodes::ASTNode* initializer = nullptr;
    bool direct = false;
    const bool deduced = type_info.type && type_info.type->is_primitive()
        && (static_cast<parser_types::PrimitiveType*>(type_info.type)->is_auto() || static_cast<parser_types::PrimitiveType*>(type_info.type)->is_dynamic());

    if (match(tokenizing::Token::Kind::Equal)) {
        try {
            initializer = parse_expression();
        } catch (const ParserError& e) {
            throw ParserError::invalid_expression(
                reporter,
                current_token(),
                "Invalid initializer expression"
            );
        }
    } else if (concrete_match(tokenizing::Token::Kind::LeftCurly) || concrete_match(tokenizing::Token::Kind::LeftParen)) {
        const bool paren = concrete_match(tokenizing::Token::Kind::LeftParen);
        const tokenizing::Token open = current_token();
        nodes::BraceInitializerList* list = nullptr;

        if (paren) {
            AngleGuard _g(this, false);
            advance();
            list = make<nodes::BraceInitializerList>(open.line());

            if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
                list->add_element(parse_expression());
                while (match(tokenizing::Token::Kind::Comma)) list->add_element(parse_expression());
            }

            if (!match(tokenizing::Token::Kind::RightParen)) {
                throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Expected ')' to close the initializer");
            }
        } else {
            list = parse_brace_initializer_list();
        }

        direct = paren;
        if (list) list->parens = paren;

        if (deduced) {
            if (list->size() != 1) {
                throw ParserError::invalid_expression(reporter, open, std::string("A variable whose type is worked out from its initializer needs exactly one value in its '") + (paren ? "(...)" : "{...}") + "'");
            }
            initializer = list->m_elements.front();
        } else {
            initializer = list;
        }
    }

    auto* decl = make<nodes::VariableDeclaration>(type_info, name, initializer, line_number);
    decl->direct_init = direct;
    decl->template_specialization = specialization;
    decl->spec_args = std::move(spec_args);
    return decl;
}

} // namespace parsing
} // namespace walnut
