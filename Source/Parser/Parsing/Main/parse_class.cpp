#include "Parser/Parsing/Main/parse_class.hpp"

namespace walnut {
namespace parsing {

bool Parser::parse_owner_name(std::vector<std::string_view>& parts, bool& global) {
    using K = tokenizing::Token::Kind;
    global = false;
    if (!(current_token().kind() == K::Identifier || (current_token().kind() == K::DoubleColon && lookahead(1).kind() == K::Identifier))) return false;
    if (match(K::DoubleColon)) global = true;

    while (true) {
        parts.push_back(current_token().lexeme());
        advance();
        if (current_token().kind() != K::DoubleColon || lookahead(1).kind() != K::Identifier) break;
        advance();
    }

    return true;
}

void Parser::check_native_member(nodes::ASTNode* member, const tokenizing::Token& at) {
    using NK = nodes::ASTNode::Kind;
    using FQ = modifiers::FunctionQualifiers;
    if (!member) return;

    if (member->kind == NK::ConstructorDeclaration) {
        auto* c = static_cast<nodes::ConstructorDeclaration*>(member);
        if (c->has_init_list() || !c->owner_parts.empty() || c->m_special == nodes::ConstructorDeclaration::Special::Default) {
            throw ParserError::invalid_declaration(reporter, at, "A constructor of a C++ class in an extern block passes its arguments to the C++ constructor, so it can't have an initializer list or '= default'");
        }
        return;
    }

    if (member->kind == NK::FunctionDeclaration) {
        auto* f = static_cast<nodes::FunctionDeclaration*>(member);
        if (f->has_body() && !f->native_name.empty()) throw ParserError::invalid_declaration(reporter, at, "A method with 'as' calls C++ code, so it can't have a body too");
        if (f->qualifiers().has_any(FQ::Virtual | FQ::Override | FQ::Abstract)) throw ParserError::invalid_declaration(reporter, at, "A method of a C++ class in an extern block can't be virtual, override or abstract");
        if (f->get_modifiers().has(modifiers::RawModifiers::Friend)) throw ParserError::invalid_declaration(reporter, at, "A C++ class in an extern block can't declare friends");
        return;
    }

    if (member->kind == NK::OperatorFunctionDeclaration) {
        auto* o = static_cast<nodes::OperatorFunctionDeclaration*>(member);
        if (o->has_body() && !o->native_name.empty()) throw ParserError::invalid_declaration(reporter, at, "An operator with 'as' calls C++ code, so it can't have a body too");
        if (o->qualifiers().has_any(FQ::Virtual | FQ::Override | FQ::Abstract)) throw ParserError::invalid_declaration(reporter, at, "An operator of a C++ class in an extern block can't be virtual, override or abstract");
        if (o->get_modifiers().has(modifiers::RawModifiers::Friend)) throw ParserError::invalid_declaration(reporter, at, "A C++ class in an extern block can't declare friends");
        return;
    }

    throw ParserError::invalid_declaration(reporter, at, "A C++ class in an extern block can only declare constructors, methods and operators");
}

nodes::ASTNode* Parser::parse_constructor_declaration(const modifiers::RawModifiers& mods, const std::size_t line_number) {
    const char* keyword_at = current_token().data();
    nodes::ASTNode* made = parse_constructor_declaration_body(mods, line_number);
    if (made && !made->at) made->at = keyword_at;
    return made;
}

nodes::ASTNode* Parser::parse_constructor_declaration_body(const modifiers::RawModifiers& mods, const std::size_t line_number) {
    expect(tokenizing::Token::Kind::ConstructorKeyword, "Expected 'constructor'");
    std::vector<std::string_view> owner;
    bool owner_global = false;
    parse_owner_name(owner, owner_global);
    nodes::FunctionParameters* params = parse_function_parameters();
    const modifiers::FunctionQualifiers quals = parse_function_qualifiers();
    using Special = nodes::ConstructorDeclaration::Special;

    if (quals.has(modifiers::FunctionQualifiers::Abstract)) {
        throw ParserError::invalid_declaration(reporter, current_token(), "A constructor can't be 'abstract'");
    }

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

    if (m_native_record_depth > 0 && match(tokenizing::Token::Kind::AsKeyword)) {
        const std::string_view native_as = parse_native_name("Expected the name of a C++ function in quotes after 'as'; its result becomes the new object");

        if (!concrete_match(tokenizing::Token::Kind::Semicolon) && !concrete_match(tokenizing::Token::Kind::DoubleSemicolon)) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::Semicolon, "A constructor with 'as' calls C++ code, so it ends with ';'");
        }

