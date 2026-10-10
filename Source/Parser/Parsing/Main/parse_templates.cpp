#include "Parser/Parsing/Main/parse_templates.hpp"

namespace walnut {
namespace parsing {

std::size_t Parser::scan_paired(std::size_t open_off, tokenizing::Token::Kind open, tokenizing::Token::Kind close) {
    constexpr std::size_t MAXK = 8192;
    int depth = 0;

    for (std::size_t k = open_off; k < open_off + MAXK; ++k) {
        tokenizing::Token::Kind kind = token_at(k).kind();
        if (kind == open) { ++depth; }
        else if (kind == close) { --depth; if (depth == 0) return k; }
        else if (kind == tokenizing::Token::Kind::End) { return 0; }
    }

    return 0;
}

std::size_t Parser::scan_angle_close(std::size_t open_off, bool* leftover) {
    using K = tokenizing::Token::Kind;
    constexpr std::size_t MAXK = 8192;
    int depth = 0;

    for (std::size_t k = open_off; k < open_off + MAXK; ++k) {
        switch (token_at(k).kind()) {
            case K::LessThan: ++depth; break;
            case K::DoubleLessThan: return 0; 
            case K::GreaterThan:
                --depth;
                if (depth == 0) return k;
                if (depth < 0)  return 0;
                break;
            case K::DoubleGreaterThan:
                if (leftover && depth == 1) { *leftover = true; return k; }
                depth -= 2;
                if (depth == 0) return k;
                if (depth < 0)  return 0;
                break;
            case K::LeftParen: {
                std::size_t e = scan_paired(k, K::LeftParen, K::RightParen);
                if (e == 0) return 0;
                k = e;
                break;
            }
            case K::LeftSquare: {
                std::size_t e = scan_paired(k, K::LeftSquare, K::RightSquare);
                if (e == 0) return 0;
                k = e;
                break;
            }
            case K::Semicolon:
            case K::DoubleSemicolon:
            case K::LeftCurly:
            case K::RightParen:
            case K::RightSquare:
            case K::End:
                return 0;
            default: break;
        }
    }
    return 0;
}

bool Parser::template_call_ahead() {
    std::size_t end = scan_angle_close(0);
    if (end == 0) return false;
    return lookahead(end + 1).kind() == tokenizing::Token::Kind::LeftParen;
}

bool Parser::looks_like_type_argument(bool include_paren) {
    using K = tokenizing::Token::Kind;
    std::size_t i = 0;

    while (
        token_at(i).is_one_of(K::TypenameKeyword, K::ConstantDeclaration, K::UnsignedDeclaration, K::ShortDeclaration, K::LongDeclaration) || 
        (include_paren && token_at(i).kind() == K::LeftParen)
    ) {
        ++i;
    }

    K k = token_at(i).kind();

    if (is_base_type_token(k) || k == K::FunctionKeyword || k == K::ArrayKeyword || k == K::VariantKeyword) {
        return true;
    }

    if (k == K::DoubleColon || k == K::Identifier) {
        if (token_at(i).kind() == K::DoubleColon) ++i;
        if (token_at(i).kind() != K::Identifier) return false;
        ++i;

        for (;;) {
            if (token_at(i).kind() == K::LessThan) {
                bool leftover = false;
                std::size_t end = scan_angle_close(i, &leftover);
                if (end == 0) return false;
                i = leftover ? end : end + 1;
                if (leftover) break;                 
            }

            if (token_at(i).kind() == K::DoubleColon && token_at(i + 1).kind() == K::Identifier) {
                i += 2;
                continue;
            }

            break;
        }

        while (token_at(i).is_one_of(K::Asterisk, K::DoubleAsterisk, K::Ampersand, K::LogicAnd, K::ConstantDeclaration)) {
            ++i;
        }

        return token_at(i).is_one_of(K::Comma, K::GreaterThan, K::DoubleGreaterThan, K::Ellipsis) || (include_paren && token_at(i).kind() == K::RightParen);
    }

    return false;
}

bool Parser::match_template_close() {
    using K = tokenizing::Token::Kind;
    if (m_pending_close_angle > 0) { --m_pending_close_angle; return true; }
    if (current_token().kind() == K::GreaterThan) { advance(); return true; }
    if (current_token().kind() == K::DoubleGreaterThan) { advance(); m_pending_close_angle = 1; return true; }
    return false;
}

bool Parser::template_scope_ahead() {
    std::size_t end = scan_angle_close(0);
    if (end == 0) return false;
    return lookahead(end + 1).kind() == tokenizing::Token::Kind::DoubleColon;
}

std::string_view Parser::template_probe_key(const nodes::ASTNode* n) {
    if (const auto* id = nodes::node_cast<nodes::Identifier>(n))          return id->get_name();
    if (const auto* q  = nodes::node_cast<nodes::QualifiedIdentifier>(n)) return q->simple_name();
    return {};
}

bool Parser::is_known_template(const nodes::ASTNode* n) const {
    std::string_view key = template_probe_key(n);
    return !key.empty() && m_template_names.count(key) != 0;
}

bool Parser::template_ambiguous_ahead() {
    using K = tokenizing::Token::Kind;
    std::size_t end = scan_angle_close(0);
    if (end == 0) return false;
    
    switch (lookahead(end + 1).kind()) {
        case K::Minus: case K::Asterisk: case K::Ampersand:
        case K::LessThan: case K::GreaterThan:
        case K::DoubleLessThan: case K::DoubleGreaterThan:
            return true;
        default:
            return false;
    }
}

nodes::TemplateParameter* Parser::parse_template_parameter() {
    const std::size_t line = current_token().line();
    auto* p = make<nodes::TemplateParameter>(static_cast<std::uint32_t>(line));

    if (current_token().is(tokenizing::Token::Kind::TypenameKeyword)) {
        p->m_form = nodes::TemplateParameter::Form::Type;
        advance();
        if (match(tokenizing::Token::Kind::Ellipsis)) { p->m_is_pack = true; }

        if (current_token().kind() == tokenizing::Token::Kind::Identifier) {
            p->m_name = current_token().lexeme();
            advance();
        }

        if (match(tokenizing::Token::Kind::Equal)) {
            p->m_default_type = parse_type_info(); 
            p->m_has_default_type = true;
        }

        return p;
    }

    if (looks_like_constrained_param()) {
        p->m_form = nodes::TemplateParameter::Form::Type;
        p->m_is_constrained = true;
        p->m_constraint = parse_type_info();                
        if (match(tokenizing::Token::Kind::Ellipsis)) { p->m_is_pack = true; }
        
        if (current_token().kind() == tokenizing::Token::Kind::Identifier) { 
            p->m_name = current_token().lexeme(); advance(); 
        } else {
            throw ParserError::unexpected_token(reporter, current_token(), {tokenizing::Token::Kind::Identifier}, "Expected a name for the constrained template parameter");
        }

        if (match(tokenizing::Token::Kind::Equal)) { 
            p->m_default_type = parse_type_info(); 
            p->m_has_default_type = true; 
        }

        return p;
    }

    p->m_form = nodes::TemplateParameter::Form::NonType;
    p->m_type = parse_type_info();
    if (match(tokenizing::Token::Kind::Ellipsis)) { p->m_is_pack = true; }

    if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
        throw ParserError::unexpected_token(
            reporter, current_token(), {tokenizing::Token::Kind::Identifier},
            "Expected name for non-type template parameter"
        );
    }

