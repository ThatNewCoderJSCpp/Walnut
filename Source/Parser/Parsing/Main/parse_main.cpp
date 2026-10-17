#include "Parser/Parsing/Main/parse_main.hpp"

namespace walnut {
namespace parsing {

nodes::BlockStatement* Parser::parse_program() {
    nodes::BlockStatement* program = make<nodes::BlockStatement>();
    while (current_token().kind() != tokenizing::Token::Kind::End) { parse_declaration_into(program); }
    return program;
}

void Parser::drain_spliced(nodes::BlockStatement* block, std::size_t mark) {
    for (std::size_t i = mark; i < m_spliced.size(); ++i) block->add_statement(m_spliced[i]);
    m_spliced.resize(mark);
}

void Parser::parse_declaration_into(nodes::BlockStatement* block) {
    const std::size_t mark = m_spliced.size();
    m_declaration_level = true;
    nodes::ASTNode* s = parse_statement();
    m_declaration_level = false;
    block->add_statement(s);
    drain_spliced(block, mark);
}

nodes::ASTNode* Parser::parse_extern_block(const modifiers::RawModifiers& mods) {
    const tokenizing::Token open = current_token();
    advance();
    std::vector<nodes::ASTNode*> items;
    const bool native = mods.is_native_extern();

    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        std::size_t item_line = current_token().line();
        const tokenizing::Token item_start = current_token();
        modifiers::RawModifiers item = parse_raw_modifiers();

        if (item.has(modifiers::RawModifiers::Extern)) {
            throw ParserError::invalid_declaration(reporter, item_start, "This is already inside an extern block, so it can't have its own 'extern'");
        }

        item.add(modifiers::RawModifiers::Extern);
        item.add_token(tokenizing::Token::Kind::ExternKeyword);
        item.set_linkage(mods.linkage());
        item.copy_native_files(mods);

        if (current_token().kind() == tokenizing::Token::Kind::FunctionKeyword && lookahead(1).kind() != tokenizing::Token::Kind::LeftParen) {
            items.push_back(parse_function_declaration(item, item_line));
            continue;
        }

        if (native && current_token().is_one_of(tokenizing::Token::Kind::TemplateKeyword, tokenizing::Token::Kind::ClassKeyword, tokenizing::Token::Kind::StructKeyword)) {
            if (mods.linkage() != modifiers::RawModifiers::Linkage::Cpp) {
                throw ParserError::invalid_declaration(reporter, current_token(), "An extern \"C\" block can only hold functions; C++ classes go in an extern \"C++\" block");
            }

            if (!item.exactly(modifiers::RawModifiers::Extern)) {
                throw ParserError::invalid_declaration(reporter, item_start, "A C++ class in an extern block can't have modifiers");
            }

            m_native_record_next = true;
            m_native_record_mods = item;
            nodes::ASTNode* d = current_token().kind() == tokenizing::Token::Kind::TemplateKeyword ? parse_template_declaration() : parse_record_declaration(item, item_line);

            if (m_native_record_next) {
                m_native_record_next = false;
                throw ParserError::invalid_declaration(reporter, item_start, "Only functions and classes can be declared in an extern \"C++\" block; a template there must be a class template");
            }

            items.push_back(d);
            continue;
        }

        if (native) {
            throw ParserError::invalid_declaration(reporter, current_token(), "An extern \"C\" block can only hold function declarations, and an extern \"C++\" block can only hold function and class declarations");
        }

        if (current_token().kind() == tokenizing::Token::Kind::ArrayKeyword) items.push_back(parse_array_declaration(item, item_line));
        else items.push_back(parse_variable_declaration(item, item_line, true));
    }

    if (!match(tokenizing::Token::Kind::RightCurly)) {
        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightCurly, "Expected '}' to close the extern block");
    }

    if (!match(tokenizing::Token::Kind::EndKeyword)) {
        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::EndKeyword, "Expected 'end extern' after the extern block");
    }

    if (!match(tokenizing::Token::Kind::ExternKeyword)) {
        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::ExternKeyword, "Expected 'extern' after 'end'");
    }

    if (items.empty()) throw ParserError::empty_block(reporter, open, "extern");
    for (std::size_t i = 1; i < items.size(); ++i) m_spliced.push_back(items[i]);
    return items[0];
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

    if (at(i).kind() == K::Ellipsis && at(i + 1).kind() == K::Colon) return true;
    return at(i).kind() == K::Colon;
}

