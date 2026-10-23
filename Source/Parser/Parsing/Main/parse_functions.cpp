#include "Parser/Parsing/Main/parse_functions.hpp"

namespace walnut {
namespace parsing {

nodes::FunctionParameter* Parser::parse_function_parameter() {
    parser_types::TypeInfo type_info = parse_type_info();
    std::size_t variadic_length = 0;
    bool exact_variadic = false;

    if (match(tokenizing::Token::Kind::Ellipsis)) {
        variadic_length = nodes::FunctionParameter::INFINITE_VARIADIC;
        
        if (match(tokenizing::Token::Kind::LessThan)) {
            if (current_token().kind() != tokenizing::Token::Kind::Integer) { throw ParserError::invalid_expression(reporter, current_token(), "Expected integer for exact variadic count"); }
            variadic_length = literal_size(current_token());
            if (variadic_length == 0) { throw ParserError::invalid_expression(reporter, current_token(), "An exact variadic count must be at least 1"); }
            advance();
            expect(tokenizing::Token::Kind::GreaterThan, "Expected '>' after exact variadic count");
            exact_variadic = true;
        } else if (match(tokenizing::Token::Kind::LeftSquare)) {
            if (current_token().kind() != tokenizing::Token::Kind::Integer) { throw ParserError::invalid_expression(reporter, current_token(), "Expected integer for variadic size"); }
            std::string size_str(current_token().lexeme());
            variadic_length = literal_size(current_token());
            advance();
            expect(tokenizing::Token::Kind::RightSquare, "Expected ']' after variadic size");
        }
    }
    
    if (!match(tokenizing::Token::Kind::Colon)) {
        throw ParserError::missing_token(
            reporter,
            current_token(),
            tokenizing::Token::Kind::Colon,
            "Function parameter requires ':' between type and name"
        );
    }
    
    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected parameter name"
        );
    }
    
    std::string_view param_name = current_token().lexeme();
    advance();
    nodes::ASTNode* default_value = nullptr;
    if (match(tokenizing::Token::Kind::Equal)) {
        if (variadic_length == nodes::FunctionParameter::INFINITE_VARIADIC) { throw ParserError::invalid_expression(reporter, current_token(), "A '...' parameter takes any number of arguments, so it can't have a default; give it a limit with '...[N]' or '...<N>' to fill missing ones with a default"); }
        default_value = parse_expression();
    }

    auto* param = make<nodes::FunctionParameter>(
        param_name,
        type_info,
        default_value,
        default_value != nullptr,
        variadic_length
    );
    param->m_variadic_exact = exact_variadic;
    return param;
}

bool Parser::parse_receiver(nodes::FunctionParameters* params) {
    using K = tokenizing::Token::Kind;
    std::size_t i = 0;
    const bool is_const = current_token().kind() == K::ConstantDeclaration;
    if (is_const) i = 1;
    const tokenizing::Token& self_tok = i == 0 ? current_token() : lookahead(i);
    if (self_tok.kind() != K::SelfKeyword) return false;
    const tokenizing::Token& after = lookahead(i + 1);
    nodes::Receiver kind = is_const ? nodes::Receiver::ConstValue : nodes::Receiver::Value;
    std::size_t used = i + 1;

    if (after.kind() == K::Ampersand) {
        if (lookahead(i + 2).kind() == K::Ampersand) {
            kind = nodes::Receiver::RValue;
            used = i + 3;
        } else {
            kind = is_const ? nodes::Receiver::ConstRef : nodes::Receiver::Ref;
            used = i + 2;
        }
    } else if (after.kind() == K::LogicAnd) {
        kind = nodes::Receiver::RValue;
        used = i + 2;
    }

    const tokenizing::Token& follow = lookahead(used);
    if (!follow.is_one_of(K::Comma, K::RightParen)) {
        throw ParserError::invalid_expression(reporter, follow, "The receiver is written on its own as the first parameter: 'self', 'const self', 'self&', 'const self&' or 'self&&', without a type or a name");
    }

    if (kind == nodes::Receiver::RValue && is_const) {
        throw ParserError::invalid_expression(reporter, self_tok, "'const self&&' isn't a receiver; use 'self&&' to take the object when it's about to go away, or 'const self&' to only read it");
    }

    params->receiver = kind;
    params->receiver_line = static_cast<std::uint32_t>(self_tok.line());
    for (std::size_t k = 0; k < used; ++k) advance();
    return true;
}

