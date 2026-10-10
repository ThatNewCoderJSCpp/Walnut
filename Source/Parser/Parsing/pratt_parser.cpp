#include "Parser/Parsing/pratt_parser.hpp"

namespace walnut {
namespace parsing {

Precedence get_infix_precedence(tokenizing::Token::Kind kind) {
    switch (kind) {
        case tokenizing::Token::Kind::Equal:
        case tokenizing::Token::Kind::PlusEqual:
        case tokenizing::Token::Kind::MinusEqual:
        case tokenizing::Token::Kind::AsteriskEqual:
        case tokenizing::Token::Kind::SlashEqual:
        case tokenizing::Token::Kind::PercentEqual:
        case tokenizing::Token::Kind::DoubleAsteriskEqual:
        case tokenizing::Token::Kind::PipeEqual:
        case tokenizing::Token::Kind::AmpersandEqual:
        case tokenizing::Token::Kind::CaretEqual:
        case tokenizing::Token::Kind::ShiftLeftEqual:
        case tokenizing::Token::Kind::ShiftRightEqual:
            return Precedence::Assignment;
        
        case tokenizing::Token::Kind::QuestionMark: return Precedence::Ternary;
        case tokenizing::Token::Kind::LogicOr:      return Precedence::LogicOr;
        case tokenizing::Token::Kind::LogicAnd:     return Precedence::LogicAnd;
        case tokenizing::Token::Kind::Pipe:         return Precedence::BitwiseOr;
        case tokenizing::Token::Kind::Caret:        return Precedence::BitwiseXor;
        case tokenizing::Token::Kind::Ampersand:    return Precedence::BitwiseAnd;
        
        case tokenizing::Token::Kind::LogicEqual:
        case tokenizing::Token::Kind::NotEqual:
            return Precedence::Equality;
        
        case tokenizing::Token::Kind::LessThan:
        case tokenizing::Token::Kind::GreaterThan:
        case tokenizing::Token::Kind::LessEqual:
        case tokenizing::Token::Kind::GreaterEqual:
            return Precedence::Comparison;
        
        case tokenizing::Token::Kind::DoubleLessThan:
        case tokenizing::Token::Kind::DoubleGreaterThan:
            return Precedence::Shift;
        
        case tokenizing::Token::Kind::Plus:
        case tokenizing::Token::Kind::Minus:
            return Precedence::Additive;
        
        case tokenizing::Token::Kind::Asterisk:
        case tokenizing::Token::Kind::Slash:
        case tokenizing::Token::Kind::Percent:
            return Precedence::Multiplicative;
        
        case tokenizing::Token::Kind::DoubleAsterisk: return Precedence::Power;
        
        case tokenizing::Token::Kind::DoublePlus:
        case tokenizing::Token::Kind::DoubleMinus:
        case tokenizing::Token::Kind::LeftSquare:
        case tokenizing::Token::Kind::LeftParen:
        case tokenizing::Token::Kind::Dot:
        case tokenizing::Token::Kind::SingleRightArrow:
        case tokenizing::Token::Kind::DoubleColon: 
            return Precedence::Postfix;
        
        default:
            return Precedence::None;
    }
}

bool is_right_associative(tokenizing::Token::Kind kind) {
    switch (kind) {
        case tokenizing::Token::Kind::Equal:
        case tokenizing::Token::Kind::PlusEqual:
        case tokenizing::Token::Kind::MinusEqual:
        case tokenizing::Token::Kind::AsteriskEqual:
        case tokenizing::Token::Kind::SlashEqual:
        case tokenizing::Token::Kind::PercentEqual:
        case tokenizing::Token::Kind::DoubleAsteriskEqual:
        case tokenizing::Token::Kind::PipeEqual:
        case tokenizing::Token::Kind::AmpersandEqual:
        case tokenizing::Token::Kind::CaretEqual:
        case tokenizing::Token::Kind::ShiftLeftEqual:
        case tokenizing::Token::Kind::ShiftRightEqual:
        case tokenizing::Token::Kind::DoubleAsterisk:
        case tokenizing::Token::Kind::QuestionMark:
            return true;
        default:
            return false;
    }
}

bool is_callable_node(const nodes::ASTNode* n) {
    if (!n) return false;
    
    switch (n->kind) {
        case nodes::ASTNode::Kind::Identifier:
        case nodes::ASTNode::Kind::QualifiedIdentifier:
        case nodes::ASTNode::Kind::MemberAccessExpression:
            return true;
        default:
            return false;
    }
}

bool is_constructible_callee(const nodes::ASTNode* n) {
    if (!n) return false;
    switch (n->kind) {
        case nodes::ASTNode::Kind::Identifier:
        case nodes::ASTNode::Kind::QualifiedIdentifier:
        case nodes::ASTNode::Kind::TemplateInstantiation:
            return true;
        case nodes::ASTNode::Kind::MemberAccessExpression:
            return static_cast<const nodes::MemberAccessExpression*>(n)->is_scope();
        default:
            return false;
    }
}

nodes::ASTNode* Parser::parse_call_argument() {
    const std::size_t line = current_token().line();
    nodes::ASTNode* arg = parse_expression_pratt(to_int(Precedence::None));
    if (match(tokenizing::Token::Kind::Ellipsis)) return make<nodes::UnaryExpression>(arg, tokenizing::Token::Kind::Ellipsis, false, line);
    return arg;
}

bool Parser::is_fold_operator(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    switch (k) {
        case K::Plus: case K::Minus: case K::Asterisk: case K::Slash: case K::Percent: case K::DoubleAsterisk:
        case K::Ampersand: case K::Pipe: case K::Caret: case K::DoubleLessThan: case K::DoubleGreaterThan:
        case K::LogicAnd: case K::LogicOr: case K::LogicEqual: case K::NotEqual:
        case K::LessThan: case K::GreaterThan: case K::LessEqual: case K::GreaterEqual:
        case K::Comma:
            return true;
        default: return false;
    }
}

nodes::ASTNode* Parser::parse_expression_pratt(int min_precedence) {
    nodes::ASTNode* left = parse_prefix();

    while (true) {
        tokenizing::Token::Kind op = current_token().kind();
        const bool callable_lt   = (op == tokenizing::Token::Kind::LessThan) && is_callable_node(left);
        const bool template_call = callable_lt && template_call_ahead();
        const bool template_inst = callable_lt && !template_call && template_scope_ahead();
        const bool template_val  = callable_lt && !template_call && !template_inst && (is_known_template(left) || template_value_ahead());
        const bool template_ambig = callable_lt && !template_call && !template_inst && !template_val && template_ambiguous_ahead();
        const bool template_angle = template_call || template_inst || template_val || template_ambig;
        const bool brace_construct = (op == tokenizing::Token::Kind::LeftCurly) && is_constructible_callee(left);

        if (m_in_template_args && (
                op == tokenizing::Token::Kind::LessThan        ||
                op == tokenizing::Token::Kind::GreaterThan     ||
                op == tokenizing::Token::Kind::DoubleLessThan  ||
                op == tokenizing::Token::Kind::DoubleGreaterThan
            )
        ) {
            if (!template_angle) break;
        }

        Precedence prec = (template_angle || brace_construct) ? Precedence::Postfix : get_infix_precedence(op);
        if (to_int(prec) < min_precedence || prec == Precedence::None) break;
        left = parse_infix(left, op, prec);
    }

    return left;
}

nodes::ASTNode* Parser::parse_prefix() {
    const std::size_t line = current_token().line();
    tokenizing::Token::Kind kind = current_token().kind();
    
    switch (kind) {
        case tokenizing::Token::Kind::DoublePlus: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::UnaryExpression>(expr, tokenizing::Token::Kind::DoublePlus, true, line);
        }
        case tokenizing::Token::Kind::DoubleMinus: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::UnaryExpression>(expr, tokenizing::Token::Kind::DoubleMinus, true, line);
        }
        case tokenizing::Token::Kind::Minus: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::UnaryExpression>(expr, tokenizing::Token::Kind::Minus, true, line);
        }
        case tokenizing::Token::Kind::ExclamationMark: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::UnaryExpression>(expr, tokenizing::Token::Kind::ExclamationMark, true, line);
        }
        case tokenizing::Token::Kind::Tilde: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::BitwiseNotExpression>(expr);
        }
        case tokenizing::Token::Kind::Asterisk: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::DereferenceExpression>(expr, line);
        }
        case tokenizing::Token::Kind::DoubleAsterisk: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::DereferenceExpression>(make<nodes::DereferenceExpression>(expr, line), line);
        }
        case tokenizing::Token::Kind::Plus: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::UnaryExpression>(expr, tokenizing::Token::Kind::Plus, true, line);
        }
        case tokenizing::Token::Kind::Ampersand: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::ReferenceExpression>(expr, line);
        }
        case tokenizing::Token::Kind::AwaitKeyword:
        case tokenizing::Token::Kind::CoAwaitKeyword: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::Unary));
            return make<nodes::AwaitExpression>(expr, line);
        }
        case tokenizing::Token::Kind::CoYieldKeyword: {
            advance();
            nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::None));
            return make<nodes::CoYieldExpression>(expr, line);
        }
        default:
            return parse_atom();
    }
}

