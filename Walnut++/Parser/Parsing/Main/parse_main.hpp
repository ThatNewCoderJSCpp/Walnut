#ifndef MAIN_PARSING_HPP
#define MAIN_PARSING_HPP

#include "../base.hpp"
#include "../../Types/type_util.hpp"

namespace walnut {
namespace parsing {

nodes::BlockStatement* Parser::parse_program() {
    nodes::BlockStatement* program = make<nodes::BlockStatement>();
    while (current_token().kind() != tokenizing::Token::Kind::End) { program->add_statement(parse_statement()); }
    return program;
}

bool Parser::looks_like_user_type_declaration() {
    using K = tokenizing::Token::Kind;
    auto at = [&](std::size_t k) -> const tokenizing::Token& { return k == 0 ? current_token() : lookahead(k); };
    std::size_t i = 0;
    if (at(i).kind() == K::DoubleColon) { ++i; }
    if (at(i).kind() != K::Identifier) { return false; }
    ++i;

    for (;;) {
        if (at(i).kind() == K::LessThan) {
            std::size_t end = scan_angle_close(i);
            if (end == 0) { return false; }
            i = end + 1;
        }

        if (at(i).kind() == K::DoubleColon && at(i + 1).kind() == K::Identifier) {
            i += 2;
            continue;
        }

        break;
    }

    while (at(i).is_one_of(K::Asterisk, K::DoubleAsterisk, K::Ampersand, K::ConstantDeclaration)) {
        ++i;
    }

    return at(i).kind() == K::Colon;
}

nodes::ASTNode* Parser::parse_statement() {
    switch (current_token().kind()) {
        case tokenizing::Token::Kind::LeftCurly:           return parse_block(); 
        case tokenizing::Token::Kind::IfKeyword:           return parse_if_statement();
        case tokenizing::Token::Kind::ForKeyword:          return looks_like_for_each() ? parse_for_each_loop() : parse_for_loop();
        case tokenizing::Token::Kind::WhileKeyword:        return parse_while_loop();
        case tokenizing::Token::Kind::ReturnKeyword:       return parse_return_statement();
        case tokenizing::Token::Kind::NamespaceKeyword:    return parse_namespace_declaration();
        case tokenizing::Token::Kind::DoKeyword:           return parse_do_while_loop();
        case tokenizing::Token::Kind::SwitchKeyword:       return parse_switch_statement();
        case tokenizing::Token::Kind::TryKeyword:          return parse_try_catch_statement();
        case tokenizing::Token::Kind::EnumKeyword:         return parse_enum_declaration({}, current_token().line());
        case tokenizing::Token::Kind::UsingKeyword:        return parse_using_declaration();
        case tokenizing::Token::Kind::TypedefKeyword:      return parse_typedef_declaration();
        case tokenizing::Token::Kind::TemplateKeyword:     return parse_template_declaration();
        case tokenizing::Token::Kind::ImportKeyword:       return parse_import_export(nodes::ImportExportDeclaration::Direction::Import);
        case tokenizing::Token::Kind::ExportKeyword:       return parse_import_export(nodes::ImportExportDeclaration::Direction::Export);
        case tokenizing::Token::Kind::CoReturnKeyword:     return parse_co_return_statement();
        case tokenizing::Token::Kind::ModuleKeyword:       return parse_module_declaration();
        case tokenizing::Token::Kind::ConceptKeyword:      return parse_concept_declaration();
        case tokenizing::Token::Kind::StaticAssertKeyword: return parse_static_assert();

        case tokenizing::Token::Kind::FunctionKeyword: {
            std::size_t line = current_token().line();
            if (lookahead(1).kind() == tokenizing::Token::Kind::LeftParen) { return parse_variable_declaration({}, line, true); }
            return parse_function_declaration({}, line);
        }
        
        case tokenizing::Token::Kind::ArrayKeyword: {
            std::size_t line = current_token().line();
            return parse_array_declaration({}, line);
        }

        case tokenizing::Token::Kind::ClassKeyword:
        case tokenizing::Token::Kind::StructKeyword:  
        case tokenizing::Token::Kind::UnionKeyword:  
            return parse_record_declaration({}, current_token().line());

        case tokenizing::Token::Kind::BreakKeyword:
        case tokenizing::Token::Kind::ContinueKeyword:
        case tokenizing::Token::Kind::FallthroughKeyword:
        case tokenizing::Token::Kind::RepeatKeyword:
            return parse_single_statement();

        case tokenizing::Token::Kind::OperatorKeyword: {
            std::size_t line = current_token().line();
            return parse_operator_function({}, line);
        }
        
        case tokenizing::Token::Kind::IntegerDeclaration:
        case tokenizing::Token::Kind::DecimalDeclaration:
        case tokenizing::Token::Kind::UnsignedDeclaration:
        case tokenizing::Token::Kind::StringDeclaration:
        case tokenizing::Token::Kind::BooleanDeclaration:
        case tokenizing::Token::Kind::CharacterDeclaration:
        case tokenizing::Token::Kind::LongDeclaration:
        case tokenizing::Token::Kind::ShortDeclaration:
        case tokenizing::Token::Kind::TextDeclaration:
        case tokenizing::Token::Kind::AutoDeclaration:
        case tokenizing::Token::Kind::DynamicDeclaration:
        case tokenizing::Token::Kind::VariantKeyword:
        case tokenizing::Token::Kind::AlignasKeyword: {
            std::size_t line = current_token().line();
            return parse_variable_declaration({}, line, true);
        }
        
        case tokenizing::Token::Kind::ConstantDeclaration:
        case tokenizing::Token::Kind::ConstinitKeyword:
        case tokenizing::Token::Kind::ThreadLocalKeyword:
        case tokenizing::Token::Kind::MutableKeyword:
        case tokenizing::Token::Kind::HoistKeyword:
        case tokenizing::Token::Kind::ConstexprKeyword:
        case tokenizing::Token::Kind::InlineKeyword:
        case tokenizing::Token::Kind::StaticKeyword:
        case tokenizing::Token::Kind::HiddenKeyword:
        case tokenizing::Token::Kind::LocalKeyword:
        case tokenizing::Token::Kind::GlobalKeyword:
        case tokenizing::Token::Kind::VolatileKeyword:
        case tokenizing::Token::Kind::ImmutableKeyword:
        case tokenizing::Token::Kind::ExternKeyword:
        case tokenizing::Token::Kind::FriendKeyword: {
            std::size_t line = current_token().line();
            modifiers::RawModifiers mods = parse_raw_modifiers();

            if (current_token().is_one_of(                    
                    tokenizing::Token::Kind::ClassKeyword,
                    tokenizing::Token::Kind::StructKeyword,
                    tokenizing::Token::Kind::UnionKeyword
                )
            ) {
                return parse_record_declaration(mods, line);
            }

            if (current_token().kind() == tokenizing::Token::Kind::OperatorKeyword) {
                return parse_operator_function(mods, line);
            }

            if (current_token().kind() == tokenizing::Token::Kind::EnumKeyword) {
                return parse_enum_declaration(mods, line);
            }
            
            if (current_token().kind() == tokenizing::Token::Kind::FunctionKeyword) {
                if (lookahead(1).kind() == tokenizing::Token::Kind::LeftParen) {
                    return parse_variable_declaration(mods, line, true);
                }
                return parse_function_declaration(mods, line);
            }
            
            if (current_token().kind() == tokenizing::Token::Kind::ArrayKeyword) {
                return parse_array_declaration(mods, line);
            }
            
            return parse_variable_declaration(mods, line, true);
        }

        case tokenizing::Token::Kind::Identifier:
        case tokenizing::Token::Kind::DoubleColon: {
            if (looks_like_user_type_declaration()) {
                std::size_t line = current_token().line();
                return parse_variable_declaration({}, line, true);
            }
            return parse_expression_statement();
        }
        
        default: return parse_expression_statement();
    }
}

inline bool Parser::is_indirection_token(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    switch (k) {
        case K::Asterisk:
        case K::DoubleAsterisk:
        case K::Ampersand:
        case K::LogicAnd:
            return true;
        default: return false;
    }
}

void Parser::parse_indirection_qualifiers(parser_types::TypeInfo& info) {
    bool pending_const = false;
    
    while (true) {
        if (current_token().kind() == tokenizing::Token::Kind::ConstantDeclaration) {
            advance();
            pending_const = true;
            
            if (!current_token().is_one_of(
                tokenizing::Token::Kind::Asterisk,
                tokenizing::Token::Kind::DoubleAsterisk,
                tokenizing::Token::Kind::Ampersand,
                tokenizing::Token::Kind::LogicAnd
            )) {
                throw ParserError::unexpected_token(
                    reporter,
                    current_token(),
                    {tokenizing::Token::Kind::Asterisk, tokenizing::Token::Kind::DoubleAsterisk,
                    tokenizing::Token::Kind::Ampersand, tokenizing::Token::Kind::LogicAnd},
                    "const must be followed by '*', '**', '&' or '&&'"
                );
            }
        } else if (current_token().kind() == tokenizing::Token::Kind::Asterisk) {
            advance();
            info.add_pointer(pending_const);
            pending_const = false;
        } else if (current_token().kind() == tokenizing::Token::Kind::DoubleAsterisk) {
            advance();
            info.add_pointer(pending_const);
            info.add_pointer(false);
            pending_const = false;
        } else if (current_token().kind() == tokenizing::Token::Kind::Ampersand) {
            advance();
            info.add_reference(pending_const);
            pending_const = false;
        } else if (current_token().kind() == tokenizing::Token::Kind::LogicAnd) {
            advance();
            info.add_rvalue_reference(pending_const);   
            pending_const = false;
        } else {
            break;
        }
    }
}

parser_types::TypeInfo Parser::parse_type_only() {
    parser_types::TypeInfo info;
    bool is_unsigned = match(tokenizing::Token::Kind::UnsignedDeclaration);
    parser_types::LengthModifier length = parser_types::LengthModifier::None;
    
    if (current_token().kind() == tokenizing::Token::Kind::ShortDeclaration) {
        advance();
        if (current_token().kind() == tokenizing::Token::Kind::ShortDeclaration) {
            advance();
            length = parser_types::LengthModifier::ShortShort;
        } else {
            length = parser_types::LengthModifier::Short;
        }
    } else if (current_token().kind() == tokenizing::Token::Kind::LongDeclaration) {
        advance();
        if (current_token().kind() == tokenizing::Token::Kind::LongDeclaration) {
            advance();
            length = parser_types::LengthModifier::LongLong;
        } else {
            length = parser_types::LengthModifier::Long;
        }
    }
    
    if (is_base_type_token(current_token().kind())) {
        std::string_view lexeme = current_token().lexeme();
        bool long_form = parser_types::is_long_form_type(lexeme);
        auto base_kind = parser_types::lexeme_to_base_kind(lexeme);
        info.type = make<parser_types::PrimitiveType>(base_kind, length, is_unsigned, long_form);
        advance();
    } else if (current_token().kind() == tokenizing::Token::Kind::FunctionKeyword) {
        info = parse_function_pointer_type();
        return info;
    } else if (current_token().kind() == tokenizing::Token::Kind::ArrayKeyword) {   
        info = parse_array_type();
        return info; 
    } else if (current_token().kind() == tokenizing::Token::Kind::VariantKeyword) {
        info = parse_variant_type();
        return info;
    } else if (current_token().kind() == tokenizing::Token::Kind::Identifier || current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        using K = tokenizing::Token::Kind;
        bool is_global = false;

        if (current_token().kind() == K::DoubleColon) {
            is_global = true;
            advance();

            if (current_token().kind() != K::Identifier) {
                throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected identifier after '::' in qualified type name");
            }
        }

        struct Comp { std::string_view name; std::vector<parser_types::TemplateArgument*> args; bool angled = false; };
        std::vector<Comp> comps;
        bool dependent_member = false;   

        auto read_component = [&]() {
            Comp c;
            c.name = current_token().lexeme();
            advance();

            // Optional turbofish
            if (current_token().kind() == K::DoubleColon && lookahead(1).kind() == K::LessThan) {
                advance(); 
            }

            if (concrete_match(K::LessThan)) {
                advance();
                c.angled = true;
                c.args = parse_template_argument_list_body();   
            }

            comps.push_back(std::move(c));
        };

        read_component();

        while (current_token().kind() == K::DoubleColon && m_pending_close_angle == 0) {   
            if (!comps.back().args.empty()) { dependent_member = true; }
            advance();

            if (current_token().kind() != K::Identifier) {
                throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected identifier after '::' in qualified type name");
            }

            read_component();
        }

        if (dependent_member) {
            auto* qt = make<parser_types::QualifiedType>(is_global);
            for (auto& c : comps) { qt->add_component(c.name, std::move(c.args)); }
            info.type = qt;
        } else {
            std::vector<std::string_view> parts;
            parts.reserve(comps.size());
            for (auto& c : comps) { parts.push_back(c.name); }
            info.type = (parts.size() == 1 && !is_global) ? make<parser_types::UserDefinedType>(parts[0]) : make<parser_types::UserDefinedType>(std::move(parts), is_global);
            info.has_angle_args = comps.back().angled;
            if (!comps.back().args.empty()) { info.template_args = std::move(comps.back().args); }
        }
    } else if (is_unsigned || length != parser_types::LengthModifier::None) {
        info.type = make<parser_types::PrimitiveType>(parser_types::PrimitiveType::BaseKind::Int, length, is_unsigned, false);
    } else {
        throw ParserError::invalid_expression(reporter, current_token(), "Expected type");
    }
    
    parse_indirection_qualifiers(info);
    return info;
}

parser_types::TypeInfo Parser::parse_type_info() {
    modifiers::RawModifiers mods = parse_raw_modifiers();
    parser_types::TemplateArgument* alignment = parse_alignas();
    parser_types::TypeInfo info = parse_type_only();
    info.modifiers = mods;
    info.alignment = alignment;
    return info;
}

parser_types::TypeInfo Parser::parse_function_pointer_type() {
    parser_types::TypeInfo info;
    const std::size_t line = current_token().line();
    expect(tokenizing::Token::Kind::FunctionKeyword, "Expected 'function' keyword");
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(' after 'function' in function pointer type");
    std::vector<parser_types::TypeInfo> param_types;

    if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
        param_types.push_back(parse_type_info());
        while (match(tokenizing::Token::Kind::Comma)) { param_types.push_back(parse_type_info()); }
    }

    expect(tokenizing::Token::Kind::RightParen, "Expected ')' after function pointer parameter types");
    parser_types::TypeInfo return_type;
    modifiers::FunctionQualifiers quals = parse_function_qualifiers();

    if (match(tokenizing::Token::Kind::SingleRightArrow) || match(tokenizing::Token::Kind::DoubleRightArrow)) {
        return_type = parse_type_info();
    } else {
        throw ParserError::invalid_expression(
            reporter,
            current_token(),
            "Function-pointer type requires a return type (e.g. 'function(int) \u2192 void')"
        );
    }

    info.type = make<parser_types::FunctionPointerType>(
        std::move(param_types),
        return_type,
        quals
    );

    parse_indirection_qualifiers(info);
    return info;
}

parser_types::TypeInfo Parser::parse_variant_type() {
    parser_types::TypeInfo info;
    expect(tokenizing::Token::Kind::VariantKeyword, "Expected 'variant' keyword");
    expect(tokenizing::Token::Kind::LeftParen, "Expected '(' after 'variant'");

    if (current_token().kind() == tokenizing::Token::Kind::RightParen) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::Identifier},
            "'variant' requires at least one alternative type, e.g. variant(int, string)"
        );
    }

    std::vector<parser_types::TypeInfo> alternatives;
    alternatives.push_back(parse_type_info());
    while (match(tokenizing::Token::Kind::Comma)) { alternatives.push_back(parse_type_info()); }
    expect(tokenizing::Token::Kind::RightParen, "Expected ')' to close 'variant' alternative list");
    info.type = make<parser_types::VariantType>(std::move(alternatives));
    parse_indirection_qualifiers(info);
    return info;
}

} // namespace parsing
} // namespace walnut

#endif // MAIN_PARSING_HPP