nodes::ASTNode* Parser::parse_statement() {
    const bool declaration_level = m_declaration_level;
    m_declaration_level = false;
    const bool list_level = m_list_level;
    m_list_level = false;
    nodes::TemplateDeclaration* owner_template = m_pending_template;
    m_pending_template = nullptr;
    const bool injected_candidate = m_injected_candidate;
    m_injected_candidate = false;
    const bool nested_hoist = m_nested_hoist;
    m_nested_hoist = false;
    take_keyword_as_name(true);

    if (current_token().kind() == tokenizing::Token::Kind::Identifier && (current_token().lexeme() == "goto" || current_token().lexeme() == "label")
        && lookahead(1).kind() == tokenizing::Token::Kind::Identifier && lookahead(2).is_one_of(tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::DoubleSemicolon)) {
        using V = nodes::SingleStatement::Variant;
        const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
        auto* jump = make<nodes::SingleStatement>(current_token().lexeme() == "goto" ? V::Goto : V::Label, at);
        advance();
        jump->label = current_token().lexeme();
        advance();
        consume_semicolons();
        return jump;
    }

    if (current_token().kind() == tokenizing::Token::Kind::Identifier && current_token().lexeme() == "invalidate"
        && lookahead(1).is_one_of(tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::ThisKeyword, tokenizing::Token::Kind::SelfKeyword)) {
        auto* inv = make<nodes::InvalidateStatement>(static_cast<std::uint32_t>(current_token().line()));
        advance();

        do {
            inv->targets.push_back(parse_expression_pratt(to_int(Precedence::None)));
        } while (match(tokenizing::Token::Kind::Comma));

        if (!current_token().is_one_of(tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Semicolon, "Expected ';' after the members named by 'invalidate'");
        }

        consume_semicolons();
        return inv;
    }

    if (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::Colon
        && lookahead(2).is_one_of(tokenizing::Token::Kind::ForKeyword, tokenizing::Token::Kind::WhileKeyword, tokenizing::Token::Kind::DoKeyword)) {
        const std::string_view label = current_token().lexeme();
        advance();
        advance();
        nodes::ASTNode* loop = parse_statement();
        if (auto* f = nodes::node_cast<nodes::ForStatement>(loop)) f->label = label;
        else if (auto* fe = nodes::node_cast<nodes::ForEachStatement>(loop)) fe->label = label;
        else if (auto* w = nodes::node_cast<nodes::WhileStatement>(loop)) w->label = label;
        else if (auto* d = nodes::node_cast<nodes::DoWhileStatement>(loop)) d->label = label;
        return loop;
    }

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
        case tokenizing::Token::Kind::ConstructorKeyword:
        case tokenizing::Token::Kind::DestructorKeyword:
            if (!declaration_level) {
                throw ParserError::invalid_declaration(reporter, current_token(), "A constructor or destructor defined outside its class must be at file or namespace level, as in 'constructor Account(int: n) { ... }'");
            }
            if (current_token().kind() == tokenizing::Token::Kind::ConstructorKeyword) return parse_constructor_declaration({}, current_token().line());
            return parse_destructor_declaration({}, current_token().line());

        case tokenizing::Token::Kind::TemplateKeyword:
            m_template_list_level = declaration_level || list_level || nested_hoist;
            m_template_injected   = injected_candidate;
            return parse_template_declaration();
        case tokenizing::Token::Kind::ImportKeyword:       return parse_import_export(nodes::ImportExportDeclaration::Direction::Import);
        case tokenizing::Token::Kind::ExportKeyword:       return parse_import_export(nodes::ImportExportDeclaration::Direction::Export);
        case tokenizing::Token::Kind::CoReturnKeyword:     return parse_co_return_statement();
        case tokenizing::Token::Kind::ModuleKeyword:       return parse_module_declaration();
        case tokenizing::Token::Kind::ConceptKeyword:      return parse_concept_declaration();
        case tokenizing::Token::Kind::StaticAssertKeyword: return parse_static_assert();

        case tokenizing::Token::Kind::FunctionKeyword: {
            std::size_t line = current_token().line();
            if (lookahead(1).kind() == tokenizing::Token::Kind::LeftParen) { return parse_variable_declaration({}, line, true); }
            return with_iterable_params(parse_function_declaration({}, line), owner_template);
        }
        
        case tokenizing::Token::Kind::ArrayKeyword: {
            std::size_t line = current_token().line();
            return parse_array_declaration({}, line);
        }

        case tokenizing::Token::Kind::ClassKeyword:
        case tokenizing::Token::Kind::StructKeyword:  
        case tokenizing::Token::Kind::UnionKeyword:  
            m_hoist_friends   = declaration_level || list_level || nested_hoist;
            m_record_template = owner_template;
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
        case tokenizing::Token::Kind::SpecialKeyword:
        case tokenizing::Token::Kind::AlignasKeyword: {
            std::size_t line = current_token().line();
            return parse_variable_declaration({}, line, true);
        }
        
        case tokenizing::Token::Kind::ConstantDeclaration:
        case tokenizing::Token::Kind::ConstinitKeyword:
        case tokenizing::Token::Kind::ThreadLocalKeyword:
        case tokenizing::Token::Kind::MutableKeyword:
        case tokenizing::Token::Kind::LazyKeyword:
        case tokenizing::Token::Kind::PublicKeyword:
        case tokenizing::Token::Kind::HoistKeyword:
        case tokenizing::Token::Kind::ConstexprKeyword:
        case tokenizing::Token::Kind::InlineKeyword:
        case tokenizing::Token::Kind::StaticKeyword:
        case tokenizing::Token::Kind::HiddenKeyword:
        case tokenizing::Token::Kind::LocalKeyword:
        case tokenizing::Token::Kind::GlobalKeyword:
        case tokenizing::Token::Kind::VolatileKeyword:
        case tokenizing::Token::Kind::ExternKeyword:
        case tokenizing::Token::Kind::FriendKeyword: {
            std::size_t line = current_token().line();
            modifiers::RawModifiers mods = parse_raw_modifiers();
            const bool was_injected = m_injected_active;
            struct InjectedGuard {
                bool& flag;
                bool  saved;
                ~InjectedGuard() { flag = saved; }
            } injected_guard{m_injected_active, was_injected};
            if (injected_candidate && mods.has(modifiers::RawModifiers::Friend)) m_injected_active = true;

            if (current_token().kind() == tokenizing::Token::Kind::RawCode) {
                const tokenizing::Token raw = current_token();

                if (!declaration_level) {
                    throw ParserError::invalid_declaration(reporter, raw, "C and C++ code can only be written at file or namespace level");
                }

                if (!mods.exactly(modifiers::RawModifiers::Extern) || mods.native_file_count()) {
                    throw ParserError::invalid_declaration(reporter, raw, "A C or C++ code block is written 'extern \"C\" code { ... } end extern' or 'extern \"C++\" code { ... } end extern', with nothing else before 'code'");
                }

                advance();
                if (!match(tokenizing::Token::Kind::EndKeyword)) throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::EndKeyword, "Expected 'end extern' after the code block");
                if (!match(tokenizing::Token::Kind::ExternKeyword)) throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::ExternKeyword, "Expected 'extern' after 'end'");
                return make<nodes::NativeCodeBlock>(raw.lexeme(), mods.linkage() == modifiers::RawModifiers::Linkage::C, static_cast<std::uint32_t>(raw.line()));
            }

            if (current_token().kind() == tokenizing::Token::Kind::LeftCurly && mods.has(modifiers::RawModifiers::Extern)) {
                if (!declaration_level) {
                    throw ParserError::invalid_declaration(reporter, current_token(), "An extern block can only be at file or namespace level");
                }

                if (!mods.exactly(modifiers::RawModifiers::Extern)) {
                    throw ParserError::invalid_declaration(reporter, current_token(), "An extern block can't have other modifiers; put them on the declarations inside it");
                }

                return parse_extern_block(mods);
            }

            if (mods.native_file_count() && current_token().kind() != tokenizing::Token::Kind::FunctionKeyword) {
                throw ParserError::invalid_declaration(reporter, current_token(), "'from' can only be used on function declarations and extern blocks");
            }

            if (current_token().is_one_of(                    
                    tokenizing::Token::Kind::ClassKeyword,
                    tokenizing::Token::Kind::StructKeyword,
                    tokenizing::Token::Kind::UnionKeyword
                )
            ) {
                m_hoist_friends   = declaration_level || list_level || nested_hoist;
                m_record_template = owner_template;
                return parse_record_declaration(mods, line);
            }

            if (current_token().kind() == tokenizing::Token::Kind::OperatorKeyword) {
                return parse_operator_function(mods, line);
            }

            if (current_token().kind() == tokenizing::Token::Kind::EnumKeyword) {
                return parse_enum_declaration(mods, line);
            }

            if (current_token().kind() == tokenizing::Token::Kind::NamespaceKeyword) {
                const tokenizing::Token at = current_token();
                nodes::ASTNode* node = parse_namespace_declaration();

                if (node && node->kind == nodes::ASTNode::Kind::NamespaceDeclaration) {
                    static_cast<nodes::NamespaceDeclaration*>(node)->m_modifiers = mods;
                } else {
                    throw ParserError::unexpected_token(reporter, at, std::vector<tokenizing::Token::Kind>{}, "A namespace alias cannot have modifiers; put them on the namespace itself");
                }

                return node;
            }
            
            if (current_token().kind() == tokenizing::Token::Kind::FunctionKeyword) {
                if (lookahead(1).kind() == tokenizing::Token::Kind::LeftParen) {
                    return parse_variable_declaration(mods, line, true);
                }
                return with_iterable_params(parse_function_declaration(mods, line), owner_template);
            }
            
            if (current_token().kind() == tokenizing::Token::Kind::ArrayKeyword) {
                return parse_array_declaration(mods, line);
            }
            
            return parse_variable_declaration(mods, line, true);
        }

        case tokenizing::Token::Kind::TypeofKeyword:
        case tokenizing::Token::Kind::DecltypeKeyword:
        case tokenizing::Token::Kind::DiscardConstKeyword: {
            if (computed_type_declaration_ahead()) {
                std::size_t line = current_token().line();
                return parse_variable_declaration({}, line, true);
            }
            return parse_expression_statement();
        }

        case tokenizing::Token::Kind::Identifier:
        case tokenizing::Token::Kind::DoubleColon: {
            if (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::LeftParen
                && parser_types::find_type_op(current_token().lexeme()) && computed_type_declaration_ahead()) {
                throw_type_op_moved(current_token());
            }

            if (current_token().kind() == tokenizing::Token::Kind::Identifier && parser_types::find_builtin_type_op(current_token().lexeme()) && computed_type_declaration_ahead()) {
                std::size_t line = current_token().line();
                return parse_variable_declaration({}, line, true);
            }

            if (looks_like_user_type_declaration()) {
                std::size_t line = current_token().line();
                return parse_variable_declaration({}, line, true);
            }
            return parse_expression_statement();
        }
        
        default: return parse_expression_statement();
    }
}

