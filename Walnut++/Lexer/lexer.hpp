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

void Lexer::report_error(const std::string& message) noexcept {
    ++m_error_count;
    m_reporter.report(ErrorPhase::Lexer, m_line_number, message);
}

Token Lexer::string_literal() noexcept {
    const char* start = m_beg;
    char quote_type = peek(); 

    if (quote_type == '"' || quote_type == '`' || quote_type == '\'') {
        get<true>();  

        while (peek() != quote_type) {
            if (peek() == '\0') {
                report_error("Unterminated string literal: missing closing quote");
                return Token(Token::Kind::Unexpected, start, m_beg - start, m_line_number);  
            }

            if (peek() == '\\') {
                get<true>();  
                if (peek() != '\0') {
                    get<true>(); 
                } else {
                    report_error("Invalid escape sequence: string ends after escape character");
                    return Token(Token::Kind::Unexpected, start, m_beg - start, m_line_number);  
                }
            } else {
                get<true>();  
            }
        }

        get<true>();  

        switch (quote_type) {
            case '"':
                return Token(Token::Kind::String, start + 1, m_beg - start - 2, m_line_number);
            case '`':
                return Token(Token::Kind::TextLiteral, start + 1, m_beg - start - 2, m_line_number);
            case '\'': {
                const std::size_t interior = std::size_t(m_beg - start - 2);

                if (interior == 0) {
                    report_error("Empty character literal");
                    return Token(Token::Kind::Unexpected, start, m_beg - start, m_line_number);
                }
                
                return Token(Token::Kind::Character, start + 1, interior, m_line_number);
            }
            default:
                return Token(Token::Kind::Unexpected, start, m_beg - start, m_line_number);  
        }
    }

    report_error("Missing opening quote");
    return Token(Token::Kind::Unexpected, start, 1, m_line_number);
}

Token Lexer::atom(Token::Kind kind) noexcept { return Token(kind, m_beg++, 1, m_line_number); }