        consume_semicolons();
        auto* ctor = make<nodes::ConstructorDeclaration>(params, std::move(init_list), mods, quals, nullptr, line_number);
        ctor->native_name = native_as;
        return ctor;
    }

    if (match(tokenizing::Token::Kind::Colon)) {
        if (concrete_match(tokenizing::Token::Kind::LeftCurly)) {
            throw ParserError::invalid_expression(
                reporter,
                current_token(),
                "Expected at least one expression in constructor initializer list"
            );
        }

        const auto member_init = [this]() -> nodes::ASTNode* {
            if (current_token().kind() == tokenizing::Token::Kind::Identifier && lookahead(1).kind() == tokenizing::Token::Kind::LeftCurly) {
                const std::size_t brace_line = current_token().line();
                nodes::ASTNode* name = make<nodes::Identifier>(current_token().lexeme(), static_cast<std::uint32_t>(brace_line));
                advance();
                nodes::BraceInitializerList* init = parse_brace_initializer_list();
                return make<nodes::BraceConstructExpression>(name, init, static_cast<std::uint32_t>(brace_line));
            }

            nodes::ASTNode* e = nullptr;
            {
                NoBraceConstructGuard no_brace(this);
                e = parse_expression();
            }

            if (e && e->kind == nodes::ASTNode::Kind::BinaryExpression) {
                auto* b = static_cast<nodes::BinaryExpression*>(e);

                if (b->op == tokenizing::Token::Kind::Equal && b->left && b->left->kind == nodes::ASTNode::Kind::Identifier) {
                    auto* call = make<nodes::CallExpression>(b->left, static_cast<std::uint32_t>(b->line));
                    call->add_argument(b->right);
                    return call;
                }
            }

            return e;
        };

        init_list.push_back(member_init());
        while (match(tokenizing::Token::Kind::Comma)) { init_list.push_back(member_init()); }
    }

    if (owner.empty() && init_list.empty() && (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon))) {
        consume_semicolons();
        return make<nodes::ConstructorDeclaration>(params, std::move(init_list), mods, quals, nullptr, line_number);
    }

    if (!concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::LeftCurly},
            owner.empty() ? "Expected '{' to start constructor body, or ';' to define it outside the class" : "Expected '{' to start constructor body"
        );
    }

    nodes::BlockStatement* body = parse_block(false);
    auto* ctor = make<nodes::ConstructorDeclaration>(params, std::move(init_list), mods, quals, body, line_number);
    ctor->owner_parts = std::move(owner);
    ctor->owner_global = owner_global;
    return ctor;
}

nodes::ASTNode* Parser::parse_destructor_declaration(const modifiers::RawModifiers& mods, const std::size_t line_number) {
    expect(tokenizing::Token::Kind::DestructorKeyword, "Expected 'destructor'");
    std::vector<std::string_view> owner;
    bool owner_global = false;
    parse_owner_name(owner, owner_global);
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

    if (quals.has(modifiers::FunctionQualifiers::Abstract)) {
        throw ParserError::invalid_declaration(reporter, current_token(), "A destructor can't be 'abstract'; make it 'virtual' and give it a body");
    }
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

    if (owner.empty() && (concrete_match(tokenizing::Token::Kind::Semicolon) || concrete_match(tokenizing::Token::Kind::DoubleSemicolon))) {
        consume_semicolons();
        return make<nodes::DestructorDeclaration>(mods, quals, nullptr, line_number);
    }

    if (!concrete_match(tokenizing::Token::Kind::LeftCurly)) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {tokenizing::Token::Kind::LeftCurly},
            owner.empty() ? "Expected '{' to start destructor body, or ';' to define it outside the class" : "Expected '{' to start destructor body"
        );
    }

    nodes::BlockStatement* body = parse_block(false);
    auto* dtor = make<nodes::DestructorDeclaration>(mods, quals, body, line_number);
    dtor->owner_parts = std::move(owner);
    dtor->owner_global = owner_global;
    return dtor;
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

