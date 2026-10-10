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

    TokenStream(TokenStream&& o) noexcept;

    TokenStream& operator=(TokenStream&& o) noexcept;

    void lex_all(const char* source, ErrorReporter& reporter);

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
    void reserve(std::size_t cap);

    void push(const Token& tok) {
        if (m_size == m_capacity) reserve(m_capacity * 2);
        m_tokens[m_size++] = tok;
    }
};

} // namespace tokenizing
} // namespace walnut

#endif // TOKEN_STREAM_HPP