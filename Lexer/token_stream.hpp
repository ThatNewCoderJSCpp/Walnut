#ifndef TOKEN_STREAM_HPP
#define TOKEN_STREAM_HPP

#include "token_macro.hpp"
#include "lexer.hpp"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

namespace walnut {
namespace tokenizing {

class TokenStream {
    Token* m_tokens = nullptr;
    std::size_t m_size = 0;
    std::size_t m_capacity = 0;
    std::size_t m_cursor = 0;
    bool m_owns = true;          

public:
    TokenStream() = default;
    explicit TokenStream(const char* source, ErrorReporter& reporter) { lex_all(source, reporter); }
    TokenStream(Token* data, std::size_t size) noexcept : m_tokens(data), m_size(size), m_capacity(size), m_owns(false) {}
    explicit TokenStream(std::vector<Token>& toks) noexcept : TokenStream(toks.data(), toks.size()) {}
    ~TokenStream() { if (m_owns) std::free(m_tokens); }
    TokenStream(const TokenStream&) = delete;
    TokenStream& operator=(const TokenStream&) = delete;

    TokenStream(TokenStream&& o) noexcept : m_tokens(o.m_tokens), m_size(o.m_size), m_capacity(o.m_capacity), m_cursor(o.m_cursor), m_owns(o.m_owns) {
        o.m_tokens = nullptr;
        o.m_size = o.m_capacity = o.m_cursor = 0;
        o.m_owns = true;
    }

    TokenStream& operator=(TokenStream&& o) noexcept {
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

    void lex_all(const char* source, ErrorReporter& reporter) {
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

    const Token& current() const noexcept { return m_tokens[m_cursor]; }
    void advance() noexcept { if (m_cursor < m_size - 1) ++m_cursor; }

    const Token& lookahead(std::size_t n) const noexcept {
        std::size_t idx = m_cursor + n;
        return idx < m_size ? m_tokens[idx] : m_tokens[m_size - 1];
    }

    const Token& peek() const noexcept { return lookahead(1); }
    std::size_t size()     const noexcept { return m_size; }
    std::size_t position() const noexcept { return m_cursor; }
    bool at_end()          const noexcept { return m_tokens[m_cursor].kind() == Token::Kind::End; }
    const Token* data()  const noexcept { return m_tokens; }
    const Token* begin() const noexcept { return m_tokens; }
    const Token* end()   const noexcept { return m_tokens + m_size; }
    void seek(std::size_t pos) noexcept { m_cursor = (pos < m_size) ? pos : m_size - 1; }

private:
    void reserve(std::size_t cap) {
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

    void push(const Token& tok) {
        if (m_size == m_capacity) reserve(m_capacity * 2);
        m_tokens[m_size++] = tok;
    }
};

} // namespace tokenizing
} // namespace walnut

#endif // TOKEN_STREAM_HPP