#include "Parser/Parsing/Main/parse_class.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_constructor_declaration(const modifiers::RawModifiers& mods, const std::size_t line_number) {
    expect(tokenizing::Token::Kind::ConstructorKeyword, "Expected 'constructor'");
    nodes::FunctionParameters* params = parse_function_parameters();
    const modifiers::FunctionQualifiers quals = parse_function_qualifiers();
    using Special = nodes::ConstructorDeclaration::Special;

    if (match(tokenizing::Token::Kind::Equal)) {
        Special special;

        if (match(tokenizing::Token::Kind::DefaultKeyword)) {
            special = Special::Default;
        } else if (match(tokenizing::Token::Kind::DeleteKeyword)) {
            special = Special::Delete;
        } else {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::DefaultKeyword, tokenizing::Token::Kind::DeleteKeyword},
                "Expected 'default' or 'delete' after '=' in constructor"
            );
        }

        if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::Semicolon,
                "Defaulted/deleted constructor must end with a semicolon"
            );
        }

        consume_semicolons();
        return make<nodes::ConstructorDeclaration>(params, mods, quals, special, line_number);
    }

    std::vector<nodes::ASTNode*> init_list;

    if (match(tokenizing::Token::Kind::Colon)) {
        if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
            throw ParserError::invalid_expression(
                reporter,
                current_token(),
                "Expected at least one expression in constructor initializer list"
            );
        }

        init_list.push_back(parse_expression());
        while (match(tokenizing::Token::Kind::Comma)) { init_list.push_back(parse_expression()); }
    }

    if (!concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::LeftCurly},
            "Expected '{' to start constructor body"
        );
    }

    nodes::BlockStatement* body = parse_block(false);
    return make<nodes::ConstructorDeclaration>(params, std::move(init_list), mods, quals, body, line_number);
}

nodes::ASTNode* Parser::parse_destructor_declaration(const modifiers::RawModifiers& mods, const std::size_t line_number) {
    expect(tokenizing::Token::Kind::DestructorKeyword, "Expected 'destructor'");
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(' after 'destructor'");

    if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::RightParen},
            "Destructor cannot take parameters"
        );
    }

    expect(tokenizing::Token::Kind::RightParen, "Expected ')' — destructor takes no parameters");
    const modifiers::FunctionQualifiers quals = parse_function_qualifiers();
    using Special = nodes::DestructorDeclaration::Special;

    if (match(tokenizing::Token::Kind::Equal)) {
        Special special;

        if (match(tokenizing::Token::Kind::DefaultKeyword)) {
            special = Special::Default;
        } else if (match(tokenizing::Token::Kind::DeleteKeyword)) {
            special = Special::Delete;
        } else {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::DefaultKeyword, tokenizing::Token::Kind::DeleteKeyword},
                "Expected 'default' or 'delete' after '=' in destructor"
            );
        }

        if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::Semicolon,
                "Defaulted/deleted destructor must end with a semicolon"
            );
        }

        consume_semicolons();
        return make<nodes::DestructorDeclaration>(mods, quals, special, line_number);
    }

    if (!concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::LeftCurly},
            "Expected '{' to start destructor body"
        );
    }

    nodes::BlockStatement* body = parse_block(false);
    return make<nodes::DestructorDeclaration>(mods, quals, body, line_number);
}

bool Parser::looks_like_special_member() {
    std::size_t i = 0;
    while (is_variable_modifier(token_at(i).kind())) { ++i; }

    return token_at(i).is_one_of(
        tokenizing::Token::Kind::ConstructorKeyword,
        tokenizing::Token::Kind::DestructorKeyword
    );
}

bool Parser::looks_like_operator_function() {
    std::size_t i = 0;
    while (is_variable_modifier(token_at(i).kind())) { ++i; }
    return token_at(i).is(tokenizing::Token::Kind::OperatorKeyword);
}

