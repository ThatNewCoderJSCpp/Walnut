#include "Lexer/lexer.hpp"

namespace walnut {
namespace tokenizing {

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

Token Lexer::next() noexcept {
    Token t = scan();
    m_prev2 = m_prev1;
    m_prev1 = t.kind();
    m_prev1_text = t.lexeme();
    return t;
}

Token Lexer::raw_code(const char* word_start) noexcept {
    const char* p = m_beg;
    std::uint32_t line = m_line_number;
    while (is_space(*p)) { if (*p == '\n') ++line; ++p; }
    if (*p != '{') return Token(Token::Kind::Identifier, word_start, m_beg, m_line_number);
    const std::uint32_t start_line = line;
    ++p;
    const char* content = p;
    int depth = 1;

    while (*p) {
        const char c = *p;

        if (c == '\n') { ++line; ++p; continue; }

        if (c == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            continue;
        }

        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') ++line; ++p; }
            if (*p) p += 2;
            continue;
        }

        if (c == '"' && p > content && p[-1] == 'R' && (p - 1 == content || !is_identifier_char(p[-2]) || p[-2] == '8' || p[-2] == 'u' || p[-2] == 'U' || p[-2] == 'L')) {
            const char* d = p + 1;
            while (*d && *d != '(' && *d != '\n') ++d;
            if (*d != '(') { ++p; continue; }
            const std::string_view delim(p + 1, static_cast<std::size_t>(d - (p + 1)));
            p = d + 1;

            while (*p) {
                if (*p == ')' && std::string_view(p + 1).substr(0, delim.size()) == delim && p[1 + delim.size()] == '"') { p += delim.size() + 2; break; }
                if (*p == '\n') ++line;
                ++p;
            }
            continue;
        }

        if (c == '"' || (c == '\'' && !(p > content && is_identifier_char(p[-1])))) {
            ++p;
            while (*p && *p != c && *p != '\n') { if (*p == '\\' && p[1]) ++p; ++p; }
            if (*p == c) ++p;
            continue;
        }

        if (c == '{') ++depth;

        if (c == '}' && --depth == 0) {
            const Token t(Token::Kind::RawCode, content, p, start_line);
            m_beg = p + 1;
            m_line_number = line;
            return t;
        }

        ++p;
    }

    m_beg = p;
    m_line_number = start_line;
    report_error("This C or C++ code block never ends; it needs a '}' that matches its '{'");
    m_line_number = line;
    return Token(Token::Kind::Unexpected, word_start, 1, start_line);
}

Token Lexer::scan() noexcept {
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

    if (lexeme == "code" && m_prev2 == Token::Kind::ExternKeyword && m_prev1 == Token::Kind::String && (m_prev1_text == "C" || m_prev1_text == "C++")) {
        return raw_code(start);
    }

    Token::Kind kind = get_keyword(lexeme);
    if (kind != Token::Kind::Unexpected) { return Token(kind, start, m_beg, m_line_number); }
    return Token(Token::Kind::Identifier, start, m_beg, m_line_number); 
}

Token Lexer::number() noexcept {
    const char* start = m_beg;
    auto in_base = [](char c, int base) {
        if (base == 2)  return c == '0' || c == '1';
        if (base == 16) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        return c >= '0' && c <= '9';
    };
    auto digits = [&](int base) {
        while (true) {
            const char c = peek();
            if (in_base(c, base)) { get<false>(); continue; }
            if ((c == '_' || c == '\'') && m_beg > start && in_base(m_beg[-1], base) && in_base(m_beg[1], base)) { get<false>(); continue; }
            break;
        }
    };
    auto suffix = [&]() { while (is_identifier_char(peek())) { get<false>(); } };

    if (m_beg[0] == '0' && (m_beg[1] == 'x' || m_beg[1] == 'X') && in_base(m_beg[2], 16)) {
        get<false>(); get<false>();
        digits(16);
        suffix();
        return Token(Token::Kind::Integer, start, m_beg, m_line_number);
    }

    if (m_beg[0] == '0' && (m_beg[1] == 'b' || m_beg[1] == 'B') && in_base(m_beg[2], 2)) {
        get<false>(); get<false>();
        digits(2);
        suffix();
        return Token(Token::Kind::Integer, start, m_beg, m_line_number);
    }

    bool is_float = false;
    digits(10);

    if (peek() == '.') {
        is_float = true;
        get<false>();
        digits(10);
    }

    if ((peek() == 'e' || peek() == 'E') && (is_digit(m_beg[1]) || ((m_beg[1] == '+' || m_beg[1] == '-') && is_digit(m_beg[2])))) {
        is_float = true;
        get<false>();
        if (peek() == '+' || peek() == '-') get<false>();
        digits(10);
    }

    suffix();
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

namespace walnut {
namespace tokenizing {

void Lexer::report_error(const std::string& message) noexcept {
    ++m_error_count;
    m_reporter.report(ErrorPhase::Lexer, m_line_number, message);
}

Token Lexer::atom(Token::Kind kind) noexcept { return Token(kind, m_beg++, 1, m_line_number); }

} // namespace tokenizing
} // namespace walnut
