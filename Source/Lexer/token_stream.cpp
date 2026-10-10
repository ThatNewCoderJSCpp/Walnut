#include "Lexer/token_stream.hpp"

namespace walnut {
namespace tokenizing {

TokenStream::TokenStream(TokenStream&& o) noexcept : m_tokens(o.m_tokens), m_size(o.m_size), m_capacity(o.m_capacity), m_cursor(o.m_cursor), m_owns(o.m_owns) {
    o.m_tokens = nullptr;
    o.m_size = o.m_capacity = o.m_cursor = 0;
    o.m_owns = true;
}

TokenStream& TokenStream::operator=(TokenStream&& o) noexcept {
    if (this != &o) {
        if (m_owns) std::free(m_tokens);
        m_tokens = o.m_tokens; m_size = o.m_size;
        m_capacity = o.m_capacity; m_cursor = o.m_cursor;
        m_owns = o.m_owns;
        o.m_tokens = nullptr;
        o.m_size = o.m_capacity = o.m_cursor = 0;
        o.m_owns = true;
    }
        
    return *this;
}

void TokenStream::lex_all(const char* source, ErrorReporter& reporter) {
    std::size_t source_len = std::strlen(source);
    reserve(source_len / 5 + 64);
    Lexer lexer(source, reporter);
    Token tok = lexer.next();

    while (tok.kind() != Token::Kind::End) {
        if (tok.kind() != Token::Kind::Comment && tok.kind() != Token::Kind::LongComment) { push(tok); }
        tok = lexer.next();
    }

    push(tok);
}

void TokenStream::reserve(std::size_t cap) {
    if (cap <= m_capacity) return;
    assert(m_owns && "cannot grow a non-owning TokenStream (buffer is arena-owned)");
    std::size_t new_cap = m_capacity ? m_capacity : 64;
    while (new_cap < cap) new_cap *= 2;
    Token* new_buf = static_cast<Token*>(std::malloc(new_cap * sizeof(Token)));
    if (!new_buf) throw std::bad_alloc();
    if (m_size > 0) std::memcpy(new_buf, m_tokens, m_size * sizeof(Token));
    std::free(m_tokens);
    m_tokens = new_buf;
    m_capacity = new_cap;
}

} // namespace tokenizing
} // namespace walnut
