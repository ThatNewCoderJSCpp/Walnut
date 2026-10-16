#include "Lexer/numeric_literal.hpp"
#include "Lexer/escapes.hpp"

namespace walnut {
namespace tokenizing {

namespace {

bool digit_in_base(char c, int base) {
    if (base == 2)  return c == '0' || c == '1';
    if (base == 8)  return c >= '0' && c <= '7';
    if (base == 16) return is_hex(c);
    return c >= '0' && c <= '9';
}

bool is_separator(char c) { return c == '_' || c == '\''; }

}

NumericLiteral parse_numeric_literal(std::string_view lexeme) {
    NumericLiteral out;
    std::size_t i = 0;

    if (lexeme.size() > 2 && lexeme[0] == '0' && (lexeme[1] == 'x' || lexeme[1] == 'X')) { out.base = 16; i = 2; }
    else if (lexeme.size() > 2 && lexeme[0] == '0' && (lexeme[1] == 'b' || lexeme[1] == 'B')) { out.base = 2; i = 2; }
    else if (lexeme.size() > 2 && lexeme[0] == '0' && (lexeme[1] == 'o' || lexeme[1] == 'O')) { out.base = 8; i = 2; }

    std::string body = out.base == 16 ? "0x" : out.base == 2 ? "0b" : out.base == 8 ? "0o" : "";
    char previous = 0;

    for (; i < lexeme.size(); ++i) {
        const char c = lexeme[i];

        if (digit_in_base(c, out.base)) {
            out.digits += c;
            body += c;
            previous = c;
            continue;
        }

        if (is_separator(c)) {
            const bool between = digit_in_base(previous, out.base) && i + 1 < lexeme.size() && digit_in_base(lexeme[i + 1], out.base);
            if (!between) { out.error = "a digit separator ('_' or ''') must sit between two digits"; return out; }
            continue;
        }

        if (out.base == 10 && c == '.' && !out.is_float) {
            out.is_float = true;
            out.digits += c;
            body += c;
            previous = c;
            continue;
        }

        if (out.base == 10 && (c == 'e' || c == 'E') && i + 1 < lexeme.size()) {
            const char n = lexeme[i + 1];
            const bool signed_exp = (n == '+' || n == '-') && i + 2 < lexeme.size() && digit_in_base(lexeme[i + 2], 10);
            if (digit_in_base(n, 10) || signed_exp) {
                out.is_float = true;
                out.digits += c;
                body += c;
                if (signed_exp) { out.digits += n; body += n; ++i; }
                previous = c;
                continue;
            }
        }

        break;
    }

    out.body = body;
    out.suffix = std::string(lexeme.substr(i));
    if (out.suffix.empty()) return out;
    out.has_suffix = true;
    int u = 0, l = 0, sh = 0;
    char family = 0;

    for (char c : out.suffix) {
        switch (c) {
            case 'u': case 'U': ++u; break;
            case 'l': case 'L': ++l; break;
            case 's': case 'S': ++sh; break;
            case 'i': case 'I': case 'f': case 'F': case 'd': case 'D': {
                const char f = static_cast<char>(c | 0x20);
                if (family && family != f) { out.error = "'" + out.suffix + "' names more than one kind of number; use only one of 'i', 'f' and 'd'"; return out; }
                if (family) { out.error = "'" + out.suffix + "' repeats '" + std::string(1, f) + "'"; return out; }
                family = f;
                break;
            }
            default:
                out.error = "'" + out.suffix + "' is not a number suffix; use 'u' for unsigned, 's' or 'l' to make it narrower or wider, and 'i', 'f' or 'd' for 'integer', 'float' or 'double'";
                return out;
        }
    }

    if (u > 1)             { out.error = "'" + out.suffix + "' has more than one 'u'"; return out; }
    if (l > 0 && sh > 0)   { out.error = "'" + out.suffix + "' mixes 'l' and 's'; use only one of them"; return out; }
    if (l > 2 || sh > 2)   { out.error = "'" + out.suffix + "' has more than two '" + std::string(l > 2 ? "l" : "s") + "'; like the type names, a suffix allows at most two"; return out; }
    if (family == 'i' && out.is_float) { out.error = "'i' makes an 'integer', so it can't go on a number with a decimal point or an exponent"; return out; }
    if ((family == 'f' || family == 'd') && out.base != 10) { out.error = "'" + std::string(1, family) + "' can't follow a hexadecimal or binary number"; return out; }

    out.is_unsigned   = u == 1;
    out.steps         = l - sh;
    out.family        = family;
    out.type_is_float = family == 'f' || family == 'd' || (family == 0 && out.is_float);
    const int base_rank = family == 'i' ? 4 : family == 'd' ? 4 : out.type_is_float ? 2 : 0;
    out.rank = base_rank + out.steps;
    return out;
}

} // namespace tokenizing
} // namespace walnut