namespace {

nodes::ASTNode* friend_function_of(nodes::ASTNode* member) {
    using RM = modifiers::RawModifiers;
    if (auto* t = nodes::node_cast<nodes::TemplateDeclaration>(member)) member = t->m_declaration;
    if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(member)) return f->m_modifiers.has(RM::Friend) && f->m_body ? f : nullptr;
    if (auto* o = nodes::node_cast<nodes::OperatorFunctionDeclaration>(member)) return o->m_modifiers.has(RM::Friend) && o->m_body ? o : nullptr;
    return nullptr;
}

void strip_friend_body(nodes::ASTNode* fn) {
    if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(fn)) { f->m_body = nullptr; return; }
    if (auto* o = nodes::node_cast<nodes::OperatorFunctionDeclaration>(fn)) o->m_body = nullptr;
}

void drop_friend(nodes::ASTNode* fn) {
    using RM = modifiers::RawModifiers;
    if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(fn)) { f->m_modifiers.remove(RM::Friend); return; }
    if (auto* o = nodes::node_cast<nodes::OperatorFunctionDeclaration>(fn)) o->m_modifiers.remove(RM::Friend);
}

} // namespace

std::vector<parser_types::TemplateArgument*> Parser::injected_arguments() {
    if (m_injected_spec && !m_injected_spec->empty()) return nodes::clone_targs(*m_injected_spec, arena);
    std::vector<parser_types::TemplateArgument*> args;

    for (nodes::TemplateParameter* p : m_injected_template->m_params) {
        auto* arg = make<parser_types::TemplateArgument>();
        arg->is_pack = p->m_is_pack;

        arg->form = parser_types::TemplateArgument::Form::Type;
        arg->type.type = make<parser_types::UserDefinedType>(p->m_name);

        args.push_back(arg);
    }

    return args;
}

nodes::TemplateDeclaration* Parser::template_for_friend(nodes::TemplateDeclaration* owner, nodes::TemplateDeclaration* own, nodes::ASTNode* fn) {
    auto* t = make<nodes::TemplateDeclaration>(fn->line);

    for (nodes::TemplateParameter* p : owner->m_params) {
        auto* c = static_cast<nodes::TemplateParameter*>(p->clone_into(arena));
        c->m_has_default_type = false;
        c->m_default_type = parser_types::TypeInfo();
        c->m_default_value = nullptr;
        t->add_param(c);
    }

    nodes::ASTNode* requires_clause = owner->m_requires_clause ? owner->m_requires_clause->clone_into(arena) : nullptr;

    if (own) {
        for (nodes::TemplateParameter* p : own->m_params) t->add_param(p);
        if (own->m_requires_clause) {
            requires_clause = requires_clause ? make<nodes::BinaryExpression>(requires_clause, tokenizing::Token::Kind::LogicAnd, own->m_requires_clause, fn->line) : own->m_requires_clause;
        }
    }

    if (requires_clause) t->set_requires_clause(requires_clause);
    t->set_declaration(fn);
    if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(fn)) register_template_name(f->get_name());
    return t;
}