bool Parser::is_indirection_token(tokenizing::Token::Kind k) {
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
    if (m_pending_close_angle > 0) return;
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
    } else if (current_token().is_one_of(tokenizing::Token::Kind::VariantKeyword, tokenizing::Token::Kind::SpecialKeyword)) {
        info = parse_variant_type();
        return info;
    } else if (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::LeftParen && parser_types::find_type_op(current_token().lexeme())) {
        throw_type_op_moved(current_token());
    } else if (current_token().is_one_of(tokenizing::Token::Kind::TypeofKeyword, tokenizing::Token::Kind::DecltypeKeyword, tokenizing::Token::Kind::DiscardConstKeyword)
               || (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::LeftParen && parser_types::find_builtin_type_op(current_token().lexeme()))) {
        info = parse_computed_type();
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

            if (m_injected_active && comps.size() == 1 && !is_global && !comps.back().angled && comps.back().name == m_injected_name) {
                info.template_args = injected_arguments();
                info.has_angle_args = true;
            }
        }
    } else if (is_unsigned || length != parser_types::LengthModifier::None) {
        info.type = make<parser_types::PrimitiveType>(parser_types::PrimitiveType::BaseKind::Int, length, is_unsigned, false);
    } else {
        throw ParserError::invalid_expression(reporter, current_token(), "Expected type");
    }
    
    parse_indirection_qualifiers(info);
    return info;
}