nodes::ASTNode* Parser::parse_atom() {
    const std::size_t line = current_token().line();
    
    if (current_token().kind() == tokenizing::Token::Kind::LeftCurly) {
        return parse_brace_initializer_list();
    }

    if (current_token().kind() == tokenizing::Token::Kind::LeftSquare) {
        advance(); 
        return parse_lambda_expression();
    }
    
    if (current_token().is_one_of(
        tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float,
        tokenizing::Token::Kind::String, tokenizing::Token::Kind::TextLiteral,
        tokenizing::Token::Kind::Character, tokenizing::Token::Kind::True, tokenizing::Token::Kind::False,
        tokenizing::Token::Kind::NullptrKeyword, tokenizing::Token::Kind::InfinityKeyword,
        tokenizing::Token::Kind::ThisKeyword
    )) {
        auto literal = make<nodes::Literal>(current_token().lexeme(), current_token().kind(), line);
        advance();
        return literal;
    }
    
    if (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        return parse_identifier_or_qualified();
    }

    if (current_token().is_one_of(
        tokenizing::Token::Kind::Identifier,
        tokenizing::Token::Kind::AutoDeclaration,
        tokenizing::Token::Kind::DynamicDeclaration
    )) {
        if (!current_token().is(tokenizing::Token::Kind::Identifier)) {
            std::string_view name = current_token().lexeme();
            advance();
            return make<nodes::Identifier>(name, line);
        }

        return parse_identifier_or_qualified();
    }
    
    if (match(tokenizing::Token::Kind::LeftParen)) {
        AngleGuard _g(this, false);
        if (fold_ahead()) { return parse_fold_expression(); } 
        nodes::ASTNode* expr = parse_expression_pratt(to_int(Precedence::None));
        
        if (!match(tokenizing::Token::Kind::RightParen)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Unclosed parenthesis in expression");
        }

        return expr;
    }

    if (current_token().is_one_of(
            tokenizing::Token::Kind::StaticCastKeyword, tokenizing::Token::Kind::DynamicCastKeyword, tokenizing::Token::Kind::ConstCastKeyword,
            tokenizing::Token::Kind::ReinterpretCastKeyword, tokenizing::Token::Kind::BitCastKeyword
        )
    ) {
        return parse_cast_expression();
    }

    if (current_token().is_one_of(
            tokenizing::Token::Kind::SizeofKeyword, tokenizing::Token::Kind::CountOfKeyword, tokenizing::Token::Kind::TypeidKeyword,
            tokenizing::Token::Kind::TypeofKeyword, tokenizing::Token::Kind::AlignofKeyword, tokenizing::Token::Kind::DecltypeKeyword,
            tokenizing::Token::Kind::AlignasKeyword
        )
    ) {
        return parse_type_query_expression();
    }

    if (current_token().is(tokenizing::Token::Kind::NewKeyword))      { return parse_new_expression();      }
    if (current_token().is(tokenizing::Token::Kind::DeleteKeyword))   { return parse_delete_expression();   }
    if (current_token().is(tokenizing::Token::Kind::NoexceptKeyword)) { return parse_noexcept_expression(); }
    if (current_token().is(tokenizing::Token::Kind::ThrowKeyword))    { return parse_throw_expression();    }

    if (current_token().is_one_of(
            tokenizing::Token::Kind::DiscardConstKeyword,
            tokenizing::Token::Kind::DiscardNodiscardKeyword
        )
    ) {
        return parse_discard_expression();
    }

    if (current_token().is(tokenizing::Token::Kind::RequiresKeyword)) {
        return parse_requires_expression();
    }
    
    throw ParserError::unexpected_token(
        reporter,
        current_token(),
        "expression (literal, identifier, '(', '[' for lambda, '{' for brace init, or a prefix operator)",
        "While parsing expression"
    );
}