nodes::ASTNode* Parser::hoist_friend_definition(nodes::ASTNode* member, nodes::TemplateDeclaration* owner, nodes::RecordDeclaration* record) {
    nodes::ASTNode* fn = friend_function_of(member);
    if (!fn) return member;

    auto* own = nodes::node_cast<nodes::TemplateDeclaration>(member);
    nodes::ASTNode* in_class = member->clone_into(arena);
    strip_friend_body(friend_function_of(in_class) ? friend_function_of(in_class) : (own ? static_cast<nodes::TemplateDeclaration*>(in_class)->m_declaration : in_class));
    drop_friend(fn);
    if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(fn)) f->hoisted_friend = true;
    if (auto* o = nodes::node_cast<nodes::OperatorFunctionDeclaration>(fn)) o->hoisted_friend = true;

    if (owner && !owner->m_params.empty()) {
        nodes::TemplateDeclaration* t = template_for_friend(owner, own, fn);
        t->friend_of = record;
        t->friend_type.type = make<parser_types::UserDefinedType>(record->get_name());
        t->friend_type.template_args = injected_arguments();
        t->friend_type.has_angle_args = true;
        t->has_friend_type = true;
        m_spliced.push_back(t);
    } else if (own) {
        own->friend_of = record;
        m_spliced.push_back(own);
    } else {
        if (auto* f = nodes::node_cast<nodes::FunctionDeclaration>(fn)) f->friend_of = record;
        if (auto* o = nodes::node_cast<nodes::OperatorFunctionDeclaration>(fn)) o->friend_of = record;
        m_spliced.push_back(member);
    }

    return in_class;
}

nodes::ASTNode* Parser::reject_friend_definition(nodes::ASTNode* member) {
    if (!friend_function_of(member)) return member;
    throw ParserError::invalid_declaration(reporter, current_token(), "A friend function or operator can't be defined inside a class that is nested in a class template. Declare it here and define it after the outer class");
}

