#include "Parser/Parsing/core.hpp"

namespace walnut {
namespace parsing {

void Parser::expect(tokenizing::Token::Kind kind) {
    if (current_token().kind() != kind) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {kind},
            ""
        );
    }
    advance();
}

void Parser::expect(tokenizing::Token::Kind kind, const std::string& msg) {
    if (msg.empty()) { expect(kind); return; }

    if (current_token().kind() != kind) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {kind},
            msg
        );
    }
    advance();
}

void Parser::check_nesting_depth(const tokenizing::Token& token, const std::string& construct_type) const { if (nesting_depth > ParserError::MAX_NESTING_DEPTH) { throw ParserError::max_nesting_exceeded(reporter, token, construct_type); }}

void Parser::consume_semicolons() { while (current_token().is_one_of(tokenizing::Token::Kind::Semicolon, tokenizing::Token::Kind::DoubleSemicolon)) { advance(); } }

} // namespace parsing
} // namespace walnut

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_import_at(std::size_t pos) {
    tokens.seek(pos);
    m_cur_tok = &tokens.current();
    return parse_import_export(nodes::ImportExportDeclaration::Direction::Import);
}

void Parser::prescan_names(const tokenizing::Token* toks, std::size_t n, ParseNames& out) {
    using K = tokenizing::Token::Kind;
    auto kind = [&](std::size_t i) { return i < n ? toks[i].kind() : K::End; };

    for (std::size_t i = 0; i < n; ++i) {
        if (kind(i) != K::TemplateKeyword || kind(i + 1) != K::LessThan) continue;
        std::size_t j = i + 2;
        int depth = 1;

        while (j < n && depth > 0) {
            const K k = kind(j);
            if (k == K::LessThan) ++depth;
            else if (k == K::GreaterThan) --depth;
            else if (k == K::DoubleGreaterThan) depth -= 2;
            else if (k == K::End) break;
            ++j;
        }

        if (kind(j) == K::RequiresKeyword) {
            int nest = 0;
            for (++j; j < n; ++j) {
                const K k = kind(j);
                if (k == K::LeftParen || k == K::LeftCurly || k == K::LeftSquare) ++nest;
                else if (k == K::RightParen || k == K::RightCurly || k == K::RightSquare) --nest;
                else if (nest == 0 && (k == K::FunctionKeyword || k == K::ClassKeyword || k == K::StructKeyword || k == K::UnionKeyword
                    || k == K::ConceptKeyword || k == K::UsingKeyword || k == K::ConstexprKeyword || k == K::ConstantDeclaration
                    || k == K::StaticKeyword || k == K::OperatorKeyword || k == K::ConstructorKeyword || k == K::Semicolon)) break;
            }
        }

        while (kind(j) == K::StaticKeyword || kind(j) == K::InlineKeyword || kind(j) == K::HiddenKeyword || kind(j) == K::LocalKeyword
            || kind(j) == K::GlobalKeyword || kind(j) == K::FriendKeyword || kind(j) == K::ExportKeyword) ++j;

        const K head = kind(j);
        auto name_at = [&](std::size_t p) -> std::string_view { return kind(p) == K::Identifier ? toks[p].lexeme() : std::string_view{}; };

        if (head == K::FunctionKeyword || head == K::ClassKeyword || head == K::StructKeyword || head == K::UnionKeyword) {
            std::size_t p = j + 1;
            while (kind(p) == K::Identifier && kind(p + 1) == K::DoubleColon) p += 2;
            if (std::string_view nm = name_at(p); !nm.empty()) out.templates.insert(nm);
            continue;
        }

        if (head == K::ConceptKeyword) {
            if (std::string_view nm = name_at(j + 1); !nm.empty()) { out.templates.insert(nm); out.concepts.insert(nm); }
            continue;
        }

        if (head == K::UsingKeyword) {
            if (std::string_view nm = name_at(j + 1); !nm.empty() && kind(j + 2) == K::Equal) out.templates.insert(nm);
            continue;
        }

        if (head == K::ConstexprKeyword || head == K::ConstantDeclaration) {
            int nest = 0;
            for (std::size_t p = j; p < n; ++p) {
                const K k = kind(p);
                if (k == K::LessThan || k == K::LeftParen) ++nest;
                else if (k == K::GreaterThan || k == K::RightParen) --nest;
                else if (k == K::DoubleGreaterThan) nest -= 2;
                else if (k == K::Semicolon || k == K::Equal || k == K::End) break;
                else if (nest <= 0 && k == K::Colon) {
                    if (std::string_view nm = name_at(p + 1); !nm.empty()) { out.templates.insert(nm); out.variable_templates.insert(nm); }
                    break;
                }
            }
        }
    }
}

void Parser::advance() {
    if (m_cur_tok) ParserError::note_previous(*m_cur_tok);
    tokens.advance();
    m_cur_tok = &tokens.current();
}

bool Parser::loose_keyword(tokenizing::Token::Kind k) { return ParserError::keyword_names_function(k); }

bool Parser::take_keyword_as_name(bool need_call) {
    if (!loose_keyword(current_token().kind())) return false;
    if (need_call && lookahead(1).kind() != tokenizing::Token::Kind::LeftParen) return false;
    const_cast<tokenizing::Token*>(m_cur_tok)->kind(tokenizing::Token::Kind::Identifier);
    return true;
}

nodes::ASTNode* Parser::make_self(std::size_t line) {
    auto* self_this = make<nodes::Literal>(std::string_view("this"), tokenizing::Token::Kind::ThisKeyword, line);
    advance();
    return make<nodes::DereferenceExpression>(self_this, static_cast<std::uint32_t>(line));
}

void Parser::synchronize() { while (!current_token().is(tokenizing::Token::Kind::End)) { consume_semicolons(); }}

bool Parser::match(tokenizing::Token::Kind kind) {
    if (concrete_match(kind)) {
        advance();
        return true;
    }
    return false;
}

const tokenizing::Token& Parser::lookahead(std::size_t n) { return tokens.lookahead(n); }

const tokenizing::Token& Parser::peek() { return tokens.peek(); }

bool Parser::concrete_match(tokenizing::Token::Kind kind) const noexcept { return current_token().kind() == kind; }

} // namespace parsing
} // namespace walnut