nodes::ASTNode* Parser::parse_infix(nodes::ASTNode* left, tokenizing::Token::Kind op, Precedence prec) {
    const std::size_t line = current_token().line();

    if (op == tokenizing::Token::Kind::LessThan && is_callable_node(left)) {
        if (template_call_ahead()) {
            advance();
            std::vector<parser_types::TemplateArgument*> targs = parse_template_argument_list_body();

            if (!match(tokenizing::Token::Kind::LeftParen)) {
                throw ParserError::missing_token(
                    reporter, current_token(), tokenizing::Token::Kind::LeftParen,
                    "Expected '(' after template arguments in call"
                );
            }

            AngleGuard _g(this, false);
            auto* call = make<nodes::CallExpression>(left, line);
            call->set_template_args(std::move(targs));

            if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
                call->add_argument(parse_call_argument());
                while (match(tokenizing::Token::Kind::Comma)) { call->add_argument(parse_call_argument()); }
            }

            if (!match(tokenizing::Token::Kind::RightParen)) {
                throw ParserError::missing_token(
                    reporter, current_token(), tokenizing::Token::Kind::RightParen,
                    "Expected ')' after function arguments"
                );
            }

            return call;
        }

        const bool known = is_known_template(left);

        if (template_scope_ahead() || template_value_ahead() || known) {
            advance();
            std::vector<parser_types::TemplateArgument*> targs = parse_template_argument_list_body();
            return make<nodes::TemplateInstantiation>(left, std::move(targs), line);
        }

        if (template_ambiguous_ahead()) {
            throw ParserError::ambiguous_template(reporter, current_token(), std::string(template_probe_key(left)));
        }
    }
    
    if (op == tokenizing::Token::Kind::DoublePlus) {
        advance();
        return make<nodes::UnaryExpression>(left, tokenizing::Token::Kind::DoublePlus, false, line);
    }

    if (op == tokenizing::Token::Kind::DoubleMinus) {
        advance();
        return make<nodes::UnaryExpression>(left, tokenizing::Token::Kind::DoubleMinus, false, line);
    }
    
    if (op == tokenizing::Token::Kind::LeftSquare) {
        advance();
        AngleGuard _g(this, false);
        nodes::ASTNode* index = parse_expression_pratt(to_int(Precedence::None));

        if (!match(tokenizing::Token::Kind::RightSquare)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightSquare, "Expected ']' after array subscript");
        }

        return make<nodes::SubscriptExpression>(left, index, line);
    }
    
    if (op == tokenizing::Token::Kind::LeftParen) {
        advance();
        AngleGuard _g(this, false);
        auto call = make<nodes::CallExpression>(left, line);

        if (!concrete_match(tokenizing::Token::Kind::RightParen)) {
            call->add_argument(parse_call_argument());

            while (match(tokenizing::Token::Kind::Comma)) {
                call->add_argument(parse_call_argument());
            }
        }

        if (!match(tokenizing::Token::Kind::RightParen)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Expected ')' after function arguments");
        }

        return call;
    }
    
    if (op == tokenizing::Token::Kind::QuestionMark) {
        advance();
        nodes::ASTNode* then_branch = parse_expression_pratt(to_int(Precedence::None));

        if (!match(tokenizing::Token::Kind::Colon)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Colon, "Expected ':' in ternary conditional operator");
        }

        nodes::ASTNode* else_branch = parse_expression_pratt(to_int(Precedence::Ternary));
        return make<nodes::TernaryExpression>(left, then_branch, else_branch, line);
    }

    if (op == tokenizing::Token::Kind::Dot) {
        advance(); 

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected member name after '.'"
            );
        }

        std::string_view member = current_token().lexeme();
        advance();
        return make<nodes::MemberAccessExpression>(left, member, nodes::MemberAccessExpression::Op::Dot, line);
    }

    if (op == tokenizing::Token::Kind::SingleRightArrow) {
        advance(); 

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected member name after '\u2192'"
            );
        }

        std::string_view member = current_token().lexeme();
        advance();
        return make<nodes::MemberAccessExpression>(left, member, nodes::MemberAccessExpression::Op::Arrow, line);
    }

    if (op == tokenizing::Token::Kind::DoubleColon) {
        advance();

        if (current_token().kind() == tokenizing::Token::Kind::LessThan && is_callable_node(left)) {   // turbofish: Name::<args>
            advance(); 
            std::vector<parser_types::TemplateArgument*> targs = parse_template_argument_list_body();
            return make<nodes::TemplateInstantiation>(left, std::move(targs), line);
        }

        if (current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(reporter, current_token(), {tokenizing::Token::Kind::Identifier}, "Expected name after '::'");
        }
        
        std::string_view member = current_token().lexeme();
        advance();
        return make<nodes::MemberAccessExpression>(left, member, nodes::MemberAccessExpression::Op::Scope, line);
    }

    if (op == tokenizing::Token::Kind::LeftCurly) {
        nodes::BraceInitializerList* init = parse_brace_initializer_list();
        return make<nodes::BraceConstructExpression>(left, init, line);
    }
    
    advance();
    int next_min_prec = is_right_associative(op) ? to_int(prec) : to_int(prec) + 1;
    nodes::ASTNode* right = parse_expression_pratt(next_min_prec);
    return make<nodes::BinaryExpression>(left, op, right, line);
}