Token Lexer::next() noexcept {
    while (is_space(peek())) { get<true>(); }

    switch (peek()) {
        case '\0': return Token(Token::Kind::End, m_beg, 1, m_line_number);
        case '(': return atom(Token::Kind::LeftParen);
        case ')': return atom(Token::Kind::RightParen);
        case '[': return atom(Token::Kind::LeftSquare);
        case ']': return atom(Token::Kind::RightSquare);
        case '{': return atom(Token::Kind::LeftCurly);
        case '}': return atom(Token::Kind::RightCurly);
        case '<':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::LessEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '-') {
                get<false>();
                return Token(Token::Kind::SingleLeftArrow, m_beg - 2, 2, m_line_number);
            } else if (peek() == '<') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::ShiftLeftEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::DoubleLessThan, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::LessThan, m_beg - 1, 1, m_line_number);
            }
        case '>':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::GreaterEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '>') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::ShiftRightEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::DoubleGreaterThan, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::GreaterThan, m_beg - 1, 1, m_line_number);
            }
        case '=':
            get<false>();
            if (peek() == '=') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::LogicEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::LogicEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '>') {
                get<false>();
                return Token(Token::Kind::DoubleRightArrow, m_beg - 2, 2, m_line_number);
            } else if (peek() == '+') {
                get<false>();
                if (peek() == '+') {
                    get<false>();
                    return Token(Token::Kind::DoublePlusEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::PlusEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '-') {
                get<false>();
                if (peek() == '-') {
                    get<false>();
                    return Token(Token::Kind::DoubleMinusEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::MinusEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Equal, m_beg - 1, 1, m_line_number);
            }
        case '+':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::PlusEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '+') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::DoublePlusEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::DoublePlus, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Plus, m_beg - 1, 1, m_line_number);
            }
        case '-':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::MinusEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '-') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::DoubleMinusEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::DoubleMinus, m_beg - 2, 2, m_line_number);
            } else if (peek() == '>') {
                get<false>();
                return Token(Token::Kind::SingleRightArrow, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Minus, m_beg - 1, 1, m_line_number);
            }
        case '*':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::AsteriskEqual, m_beg - 2, 2, m_line_number);
            } else if (peek() == '*') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::DoubleAsteriskEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::DoubleAsterisk, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Asterisk, m_beg - 1, 1, m_line_number);
            }
        case '/': return slash_or_comment();
        case '#': return atom(Token::Kind::Hash);
        case '.':
            get<false>();
            if (peek() == '.') {
                get<false>();

                if (peek() == '.') {
                    get<false>();
                    return Token(Token::Kind::Ellipsis, m_beg - 3, 3, m_line_number);
                }

                report_error("Unexpected sequence '..'");
                return Token(Token::Kind::Unexpected, m_line_number);
            }

            return Token(Token::Kind::Dot, m_beg - 1, 1, m_line_number);   
        case ',': return atom(Token::Kind::Comma);
        case ':': 
            get<false>();
            if (peek() == ':') {
                get<false>();
                return Token(Token::Kind::DoubleColon, m_beg - 2, 2, m_line_number);
            } else if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::ColonEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Colon, m_beg - 1, 1, m_line_number);
            }
        case ';': 
            get<false>();
            if (peek() == ';') {
                get<false>();
                return Token(Token::Kind::DoubleSemicolon, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Semicolon, m_beg - 1, 1, m_line_number);
            }
        case '\'': 
        case '"': 
        case '`': 
            return string_literal();
        case '|':
            get<false>();
            if (peek() == '|') {
                get<false>();
                return Token(Token::Kind::LogicOr, m_beg - 2, 2, m_line_number);
            } else if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::PipeEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Pipe, m_beg - 1, 1, m_line_number);
            }
        case '~': return atom(Token::Kind::Tilde);
        case '!':
            get<false>();
            if (peek() == '=') {
                get<false>();
                if (peek() == '=') {
                    get<false>();
                    return Token(Token::Kind::NotEqual, m_beg - 3, 3, m_line_number);
                }
                return Token(Token::Kind::NotEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::ExclamationMark, m_beg - 1, 1, m_line_number);
            }
        case '@': return atom(Token::Kind::AtSymbol);
        case '%':
            get<false>();
            if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::PercentEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Percent, m_beg - 1, 1, m_line_number);
            }
        case '\\': return atom(Token::Kind::BackSlash);
        case '^':
            get<false>();
            if (peek() == '=') { 
                get<false>(); 
                return Token(Token::Kind::CaretEqual, m_beg - 2, 2, m_line_number); 
            } else { 
                return Token(Token::Kind::Caret, m_beg - 1, 1, m_line_number); 
            }  
        case '?': return atom(Token::Kind::QuestionMark);
        case '$': return atom(Token::Kind::MoneySymbol);
        // case '_': return atom(Token::Kind::Underscore);
        case '&':
            get<false>();
            if (peek() == '&') {
                get<false>();
                return Token(Token::Kind::LogicAnd, m_beg - 2, 2, m_line_number);
            } else if (peek() == '=') {
                get<false>();
                return Token(Token::Kind::AmpersandEqual, m_beg - 2, 2, m_line_number);
            } else {
                return Token(Token::Kind::Ampersand, m_beg - 1, 1, m_line_number);
            }
        default:
            if (is_digit(peek())) {
                return number();
            } else if (is_identifier_char(peek())) {
                return identifier();
            } else {
                std::string unexpected(1, peek());
                report_error("Unexpected character: " + unexpected);
                return Token(Token::Kind::Unexpected, m_beg, 1, m_line_number);
            }
            break;
    }
}

Token Lexer::identifier() noexcept {
    const char* start = m_beg;
    get<false>();
    while (is_identifier_char(peek())) { get<false>(); }
    std::string_view lexeme(start, m_beg - start);
    Token::Kind kind = get_keyword(lexeme);
    if (kind != Token::Kind::Unexpected) { return Token(kind, start, m_beg, m_line_number); }
    return Token(Token::Kind::Identifier, start, m_beg, m_line_number); 
}

