#include "Parser/Parsing/Main/parse_match.hpp"

#include <functional>
#include <string>

namespace walnut {
namespace parsing {

Parser::ParamFrame::ParamFrame(Parser& p, const nodes::FunctionParameters* params) : parser(p) {
    std::vector<ParamInfo> frame;
    if (params) {
        for (const nodes::FunctionParameter* param : params->m_params) {
            if (!param) continue;
            ParamInfo info;
            info.name = param->get_name();
            info.variadic = param->is_variadic();
            const parser_types::TypeInfo& t = param->get_type();
            if (info.variadic && t.type && t.type->is_user_defined()) {
                const auto* named = static_cast<const parser_types::UserDefinedType*>(t.type);
                if (named->parts().size() == 1) {
                    for (std::string_view pack : p.m_template_pack_names) {
                        if (pack == named->name()) info.template_pack = true;
                    }
                }
            }
            frame.push_back(info);
        }
    }
    p.m_param_frames.push_back(std::move(frame));
}

bool Parser::match_ahead() {
    using K = tokenizing::Token::Kind;
    if (current_token().kind() != K::Identifier || current_token().lexeme() != "match") return false;
    if (lookahead(1).kind() != K::LeftParen) return false;
    int depth = 0;
    for (std::size_t i = 1; ; ++i) {
        const tokenizing::Token& t = lookahead(i);
        if (t.kind() == K::End) return false;
        if (t.is_one_of(K::LeftParen, K::LeftSquare, K::LeftCurly)) ++depth;
        if (t.is_one_of(K::RightParen, K::RightSquare, K::RightCurly)) {
            if (--depth == 0) return t.kind() == K::RightParen && lookahead(i + 1).kind() == K::LeftCurly;
        }
    }
}

bool Parser::typed_binding_ahead() {
    using K = tokenizing::Token::Kind;
    int depth = 0;
    for (std::size_t i = 0; ; ++i) {
        const tokenizing::Token& t = i == 0 ? current_token() : lookahead(i);
        switch (t.kind()) {
            case K::End: case K::Semicolon: case K::LeftCurly: case K::QuestionMark: case K::DoubleRightArrow: case K::IfKeyword:
                return false;
            case K::LeftParen: case K::LeftSquare: case K::LessThan: ++depth; break;
            case K::RightParen: case K::RightSquare: case K::GreaterThan:
                if (depth == 0) return false;
                --depth;
                break;
            case K::DoubleGreaterThan:
                if (depth < 2) return false;
                depth -= 2;
                break;
            case K::Comma:
                if (depth == 0) return false;
                break;
            case K::Colon: {
                if (depth != 0 || i == 0) return false;
                const tokenizing::Token& name = lookahead(i + 1);
                const tokenizing::Token& after = lookahead(i + 2);
                return name.kind() == K::Identifier && after.is_one_of(K::DoubleRightArrow, K::Comma, K::RightParen, K::IfKeyword);
            }
            default: break;
        }
    }
}

bool Parser::list_pattern_ahead() {
    using K = tokenizing::Token::Kind;
    if (current_token().kind() != K::LeftParen) return false;
    if (lookahead(1).kind() == K::RightParen) return true;
    int depth = 0;
    for (std::size_t i = 1; ; ++i) {
        const tokenizing::Token& t = lookahead(i);
        switch (t.kind()) {
            case K::End: return false;
            case K::LeftParen: case K::LeftSquare: case K::LeftCurly: ++depth; break;
            case K::RightSquare: case K::RightCurly: --depth; break;
            case K::RightParen:
                if (depth == 0) return false;
                --depth;
                break;
            case K::Comma: case K::Ellipsis: case K::Underscore:
                if (depth == 0) return true;
                break;
            case K::Colon:
                if (depth == 0 && lookahead(i + 1).kind() == K::Identifier) return true;
                break;
            default: break;
        }
    }
}

Parser::MatchPattern Parser::parse_match_pattern(bool top, bool pack_subject) {
    using K = tokenizing::Token::Kind;
    using Form = MatchPattern::Form;
    MatchPattern p;

    if (current_token().kind() == K::Underscore || (current_token().kind() == K::Identifier && current_token().lexeme() == "_")) {
        advance();
        p.form = Form::Wildcard;
        return p;
    }

    if (current_token().kind() == K::LeftParen && top && (pack_subject || list_pattern_ahead())) {
        advance();
        p.form = Form::List;
        while (current_token().kind() != K::RightParen) {
            if (p.has_rest) {
                throw ParserError::invalid_expression(reporter, current_token(), "The rest of a list pattern ('rest...' or '...') has to come last");
            }
            if (current_token().kind() == K::Ellipsis) {
                advance();
                p.has_rest = true;
            } else if (current_token().kind() == K::Identifier && lookahead(1).kind() == K::Ellipsis) {
                p.rest_name = current_token().lexeme();
                advance();
                advance();
                p.has_rest = true;
            } else {
                if (current_token().kind() == K::LeftParen && list_pattern_ahead()) {
                    throw ParserError::invalid_expression(reporter, current_token(), "A list pattern can't hold another list pattern; match the inner value in its own 'match'");
                }
                p.items.push_back(parse_match_pattern(false, false));
            }
            if (!match(K::Comma)) break;
        }
        expect(K::RightParen, "Expected ')' to close the list pattern");
        if (p.has_rest && !p.rest_name.empty() && !pack_subject) {
            throw ParserError::invalid_expression(reporter, current_token(), "'" + std::string(p.rest_name) + "...' collects the rest of a variadic parameter, so it only works when the match is on one, as in 'match (values)'");
        }
        return p;
    }

    if (current_token().is_one_of(K::LessThan, K::LessEqual, K::GreaterThan, K::GreaterEqual, K::LogicEqual, K::NotEqual)) {
        p.form = Form::Relation;
        p.relation = current_token().kind();
        advance();
        p.value = parse_expression_pratt(to_int(Precedence::Shift));
        return p;
    }

    const bool not_in = current_token().kind() == K::Identifier && current_token().lexeme() == "not" && lookahead(1).kind() == K::InKeyword;
    const bool bang_in = current_token().kind() == K::ExclamationMark && lookahead(1).kind() == K::InKeyword;
    if (current_token().kind() == K::InKeyword || not_in || bang_in) {
        p.form = Form::In;
        p.negate = not_in || bang_in;
        if (p.negate) advance();
        advance();
        p.value = parse_expression_pratt(to_int(Precedence::Shift));
        return p;
    }

    if (current_token().kind() == K::IsKeyword) {
        advance();
        p.form = Form::Type;
        p.type = parse_type_info();
        return p;
    }

    if (typed_binding_ahead()) {
        p.form = Form::Type;
        p.type = parse_type_info();
        expect(K::Colon, "Expected ':' between the type and the name");
        p.name = current_token().lexeme();
        advance();
        return p;
    }

    if (!top && current_token().kind() == K::Identifier && lookahead(1).is_one_of(K::Comma, K::RightParen)) {
        p.form = Form::Bind;
        p.name = current_token().lexeme();
        advance();
        return p;
    }

    p.form = Form::Value;
    p.value = parse_expression_pratt(to_int(Precedence::LogicOr));
    return p;
}

nodes::ASTNode* Parser::parse_match(bool as_expression) {
    using K = tokenizing::Token::Kind;
    using Form = MatchPattern::Form;
    const std::uint32_t at = static_cast<std::uint32_t>(current_token().line());
    const std::uint32_t first_order = m_node_order;
    const std::size_t id = m_match_count++;
    advance();
    expect(K::LeftParen, "Expected '(' after 'match'");
    nodes::ASTNode* subject = parse_expression();
    expect(K::RightParen, "Expected ')' after the value to match");
    expect(K::LeftCurly, "Expected '{' to start the cases of the match");

    enum class Subject { Plain, RuntimePack, TemplatePack };
    Subject kind = Subject::Plain;
    std::string_view subject_name;
    if (subject->kind == nodes::ASTNode::Kind::Identifier && static_cast<nodes::Identifier*>(subject)->pack_offset == 0) {
        subject_name = static_cast<nodes::Identifier*>(subject)->name;
        bool found = false;
        for (auto frame = m_param_frames.rbegin(); frame != m_param_frames.rend() && !found; ++frame) {
            for (const ParamInfo& info : *frame) {
                if (info.name != subject_name) continue;
                found = true;
                if (info.variadic) kind = info.template_pack ? Subject::TemplatePack : Subject::RuntimePack;
                break;
            }
        }
    }
    const bool pack = kind != Subject::Plain;
    std::function<bool(const MatchPattern&)> pattern_binds = [&](const MatchPattern& p) {
        if (p.form == Form::Bind || !p.name.empty() || !p.rest_name.empty()) return true;
        for (const MatchPattern& item : p.items) if (pattern_binds(item)) return true;
        return false;
    };

    struct Arm {
        std::vector<MatchPattern> patterns;
        nodes::ASTNode* guard = nullptr;
        nodes::ASTNode* body = nullptr;
        bool is_default = false;
        tokenizing::Token start;
    };
    std::vector<Arm> arms;
    bool seen_default = false;

    auto parse_body = [&]() -> nodes::ASTNode* {
        expect(K::DoubleRightArrow, "Expected '=>' before what the case does");
        if (as_expression) {
            nodes::ASTNode* value = parse_expression();
            if (!match(K::Comma)) consume_semicolons();
            return value;
        }
        if (concrete_match(K::LeftCurly)) return parse_block();
        auto* block = make<nodes::BlockStatement>();
        const std::size_t mark = m_spliced.size();
        block->add_statement(parse_statement());
        for (std::size_t i = mark; i < m_spliced.size(); ++i) block->add_statement(m_spliced[i]);
        m_spliced.resize(mark);
        return block;
    };

    while (current_token().kind() != K::RightCurly) {
        if (current_token().kind() == K::End) {
            throw ParserError::missing_token(reporter, current_token(), K::RightCurly, "Expected '}' to close the match");
        }
        Arm arm{ {}, nullptr, nullptr, false, current_token() };
        if (seen_default) {
            throw ParserError::invalid_expression(reporter, current_token(), "'default' has to be the last case of a match, because nothing after it could ever be reached");
        }
        if (match(K::DefaultKeyword)) {
            arm.is_default = true;
            seen_default = true;
            arm.body = parse_body();
            arms.push_back(arm);
            continue;
        }
        if (!match(K::CaseKeyword)) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::CaseKeyword, K::DefaultKeyword}, "Expected 'case' or 'default' in the match");
        }
        arm.patterns.push_back(parse_match_pattern(true, pack));
        while (match(K::Comma)) arm.patterns.push_back(parse_match_pattern(true, pack));

