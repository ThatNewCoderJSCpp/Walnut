#ifndef WALNUT_NODES_OPERATOR_KIND_HPP
#define WALNUT_NODES_OPERATOR_KIND_HPP

#include <vector>
#include <cstdint>
#include "../../Lexer/token_macro.hpp"

namespace walnut {
namespace nodes {

enum class OverloadableOperator : std::uint8_t {
    None = 0,
    Plus, Minus, Star, Slash, Percent, Power,
    Increment, Decrement,
    EqualEqual, NotEqual, Less, Greater, LessEqual, GreaterEqual,
    LogicalAnd, LogicalOr, LogicalNot,
    BitAnd, BitOr, BitXor, BitNot, 
    ShiftLeft, ShiftRight,
    Assign,
    PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual, PowerEqual,
    BitAndEqual, BitOrEqual, BitXorEqual, 
    ShiftLeftEqual, ShiftRightEqual,
    Subscript, Call, Arrow,
    New, Delete, NewArray, DeleteArray,
    Conversion
};

const char* overloadable_operator_name(OverloadableOperator op);

OverloadableOperator classify_single_operator(tokenizing::Token::Kind k);

inline bool is_overloadable_operator_token(tokenizing::Token::Kind k) {
    return classify_single_operator(k) != OverloadableOperator::None;
}

OverloadableOperator classify_overloadable_operator(
    const std::vector<tokenizing::Token::Kind>& t
);

} // namespace nodes

bool is_assignment_op(tokenizing::Token::Kind k);

inline bool is_equality_op(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    return k == K::LogicEqual || k == K::NotEqual;
}

bool is_comparison_op(tokenizing::Token::Kind k);

inline bool is_logical_op(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    return k == K::LogicAnd || k == K::LogicOr;
}

inline bool is_shift_op(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    return k == K::DoubleLessThan || k == K::DoubleGreaterThan;
}

} // namespace walnut

#endif // WALNUT_NODES_OPERATOR_KIND_HPP