void Parser::apply_receiver(nodes::FunctionParameters* params, modifiers::FunctionQualifiers& quals, const tokenizing::Token& at) {
    using FQ = modifiers::FunctionQualifiers;
    if (!params || !params->has_receiver()) return;

    if (quals.has_any(FQ::Const | FQ::LValueRef | FQ::RValueRef)) {
        throw ParserError::invalid_declaration(reporter, at, "A method with a 'self' receiver says how it uses the object in the receiver, so it can't also have 'const', '&' or '&&' after its parameters; write 'const self&' instead of 'const'");
    }

    switch (params->receiver) {
        case nodes::Receiver::ConstRef:   quals.add(FQ::Const); break;
        case nodes::Receiver::Value:
        case nodes::Receiver::ConstValue: quals.add(FQ::Const); break;
        case nodes::Receiver::RValue:     quals.add(FQ::RValueRef); break;
        default: break;
    }
}

void Parser::reject_receiver(nodes::FunctionParameters* params, const char* what) {
    if (!params || !params->has_receiver()) return;
    throw ParserError::invalid_declaration(reporter, current_token(), std::string("Only methods take a 'self' receiver; ") + what + " can't have one");
}

void Parser::add_receiver_copy(nodes::FunctionParameters* params, nodes::ASTNode* body) {
    if (!params || !params->receiver_by_value() || !body || body->kind != nodes::ASTNode::Kind::BlockStatement) return;
    const std::uint32_t at = params->receiver_line;
    parser_types::TypeInfo info;
    info.type = parser_types::PrimitiveType::make_auto(arena);
    if (params->receiver == nodes::Receiver::ConstValue) info.modifiers.add(modifiers::RawModifiers::Const);
    auto* self_this = make<nodes::Literal>(std::string_view("this"), tokenizing::Token::Kind::ThisKeyword, at);
    auto* init = make<nodes::DereferenceExpression>(self_this, at);
    auto* decl = make<nodes::VariableDeclaration>(info, std::string_view("self"), init, at);
    decl->receiver_copy = true;
    decl->order = params->order;
    init->order = params->order;
    self_this->order = params->order;
    auto* block = static_cast<nodes::BlockStatement*>(body);
    block->statements.insert(block->statements.begin(), decl);
}