    p->m_name = current_token().lexeme();
    advance();
    if (match(tokenizing::Token::Kind::Equal)) { p->m_default_value = parse_template_value_expr();  }
    return p;
}

nodes::ASTNode* Parser::parse_template_declaration() {
    const std::size_t line = current_token().line();
    expect(tokenizing::Token::Kind::TemplateKeyword, "Expected 'template'");
    expect(tokenizing::Token::Kind::LessThan, "Expected '<' after 'template'");
    auto* decl = make<nodes::TemplateDeclaration>(static_cast<std::uint32_t>(line));

    if (!match_template_close()) {
        decl->add_param(parse_template_parameter());
        while (match(tokenizing::Token::Kind::Comma)) { decl->add_param(parse_template_parameter()); }

        if (!match_template_close()) {
            throw ParserError::missing_token(
                reporter, current_token(), tokenizing::Token::Kind::GreaterThan,
                "Expected '>' to close template parameter list"
            );
        }
    } else {
        decl->set_empty_template(true);   
    }

    if (match(tokenizing::Token::Kind::RequiresKeyword)) {
        decl->set_requires_clause(parse_expression());
    }

    nodes::ASTNode* inner = parse_statement();
    decl->set_declaration(inner);

    if (auto* r = nodes::node_cast<nodes::RecordDeclaration>(inner)) {
        register_template_name(r->get_name());
    } else if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(inner)) {
        register_template_name(f->get_name());
    } else if (auto* v = nodes::node_cast<nodes::VariableDeclaration>(inner)) {
        register_template_name(v->get_name());
    } else if (auto* u = nodes::node_cast<nodes::UsingDeclaration>(inner)) {
        if (u->is_alias()) { register_template_name(u->name); }   
    }

    return decl;
}

} // namespace parsing
} // namespace walnut

namespace walnut {
namespace parsing {

void Parser::register_template_name(std::string_view name) {
    if (!name.empty()) { m_template_names.insert(name); }
}

nodes::ASTNode* Parser::parse_template_value_expr() {
    AngleGuard _g(this, true);
    return parse_expression_pratt(to_int(Precedence::None));
}

std::vector<parser_types::TemplateArgument*> Parser::parse_template_argument_list_body() {
    std::vector<parser_types::TemplateArgument*> args;
    if (match_template_close()) { return args; } 

    do {
        auto* arg = make<parser_types::TemplateArgument>();

        if (looks_like_type_argument() || match(tokenizing::Token::Kind::TypenameKeyword)) {
            arg->form = parser_types::TemplateArgument::Form::Type;
            arg->type = parse_type_info();
            if (match(tokenizing::Token::Kind::Ellipsis)) { arg->is_pack = true; }
        } else {
            arg->form = parser_types::TemplateArgument::Form::Value;
            nodes::ASTNode* e = parse_template_value_expr();
            arg->value = e;
            arg->value_repr = template_value_repr(e);
            if (match(tokenizing::Token::Kind::Ellipsis)) { arg->is_pack = true; }
        }

        args.push_back(arg);
    } while (match(tokenizing::Token::Kind::Comma));

    if (!match_template_close()) {
        throw ParserError::missing_token(
            reporter, current_token(), tokenizing::Token::Kind::GreaterThan,
            "Expected '>' to close template argument list "
            "(comparisons/shifts inside <...> must be wrapped in parentheses)"
        );
    }

    return args;
}

} // namespace parsing
} // namespace walnut