nodes::ASTNode* Parser::parse_cast_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    nodes::CastExpression::CastKind ck;

    switch (current_token().kind()) {
        case K::StaticCastKeyword:      ck = nodes::CastExpression::CastKind::Static;      break;
        case K::DynamicCastKeyword:     ck = nodes::CastExpression::CastKind::Dynamic;     break;
        case K::ConstCastKeyword:       ck = nodes::CastExpression::CastKind::Const;       break;
        case K::ReinterpretCastKeyword: ck = nodes::CastExpression::CastKind::Reinterpret; break;
        case K::BitCastKeyword:         ck = nodes::CastExpression::CastKind::Bit;         break;
        default:
            throw ParserError::unexpected_token(reporter, current_token(),
                {K::StaticCastKeyword, K::DynamicCastKeyword, K::ConstCastKeyword,
                 K::ReinterpretCastKeyword, K::BitCastKeyword},   
                "Expected a cast operator");
    }

    advance();

    if (!match(K::LessThan)) {
        throw ParserError::missing_token(reporter, current_token(), K::LessThan, "Expected '<' after cast operator");
    }

    parser_types::TypeInfo target;

    {
        AngleGuard _g(this, true);             
        target = parse_type_info();

        if (!match_template_close()) {        
            throw ParserError::missing_token(reporter, current_token(), K::GreaterThan, "Expected '>' to close cast target");
        }
    }

    if (!match(K::LeftParen)) {
        throw ParserError::missing_token(reporter, current_token(), K::LeftParen, "Expected '(' after cast target");
    }

    AngleGuard _g(this, false);
    nodes::ASTNode* operand = parse_expression();

    if (!match(K::RightParen)) {
        throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close cast operand");
    }

    return make<nodes::CastExpression>(ck, target, operand, static_cast<std::uint32_t>(line));
}

