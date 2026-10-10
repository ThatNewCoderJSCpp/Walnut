#ifndef PARSE_FUNCTIONS_HPP
#define PARSE_FUNCTIONS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

static inline bool operator_spec_starts_type(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    if (is_base_type_token(k)) return true;

    switch (k) {
        case K::Identifier:
        case K::DoubleColon:
        case K::UnsignedDeclaration:
        case K::ShortDeclaration:
        case K::LongDeclaration:
        case K::ConstantDeclaration:
        case K::FunctionKeyword:
        case K::ArrayKeyword:
        case K::VariantKeyword:
            return true;
        default:
            return false;
    }
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_FUNCTIONS_HPP