nodes::FunctionParameters* Parser::parse_function_parameters() {
    auto params = make<nodes::FunctionParameters>();
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(' to start parameter list");
    if (match(tokenizing::Token::Kind::RightParen)) { return params; }
    if (parse_receiver(params)) {
        if (match(tokenizing::Token::Kind::RightParen)) { return params; }
        expect(tokenizing::Token::Kind::Comma, "Expected ',' or ')' after the receiver 'self'");
    }
    params->add(parse_function_parameter());
    while (match(tokenizing::Token::Kind::Comma)) { params->add(parse_function_parameter()); }
    expect(tokenizing::Token::Kind::RightParen, "Expected ')' to end parameter list");
    
    if (params->has_duplicate_names()) {
        auto [first_idx, second_idx] = params->find_duplicate_indices();
        (void)second_idx;
        throw ParserError::invalid_expression(reporter, current_token(), "Duplicate parameter name: '" + std::string((*params)[first_idx]->get_name()) + "'");
    }
    
    return params;
}

modifiers::FunctionQualifiers Parser::parse_function_qualifiers() {
    modifiers::FunctionQualifiers quals;
    
    while (true) {
        tokenizing::Token::Kind kind = current_token().kind();
        
        switch (kind) {
            case tokenizing::Token::Kind::ConstantDeclaration:
                if (quals.has(modifiers::FunctionQualifiers::Const)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Const);
                advance();
                break;

            case tokenizing::Token::Kind::ConstevalKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Consteval)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Consteval);
                advance();
                break;

            case tokenizing::Token::Kind::ExplicitKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Explicit)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Explicit);
                advance();
                break;

            case tokenizing::Token::Kind::NoexceptKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Noexcept)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                advance();

                if (current_token().kind() == tokenizing::Token::Kind::LeftParen) {
                    advance();
                    AngleGuard _g(this, false);
                    nodes::ASTNode* cond = parse_expression_pratt(to_int(Precedence::None));

                    if (!match(tokenizing::Token::Kind::RightParen)) {
                        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Expected ')' to close noexcept condition");
                    }
                    
                    quals.set_noexcept_expr(cond);
                } else {
                    quals.add(modifiers::FunctionQualifiers::Noexcept);
                }
                break;

            case tokenizing::Token::Kind::MutableKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Mutable)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Mutable);
                advance();
                break;

            case tokenizing::Token::Kind::OverrideKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Override)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Override);
                advance();
                break;

            case tokenizing::Token::Kind::VirtualKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Virtual) && !quals.has(modifiers::FunctionQualifiers::Abstract)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Virtual);
                advance();
                break;

            case tokenizing::Token::Kind::AbstractKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Abstract)) throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                quals.add(modifiers::FunctionQualifiers::Abstract);
                quals.add(modifiers::FunctionQualifiers::Virtual);
                advance();
                break;

            case tokenizing::Token::Kind::LogicAnd:
                if (quals.has(modifiers::FunctionQualifiers::LValueRef) || quals.has(modifiers::FunctionQualifiers::RValueRef)) {
                    throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                }
                quals.add(modifiers::FunctionQualifiers::RValueRef);
                advance();
                break;

            case tokenizing::Token::Kind::Ampersand: {
                if (quals.has(modifiers::FunctionQualifiers::LValueRef) || quals.has(modifiers::FunctionQualifiers::RValueRef)) {
                    throw ParserError::duplicate_function_qualifiers(reporter, current_token());
                }

                advance(); // consume first '&'

                if (current_token().kind() == tokenizing::Token::Kind::Ampersand) {
                    quals.add(modifiers::FunctionQualifiers::RValueRef);
                    advance(); // consume second '&'
                } else {
                    // Only one '&'
                    quals.add(modifiers::FunctionQualifiers::LValueRef);
                }

                break;
            }

            case tokenizing::Token::Kind::NodiscardKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Nodiscard)) { throw ParserError::duplicate_function_qualifiers(reporter, current_token()); }
                advance();

                if (current_token().kind() == tokenizing::Token::Kind::LeftParen) {
                    advance();
                    
                    if (current_token().kind() != tokenizing::Token::Kind::String) {
                        throw ParserError::unexpected_token(reporter, current_token(), {tokenizing::Token::Kind::String}, "Expected a string message inside nodiscard(...)");
                    }
                    
                    validate_string_literal(current_token());
                    std::string_view msg = current_token().lexeme();
                    advance();
                    
                    if (!match(tokenizing::Token::Kind::RightParen)) {
                        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Expected ')' to close nodiscard message");
                    }
                    
                    quals.set_nodiscard(msg);
                } else {
                    quals.add(modifiers::FunctionQualifiers::Nodiscard);
                }
                break;

            case tokenizing::Token::Kind::AsyncKeyword:
                if (quals.has(modifiers::FunctionQualifiers::Async)) { throw ParserError::duplicate_function_qualifiers(reporter, current_token()); }
                quals.add(modifiers::FunctionQualifiers::Async);
                advance();
                break;

                
            default:
                return quals;
        }
    }
}

parser_types::TypeInfo Parser::parse_return_type() {
    parser_types::TypeInfo return_type;
    
    if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        return_type.type = parser_types::PrimitiveType::make_auto(arena);
    } else if (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::invalid_expression(
            reporter,
            current_token(),
            "Arrow operator requires an explicit return type (e.g. '\u2192 int')"
        );
    } else {
        return_type = parse_type_info();
    }
    
    return return_type;
}