parser_types::TemplateArgument* Parser::parse_type_or_value_operand(bool force_type) {
    using K = tokenizing::Token::Kind;
    auto* arg = make<parser_types::TemplateArgument>();

    if (force_type) {
        match(K::TypenameKeyword); // optional here, consume if written
        arg->form = parser_types::TemplateArgument::Form::Type;
        arg->type = parse_type_info();
        return arg;
    }

    if (looks_like_type_argument(true) || match(K::TypenameKeyword)) {
        arg->form = parser_types::TemplateArgument::Form::Type;
        arg->type = parse_type_info();
    } else {
        arg->form  = parser_types::TemplateArgument::Form::Value;
        arg->value = parse_expression();
    }

    return arg;
}

nodes::ASTNode* Parser::parse_type_query_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    nodes::TypeQueryExpression::Op op;

    switch (current_token().kind()) {
        case K::SizeofKeyword:   op = nodes::TypeQueryExpression::Op::Sizeof;   break;
        case K::CountOfKeyword:  op = nodes::TypeQueryExpression::Op::Countof;  break;
        case K::TypeidKeyword:   op = nodes::TypeQueryExpression::Op::Typeid;   break;
        case K::TypeofKeyword:   op = nodes::TypeQueryExpression::Op::Typeof;   break;
        case K::AlignofKeyword:  op = nodes::TypeQueryExpression::Op::Alignof;  break;
        case K::DecltypeKeyword: op = nodes::TypeQueryExpression::Op::Decltype; break;
        default:
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {
                    K::SizeofKeyword, K::CountOfKeyword, K::TypeidKeyword,
                    K::TypeofKeyword, K::AlignofKeyword, K::DecltypeKeyword
                },
                "Expected a type-query operator"
            );
    }

    advance();
    
    if (!match(K::LeftParen)) {
        throw ParserError::missing_token(reporter, current_token(), K::LeftParen, "Expected '(' after type-query operator");
    }

    AngleGuard _g(this, false);
    parser_types::TemplateArgument* operand = parse_type_or_value_operand();

    if (!match(K::RightParen)) {
        throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close type-query operand");
    }

    return make<nodes::TypeQueryExpression>(op, operand, static_cast<std::uint32_t>(line));
}

