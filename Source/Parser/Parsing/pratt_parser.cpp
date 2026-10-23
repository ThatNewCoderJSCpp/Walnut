#include "Parser/Parsing/pratt_parser.hpp"
#include <cstring>

namespace walnut {
namespace parsing {

namespace {

std::string_view builtin_member_name(tokenizing::Token::Kind kind) {
    switch (kind) {
        case tokenizing::Token::Kind::SizeofKeyword:  return "sizeof";
        case tokenizing::Token::Kind::AlignofKeyword: return "alignof";
        case tokenizing::Token::Kind::CountOfKeyword: return "countof";
        case tokenizing::Token::Kind::EndKeyword:     return "end";
        default:                                      return {};
    }
}

} // namespace

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
        case tokenizing::Token::Kind::DoubleQuestion: return Precedence::Ternary;
        case tokenizing::Token::Kind::QuestionDot: return Precedence::Postfix;
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

bool Parser::initializer_brace_ahead() {
    using K = tokenizing::Token::Kind;
    int depth = 0;

    for (std::size_t k = 0; k < 4096; ++k) {
        const tokenizing::Token& t = token_at(k);
        if (t.kind() == K::End) return false;
        if (t.kind() == K::LeftCurly) ++depth;
        else if (t.kind() == K::RightCurly && --depth == 0) {
            const K after = token_at(k + 1).kind();
            return after == K::Comma || after == K::LeftCurly;
        }
    }

    return false;
}

nodes::ASTNode* Parser::parse_call_argument() {
    const std::size_t line = current_token().line();
    const bool named = current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::Equal;
    nodes::ASTNode* arg = parse_expression_pratt(to_int(Precedence::None));
    if (named && arg && arg->kind == nodes::ASTNode::Kind::BinaryExpression) {
        auto* b = static_cast<nodes::BinaryExpression*>(arg);
        if (b->op == tokenizing::Token::Kind::Equal && b->left && b->left->kind == nodes::ASTNode::Kind::Identifier) b->named_argument = true;
    }
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
    const char* prefix_at = current_token().data();
    nodes::ASTNode* left = parse_prefix();
    if (left && !left->at) left->at = prefix_at;

    while (true) {
        tokenizing::Token::Kind op = current_token().kind();
        const bool callable_lt   = (op == tokenizing::Token::Kind::LessThan) && is_callable_node(left);
        const bool template_call = callable_lt && template_call_ahead();
        const bool template_inst = callable_lt && !template_call && template_scope_ahead();
        const bool template_val  = callable_lt && !template_call && !template_inst && ((is_known_template(left) && known_template_ahead()) || template_value_ahead());
        const bool template_ambig = callable_lt && !template_call && !template_inst && !template_val && template_ambiguous_ahead();
        const bool template_angle = template_call || template_inst || template_val || template_ambig;
        const bool brace_construct = (op == tokenizing::Token::Kind::LeftCurly) && (!m_no_brace_construct || initializer_brace_ahead()) && is_constructible_callee(left);

        if (m_in_template_args && (
                op == tokenizing::Token::Kind::LessThan        ||
                op == tokenizing::Token::Kind::GreaterThan     ||
                op == tokenizing::Token::Kind::DoubleLessThan  ||
                op == tokenizing::Token::Kind::DoubleGreaterThan
            )
        ) {
            if (!template_angle) break;
        }

        if (op == tokenizing::Token::Kind::QuestionMark && lookahead(1).is_one_of(
                tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::DoubleSemicolon, tokenizing::Token::Kind::RightParen,
                tokenizing::Token::Kind::RightSquare, tokenizing::Token::Kind::RightCurly, tokenizing::Token::Kind::Comma, tokenizing::Token::Kind::End)) {
            if (m_propagation.empty()) throw ParserError::invalid_expression(reporter, current_token(), "'?' passes a missing value or an error on to the caller, so it can only be used inside a function");
            const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
            if (!m_propagation.back()) m_propagation.back() = m_cur_tok;
            advance();
            left = make_library_call({ "std", "optional", "detail", "unwrap" }, { left }, at);
            continue;
        }

        if (to_int(Precedence::Comparison) >= min_precedence) {
            std::size_t skip = 0;
            bool negate = false;
            if (op == tokenizing::Token::Kind::InKeyword) skip = 1;
            else if (op == tokenizing::Token::Kind::Identifier && current_token().lexeme() == "not" && lookahead(1).kind() == tokenizing::Token::Kind::InKeyword) { skip = 2; negate = true; }
            else if (op == tokenizing::Token::Kind::ExclamationMark && lookahead(1).kind() == tokenizing::Token::Kind::InKeyword) { skip = 2; negate = true; }

            const auto has_word = [](const tokenizing::Token& t) { return t.kind() == tokenizing::Token::Kind::Identifier && t.lexeme() == "has"; };
            std::size_t has_skip = 0;
            if (has_word(current_token())) has_skip = 1;
            else if (op == tokenizing::Token::Kind::ExclamationMark && has_word(lookahead(1))) { has_skip = 2; negate = true; }
            else if (op == tokenizing::Token::Kind::Identifier && current_token().lexeme() == "not" && has_word(lookahead(1))) { has_skip = 2; negate = true; }

            if (has_skip) {
                const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
                const char* op_at = current_token().data();
                for (std::size_t i = 0; i < has_skip; ++i) advance();
                nodes::ASTNode* value = parse_expression_pratt(to_int(Precedence::Comparison) + 1);
                left = make_in_test(value, left, negate, at);
                if (left && !left->at) left->at = op_at;
                continue;
            }

            if (skip) {
                const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
                const char* op_at = current_token().data();
                for (std::size_t i = 0; i < skip; ++i) advance();
                nodes::ASTNode* container = parse_expression_pratt(to_int(Precedence::Comparison) + 1);
                left = make_in_test(left, container, negate, at);
                if (left && !left->at) left->at = op_at;
                continue;
            }
        }

        if ((op == tokenizing::Token::Kind::IsKeyword || op == tokenizing::Token::Kind::AsKeyword) && to_int(Precedence::Comparison) >= min_precedence) {
            left = parse_type_test(left, op == tokenizing::Token::Kind::IsKeyword);
            continue;
        }

        if (op == tokenizing::Token::Kind::DoubleColon && m_slice_depth > 0 && !lookahead(1).is_one_of(tokenizing::Token::Kind::Identifier, tokenizing::Token::Kind::LessThan) && !loose_keyword(lookahead(1).kind())) break;

        Precedence prec = (template_angle || brace_construct) ? Precedence::Postfix : get_infix_precedence(op);
        if (to_int(prec) < min_precedence || prec == Precedence::None) break;
        const char* op_at = current_token().data();
        nodes::ASTNode* made = parse_infix(left, op, prec);
        if (made && made != left && !made->at) made->at = op_at;
        left = made;
    }

    return left;
}

bool Parser::comprehension_ahead() {
    using K = tokenizing::Token::Kind;
    int depth = 0;
    for (std::size_t i = 1; ; ++i) {
        const tokenizing::Token& t = lookahead(i);
        switch (t.kind()) {
            case K::End: return false;
            case K::LeftParen: case K::LeftSquare: case K::LeftCurly: ++depth; break;
            case K::RightParen: case K::RightCurly: --depth; break;
            case K::RightSquare:
                if (depth == 0) return false;
                --depth;
                break;
            case K::ForKeyword:
                if (depth == 0) return i > 1;
                break;
            default: break;
        }
        if (depth < 0) return false;
    }
}

nodes::ASTNode* Parser::parse_comprehension() {
    using K = tokenizing::Token::Kind;
    const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
    const std::uint32_t first_order = m_node_order;
    m_needs_collections = true;
    expect(K::LeftSquare, "Expected '['");
    nodes::ASTNode* element = parse_expression_pratt(to_int(Precedence::None));

    struct Clause {
        bool is_for = true;
        parser_types::TypeInfo type;
        modifiers::RawModifiers mods;
        std::string_view name;
        SmallVector<std::string_view, 4> bindings;
        nodes::ASTNode* source = nullptr;
        nodes::ASTNode* condition = nullptr;
    };
    std::vector<Clause> clauses;

    while (current_token().is_one_of(K::ForKeyword, K::IfKeyword)) {
        Clause c;
        if (match(K::IfKeyword)) {
            c.is_for = false;
            expect(K::LeftParen, "Expected '(' after 'if' in a comprehension");
            c.condition = parse_expression();
            expect(K::RightParen, "Expected ')' after the condition");
            clauses.push_back(c);
            continue;
        }
        advance();
        expect(K::LeftParen, "Expected '(' after 'for' in a comprehension, as in '[x * 2 for (int: x in values)]'");
        c.mods = parse_raw_modifiers();
        c.type = parse_type_info();
        expect(K::Colon, "Expected ':' after the type in a comprehension's 'for'");
        if (concrete_match(K::LeftSquare)) {
            c.bindings = parse_binding_names();
        } else {
            if (current_token().kind() != K::Identifier) throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected a name for the loop variable");
            c.name = current_token().lexeme();
            advance();
        }
        expect(K::InKeyword, "Expected 'in' in a comprehension's 'for'");
        c.source = parse_expression();
        expect(K::RightParen, "Expected ')' to close the comprehension's 'for'");
        if (c.mods.has(modifiers::RawModifiers::Const)) c.type.modifiers.add(modifiers::RawModifiers::Const);
        clauses.push_back(c);
    }

    if (!match(K::RightSquare)) throw ParserError::missing_token(reporter, current_token(), K::RightSquare, "Expected ']' to close the comprehension");
    if (clauses.empty() || !clauses.front().is_for) throw ParserError::invalid_expression(reporter, current_token(), "A comprehension starts with a 'for' clause, as in '[x * 2 for (int: x in values)]'");

    const std::string_view out_name = "walnut_comprehension";
    auto* body = make<nodes::BlockStatement>();
    auto early = [first_order](nodes::ASTNode* n) {
        n->order = first_order;
        if (n->kind == nodes::ASTNode::Kind::VariableDeclaration) static_cast<nodes::VariableDeclaration*>(n)->synthetic = true;
        return n;
    };
    std::size_t dummy = 0;

    for (const Clause& c : clauses) {
        if (!c.is_for) continue;
        auto element_of = [&]() {
            parser_types::TypeInfo info;
            nodes::ASTNode* call = make_library_call({ "std", "core", "element_of" }, { c.source->clone_into(arena) }, at);
            info.type = make<parser_types::ComputedType>(parser_types::TypeOp::Typeof, call, std::vector<parser_types::TypeInfo>{});
            return info;
        };
        const bool plain = c.bindings.empty() && c.type.type && !c.type.has_reference() && !c.type.has_pointer()
                        && !(c.type.type->kind() == parser_types::Type::Kind::Primitive && static_cast<const parser_types::PrimitiveType*>(c.type.type)->is_auto());
        if (!c.bindings.empty()) {
            const std::string_view holder = intern("walnut_comprehension_item_" + std::to_string(dummy++));
            body->add_statement(early(make<nodes::VariableDeclaration>(element_of(), holder, nullptr, at)));
            parser_types::TypeInfo auto_info;
            auto_info.type = parser_types::PrimitiveType::make_auto(arena);
            body->add_statement(early(make<nodes::VariableDeclaration>(auto_info, c.bindings, early(make<nodes::Identifier>(holder, at)), at)));
        } else if (plain) {
            parser_types::TypeInfo info = c.type.clone_into(arena);
            info.modifiers.clear();
            body->add_statement(early(make<nodes::VariableDeclaration>(info, c.name, nullptr, at)));
        } else {
            body->add_statement(early(make<nodes::VariableDeclaration>(element_of(), c.name, nullptr, at)));
        }
    }

    parser_types::TypeInfo element_type;
    element_type.type = make<parser_types::ComputedType>(parser_types::TypeOp::Typeof, element->clone_into(arena), std::vector<parser_types::TypeInfo>{});
    auto* targ = make<parser_types::TemplateArgument>();
    targ->form = parser_types::TemplateArgument::Form::Type;
    targ->type = element_type;
    auto* make_list = static_cast<nodes::CallExpression*>(make_library_call({ "std", "collections", "detail", "empty_list" }, {}, at));
    make_list->set_template_args({ targ });
    make_list->has_angle_args = true;
    parser_types::TypeInfo auto_info;
    auto_info.type = parser_types::PrimitiveType::make_auto(arena);
    body->add_statement(make<nodes::VariableDeclaration>(auto_info, out_name, make_list, at));

    auto* push = make<nodes::MemberAccessExpression>(make<nodes::Identifier>(out_name, at), std::string_view("push_back"), nodes::MemberAccessExpression::Op::Dot, at);
    auto* push_call = make<nodes::CallExpression>(push, at);
    push_call->add_argument(element);
    nodes::ASTNode* inner = make<nodes::ExpressionStatement>(push_call);

    for (std::size_t i = clauses.size(); i-- > 0; ) {
        const Clause& c = clauses[i];
        auto* block = make<nodes::BlockStatement>();
        block->add_statement(inner);
        if (!c.is_for) {
            auto* branch = make<nodes::IfBranch>(c.condition, block);
            auto* test = make<nodes::IfStatement>();
            test->branches.push_back(branch);
            inner = test;
        } else if (!c.bindings.empty()) {
            inner = make<nodes::ForEachStatement>(c.type, c.mods, c.bindings, c.source, block, at);
        } else {
            inner = make<nodes::ForEachStatement>(c.type, c.mods, c.name, c.source, block, at);
        }
    }

    body->add_statement(inner);
    body->add_statement(make<nodes::ReturnStatement>(make<nodes::Identifier>(out_name, at), at));

    auto* captures = make<nodes::LambdaCaptureList>();
    captures->m_captures.push_back(nodes::LambdaCaptureItem(nodes::LambdaCaptureItem::Mode::AllByReference));
    parser_types::TypeInfo return_type;
    return_type.type = parser_types::PrimitiveType::make_auto(arena);
    auto* lambda = make<nodes::LambdaExpression>(captures, make<nodes::FunctionParameters>(), return_type, modifiers::FunctionQualifiers{}, body, at);
    return make<nodes::CallExpression>(lambda, at);
}

nodes::ASTNode* Parser::wrap_propagation(nodes::ASTNode* body, const parser_types::TypeInfo& result, std::uint32_t line) {
    if (!body || body->kind != nodes::ASTNode::Kind::BlockStatement) return body;
    const bool deduced = !result.type || (result.type->kind() == parser_types::Type::Kind::Primitive
        && (static_cast<const parser_types::PrimitiveType*>(result.type)->is_auto() || static_cast<const parser_types::PrimitiveType*>(result.type)->base_kind() == parser_types::PrimitiveType::BaseKind::Dynamic));
    const tokenizing::Token& used_at = m_propagation.empty() || !m_propagation.back() ? current_token() : *m_propagation.back();
    if (deduced) {
        throw ParserError::invalid_expression(reporter, used_at, "A function that uses '?' needs a written result type, an Optional or a Result, so the missing value or error has somewhere to go");
    }
    if (result.type && result.type->kind() == parser_types::Type::Kind::Primitive && !result.has_indirection()) {
        throw ParserError::invalid_expression(reporter, used_at, "'?' passes a missing value or an error on by returning it, so the function that uses it has to return an Optional or a Result, not a built-in type");
    }
    auto* targ = make<parser_types::TemplateArgument>();
    targ->form = parser_types::TemplateArgument::Form::Type;
    targ->type = result.clone_into(arena);
    const std::string_view caught = "walnut_propagated";
    auto* pass_on = static_cast<nodes::CallExpression*>(make_library_call({ "std", "optional", "detail", "propagate" }, { make<nodes::Identifier>(caught, line) }, line));
    pass_on->set_template_args({ targ });
    pass_on->has_angle_args = true;
    auto* handler = make<nodes::BlockStatement>();
    handler->add_statement(make<nodes::ReturnStatement>(pass_on, line));
    parser_types::TypeInfo catch_type;
    catch_type.type = make<parser_types::UserDefinedType>(std::vector<std::string_view>{ "std", "optional", "detail", "Propagated" });
    catch_type.modifiers.add(modifiers::RawModifiers::Const);
    catch_type.add_reference();
    auto* guarded = make<nodes::TryCatchStatement>(static_cast<nodes::BlockStatement*>(body), caught, catch_type, true, handler, line);
    auto* outer = make<nodes::BlockStatement>();
    outer->add_statement(guarded);
    return outer;
}

std::string_view Parser::intern(const std::string& text) {
    char* kept = static_cast<char*>(arena.alloc_raw(text.size() + 1, 1));
    std::memcpy(kept, text.data(), text.size());
    kept[text.size()] = '\0';
    return std::string_view(kept, text.size());
}

nodes::ASTNode* Parser::make_library_call(std::initializer_list<std::string_view> path, std::vector<nodes::ASTNode*> args, std::uint32_t line) {
    auto* name = make<nodes::QualifiedIdentifier>(line, false);
    for (std::string_view part : path) name->add_part(part);
    auto* call = make<nodes::CallExpression>(name, line);
    for (nodes::ASTNode* a : args) call->add_argument(a);
    return call;
}

nodes::ASTNode* Parser::make_in_test(nodes::ASTNode* value, nodes::ASTNode* container, bool negate, std::uint32_t line) {
    nodes::ASTNode* call = nullptr;
    if (container && container->kind == nodes::ASTNode::Kind::BraceInitializerList) {
        std::vector<nodes::ASTNode*> args{ value };
        for (nodes::ASTNode* option : static_cast<nodes::BraceInitializerList*>(container)->m_elements) args.push_back(option);
        call = make_library_call({ "std", "core", "equals_any" }, std::move(args), line);
    } else {
        call = make_library_call({ "std", "core", "contains_in" }, { container, value }, line);
    }
    if (!negate) return call;
    return make<nodes::UnaryExpression>(call, tokenizing::Token::Kind::ExclamationMark, true, line);
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
    take_keyword_as_name(true);
    if (current_token().kind() == tokenizing::Token::Kind::SelfKeyword) return make_self(line);
    
    if (current_token().kind() == tokenizing::Token::Kind::LeftCurly) {
        return parse_brace_initializer_list();
    }

    if (match_ahead()) return parse_match(true);

    if (current_token().kind() == tokenizing::Token::Kind::LeftSquare) {
        if (comprehension_ahead()) return parse_comprehension();
        advance(); 
        return parse_lambda_expression();
    }
    
    if (current_token().is_one_of(
        tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float,
        tokenizing::Token::Kind::String, tokenizing::Token::Kind::TextLiteral,
        tokenizing::Token::Kind::Character, tokenizing::Token::Kind::True, tokenizing::Token::Kind::False,
        tokenizing::Token::Kind::NullptrKeyword, tokenizing::Token::Kind::InfinityKeyword,
        tokenizing::Token::Kind::NanKeyword, tokenizing::Token::Kind::UndefinedKeyword, tokenizing::Token::Kind::IndeterminateKeyword,
        tokenizing::Token::Kind::ThisKeyword
    )) {
        if (current_token().is_one_of(tokenizing::Token::Kind::Integer, tokenizing::Token::Kind::Float)) validate_numeric_literal(current_token());

        if (current_token().kind() == tokenizing::Token::Kind::TextLiteral && has_interpolation(current_token().lexeme())) {
            const tokenizing::Token tok = current_token();
            advance();
            return parse_text_template(tok);
        }

        auto literal = make<nodes::Literal>(current_token().lexeme(), current_token().kind(), line);
        advance();
        return literal;
    }
    
    if (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        return parse_identifier_or_qualified();
    }

    if (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::LeftParen) {
        const std::string_view trait_spelling = current_token().lexeme();
        const parser_types::TypeTraitInfo* trait = (trait_spelling.size() > 2 && trait_spelling.substr(0, 2) == "__") ? parser_types::find_type_trait(trait_spelling.substr(2)) : nullptr;

        if (!trait && parser_types::find_type_trait(trait_spelling)) {
            using TK = tokenizing::Token::Kind;
            switch (lookahead(2).kind()) {
                case TK::AutoDeclaration: case TK::DynamicDeclaration: case TK::BooleanDeclaration: case TK::CharacterDeclaration:
                case TK::ConstantDeclaration: case TK::DecimalDeclaration: case TK::IntegerDeclaration: case TK::LongDeclaration:
                case TK::StringDeclaration: case TK::ShortDeclaration: case TK::UnsignedDeclaration: case TK::TextDeclaration:
                case TK::VoidKeyword:
                    throw ParserError::invalid_expression(reporter, current_token(),
                        "type traits are variable templates in std::traits: write 'std::traits::" + std::string(trait_spelling) + "<...>' instead of '" + std::string(trait_spelling) + "(...)'");
                default: break;
            }
        }

        if (trait) {
            const tokenizing::Token at = current_token();
            advance();
            advance();
            AngleGuard _g(this, false);
            std::vector<parser_types::TemplateArgument*> operands;

            do {
                auto* arg = make<parser_types::TemplateArgument>();
                arg->form = parser_types::TemplateArgument::Form::Type;
                arg->type = parse_type_info();
                if (match(tokenizing::Token::Kind::Ellipsis)) {
                    if (trait->arity >= 0 || operands.empty()) throw ParserError::invalid_expression(reporter, at, std::string(trait->name) + " can't take a pack here; only the types after the first one of a trait that takes any number of types can be a pack");
                    arg->type.pack_expansion = true;
                }
                operands.push_back(arg);
            } while (match(tokenizing::Token::Kind::Comma));

            expect(tokenizing::Token::Kind::RightParen, "Expected ')' to close the type trait");

            if (trait->arity < 0 && operands.empty()) {
                throw ParserError::invalid_expression(reporter, at, std::string(trait->name) + " takes at least 1 type, but none was given");
            }

            if (trait->arity >= 0 && static_cast<int>(operands.size()) != trait->arity) {
                throw ParserError::invalid_expression(reporter, at,
                    std::string(trait->name) + " takes " + std::to_string(trait->arity) + (trait->arity == 1 ? " type" : " types") + ", but " + std::to_string(operands.size()) + (operands.size() == 1 ? " was given" : " were given"));
            }

            auto* q = make<nodes::TypeQueryExpression>(nodes::TypeQueryExpression::Op::Trait, operands.front(), line);
            q->trait = trait->trait;
            for (std::size_t i = 1; i < operands.size(); ++i) q->extra_operands.push_back(operands[i]);
            return q;
        }
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

nodes::ASTNode* Parser::qualified_member(nodes::ASTNode* left, std::string_view member, nodes::MemberAccessExpression::Op op, std::size_t line) {
    std::vector<std::string_view> qualifier;

    while (current_token().kind() == tokenizing::Token::Kind::DoubleColon) {
        std::string_view next = builtin_member_name(lookahead(1).kind());
        if (next.empty() && loose_keyword(lookahead(1).kind())) next = lookahead(1).lexeme();
        if (next.empty() && lookahead(1).kind() != tokenizing::Token::Kind::Identifier) break;
        if (next.empty()) next = lookahead(1).lexeme();
        advance();
        advance();
        qualifier.push_back(member);
        member = next;
    }

    auto* m = make<nodes::MemberAccessExpression>(left, member, op, line);
    m->qualifier = std::move(qualifier);
    return m;
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
            call->has_angle_args = true;

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

        const bool known = is_known_template(left) && known_template_ahead();

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
        using K = tokenizing::Token::Kind;
        const bool bare_double = current_token().kind() == K::DoubleColon && lookahead(1).kind() != K::Identifier && !loose_keyword(lookahead(1).kind());
        ++m_slice_depth;
        nodes::ASTNode* index = (current_token().kind() == K::Colon || bare_double) ? nullptr : parse_expression_pratt(to_int(Precedence::None));
        --m_slice_depth;

        if (current_token().is_one_of(K::Colon, K::DoubleColon)) {
            const bool both = current_token().kind() == K::DoubleColon;
            advance();
            nodes::ASTNode* stop = nullptr;
            nodes::ASTNode* step = nullptr;
            bool step_follows = both;
            if (!both) {
                if (!current_token().is_one_of(K::Colon, K::RightSquare)) {
                    ++m_slice_depth;
                    stop = parse_expression_pratt(to_int(Precedence::None));
                    --m_slice_depth;
                }
                step_follows = match(K::Colon);
            }
            if (step_follows && current_token().kind() != K::RightSquare) step = parse_expression_pratt(to_int(Precedence::None));
            if (!match(K::RightSquare)) throw ParserError::missing_token(reporter, current_token(), K::RightSquare, "Expected ']' after a slice such as 'v[1:3]'");
            const std::uint32_t at = static_cast<std::uint32_t>(line);
            auto number = [&](std::string_view text) { return make<nodes::Literal>(text, K::Integer, at); };
            auto flag = [&](bool on) { return make<nodes::Literal>(std::string_view(on ? "true" : "false"), on ? K::True : K::False, at); };
            auto* member = make<nodes::MemberAccessExpression>(left, std::string_view("sliced"), nodes::MemberAccessExpression::Op::Dot, at);
            auto* call = make<nodes::CallExpression>(member, at);
            call->add_argument(index ? index : number("0"));
            call->add_argument(stop ? stop : number("0"));
            call->add_argument(step ? step : number("1"));
            call->add_argument(flag(index != nullptr));
            call->add_argument(flag(stop != nullptr));
            return call;
        }

        if (!index) throw ParserError::invalid_expression(reporter, current_token(), "Expected an index inside '[ ]'");

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
    
    if (op == tokenizing::Token::Kind::DoubleQuestion) {
        const std::uint32_t at = static_cast<std::uint32_t>(line);
        advance();
        nodes::ASTNode* fallback = parse_expression_pratt(to_int(Precedence::Ternary));
        auto* body = make<nodes::BlockStatement>();
        body->add_statement(make<nodes::ReturnStatement>(fallback, at));
        auto* captures = make<nodes::LambdaCaptureList>();
        captures->m_captures.push_back(nodes::LambdaCaptureItem(nodes::LambdaCaptureItem::Mode::AllByReference));
        parser_types::TypeInfo result;
        result.type = parser_types::PrimitiveType::make_auto(arena);
        auto* lambda = make<nodes::LambdaExpression>(captures, make<nodes::FunctionParameters>(), result, modifiers::FunctionQualifiers{}, body, at);
        return make_library_call({ "std", "optional", "detail", "coalesce" }, { left, lambda }, at);
    }

    if (op == tokenizing::Token::Kind::QuestionDot) {
        const std::uint32_t at = static_cast<std::uint32_t>(line);
        const std::uint32_t first_order = m_node_order;
        const std::string_view name = intern("walnut_value_" + std::to_string(m_node_order));
        const_cast<tokenizing::Token*>(m_cur_tok)->kind(tokenizing::Token::Kind::Dot);
        nodes::ASTNode* inner = parse_infix(make<nodes::Identifier>(name, at), tokenizing::Token::Kind::Dot, Precedence::Postfix);
        while (current_token().is_one_of(tokenizing::Token::Kind::Dot, tokenizing::Token::Kind::LeftParen, tokenizing::Token::Kind::LeftSquare, tokenizing::Token::Kind::SingleRightArrow)) {
            inner = parse_infix(inner, current_token().kind(), Precedence::Postfix);
        }
        parser_types::TypeInfo param_type;
        param_type.type = parser_types::PrimitiveType::make_auto(arena);
        param_type.modifiers.add(modifiers::RawModifiers::Const);
        param_type.add_reference();
        auto* param = make<nodes::FunctionParameter>(name, param_type);
        param->order = first_order;
        auto* params = make<nodes::FunctionParameters>();
        params->add(param);
        params->order = first_order;
        auto* body = make<nodes::BlockStatement>();
        body->add_statement(make<nodes::ReturnStatement>(inner, at));
        auto* captures = make<nodes::LambdaCaptureList>();
        captures->m_captures.push_back(nodes::LambdaCaptureItem(nodes::LambdaCaptureItem::Mode::AllByReference));
        parser_types::TypeInfo result;
        result.type = parser_types::PrimitiveType::make_auto(arena);
        auto* lambda = make<nodes::LambdaExpression>(captures, params, result, modifiers::FunctionQualifiers{}, body, at);
        return make_library_call({ "std", "optional", "detail", "chain" }, { left, lambda }, at);
    }

    if (op == tokenizing::Token::Kind::QuestionMark) {
        advance();
        nodes::ASTNode* then_branch = parse_expression_pratt(to_int(Precedence::None));

        if (!match(tokenizing::Token::Kind::Colon)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Colon, "Expected ':' in ternary conditional operator; if the '?' was meant to pass a missing value or an error on to the caller, put it in parentheses, as in '(value?) + 1'");
        }

        nodes::ASTNode* else_branch = parse_expression_pratt(to_int(Precedence::Ternary));
        return make<nodes::TernaryExpression>(left, then_branch, else_branch, line);
    }

    if (op == tokenizing::Token::Kind::Dot) {
        advance(); 

        std::string_view member = builtin_member_name(current_token().kind());
        if (member.empty() && current_token().is_keyword() && !current_token().lexeme().empty()) member = current_token().lexeme();

        if (member.empty() && current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected member name after '.'"
            );
        }

        if (member.empty()) member = current_token().lexeme();
        advance();
        return qualified_member(left, member, nodes::MemberAccessExpression::Op::Dot, line);
    }

    if (op == tokenizing::Token::Kind::SingleRightArrow) {
        advance(); 

        std::string_view member = builtin_member_name(current_token().kind());
        if (member.empty() && current_token().is_keyword() && !current_token().lexeme().empty()) member = current_token().lexeme();

        if (member.empty() && current_token().kind() != tokenizing::Token::Kind::Identifier) {
            throw ParserError::unexpected_token(
                reporter,
                current_token(),
                {tokenizing::Token::Kind::Identifier},
                "Expected member name after '\u2192'"
            );
        }

        if (member.empty()) member = current_token().lexeme();
        advance();
        return qualified_member(left, member, nodes::MemberAccessExpression::Op::Arrow, line);
    }

    if (op == tokenizing::Token::Kind::DoubleColon) {
        advance();

        if (current_token().kind() == tokenizing::Token::Kind::LessThan && is_callable_node(left)) {   // turbofish: Name::<args>
            advance(); 
            std::vector<parser_types::TemplateArgument*> targs = parse_template_argument_list_body();
            return make<nodes::TemplateInstantiation>(left, std::move(targs), line);
        }

        take_keyword_as_name(false);
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

nodes::ASTNode* Parser::parse_type_test(nodes::ASTNode* operand, bool is_test) {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    advance();

    if (current_token().is_one_of(K::InfinityKeyword, K::NanKeyword, K::UndefinedKeyword, K::IndeterminateKeyword)) {
        if (!is_test) throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "'as' needs a type; to check for a special value, use 'is' (for example 'x is undefined')");
        const K which = current_token().kind();
        advance();
        auto* test = make<nodes::CastExpression>(nodes::CastExpression::CastKind::Is, parser_types::TypeInfo{}, operand, static_cast<std::uint32_t>(line));
        test->special_test = which == K::InfinityKeyword ? 1 : which == K::NanKeyword ? 2 : which == K::UndefinedKeyword ? 3 : 4;
        return test;
    }
    parser_types::TypeInfo target = parse_type_info();
    const std::size_t end = tokens.position();

    if (const parser_types::IndirectionQualifier* last = target.indirection.back(); last && last->is_reference() && end > 0) {
        const K before = tokens.begin()[end - 1].kind();
        const K next = current_token().kind();
        const bool terminator = next == K::RightParen || next == K::Semicolon || next == K::Comma || next == K::RightSquare || next == K::RightCurly || next == K::End || next == K::QuestionMark || next == K::Colon;

        if ((before == K::Ampersand || before == K::LogicAnd) && !terminator) {
            target.indirection.pop_back();
            tokens.seek(end - 1);
            m_cur_tok = &tokens.current();
        }
    }

    return make<nodes::CastExpression>(is_test ? nodes::CastExpression::CastKind::Is : nodes::CastExpression::CastKind::As, target, operand, static_cast<std::uint32_t>(line));
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
    parser_types::TemplateArgument* operand = nullptr;
    if (current_token().kind() == K::Identifier && lookahead(1).kind() == K::RightParen) {
        for (auto it = m_pack_aliases.rbegin(); it != m_pack_aliases.rend(); ++it) {
            if (it->name != current_token().lexeme()) continue;
            operand = make<parser_types::TemplateArgument>();
            operand->form = parser_types::TemplateArgument::Form::Value;
            operand->value = parse_identifier_or_qualified();
            break;
        }
    }
    if (!operand) operand = parse_type_or_value_operand();

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
            args.push_back(parse_call_argument());
            while (match(K::Comma)) { args.push_back(parse_call_argument()); }
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
        case K::DiscardConstKeyword:
            throw ParserError::invalid_expression(reporter, current_token(),
                "discard_const works on types, like 'discard_const(typeof(x)): y = x;'; to remove const from a value, use const_cast<T>(value)");
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
        reject_receiver(params, "a requires-expression");
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