bool Parser::computed_type_declaration_ahead() {
    using K = tokenizing::Token::Kind;
    if (token_at(1).kind() != K::LeftParen) return false;
    int depth = 0;
    std::size_t k = 1;

    for (; k < 4096; ++k) {
        const K t = token_at(k).kind();
        if (t == K::End) return false;
        if (t == K::LeftParen) ++depth;
        else if (t == K::RightParen && --depth == 0) break;
    }

    for (++k; k < 4096; ++k) {
        const K t = token_at(k).kind();
        if (t == K::Colon) return true;
        if (t != K::Asterisk && t != K::DoubleAsterisk && t != K::Ampersand && t != K::LogicAnd && t != K::ConstantDeclaration) return false;
    }

    return false;
}

parser_types::TypeInfo Parser::parse_computed_type() {
    using K = tokenizing::Token::Kind;
    using Op = parser_types::TypeOp;
    parser_types::TypeInfo info;
    const tokenizing::Token at = current_token();
    const K kw = at.kind();
    Op op = Op::DiscardConst;
    int min_types = 1;
    int max_types = 1;

    if (kw == K::TypeofKeyword || kw == K::DecltypeKeyword) {
        op = kw == K::TypeofKeyword ? Op::Typeof : Op::Decltype;
        min_types = max_types = 0;
    } else if (kw == K::Identifier) {
        const parser_types::TypeOpInfo* info_op = parser_types::find_builtin_type_op(at.lexeme());
        if (!info_op) throw ParserError::invalid_expression(reporter, at, "Expected a type operation");
        op = info_op->op;
        min_types = info_op->min_types;
        max_types = info_op->max_types;
    }

    advance();
    expect(K::LeftParen, "Expected '(' after type operation");
    AngleGuard _g(this, false);
    nodes::ASTNode* e = nullptr;
    std::vector<parser_types::TypeInfo> args;

    if (op == Op::Typeof || op == Op::Decltype || op == Op::Conditional) {
        e = parse_expression_pratt(to_int(Precedence::None));
        if (op == Op::Conditional) expect(K::Comma, "conditional(condition, TrueType, FalseType) needs a ',' after the condition");
    }

    if (max_types != 0) {
        do {
            args.push_back(parse_type_info());
            if (match(K::Ellipsis)) args.back().pack_expansion = true;
        } while (match(K::Comma));
    }

    const int count = static_cast<int>(args.size());

    if (count < min_types || (max_types >= 0 && count > max_types)) {
        std::string want = std::to_string(min_types);
        if (max_types < 0) want = "at least " + want;
        else if (max_types != min_types) want += " to " + std::to_string(max_types);
        throw ParserError::invalid_expression(reporter, at,
            std::string(parser_types::type_op_name(op)) + " takes " + want + (min_types == 1 && max_types == 1 ? " type" : " types") + ", but " + std::to_string(count) + (count == 1 ? " was given" : " were given"));
    }

    expect(K::RightParen, "Expected ')' to close the type operation");
    info.type = make<parser_types::ComputedType>(op, e, std::move(args));
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
    std::vector<parser_types::FunctionPointerType::ParamExtra> extras;
    bool any_extra = false;

    auto read_param = [&]() {
        param_types.push_back(parse_type_info());
        parser_types::FunctionPointerType::ParamExtra extra;

        if (match(tokenizing::Token::Kind::Ellipsis)) {
            extra.variadic = parser_types::FunctionPointerType::UNBOUNDED;

            if (match(tokenizing::Token::Kind::LessThan)) {
                if (current_token().kind() != tokenizing::Token::Kind::Integer) { throw ParserError::invalid_expression(reporter, current_token(), "Expected integer for exact variadic count"); }
                extra.variadic = literal_size(current_token());
                if (extra.variadic == 0) { throw ParserError::invalid_expression(reporter, current_token(), "An exact variadic count must be at least 1"); }
                advance();
                expect(tokenizing::Token::Kind::GreaterThan, "Expected '>' after exact variadic count");
                extra.exact = true;
            } else if (match(tokenizing::Token::Kind::LeftSquare)) {
                if (current_token().kind() != tokenizing::Token::Kind::Integer) { throw ParserError::invalid_expression(reporter, current_token(), "Expected integer for variadic size"); }
                extra.variadic = literal_size(current_token());
                advance();
                expect(tokenizing::Token::Kind::RightSquare, "Expected ']' after variadic size");
            }
        }

        if (match(tokenizing::Token::Kind::Equal)) {
            if (extra.variadic == parser_types::FunctionPointerType::UNBOUNDED) { throw ParserError::invalid_expression(reporter, current_token(), "A '...' parameter takes any number of arguments, so it can't have a default; give it a limit with '...[N]' or '...<N>' to fill missing ones with a default"); }
            extra.default_value = parse_expression();
        }

        any_extra = any_extra || extra.variadic || extra.default_value;
        extras.push_back(extra);
    };

    if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
        read_param();
        while (match(tokenizing::Token::Kind::Comma)) { read_param(); }
    }

    if (!any_extra) extras.clear();

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
        quals,
        std::move(extras)
    );

    parse_indirection_qualifiers(info);
    return info;
}

parser_types::TypeInfo Parser::parse_variant_type() {
    parser_types::TypeInfo info;

    if (match(tokenizing::Token::Kind::SpecialKeyword)) {
        expect(tokenizing::Token::Kind::LeftParen, "Expected '(' after 'special'");
        std::vector<parser_types::TypeInfo> held;
        held.push_back(parse_type_info());
        expect(tokenizing::Token::Kind::RightParen, "'special' holds exactly one type, e.g. special(string)");
        info.type = make<parser_types::VariantType>(std::move(held), true);
        parse_indirection_qualifiers(info);
        return info;
    }

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
