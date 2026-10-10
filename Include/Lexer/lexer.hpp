#ifndef LEXER_HPP
#define LEXER_HPP

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <vector>
#include "token_macro.hpp"
#include "keywords.hpp"
#include "escapes.hpp"
#include "../Common/error_reporter.hpp"
#include "../Common/profiler.hpp"

namespace walnut {
namespace tokenizing {

class Lexer {
public:
    Lexer(const char* beg, ErrorReporter& reporter) noexcept : m_beg{beg}, m_reporter{reporter} {}
    Token next() noexcept;

private:
    Token identifier() noexcept;
    Token number() noexcept;
    Token slash_or_comment() noexcept;
    Token atom(Token::Kind) noexcept;
    Token string_literal() noexcept;
    void report_error(const std::string& message) noexcept;
    char peek() const noexcept { return *m_beg; }

    template <bool CheckNewline>
    char get() noexcept;

private:
    const char* m_beg = nullptr;
    ErrorReporter& m_reporter;
    std::uint32_t m_line_number = 1;
    std::size_t m_error_count = 0;
};

constexpr bool is_space(const char c) noexcept {
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            return true;
        default:
            return false;
    }
}

constexpr bool is_digit(const char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_identifier_char(const char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

template <bool CheckNewline>
char Lexer::get() noexcept { 
    char c = *m_beg++;
    if constexpr (CheckNewline) { if (c == '\n') { ++m_line_number; }}
    return c;
}

void display_all_tokens(const char* code, const std::string& output_file);

} // namespace tokenizing
} // namespace walnut

#endif // LEXER_HPP