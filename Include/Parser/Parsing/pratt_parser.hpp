#ifndef PRATT_PARSER_HPP
#define PRATT_PARSER_HPP

#include "base.hpp"

namespace walnut {
namespace parsing {

Precedence get_infix_precedence(tokenizing::Token::Kind kind);

bool is_right_associative(tokenizing::Token::Kind kind);

bool is_callable_node(const nodes::ASTNode* n);

bool is_constructible_callee(const nodes::ASTNode* n);

} // namespace parsing
} // namespace walnut

#endif // PRATT_PARSER_HPP