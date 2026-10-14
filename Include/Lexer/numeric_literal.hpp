#ifndef WALNUT_LEXER_NUMERIC_LITERAL_HPP
#define WALNUT_LEXER_NUMERIC_LITERAL_HPP

#include <string>
#include <string_view>

namespace walnut {
namespace tokenizing {

struct NumericLiteral {
    std::string body;
    std::string digits;
    std::string suffix;
    std::string error;
    int         base          = 10;
    int         steps         = 0;
    int         rank          = 0;
    char        family        = 0;
    bool        is_float      = false;
    bool        type_is_float = false;
    bool        has_suffix    = false;
    bool        is_unsigned   = false;
};

NumericLiteral parse_numeric_literal(std::string_view lexeme);

} // namespace tokenizing
} // namespace walnut

#endif // WALNUT_LEXER_NUMERIC_LITERAL_HPP
