#include "Lexer/escapes.hpp"

namespace walnut {
namespace tokenizing {

const char* escape_error_text(EscapeError e) {
    switch (e) {
        case EscapeError::UnknownEscape:      return "unknown escape sequence";
        case EscapeError::TrailingBackslash:  return "literal ends with a lone backslash";
        case EscapeError::MissingHexDigits:   return "'\\x' must be followed by at least one hex digit";
        case EscapeError::BadUnicodeBrace:    return "'\\u{...}' must contain at least one hex digit and a closing '}'";
        case EscapeError::UnicodeOutOfRange:  return "code point exceeds U+10FFFF";
        case EscapeError::UnicodeSurrogate:   return "U+D800..U+DFFF are surrogates, not scalar values";
        case EscapeError::InvalidUtf8:        return "malformed UTF-8 in source";
        case EscapeError::EmptyCharLiteral:   return "character literal is empty";
        case EscapeError::MultiCharLiteral:   return "character literal must hold exactly one code point";
        default:                              return "invalid literal";
    }
}

bool valid_scalar(std::uint32_t cp, EscapeError& err) {
    if (cp > 0x10FFFFu)                  { err = EscapeError::UnicodeOutOfRange; return false; }
    if (cp >= 0xD800u && cp <= 0xDFFFu)  { err = EscapeError::UnicodeSurrogate;  return false; }
    return true;
}

void utf8_encode(std::uint32_t cp, std::string& out) {
    if (cp < 0x80u) {
        out += char(cp);
    } else if (cp < 0x800u) {
        out += char(0xC0u | (cp >> 6));
        out += char(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += char(0xE0u | (cp >> 12));
        out += char(0x80u | ((cp >> 6) & 0x3Fu));
        out += char(0x80u | (cp & 0x3Fu));
    } else {
        out += char(0xF0u | (cp >> 18));
        out += char(0x80u | ((cp >> 12) & 0x3Fu));
        out += char(0x80u | ((cp >> 6) & 0x3Fu));
        out += char(0x80u | (cp & 0x3Fu));
    }
}

bool utf8_decode(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    std::size_t extra;

    if      (c0 < 0x80u)              { cp = c0;            extra = 0; }
    else if ((c0 & 0xE0u) == 0xC0u)   { cp = c0 & 0x1Fu;    extra = 1; }
    else if ((c0 & 0xF0u) == 0xE0u)   { cp = c0 & 0x0Fu;    extra = 2; }
    else if ((c0 & 0xF8u) == 0xF0u)   { cp = c0 & 0x07u;    extra = 3; }
    else                              { err = EscapeError::InvalidUtf8; return false; }

    if (i + extra >= s.size()) { err = EscapeError::InvalidUtf8; return false; }

    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char cn = static_cast<unsigned char>(s[i + k]);
        if ((cn & 0xC0u) != 0x80u) { err = EscapeError::InvalidUtf8; return false; }
        cp = (cp << 6) | (cn & 0x3Fu);
    }

    if ((extra == 1 && cp < 0x80u) || (extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u)) {
        err = EscapeError::InvalidUtf8;
        return false;
    }

    if (!valid_scalar(cp, err)) return false;
    i += extra + 1;
    return true;
}

bool decode_escape(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err) {
    if (i + 1 >= s.size()) { err = EscapeError::TrailingBackslash; return false; }
    const char e = s[i + 1];
    i += 2;

    switch (e) {
        case 'n':  cp = 0x0A; return true;
        case 't':  cp = 0x09; return true;
        case 'r':  cp = 0x0D; return true;
        case 'a':  cp = 0x07; return true;
        case 'b':  cp = 0x08; return true;
        case 'f':  cp = 0x0C; return true;
        case 'v':  cp = 0x0B; return true;
        case 'e':  cp = 0x1B; return true;   
        case '0':  cp = 0x00; return true;
        case '\\': case '\'': case '"': case '?': case '`':
            cp = std::uint32_t(static_cast<unsigned char>(e)); return true;

        case 'x': {
            if (i >= s.size() || !is_hex(s[i])) { err = EscapeError::MissingHexDigits; return false; }
            std::uint32_t v = 0;
            
            while (i < s.size() && is_hex(s[i])) {
                v = v * 16u + std::uint32_t(hex_val(s[i]));
                if (v > 0x10FFFFu) { err = EscapeError::UnicodeOutOfRange; return false; }
                ++i;
            }
            
            cp = v;
            return valid_scalar(cp, err);
        }

        case 'u':
            if (i < s.size() && s[i] == '{') {               
                ++i;
                std::uint32_t v = 0;
                std::size_t digits = 0;

                while (i < s.size() && is_hex(s[i])) {
                    v = v * 16u + std::uint32_t(hex_val(s[i]));
                    if (v > 0x10FFFFu) { err = EscapeError::UnicodeOutOfRange; return false; }
                    ++i; ++digits;
                }

                if (digits == 0 || i >= s.size() || s[i] != '}') { err = EscapeError::BadUnicodeBrace; return false; }
                ++i;
                cp = v;
                return valid_scalar(cp, err);
            }
            [[fallthrough]];

        case 'U': {
            const std::size_t want = (e == 'u') ? 4 : 8;
            if (i + want > s.size()) { err = EscapeError::MissingHexDigits; return false; }
            std::uint32_t v = 0;

            for (std::size_t k = 0; k < want; ++k) {
                if (!is_hex(s[i + k])) { err = EscapeError::MissingHexDigits; return false; }
                v = v * 16u + std::uint32_t(hex_val(s[i + k]));
            }

            i += want;
            cp = v;
            return valid_scalar(cp, err);
        }

        default:
            err = EscapeError::UnknownEscape;
            return false;
    }
}

bool decode_literal(std::string_view interior, std::string& out, EscapeError& err, std::size_t* bad_at) {
    out.clear();
    out.reserve(interior.size());
    std::size_t i = 0;

    while (i < interior.size()) {
        const std::size_t start = i;
        std::uint32_t cp = 0;

        if (interior[i] == '\\') { if (!decode_escape(interior, i, cp, err)) { if (bad_at) *bad_at = start; return false; } }
        else                     { if (!utf8_decode(interior, i, cp, err))   { if (bad_at) *bad_at = start; return false; } }

        utf8_encode(cp, out);
    }

    err = EscapeError::None;
    return true;
}

bool decode_char(std::string_view interior, std::uint32_t& cp, EscapeError& err) {
    if (interior.empty()) { err = EscapeError::EmptyCharLiteral; return false; }
    std::size_t i = 0;

    if (interior[0] == '\\') { if (!decode_escape(interior, i, cp, err)) return false; }
    else                     { if (!utf8_decode(interior, i, cp, err))   return false; }

    if (i != interior.size()) { err = EscapeError::MultiCharLiteral; return false; }
    err = EscapeError::None;
    return true;
}

} // namespace tokenizing
} // namespace walnut