        if (arm.patterns.size() > 1) {
            for (const MatchPattern& p : arm.patterns) {
                if (pattern_binds(p) || p.form == Form::List) {
                    throw ParserError::invalid_expression(reporter, arm.start, "A case with several patterns separated by commas can't use list patterns or give names, because only one of them matches; write a separate case for each");
                }
            }
        }
        for (const MatchPattern& p : arm.patterns) {
            if (pack && p.form != Form::List && p.form != Form::Wildcard) {
                throw ParserError::invalid_expression(reporter, arm.start, "'" + std::string(subject_name) + "' is a variadic parameter, so its cases are list patterns such as '()', '(x)' or '(first, rest...)'");
            }
        }

        const std::size_t aliases = m_pack_aliases.size();
        if (kind == Subject::TemplatePack && arm.patterns.size() == 1 && !arm.patterns[0].rest_name.empty()) {
            m_pack_aliases.push_back(PackAlias{ arm.patterns[0].rest_name, subject_name, static_cast<std::uint32_t>(arm.patterns[0].items.size()) });
        }
        if (match(K::IfKeyword)) {
            expect(K::LeftParen, "Expected '(' after 'if' in a case");
            arm.guard = parse_expression();
            expect(K::RightParen, "Expected ')' after the case's condition");
        }
        arm.body = parse_body();
        m_pack_aliases.resize(aliases);
        arms.push_back(arm);
    }
    expect(K::RightCurly, "Expected '}' to close the match");

    if (arms.empty()) {
        throw ParserError::invalid_expression(reporter, current_token(), "A match needs at least one 'case' or a 'default'");
    }

    auto early = [first_order](nodes::ASTNode* n, bool hidden) {
        n->order = first_order;
        if (hidden && n->kind == nodes::ASTNode::Kind::VariableDeclaration) static_cast<nodes::VariableDeclaration*>(n)->synthetic = true;
        return n;
    };
    auto auto_type = [&](bool reference) {
        parser_types::TypeInfo info;
        info.type = parser_types::PrimitiveType::make_auto(arena);
        if (reference) info.add_reference();
        return info;
    };
    auto ident = [&](std::string_view name) -> nodes::ASTNode* { return make<nodes::Identifier>(name, at); };
    auto number = [&](std::size_t n) -> nodes::ASTNode* { return make<nodes::Literal>(intern(std::to_string(n)), K::Integer, at); };
    auto binary = [&](nodes::ASTNode* l, K op, nodes::ASTNode* r) -> nodes::ASTNode* { return make<nodes::BinaryExpression>(l, op, r, at); };
    auto both = [&](nodes::ASTNode* l, nodes::ASTNode* r) -> nodes::ASTNode* { return !l ? r : (!r ? l : binary(l, K::LogicAnd, r)); };
    auto either = [&](nodes::ASTNode* l, nodes::ASTNode* r) -> nodes::ASTNode* { return !l ? r : (!r ? l : binary(l, K::LogicOr, r)); };
    auto countof_of = [&](nodes::ASTNode* e) -> nodes::ASTNode* {
        auto* arg = make<parser_types::TemplateArgument>();
        arg->form = parser_types::TemplateArgument::Form::Value;
        arg->value = e;
        return make<nodes::TypeQueryExpression>(nodes::TypeQueryExpression::Op::Countof, arg, at);
    };
    auto typed_call = [&](std::initializer_list<std::string_view> path, const parser_types::TypeInfo& type, nodes::ASTNode* arg) -> nodes::ASTNode* {
        auto* targ = make<parser_types::TemplateArgument>();
        targ->form = parser_types::TemplateArgument::Form::Type;
        targ->type = type.clone_into(arena);
        auto* call = static_cast<nodes::CallExpression*>(make_library_call(path, { arg }, at));
        call->set_template_args({ targ });
        call->has_angle_args = true;
        return call;
    };
    auto block_of = [&](std::vector<nodes::ASTNode*> statements) {
        auto* block = make<nodes::BlockStatement>();
        for (nodes::ASTNode* s : statements) block->add_statement(s);
        return block;
    };
    auto when = [&](nodes::ASTNode* condition, std::vector<nodes::ASTNode*> statements, bool constant) -> nodes::ASTNode* {
        auto* branch = make<nodes::IfBranch>(condition, block_of(std::move(statements)));
        branch->is_constexpr = constant;
        auto* test = make<nodes::IfStatement>();
        test->branches.push_back(branch);
        return test;
    };

    auto* outer = make<nodes::BlockStatement>();
    std::string_view value_name = subject_name;
    auto subject_ref = [&]() -> nodes::ASTNode* { return value_name == subject_name && !subject_name.empty() ? subject->clone_into(arena) : ident(value_name); };
    if (subject_name.empty()) {
        value_name = intern("walnut_match_value_" + std::to_string(id));
        outer->add_statement(early(make<nodes::VariableDeclaration>(auto_type(false), value_name, subject, at), true));
    }

    const std::string_view matched_name = intern("walnut_matched_" + std::to_string(id));
    if (!as_expression) {
        parser_types::TypeInfo flag_type;
        flag_type.type = parser_types::PrimitiveType::make_bool(arena);
        outer->add_statement(early(make<nodes::VariableDeclaration>(flag_type, matched_name, make<nodes::Literal>(std::string_view("false"), K::False, at), at), true));
    }

    auto finish = [&](nodes::ASTNode* body) -> std::vector<nodes::ASTNode*> {
        if (as_expression) return { make<nodes::ReturnStatement>(body, at) };
        nodes::ASTNode* set = binary(ident(matched_name), K::Equal, make<nodes::Literal>(std::string_view("true"), K::True, at));
        return { make<nodes::ExpressionStatement>(set), body };
    };

    auto element_test = [&](const MatchPattern& p, nodes::ASTNode* element, std::vector<nodes::ASTNode*>& binds) -> nodes::ASTNode* {
        switch (p.form) {
            case Form::Wildcard: return nullptr;
            case Form::Value:    return binary(element, K::LogicEqual, p.value);
            case Form::Relation: return binary(element, p.relation, p.value);
            case Form::In:       return make_in_test(element, p.value, p.negate, at);
            case Form::Bind:
                binds.push_back(early(make<nodes::VariableDeclaration>(auto_type(true), p.name, element, at), false));
                return nullptr;
            case Form::Type: {
                nodes::ASTNode* test = typed_call({ "std", "core", "detail", "holds_type" }, p.type, element);
                if (!p.name.empty()) {
                    nodes::ASTNode* view = typed_call({ "std", "core", "detail", "view_as" }, p.type, element->clone_into(arena));
                    const bool copy = p.type.has_pointer() || (p.type.type && p.type.type->kind() == parser_types::Type::Kind::Primitive && !p.type.has_reference());
                    binds.push_back(early(make<nodes::VariableDeclaration>(auto_type(!copy), p.name, view, at), false));
                }
                return test;
            }
            case Form::List: return nullptr;
        }
        return nullptr;
    };

    std::vector<nodes::ASTNode*> sequence;
    nodes::ASTNode* fallback = nullptr;

    std::size_t arm_index = 0;
    for (Arm& arm : arms) {
        ++arm_index;
        if (arm.is_default) {
            fallback = arm.body;
            continue;
        }

        std::vector<nodes::ASTNode*> fixed;
        std::vector<nodes::ASTNode*> setup;
        std::vector<nodes::ASTNode*> binds;
        nodes::ASTNode* test = nullptr;

        if (arm.patterns.size() > 1) {
            for (const MatchPattern& p : arm.patterns) {
                std::vector<nodes::ASTNode*> none;
                nodes::ASTNode* one = p.form == Form::Wildcard ? make<nodes::Literal>(std::string_view("true"), K::True, at) : element_test(p, subject_ref(), none);
                test = either(test, one);
            }
        } else {
            const MatchPattern& p = arm.patterns[0];
            if (p.form != Form::List) {
                test = element_test(p, subject_ref(), binds);
            } else if (pack) {
                const std::size_t n = p.items.size();
                nodes::ASTNode* count = binary(countof_of(subject_ref()), p.has_rest ? K::GreaterEqual : K::LogicEqual, number(n));
                if (kind == Subject::TemplatePack) fixed.push_back(count);
                else test = count;
                for (std::size_t k = 0; k < n; ++k) {
                    const MatchPattern& item = p.items[k];
                    nodes::ASTNode* element = make<nodes::SubscriptExpression>(subject_ref(), number(k), at);
                    if (kind == Subject::TemplatePack && item.form == Form::Type) {
                        parser_types::TypeInfo elem_type;
                        elem_type.type = make<parser_types::ComputedType>(parser_types::TypeOp::Typeof, element->clone_into(arena), std::vector<parser_types::TypeInfo>{});
                        auto* want = make<parser_types::TemplateArgument>();
                        want->form = parser_types::TemplateArgument::Form::Type;
                        want->type = item.type.clone_into(arena);
                        auto* have = make<parser_types::TemplateArgument>();
                        have->form = parser_types::TemplateArgument::Form::Type;
                        have->type = elem_type;
                        auto* name = make<nodes::QualifiedIdentifier>(at, false);
                        for (std::string_view part : { "std", "core", "detail", "same_type" }) name->add_part(part);
                        fixed.push_back(make<nodes::TemplateInstantiation>(name, std::vector<parser_types::TemplateArgument*>{ want, have }, at));
                        if (!item.name.empty()) binds.push_back(early(make<nodes::VariableDeclaration>(auto_type(true), item.name, element, at), false));
                        continue;
                    }
                    test = both(test, element_test(item, element, binds));
                }
                if (kind == Subject::RuntimePack && !p.rest_name.empty()) {
                    nodes::ASTNode* tail = make_library_call({ "std", "core", "detail", "pack_tail" }, { subject_ref(), number(n) }, at);
                    binds.push_back(early(make<nodes::VariableDeclaration>(auto_type(false), p.rest_name, tail, at), false));
                }
            } else {
                SmallVector<std::string_view, 4> names;
                for (std::size_t k = 0; k < p.items.size(); ++k) {
                    const MatchPattern& item = p.items[k];
                    if (item.form == Form::Bind) {
                        names.push_back(item.name);
                        continue;
                    }
                    const std::string_view part = intern("walnut_match_part_" + std::to_string(id) + "_" + std::to_string(arm_index) + "_" + std::to_string(k));
                    names.push_back(part);
                    test = both(test, element_test(item, ident(part), binds));
                }
                if (p.has_rest) {
                    throw ParserError::invalid_expression(reporter, arm.start, "'...' in a list pattern only works when the match is on a variadic parameter; to take apart a struct or tuple, give a pattern for each of its parts");
                }
                if (names.empty()) {
                    throw ParserError::invalid_expression(reporter, arm.start, "'()' only matches a variadic parameter that got no arguments");
                }
                auto* split = make<nodes::VariableDeclaration>(auto_type(true), names, subject_ref(), at);
                setup.push_back(early(split, true));
            }
        }

        std::vector<nodes::ASTNode*> inner = finish(arm.body);
        if (arm.guard) inner = { when(arm.guard, std::move(inner), false) };
        std::vector<nodes::ASTNode*> staged = binds;
        staged.insert(staged.end(), inner.begin(), inner.end());
        if (test) staged = { when(test, std::move(staged), false) };
        std::vector<nodes::ASTNode*> prepared = setup;
        prepared.insert(prepared.end(), staged.begin(), staged.end());
        for (std::size_t i = fixed.size(); i-- > 0; ) prepared = { when(fixed[i], std::move(prepared), true) };
        if (!as_expression) {
            auto* unmatched = make<nodes::UnaryExpression>(ident(matched_name), K::ExclamationMark, true, at);
            prepared = { when(unmatched, std::move(prepared), false) };
        }
        sequence.push_back(block_of(std::move(prepared)));
    }

    for (nodes::ASTNode* s : sequence) outer->add_statement(s);

    if (as_expression) {
        if (fallback) {
            outer->add_statement(make<nodes::ReturnStatement>(fallback, at));
        } else {
            nodes::ASTNode* error = make_library_call({ "std", "errors", "match_error" }, { make<nodes::Literal>(std::string_view("no 'case' in the match fit the value, and it has no 'default'"), K::String, at) }, at);
            outer->add_statement(make<nodes::ExpressionStatement>(make<nodes::ThrowExpression>(error, at)));
        }
        auto* captures = make<nodes::LambdaCaptureList>();
        captures->m_captures.push_back(nodes::LambdaCaptureItem(nodes::LambdaCaptureItem::Mode::AllByReference));
        auto* lambda = make<nodes::LambdaExpression>(captures, make<nodes::FunctionParameters>(), auto_type(false), modifiers::FunctionQualifiers{}, outer, at);
        return make<nodes::CallExpression>(lambda, at);
    }

    if (fallback) {
        auto* unmatched = make<nodes::UnaryExpression>(ident(matched_name), K::ExclamationMark, true, at);
        outer->add_statement(when(unmatched, { fallback }, false));
    }
    return outer;
}

} // namespace parsing
} // namespace walnut
