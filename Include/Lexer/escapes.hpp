#ifndef WALNUT_LEXER_ESCAPES_HPP
#define WALNUT_LEXER_ESCAPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace walnut {
namespace tokenizing {

enum class EscapeError : std::uint8_t {
    None = 0,
    UnknownEscape,        // \q
    TrailingBackslash,    // "abc\"
    MissingHexDigits,     // \x with nothing after it
    BadUnicodeBrace,      // \u{ never closed, or empty
    UnicodeOutOfRange,    // > 0x10FFFF
    UnicodeSurrogate,     // D800..DFFF is not a scalar value
    InvalidUtf8,          // malformed raw source bytes
    EmptyCharLiteral,
    MultiCharLiteral
};

const char* escape_error_text(EscapeError e);

constexpr bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool valid_scalar(std::uint32_t cp, EscapeError& err);

void utf8_encode(std::uint32_t cp, std::string& out);

bool utf8_decode(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err);

bool decode_escape(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err);

bool decode_literal(std::string_view interior, std::string& out, EscapeError& err, std::size_t* bad_at = nullptr);

bool decode_char(std::string_view interior, std::uint32_t& cp, EscapeError& err);

inline bool validate_literal(std::string_view interior, EscapeError& err, std::size_t* bad_at = nullptr) {
    std::string scratch;
    return decode_literal(interior, scratch, err, bad_at);
}

} // namespace tokenizing
} // namespace walnut

#endif