nodes::ASTNode* Parser::parse_record_declaration(const modifiers::RawModifiers& mods, std::size_t line_number) {
    const bool hoist_friends = m_hoist_friends;
    m_hoist_friends = false;
    const bool native = m_native_record_next;
    m_native_record_next = false;
    nodes::TemplateDeclaration* owner_template = m_record_template;
    m_record_template = nullptr;
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

    std::string_view native_name;

    if (native) {
        if (is_specialization) {
            throw ParserError::invalid_declaration(reporter, current_token(), "A C++ class in an extern block can't be a specialization");
        }

        if (match(tokenizing::Token::Kind::AsKeyword)) native_name = parse_native_name("Expected the C++ name of the class in quotes after 'as', as in 'class Vector as \"std::vector\"'");

        if (current_token().is_one_of(tokenizing::Token::Kind::InheritsKeyword, tokenizing::Token::Kind::FromKeyword, tokenizing::Token::Kind::ExtendsKeyword)) {
            throw ParserError::invalid_declaration(reporter, current_token(), "A C++ class in an extern block can't inherit from another class");
        }

        if (current_token().kind() != tokenizing::Token::Kind::LeftCurly) {
            throw ParserError::missing_token(reporter, current_token(), tokenizing::Token::Kind::LeftCurly, "Expected '{' and the methods of the C++ class that Walnut may call");
        }
    }

    parser_types::TypeInfo inherits; 
    std::vector<parser_types::TypeInfo> extra_bases;
    
    if (
        current_token().is_one_of(
            tokenizing::Token::Kind::InheritsKeyword,
            tokenizing::Token::Kind::FromKeyword,
            tokenizing::Token::Kind::ExtendsKeyword
        )
    ) {
        advance(); 
        inherits = parse_type_info();  
        while (match(tokenizing::Token::Kind::Comma)) extra_bases.push_back(parse_type_info());
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

    auto* decl = make<nodes::RecordDeclaration>(form, std::move(name_parts), native ? m_native_record_mods : mods, inherits, false, line_number);
    decl->m_extra_bases = std::move(extra_bases);
    decl->m_native = native;
    decl->m_native_name = native_name;

    struct NativeDepth {
        int& depth;
        bool on;
        NativeDepth(int& d, bool o) : depth(d), on(o) { if (on) ++depth; }
        ~NativeDepth() { if (on) --depth; }
    } native_depth{m_native_record_depth, native};
    decl->set_alignment(alignment);
    if (is_specialization) { decl->set_specialization(std::move(spec_args)); }
    nodes::AccessLevel access = (form == Form::Class) ? nodes::AccessLevel::Private : nodes::AccessLevel::Public;

    struct InjectedScope {
        Parser& p;
        std::string_view name;
        nodes::TemplateDeclaration* tmpl;
        const std::vector<parser_types::TemplateArgument*>* spec;
        bool active;
        ~InjectedScope() { p.m_injected_name = name; p.m_injected_template = tmpl; p.m_injected_spec = spec; p.m_injected_active = active; }
    } injected_scope{*this, m_injected_name, m_injected_template, m_injected_spec, m_injected_active};

    const bool injects = hoist_friends && owner_template;
    m_injected_active = false;
    if (injects) {
        m_injected_name     = decl->get_name();
        m_injected_template = owner_template;
        m_injected_spec     = &decl->m_spec_args;
    }

    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        if (current_token().is_one_of(
                tokenizing::Token::Kind::PublicKeyword,
                tokenizing::Token::Kind::PrivateKeyword,
                tokenizing::Token::Kind::ProtectedKeyword
            ) && !(current_token().kind() == tokenizing::Token::Kind::PublicKeyword && lookahead().kind() == tokenizing::Token::Kind::LazyKeyword)
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
            const tokenizing::Token member_start = current_token();
            const modifiers::RawModifiers mods = parse_raw_modifiers();

            if (current_token().kind() == tokenizing::Token::Kind::ConstructorKeyword) {
                nodes::ASTNode* ctor = parse_constructor_declaration(mods, line);
                if (native) check_native_member(ctor, member_start);
                decl->add_member(access, ctor);
            } else {
                if (native) throw ParserError::invalid_declaration(reporter, member_start, "A C++ class in an extern block doesn't declare a destructor; the C++ destructor runs on its own");
                decl->add_member(access, parse_destructor_declaration(mods, line));
            }
            
            continue;
        }

        if (native) {
            const tokenizing::Token member_start = current_token();
            const std::size_t line = current_token().line();
            nodes::ASTNode* member = nullptr;

            if (current_token().kind() == tokenizing::Token::Kind::TemplateKeyword) {
                m_member_template = true;
                member = parse_statement();
                m_member_template = false;
                auto* t = nodes::node_cast<nodes::TemplateDeclaration>(member);
                auto* f = t ? nodes::node_cast<nodes::FunctionDeclaration>(t->m_declaration) : nullptr;
                if (!f || !f->has_body() || !f->native_name.empty()) {
                    throw ParserError::invalid_declaration(reporter, member_start, "A C++ class in an extern block can only have template methods written in Walnut, with a body, as in 'template <typename F> function each(F: f) -> void { ... }'");
                }
                decl->add_member(access, member);
                continue;
            }

            if (looks_like_operator_function()) {
                const modifiers::RawModifiers mods = parse_raw_modifiers();
                member = parse_operator_function(mods, line);
            } else {
                const modifiers::RawModifiers mods = parse_raw_modifiers();
                if (current_token().kind() != tokenizing::Token::Kind::FunctionKeyword) {
                    throw ParserError::invalid_declaration(reporter, member_start, "A C++ class in an extern block can only declare constructors, methods and operators; its fields stay in C++");
                }
                member = parse_function_declaration(mods, line);
            }

            check_native_member(member, member_start);
            decl->add_member(access, member);
            continue;
        }

        if (looks_like_operator_function()) {
            const std::size_t line = current_token().line();
            const modifiers::RawModifiers mods = parse_raw_modifiers();
            m_injected_active = injects && mods.has(modifiers::RawModifiers::Friend);
            nodes::ASTNode* op = parse_operator_function(mods, line);
            m_injected_active = false;
            decl->add_member(access, hoist_friends ? hoist_friend_definition(op, owner_template, decl) : reject_friend_definition(op));
            continue;
        }

        m_injected_candidate = injects;
        m_nested_hoist = hoist_friends && !owner_template;
        const std::size_t mark = m_spliced.size();
        m_member_template = current_token().kind() == tokenizing::Token::Kind::TemplateKeyword;
        nodes::ASTNode* member = parse_statement();
        m_member_template = false;
        m_injected_candidate = false;
        m_nested_hoist = false;
        decl->add_member(access, hoist_friends ? hoist_friend_definition(member, owner_template, decl) : reject_friend_definition(member));

        for (std::size_t i = mark; i < m_spliced.size(); ) {
            if (m_spliced[i] && m_spliced[i]->kind == nodes::ASTNode::Kind::VariableDeclaration) {
                decl->add_member(access, m_spliced[i]);
                m_spliced.erase(m_spliced.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
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
