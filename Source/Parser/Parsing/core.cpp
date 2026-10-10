#include "Parser/Parsing/core.hpp"

namespace walnut {
namespace parsing {

void Parser::expect(tokenizing::Token::Kind kind) {
    if (current_token().kind() != kind) {
        throw ParserError::unexpected_token(
            reporter,
            current_token(),
            {kind},
            "While parsing expression"
        );
    }
    advance();
}

void Parser::expect(tokenizing::Token::Kind kind, const std::string& msg) {
    if (msg.empty() || msg == "") { expect(kind); }
    
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

void Parser::advance() {
    tokens.advance();
    m_cur_tok = &tokens.current();
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