nodes::ASTNode* Parser::parse_function_declaration(
  const modifiers::RawModifiers& mods,
  const std::size_t line_number
) {
    expect(tokenizing::Token::Kind::FunctionKeyword, "Expected 'function' keyword");
    bool is_global_qualified = false;

    if (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        is_global_qualified = true;
        advance();
    }

    take_keyword_as_name(false);
    if (current_token().kind() != tokenizing::Token::Kind::Identifier && current_token().kind() != tokenizing::Token::Kind::EndKeyword) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "Expected function name"
        );
    }

    std::vector<std::string_view> name_parts;
    name_parts.push_back(current_token().lexeme());
    advance();

    while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        advance();

        take_keyword_as_name(false);
    if (current_token().kind() != tokenizing::Token::Kind::Identifier && current_token().kind() != tokenizing::Token::Kind::EndKeyword) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected identifier after '::' in qualified function name"
            );
        }

        name_parts.push_back(current_token().lexeme());
        advance();
    }

    std::vector<parser_types::TemplateArgument*> spec_args;
    bool is_specialization = false;

    if (concrete_match(tokenizing::Token::Kind::LessThan)) {
        advance();
        spec_args = parse_template_argument_list_body();
        is_specialization = true;
    }

    auto parameters = parse_function_parameters();
    ParamFrame param_frame(*this, parameters);
    modifiers::FunctionQualifiers quals = parse_function_qualifiers();
    apply_receiver(parameters, quals, current_token());
    struct SelfDepthGuard {
        int& depth;
        int  saved;
        ~SelfDepthGuard() { depth = saved; }
    } self_guard{m_self_value_depth, m_self_value_depth};
    m_self_value_depth = parameters->receiver_by_value() ? 1 : 0;
    m_propagation.push_back(nullptr);
    struct PropagationGuard {
        std::vector<const tokenizing::Token*>& stack;
        ~PropagationGuard() { stack.pop_back(); }
    } propagation_guard{m_propagation};
    parser_types::TypeInfo return_type;
    nodes::ASTNode* body = nullptr;
    bool has_arrow = match(tokenizing::Token::Kind::SingleRightArrow) || match(tokenizing::Token::Kind::DoubleRightArrow);

    std::string_view native_as;

    if (has_arrow) {
        return_type = parse_return_type();
        if (m_native_record_depth > 0 && match(tokenizing::Token::Kind::AsKeyword)) native_as = parse_native_name("Expected the name of the C++ method or function in quotes after 'as'");

        if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
            if (quals.has(modifiers::FunctionQualifiers::Abstract)) {
                throw ParserError::invalid_declaration(reporter, current_token(), "An 'abstract' method has no body; end it with ';' instead, or remove 'abstract' to give it one");
            }
            body = parse_block();
        } else if (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            consume_semicolons();
        } else {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                { tokenizing::Token::Kind::LeftCurly, tokenizing::Token::Kind::Semicolon },
                "Expected '{' or ';' after function return type"
            );
        }

        if (body != nullptr) {
            if (!match(tokenizing::Token::Kind::EndKeyword)) {
                throw ParserError::missing_token(
                    reporter,
                    current_token(),
                    tokenizing::Token::Kind::EndKeyword,
                    "A function body is closed with '} end function " + std::string(name_parts.back()) + "'"
                );
            }

            if (!match(tokenizing::Token::Kind::FunctionKeyword)) {
                throw ParserError::missing_token(
                    reporter,
                    current_token(),
                    tokenizing::Token::Kind::FunctionKeyword,
                    "Expected 'function' after 'end'"
                );
            }

            std::vector<std::string_view> closing_parts;

            take_keyword_as_name(false);
    if (current_token().kind() != tokenizing::Token::Kind::Identifier && current_token().kind() != tokenizing::Token::Kind::EndKeyword) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    { tokenizing::Token::Kind::Identifier },
                    "Expected function name after 'end function'"
                );
            }

            closing_parts.push_back(current_token().lexeme());
            advance();

            while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
                advance(); 

                take_keyword_as_name(false);
    if (current_token().kind() != tokenizing::Token::Kind::Identifier && current_token().kind() != tokenizing::Token::Kind::EndKeyword) {
                    throw ParserError::unexpected_token(
                        reporter,
                        current_token(),
                        { tokenizing::Token::Kind::Identifier },
                        "Expected identifier after '::' in closing function name"
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

                throw ParserError::function_mismatch(
                    reporter,
                    current_token(),
                    opening_name,
                    closing_name
                );
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

                    throw ParserError::function_mismatch(
                        reporter,
                        current_token(),
                        opening_name,
                        closing_name
                    );
                }
            }
        }

        add_receiver_copy(parameters, body);
        if (m_propagation.back() && body) body = wrap_propagation(body, return_type, static_cast<std::uint32_t>(line_number));
        auto* decl = make<nodes::FunctionDeclaration>(
            std::move(name_parts), is_global_qualified, return_type,
            mods, quals, line_number, parameters, body
        );

        if (is_specialization) { decl->set_specialization(std::move(spec_args)); }
        decl->native_name = native_as;
        return decl;
    }

    if (m_native_record_depth > 0) {
        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::SingleRightArrow, "A method of a C++ class in an extern block needs a result type after '->'");
    }

    return_type.type = parser_types::PrimitiveType::make_dynamic(arena);

    if (current_token().kind() == tokenizing::Token::Kind::LeftCurly) {
        body = parse_block();
    } else if (current_token().kind() == tokenizing::Token::Kind::Semicolon || current_token().kind() == tokenizing::Token::Kind::DoubleSemicolon) {
        consume_semicolons();
    } else {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {
                tokenizing::Token::Kind::SingleRightArrow,
                tokenizing::Token::Kind::DoubleRightArrow,
                tokenizing::Token::Kind::LeftCurly,
                tokenizing::Token::Kind::Semicolon
            },
            "Function declaration requires either a return type ('\u2192 <type>'), a body ('{ ... }'), or a terminating semicolon"
        );
    }

    if (body != nullptr) {
        if (!match(tokenizing::Token::Kind::EndKeyword)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::EndKeyword,
                "A function body is closed with '} end function " + std::string(name_parts.back()) + "'"
            );
        }

        if (!match(tokenizing::Token::Kind::FunctionKeyword)) {
            throw ParserError::missing_token(
                reporter,
                current_token(),
                tokenizing::Token::Kind::FunctionKeyword,
                "Expected 'function' after 'end'"
            );
        }

        std::vector<std::string_view> closing_parts;
        take_keyword_as_name(false);

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                { tokenizing::Token::Kind::Identifier },
                "Expected function name after 'end function'"
            );
        }

        closing_parts.push_back(current_token().lexeme());
        advance();

        while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
            advance(); 

            if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    { tokenizing::Token::Kind::Identifier },
                    "Expected identifier after '::' in closing function name"
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

            throw ParserError::function_mismatch(
                reporter,
                current_token(),
                opening_name,
                closing_name
            );
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

                throw ParserError::function_mismatch(
                    reporter,
                    current_token(),
                    opening_name,
                    closing_name
                );
            }
        }
    }

    add_receiver_copy(parameters, body);
    if (m_propagation.back() && body) body = wrap_propagation(body, return_type, static_cast<std::uint32_t>(line_number));
    auto* decl = make<nodes::FunctionDeclaration>(
        std::move(name_parts), is_global_qualified, return_type,
        mods, quals, line_number, parameters, body
    );

    if (is_specialization) { decl->set_specialization(std::move(spec_args)); }
    return decl;
}

