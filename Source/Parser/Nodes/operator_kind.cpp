#include "Parser/Nodes/operator_kind.hpp"

namespace walnut {
namespace nodes {

const char* overloadable_operator_name(OverloadableOperator op) {
    switch (op) {
        case OverloadableOperator::None:            return "<none>";
        case OverloadableOperator::Plus:            return "+";
        case OverloadableOperator::Minus:           return "-";
        case OverloadableOperator::Star:            return "*";
        case OverloadableOperator::Slash:           return "/";
        case OverloadableOperator::Percent:         return "%";
        case OverloadableOperator::Power:           return "**";
        case OverloadableOperator::Increment:       return "++";
        case OverloadableOperator::Decrement:       return "--";
        case OverloadableOperator::EqualEqual:      return "==";
        case OverloadableOperator::NotEqual:        return "!=";
        case OverloadableOperator::Less:            return "<";
        case OverloadableOperator::Greater:         return ">";
        case OverloadableOperator::LessEqual:       return "<=";
        case OverloadableOperator::GreaterEqual:    return ">=";
        case OverloadableOperator::LogicalAnd:      return "&&";
        case OverloadableOperator::LogicalOr:       return "||";
        case OverloadableOperator::LogicalNot:      return "!";
        case OverloadableOperator::BitAnd:          return "&";
        case OverloadableOperator::BitOr:           return "|";
        case OverloadableOperator::BitXor:          return "^";
        case OverloadableOperator::BitNot:          return "~";
        case OverloadableOperator::ShiftLeft:       return "<<";
        case OverloadableOperator::ShiftRight:      return ">>";
        case OverloadableOperator::Assign:          return "=";
        case OverloadableOperator::PlusEqual:       return "+=";
        case OverloadableOperator::MinusEqual:      return "-=";
        case OverloadableOperator::StarEqual:       return "*=";
        case OverloadableOperator::SlashEqual:      return "/=";
        case OverloadableOperator::PercentEqual:    return "%=";
        case OverloadableOperator::PowerEqual:      return "**=";
        case OverloadableOperator::BitAndEqual:     return "&=";
        case OverloadableOperator::BitOrEqual:      return "|=";
        case OverloadableOperator::BitXorEqual:     return "^=";
        case OverloadableOperator::ShiftLeftEqual:  return "<<=";
        case OverloadableOperator::ShiftRightEqual: return ">>=";
        case OverloadableOperator::Subscript:       return "[]";
        case OverloadableOperator::Call:            return "()";
        case OverloadableOperator::Arrow:           return "\u2192";
        case OverloadableOperator::New:             return "new";
        case OverloadableOperator::Delete:          return "delete";
        case OverloadableOperator::NewArray:        return "new[]";
        case OverloadableOperator::DeleteArray:     return "delete[]";
        case OverloadableOperator::Conversion:      return "conversion";
    }
    return "<unknown>";
}

OverloadableOperator classify_single_operator(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    switch (k) {
        case K::Plus:                return OverloadableOperator::Plus;
        case K::Minus:               return OverloadableOperator::Minus;
        case K::Asterisk:            return OverloadableOperator::Star;
        case K::Slash:               return OverloadableOperator::Slash;
        case K::Percent:             return OverloadableOperator::Percent;
        case K::DoubleAsterisk:      return OverloadableOperator::Power;
        case K::DoublePlus:          return OverloadableOperator::Increment;
        case K::DoubleMinus:         return OverloadableOperator::Decrement;
        case K::LogicEqual:          return OverloadableOperator::EqualEqual;
        case K::NotEqual:            return OverloadableOperator::NotEqual;
        case K::LessThan:            return OverloadableOperator::Less;
        case K::GreaterThan:         return OverloadableOperator::Greater;
        case K::LessEqual:           return OverloadableOperator::LessEqual;
        case K::GreaterEqual:        return OverloadableOperator::GreaterEqual;
        case K::LogicAnd:            return OverloadableOperator::LogicalAnd;
        case K::LogicOr:             return OverloadableOperator::LogicalOr;
        case K::ExclamationMark:     return OverloadableOperator::LogicalNot;
        case K::Ampersand:           return OverloadableOperator::BitAnd;
        case K::Pipe:                return OverloadableOperator::BitOr;
        case K::Caret:               return OverloadableOperator::BitXor;
        case K::Tilde:               return OverloadableOperator::BitNot;
        case K::DoubleLessThan:      return OverloadableOperator::ShiftLeft;
        case K::DoubleGreaterThan:   return OverloadableOperator::ShiftRight;
        case K::Equal:               return OverloadableOperator::Assign;
        case K::PlusEqual:           return OverloadableOperator::PlusEqual;
        case K::MinusEqual:          return OverloadableOperator::MinusEqual;
        case K::AsteriskEqual:       return OverloadableOperator::StarEqual;
        case K::SlashEqual:          return OverloadableOperator::SlashEqual;
        case K::PercentEqual:        return OverloadableOperator::PercentEqual;
        case K::DoubleAsteriskEqual: return OverloadableOperator::PowerEqual;
        case K::AmpersandEqual:      return OverloadableOperator::BitAndEqual;
        case K::PipeEqual:           return OverloadableOperator::BitOrEqual;
        case K::CaretEqual:          return OverloadableOperator::BitXorEqual;
        case K::ShiftLeftEqual:      return OverloadableOperator::ShiftLeftEqual;
        case K::ShiftRightEqual:     return OverloadableOperator::ShiftRightEqual;
        case K::SingleRightArrow:    return OverloadableOperator::Arrow;
        case K::NewKeyword:          return OverloadableOperator::New;
        case K::DeleteKeyword:       return OverloadableOperator::Delete;
        default:                     return OverloadableOperator::None;
    }
}

OverloadableOperator classify_overloadable_operator(
    const std::vector<tokenizing::Token::Kind>& t
) {
    using K = tokenizing::Token::Kind;

    if (t.size() == 1) {
        return classify_single_operator(t[0]);
    }

    if (t.size() == 2) {
        if (t[0] == K::LeftSquare && t[1] == K::RightSquare) return OverloadableOperator::Subscript; // []
        if (t[0] == K::LeftParen  && t[1] == K::RightParen)  return OverloadableOperator::Call;      // ()
        return OverloadableOperator::None;
    }

    if (t.size() == 3) {
        if (t[0] == K::NewKeyword    && t[1] == K::LeftSquare && t[2] == K::RightSquare) return OverloadableOperator::NewArray;    // new[]
        if (t[0] == K::DeleteKeyword && t[1] == K::LeftSquare && t[2] == K::RightSquare) return OverloadableOperator::DeleteArray; // delete[]
        return OverloadableOperator::None;
    }

    return OverloadableOperator::None;
}

} // namespace nodes
} // namespace walnut

namespace walnut {

bool is_assignment_op(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;

    switch (k) {
        case K::Equal: case K::PlusEqual: case K::MinusEqual: case K::AsteriskEqual:
        case K::SlashEqual: case K::PercentEqual: case K::DoubleAsteriskEqual:
        case K::PipeEqual: case K::AmpersandEqual: case K::CaretEqual:
        case K::ShiftLeftEqual: case K::ShiftRightEqual: return true;
        default: return false;
    }
}

bool is_comparison_op(tokenizing::Token::Kind k) {
    using K = tokenizing::Token::Kind;
    return k == K::LessThan || k == K::GreaterThan || k == K::LessEqual || k == K::GreaterEqual;
}

} // namespace walnut