Token Lexer::number() noexcept {
    const char* start = m_beg;

    if (m_beg[0] == '0' && (m_beg[1] == 'x' || m_beg[1] == 'X')) {
        const char h = m_beg[2];
        if ((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')) {
            get<false>(); get<false>();
            while ((peek() >= '0' && peek() <= '9') || (peek() >= 'a' && peek() <= 'f') || (peek() >= 'A' && peek() <= 'F')) { get<false>(); }
            return Token(Token::Kind::Integer, start, m_beg, m_line_number);
        }
    }

    if (m_beg[0] == '0' && (m_beg[1] == 'b' || m_beg[1] == 'B') && (m_beg[2] == '0' || m_beg[2] == '1')) {
        get<false>(); get<false>();
        while (peek() == '0' || peek() == '1') { get<false>(); }
        return Token(Token::Kind::Integer, start, m_beg, m_line_number);
    }

    get<false>(); 
    bool is_float = false; 
    while (is_digit(peek())) { get<false>(); } 

    if (peek() == '.') {
        is_float = true; 
        get<false>(); 
        while (is_digit(peek())) { get<false>(); }
    }

    if ((peek() == 'e' || peek() == 'E') && (is_digit(m_beg[1]) || ((m_beg[1] == '+' || m_beg[1] == '-') && is_digit(m_beg[2])))) {
        is_float = true;
        get<false>();
        if (peek() == '+' || peek() == '-') get<false>();
        while (is_digit(peek())) { get<false>(); }
    }

    if (is_float) { return Token(Token::Kind::Float, start, m_beg, m_line_number); }
    return Token(Token::Kind::Integer, start, m_beg, m_line_number);
}

Token Lexer::slash_or_comment() noexcept {
    get<false>();                          

    if (peek() == '/') {
        get<false>();
        const char* start = m_beg;
        while (peek() != '\0' && peek() != '\n') { get<false>(); }
        return Token(Token::Kind::Comment, start, std::size_t(m_beg - start), m_line_number);
    }

    if (peek() == '*') {
        get<false>();
        const char* start = m_beg;
        const std::uint32_t open_line = m_line_number;   
        std::size_t depth = 1;

        while (peek() != '\0') {
            const char c = get<true>();                  

            if (c == '*' && peek() == '/') {
                get<false>();
                if (--depth == 0) {
                    return Token(Token::Kind::LongComment, start, std::size_t(m_beg - start) - 2, m_line_number);
                }
            } else if (c == '/' && peek() == '*') {
                get<false>();
                ++depth;
            }
        }

        m_line_number = open_line;
        report_error("Unterminated block comment");
        return Token(Token::Kind::Unexpected, m_beg, 1, m_line_number);
    }

    if (peek() == '=') { get<false>(); return Token(Token::Kind::SlashEqual, m_beg - 2, 2, m_line_number); }
    return Token(Token::Kind::Slash, m_beg - 1, 1, m_line_number);
}

void display_all_tokens(const char* code, const std::string& output_file) {
    std::ofstream out(output_file);
    
    if (!out.is_open()) {
        std::cerr << "Error: Could not open output file: " << output_file << std::endl;
        return;
    }

    std::size_t max_token_name_length = 0;

    for (std::size_t i = 0; i < static_cast<std::size_t>(Token::Kind::COUNT_); ++i) {
        max_token_name_length = std::max(max_token_name_length, Token::names[i].size());
    }

    ErrorReporter reporter;
    Lexer lexer(code, reporter);
    std::vector<Token> tokens;
    tokens.reserve(256);
    std::uint32_t max_line_number = 0;

    for (Token token = lexer.next(); token.kind() != Token::Kind::End; token = lexer.next()) {
        max_line_number = std::max(max_line_number, token.line());
        tokens.emplace_back(std::move(token));
    }

    const std::size_t line_number_width = std::to_string(max_line_number).length();

    for (const Token& token : tokens) {
        std::string lexeme(token.lexeme());
        std::replace(lexeme.begin(), lexeme.end(), '\n', ' ');

        out << "Line "
            << std::setw(line_number_width) << std::right << token.line() << ": "
            << std::setw(static_cast<int>(max_token_name_length)) << std::left
            << token.kind_name()
            << " |" << lexeme << "|\n";
    }

    out << "Total Tokens: " << tokens.size();
}

} // namespace tokenizing
} // namespace walnut

#endif // LEXER_HPP