nodes::ASTNode* Parser::parse_return_statement() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::ReturnKeyword, "Expected 'return' keyword");
    nodes::ASTNode* return_value = nullptr;
    
    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && 
        !concrete_match(tokenizing::Token::Kind::DoubleSemicolon) &&
        !concrete_match(tokenizing::Token::Kind::RightCurly)
    ) {
        return_value = parse_expression();
    }
    
    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Semicolon,
            "Return statement must end with a semicolon"
        );
    }

    consume_semicolons();
    return make<nodes::ReturnStatement>(return_value, line_number);
}

nodes::ASTNode* Parser::parse_operator_function(
  const modifiers::RawModifiers& mods,
  const std::size_t line_number
) {
    const char* keyword_at = current_token().data();
    nodes::ASTNode* made = parse_operator_function_body(mods, line_number);
    if (made && !made->at) made->at = keyword_at;
    return made;
}

nodes::ASTNode* Parser::parse_operator_function_body(
  const modifiers::RawModifiers& mods,
  const std::size_t line_number
) {
    using K = tokenizing::Token::Kind;
    expect(K::OperatorKeyword, "Expected 'operator' keyword");
    expect(K::LeftParen, "Expected '(' after 'operator'");

    if (lookahead(1).kind() == K::RightParen) {
        if (current_token().is_one_of(K::TypeofKeyword, K::DecltypeKeyword, K::DiscardConstKeyword)) {
            throw ParserError::invalid_expression(reporter, current_token(), "'" + std::string(current_token().lexeme()) + "' cannot be overloaded");
        }

        parser_types::TypeQueryInfo info;
        if (current_token().is(K::Identifier) && parser_types::find_type_query(current_token().lexeme(), info)) return parse_type_query_overload(info, line_number);
    }

    nodes::OperatorFunctionDeclaration::Form form;
    std::vector<tokenizing::Token::Kind>      op_tokens;
    parser_types::TypeInfo                    conversion_type;
    bool is_conversion = false;

    if (operator_spec_starts_type(current_token().kind())) {
        is_conversion = true;
        form = nodes::OperatorFunctionDeclaration::Form::Conversion;
        conversion_type = parse_type_info();       
        expect(K::RightParen, "Expected ')' after conversion type in 'operator(...)'");
    } else {
        form = nodes::OperatorFunctionDeclaration::Form::Symbol;
        int depth = 1;

        while (true) {
            K k = current_token().kind();

            if (k == K::End) {
                throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Unterminated operator specifier; expected ')'");
            }

            if (k == K::LeftParen) { 
                ++depth; 
                op_tokens.push_back(k); 
                advance(); 
                continue; 
            }

            if (k == K::RightParen) {
                if (--depth == 0) { 
                    advance(); 
                    break; 
                }

                op_tokens.push_back(k); 
                advance(); 
                continue;
            }

            op_tokens.push_back(k);
            advance();
        }

        if (op_tokens.empty()) {
            throw ParserError::invalid_expression(reporter, current_token(), "Expected an operator symbol inside 'operator(...)'");
        }
    }

    nodes::OverloadableOperator op_kind = nodes::OverloadableOperator::None;

    if (!is_conversion) {
        op_kind = nodes::classify_overloadable_operator(op_tokens);

        if (op_kind == nodes::OverloadableOperator::None) {
            throw ParserError::invalid_operator_overload(reporter, current_token(), op_tokens);
        }
    }

    expect(K::FunctionKeyword, "Expected 'function' after the operator specifier");
    std::vector<std::string_view> owner;
    bool owner_global = false;
    parse_owner_name(owner, owner_global);
    nodes::FunctionParameters*   parameters = parse_function_parameters();
    ParamFrame param_frame(*this, parameters);
    modifiers::FunctionQualifiers quals     = parse_function_qualifiers();
    apply_receiver(parameters, quals, current_token());
    struct SelfDepthGuard {
        int& depth;
        int  saved;
        ~SelfDepthGuard() { depth = saved; }
    } self_guard{m_self_value_depth, m_self_value_depth};
    m_self_value_depth = parameters->receiver_by_value() ? 1 : 0;
    m_propagation.push_back(nullptr);
    struct PropagationGuard {
        std::vector<const tokenizing::Token*>& stack;
        ~PropagationGuard() { stack.pop_back(); }
    } propagation_guard{m_propagation};

    if (quals.has(modifiers::FunctionQualifiers::Abstract)) {
        throw ParserError::invalid_declaration(reporter, current_token(), "Only methods can be 'abstract'; an operator can be 'virtual' with no body instead");
    }
    parser_types::TypeInfo return_type;
    nodes::ASTNode*        body = nullptr;

    if (!is_conversion) {
        bool has_arrow = match(K::SingleRightArrow) || match(K::DoubleRightArrow);

        if (has_arrow) {
            return_type = parse_return_type();
        } else {
            return_type.type = parser_types::PrimitiveType::make_dynamic(arena);
        }
    } else {
        return_type = conversion_type;
    }

    bool deleted = false;
    std::string_view native_as;
    if (m_native_record_depth > 0 && match(K::AsKeyword)) native_as = parse_native_name("Expected the name of the C++ method or function in quotes after 'as'");

    if (match(K::Equal)) {
        expect(K::DeleteKeyword, "Expected 'delete' after '=' in an operator function");

        if (!concrete_match(K::Semicolon) && !concrete_match(K::DoubleSemicolon)) {
            throw ParserError::missing_token(reporter, current_token(), K::Semicolon, "A deleted operator function must end with a semicolon");
        }

        consume_semicolons();
        deleted = true;
    } else if (concrete_match(K::LeftCurly)) {
        body = parse_block();
    } else if (concrete_match(K::Semicolon) || concrete_match(K::DoubleSemicolon)) {
        consume_semicolons();
    } else {
        throw ParserError::unexpected_token(reporter, current_token(), { K::LeftCurly, K::Semicolon }, "Operator function requires a body ('{ ... }'), '= delete;' or a terminating ';'");
    }

    add_receiver_copy(parameters, body);
    if (m_propagation.back() && body) body = wrap_propagation(body, return_type, static_cast<std::uint32_t>(line_number));

    if (form == nodes::OperatorFunctionDeclaration::Form::Conversion) {
        auto* node = make<nodes::OperatorFunctionDeclaration>(conversion_type, return_type, mods, quals, static_cast<std::uint32_t>(line_number), parameters, body);
        node->overload = nodes::OverloadableOperator::Conversion;   
        node->m_deleted = deleted;
        node->native_name = native_as;
        node->owner_parts = std::move(owner);
        node->owner_global = owner_global;
        return node;
    }

    auto* node = make<nodes::OperatorFunctionDeclaration>(std::move(op_tokens), return_type, mods, quals, static_cast<std::uint32_t>(line_number), parameters, body);
    node->overload = op_kind;
    node->m_deleted = deleted;
    node->native_name = native_as;
    node->owner_parts = std::move(owner);
    node->owner_global = owner_global;
    return node;
}