nodes::ASTNode* Parser::parse_new_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::NewKeyword, "Expected 'new'");
    parser_types::TypeInfo type = parse_type_info();
    std::vector<nodes::ASTNode*> args;
    nodes::ASTNode* array_size = nullptr;
    bool is_array = false;

    if (match(K::LeftSquare)) {     
        is_array = true;
        AngleGuard _g(this, false);
        if (current_token().kind() != K::RightSquare) { array_size = parse_expression(); }

        if (!match(K::RightSquare)) {
            throw ParserError::missing_token(reporter, current_token(), K::RightSquare, "Expected ']' to close array 'new' size");
        }
    } else if (match(K::LeftParen)) {       
        AngleGuard _g(this, false);

        if (current_token().kind() != K::RightParen) {
            args.push_back(parse_expression());
            while (match(K::Comma)) { args.push_back(parse_expression()); }
        }

        if (!match(K::RightParen)) {
            throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close 'new' arguments");
        }
    }

    return make<nodes::NewExpression>(type, std::move(args), array_size, is_array, static_cast<std::uint32_t>(line));
}

parser_types::TemplateArgument* Parser::parse_alignas() {
    using K = tokenizing::Token::Kind;
    if (current_token().kind() != K::AlignasKeyword) return nullptr;   
    advance();
    expect(K::LeftParen, "Expected '(' after 'alignas'");
    AngleGuard _g(this, false);
    parser_types::TemplateArgument* operand = parse_type_or_value_operand();
    expect(K::RightParen, "Expected ')' to close 'alignas'");
    return operand;
}

nodes::ASTNode* Parser::parse_delete_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::DeleteKeyword, "Expected 'delete'");
    bool is_array = false;

    if (match(K::LeftSquare)) {
        is_array = true;
        if (!match(K::RightSquare)) { throw ParserError::missing_token(reporter, current_token(), K::RightSquare, "Expected ']' for array 'delete' (delete[])"); }
    }

    nodes::ASTNode* operand = parse_expression_pratt(to_int(Precedence::Unary));
    return make<nodes::DeleteExpression>(operand, is_array, static_cast<std::uint32_t>(line));
}

nodes::ASTNode* Parser::parse_noexcept_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::NoexceptKeyword, "Expected 'noexcept'");
    expect(K::LeftParen, "Expected '(' in conditional noexcept");
    AngleGuard _g(this, false);
    nodes::ASTNode* cond = parse_expression_pratt(to_int(Precedence::None));

    if (!match(tokenizing::Token::Kind::RightParen)) {
        throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::RightParen, "Expected ')' to close noexcept condition");
    }

    return make<nodes::NoexceptExpression>(cond, static_cast<std::uint32_t>(line));
}

nodes::ASTNode* Parser::parse_discard_expression() {
    using K = tokenizing::Token::Kind;
    using DK = nodes::DiscardExpression::DiscardKind;
    const std::size_t line = current_token().line();
    DK dk;

    switch (current_token().kind()) {
        case K::DiscardConstKeyword:     dk = DK::Const;     break;
        case K::DiscardNodiscardKeyword: dk = DK::Nodiscard; break;
        default:
            throw ParserError::unexpected_token(
                reporter, current_token(),
                {K::DiscardConstKeyword, K::DiscardNodiscardKeyword},
                "Expected a discard operator");
    }

    advance();
    expect(K::LeftParen, "Expected '(' after discard operator");
    AngleGuard _g(this, false);
    nodes::ASTNode* operand = parse_expression_pratt(to_int(Precedence::None));

    if (!match(K::RightParen)) {
        throw ParserError::missing_token(reporter, current_token(), K::RightParen, "Expected ')' to close discard operand");
    }

    return make<nodes::DiscardExpression>(dk, operand, static_cast<std::uint32_t>(line));
}