nodes::ASTNode* Parser::parse_record_declaration(const modifiers::RawModifiers& mods, std::size_t line_number) {
    using Form = nodes::RecordDeclaration::Form;
    Form                    form;
    tokenizing::Token::Kind keyword_kind;

    if (match(tokenizing::Token::Kind::ClassKeyword)) {
        form         = Form::Class;
        keyword_kind = tokenizing::Token::Kind::ClassKeyword;
    } else if (match(tokenizing::Token::Kind::StructKeyword)) {
        form         = Form::Struct;
        keyword_kind = tokenizing::Token::Kind::StructKeyword;
    } else if (match(tokenizing::Token::Kind::UnionKeyword)) {
        form         = Form::Union;
        keyword_kind = tokenizing::Token::Kind::UnionKeyword;
    } else {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::ClassKeyword, tokenizing::Token::Kind::StructKeyword, tokenizing::Token::Kind::UnionKeyword},
            "For record declaration"
        );
    }

    const char* kw = form == Form::Class ? "class" : form == Form::Struct ? "struct" : "union";
    parser_types::TemplateArgument* alignment = parse_alignas();

    auto parse_qualified_name = [&](const std::string& what) -> std::vector<std::string_view> {
        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter, current_token(), {tokenizing::Token::Kind::Identifier}, what
            );
        }

        if (is_keyword(current_token().lexeme())) {
            throw ParserError::unexpected_token(
                reporter, current_token(), {tokenizing::Token::Kind::Identifier},
                std::string(kw) + " name cannot be a keyword"
            );
        }

        std::vector<std::string_view> parts;
        parts.push_back(current_token().lexeme());
        advance();

        while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
            advance();

            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter, current_token(), {tokenizing::Token::Kind::Identifier},
                    "Expected identifier after '::' in qualified name"
                );
            }

            parts.push_back(current_token().lexeme());
            advance();
        }

        return parts;
    };

    auto join_name = [](const std::vector<std::string_view>& parts) -> std::string {
        std::string out;

        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) out += "::";
            out.append(parts[i].data(), parts[i].size());
        }

        return out;
    };

    std::vector<std::string_view> name_parts   = parse_qualified_name(std::string("Expected ") + kw + " name");
    const std::string             opening_name = join_name(name_parts);

    std::vector<parser_types::TemplateArgument*> spec_args;
    bool is_specialization = false;

    if (concrete_match(tokenizing::Token::Kind::LessThan)) {
        advance();
        spec_args = parse_template_argument_list_body();  
        is_specialization = true;
    }

    parser_types::TypeInfo inherits; 
    
    if (
        current_token().is_one_of(
            tokenizing::Token::Kind::InheritsKeyword,
            tokenizing::Token::Kind::FromKeyword,
            tokenizing::Token::Kind::ExtendsKeyword
        )
    ) {
        advance(); 
        inherits = parse_type_info();  
    }

    if (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        consume_semicolons();
        auto* fwd = make<nodes::RecordDeclaration>(form, std::move(name_parts), mods, inherits, true, line_number);
        fwd->set_alignment(alignment);
        if (is_specialization) { fwd->set_specialization(std::move(spec_args)); }
        return fwd;
    }

    if (!match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::LeftCurly,
            std::string("Expected '{' to start ") + kw + " body, or ';' for a forward declaration"
        );
    }

    auto* decl = make<nodes::RecordDeclaration>(form, std::move(name_parts), mods, inherits, false, line_number);
    decl->set_alignment(alignment);
    if (is_specialization) { decl->set_specialization(std::move(spec_args)); }
    nodes::AccessLevel access = (form == Form::Class) ? nodes::AccessLevel::Private : nodes::AccessLevel::Public;

    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        if (current_token().is_one_of(
                tokenizing::Token::Kind::PublicKeyword,
                tokenizing::Token::Kind::PrivateKeyword,
                tokenizing::Token::Kind::ProtectedKeyword
            )
        ) {
            switch (current_token().kind()) {
                case tokenizing::Token::Kind::PublicKeyword:    access = nodes::AccessLevel::Public;    break;
                case tokenizing::Token::Kind::PrivateKeyword:   access = nodes::AccessLevel::Private;   break;
                case tokenizing::Token::Kind::ProtectedKeyword: access = nodes::AccessLevel::Protected; break;
                default: break;
            }

            advance();
            expect(tokenizing::Token::Kind::Colon, "Expected ':' after access specifier");
            continue;
        }

        if (looks_like_special_member()) {
            const std::size_t line = current_token().line();
            const modifiers::RawModifiers mods = parse_raw_modifiers();

            if (current_token().kind() == tokenizing::Token::Kind::ConstructorKeyword) {
                decl->add_member(access, parse_constructor_declaration(mods, line));
            } else {
                decl->add_member(access, parse_destructor_declaration(mods, line));
            }
            
            continue;
        }

        if (looks_like_operator_function()) {
            const std::size_t line = current_token().line();
            const modifiers::RawModifiers mods = parse_raw_modifiers();
            decl->add_member(access, parse_operator_function(mods, line));
            continue;
        }

        decl->add_member(access, parse_statement());
    }

    if (!match(tokenizing::Token::Kind::RightCurly)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::RightCurly,
            std::string("Expected '}' to close ") + kw + " body"
        );
    }

    if (!match(tokenizing::Token::Kind::EndKeyword)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::EndKeyword,
            std::string("Expected 'end' keyword to close ") + kw
        );
    }

    if (!match(keyword_kind)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            keyword_kind,
            std::string("Expected '") + kw + "' after 'end'"
        );
    }

    std::vector<std::string_view> closing_parts = parse_qualified_name(std::string("Expected ") + kw + " name after 'end " + kw + "'");
    const std::string closing_name = join_name(closing_parts);

    if (closing_name != opening_name) {
        throw ParserError::record_mismatch(
            reporter,
            current_token(),
            kw,
            opening_name,
            closing_name
        );
    }

    return decl;
}

} // namespace parsing
} // namespace walnut