nodes::ASTNode* Parser::parse_type_query_overload(const parser_types::TypeQueryInfo& info, std::size_t line_number) {
    using K = tokenizing::Token::Kind;
    const std::string name(info.name);
    const tokenizing::Token name_token = current_token();
    advance();
    expect(K::RightParen, "Expected ')' after '" + name + "'");

    if (info.overload == parser_types::OverloadPolicy::Never) {
        throw ParserError::invalid_expression(reporter, name_token, "'" + name + "' cannot be overloaded; its answer depends only on how the type is written");
    }

    auto* node = make<nodes::TypeQueryOverload>(info.name, static_cast<std::uint32_t>(line_number));
    node->is_trait = info.is_trait;
    node->op       = info.op;
    node->trait    = info.trait;

    expect(K::FunctionKeyword, "Expected 'function' after 'operator(" + name + ")'");
    expect(K::LeftParen, "Expected '(' to start the parameter types of 'operator(" + name + ")'");

    if (!match(K::RightParen)) {
        do {
            node->m_params.push_back(parse_type_info());
        } while (match(K::Comma));

        expect(K::RightParen, "Expected ')' to end the parameter types of 'operator(" + name + ")'");
    }

    expect(K::SingleRightArrow, "'operator(" + name + ")' needs a result type: '-> <type>'");
    node->m_return_type = parse_type_info();

    if (!info.is_trait) {
        if (current_token().is(K::LeftCurly)) {
            throw ParserError::invalid_expression(reporter, current_token(), "'operator(" + name + ")' has no body; the type after '->' is the result");
        }

        if (!concrete_match(K::Semicolon) && !concrete_match(K::DoubleSemicolon)) {
            throw ParserError::missing_token(reporter, current_token(), K::Semicolon, "'operator(" + name + ")' must end with a semicolon");
        }

        consume_semicolons();
        return node;
    }

    if (!current_token().is(K::LeftCurly)) {
        throw ParserError::invalid_expression(reporter, current_token(), "'operator(" + name + ")' needs a body: '{ return <constant>; }'");
    }

    const tokenizing::Token body_token = current_token();
    nodes::ASTNode* body = parse_block();
    auto* block = body && body->kind == nodes::ASTNode::Kind::BlockStatement ? static_cast<nodes::BlockStatement*>(body) : nullptr;

    if (!block || block->statements.size() != 1 || !block->statements[0] || block->statements[0]->kind != nodes::ASTNode::Kind::ReturnStatement
        || !static_cast<nodes::ReturnStatement*>(block->statements[0])->get_value()) {
        throw ParserError::invalid_expression(reporter, body_token, "the body of 'operator(" + name + ")' must be a single 'return <constant>;'");
    }

    node->m_value = static_cast<nodes::ReturnStatement*>(block->statements[0])->get_value();
    return node;
}