nodes::ASTNode* Parser::parse_requires_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::RequiresKeyword, "Expected 'requires'");
    nodes::FunctionParameters* params = nullptr;

    if (concrete_match(K::LeftParen)) {
        AngleGuard _g(this, false);   
        params = parse_function_parameters();
    }

    if (!match(K::LeftCurly)) {
        throw ParserError::missing_token(reporter, current_token(), K::LeftCurly, "Expected '{' to open requires-expression body");
    }

    AngleGuard _g(this, false);
    auto* req = make<nodes::RequiresExpression>(params, static_cast<std::uint32_t>(line));

    if (concrete_match(K::RightCurly)) {
        throw ParserError::invalid_expression(reporter, current_token(), "requires-expression must contain at least one requirement");
    }

    while (!concrete_match(K::RightCurly)) {
        if (current_token().is(K::End)) {
            throw ParserError::missing_token(reporter, current_token(), K::RightCurly, "Unterminated requires-expression");
        }

        req->add_requirement(parse_requirement());
    }

    expect(K::RightCurly, "Expected '}' to close requires-expression");
    return req;
}

nodes::RequiresExpression::Requirement Parser::parse_requirement() {
    using K  = tokenizing::Token::Kind;
    using RF = nodes::RequiresExpression::Requirement::Form;
    nodes::RequiresExpression::Requirement r;

    if (match(K::TypenameKeyword)) {                       
        r.form = RF::Type;
        r.type = parse_type_info();
    } else if (current_token().is(K::RequiresKeyword)) {   
        advance();
        r.form = RF::Nested;
        r.expr = parse_expression();
    } else if (match(K::LeftCurly)) {                      
        r.form = RF::Compound;
        r.expr = parse_expression();

        if (!match(K::RightCurly)) {
            throw ParserError::missing_token(reporter, current_token(), K::RightCurly, "Expected '}' to close compound requirement");
        }

        if (match(K::NoexceptKeyword)) { r.is_noexcept = true; }

        if (match(K::SingleRightArrow) || match(K::DoubleRightArrow)) {   
            r.has_type_constraint = true;
            r.type = parse_type_info();
        }
    } else {                                               
        r.form = RF::Simple;
        r.expr = parse_expression();
    }

    if (!match(K::Semicolon)) {
        throw ParserError::missing_token(reporter, current_token(), K::Semicolon, "Expected ';' after requirement");
    }

    return r;
}

nodes::ASTNode* Parser::parse_throw_expression() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::ThrowKeyword, "Expected 'throw'");

    if (current_token().is_one_of(
        K::Semicolon, K::DoubleSemicolon, K::RightParen,
        K::RightSquare, K::RightCurly, K::Colon, K::Comma, K::End
    )) {
        return make<nodes::ThrowExpression>(nullptr, static_cast<std::uint32_t>(line));
    }

    nodes::ASTNode* operand = parse_expression_pratt(to_int(Precedence::Assignment));
    return make<nodes::ThrowExpression>(operand, static_cast<std::uint32_t>(line));
}

SmallVector<std::string_view, 4> Parser::parse_binding_names() {
    using K = tokenizing::Token::Kind;
    expect(K::LeftSquare, "Expected '[' to start structured binding");
    SmallVector<std::string_view, 4> names;

    if (concrete_match(K::RightSquare)) {
        throw ParserError::invalid_expression(reporter, current_token(), "Structured binding must name at least one variable");
    }

    do {
        if (current_token().kind() != K::Identifier) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected a binding name");
        }

        std::string_view nm = current_token().lexeme();
        
        if (is_keyword(nm)) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Binding name cannot be a keyword");
        }

        names.push_back(nm);
        advance();
    } while (match(K::Comma));

    if (!match(K::RightSquare)) {
        throw ParserError::missing_token(reporter, current_token(), K::RightSquare, "Expected ']' to close structured binding");
    }

    return names;
}

} // namespace parsing
} // namespace walnut