bool Parser::template_value_ahead() {
    using K = tokenizing::Token::Kind;
    std::size_t end = scan_angle_close(0);
    if (end == 0) return false;
    K next = lookahead(end + 1).kind();

    switch (next) {
        case K::Semicolon: case K::DoubleSemicolon:
        case K::Comma:
        case K::RightParen: case K::RightSquare: case K::RightCurly:
        case K::QuestionMark: case K::Colon:
        case K::End:
        case K::Plus: case K::Slash: case K::Percent: case K::DoubleAsterisk:
        case K::LogicEqual: case K::NotEqual:
        case K::LessEqual:  case K::GreaterEqual:
        case K::LogicAnd:   case K::LogicOr:
        case K::Pipe:       case K::Caret:
        case K::Equal: case K::PlusEqual: case K::MinusEqual:
        case K::AsteriskEqual: case K::SlashEqual: case K::PercentEqual:
        case K::DoubleAsteriskEqual: case K::PipeEqual: case K::AmpersandEqual:
        case K::CaretEqual: case K::ShiftLeftEqual: case K::ShiftRightEqual:
        case K::Dot: case K::SingleRightArrow: case K::LeftCurly:
        case K::FunctionKeyword: case K::OperatorKeyword:
        case K::ConstructorKeyword: case K::DestructorKeyword:
        case K::ClassKeyword: case K::StructKeyword:
        case K::ConstexprKeyword: case K::ConstevalKeyword:
        case K::FriendKeyword: case K::InlineKeyword:
        case K::StaticKeyword: case K::VirtualKeyword:
        case K::TemplateKeyword: case K::ConceptKeyword:
        case K::NamespaceKeyword: case K::UsingKeyword:
        case K::EnumKeyword: case K::UnionKeyword: case K::TypedefKeyword:
        case K::ExternKeyword: case K::HiddenKeyword: case K::LocalKeyword: case K::GlobalKeyword:
        case K::PublicKeyword: case K::PrivateKeyword: case K::ProtectedKeyword:
        case K::EndKeyword:
            return true;
        default:
            return false;
    }
}

nodes::ASTNode* Parser::parse_co_return_statement() {
    const std::size_t line_number = current_token().line();
    expect(tokenizing::Token::Kind::CoReturnKeyword, "Expected 'co_return' keyword");
    nodes::ASTNode* value = nullptr;

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) &&
        !concrete_match(tokenizing::Token::Kind::DoubleSemicolon) &&
        !concrete_match(tokenizing::Token::Kind::RightCurly)
    ) {
        value = parse_expression();
    }

    if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::Semicolon,
            "co_return statement must end with a semicolon"
        );
    }

    consume_semicolons();
    return make<nodes::CoReturnStatement>(value, line_number);
}

} // namespace parsing
} // namespace walnut
