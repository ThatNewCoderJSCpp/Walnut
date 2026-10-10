#ifndef PARSER_TYPE_UTLITY_FUNCTIONS_HPP
#define PARSER_TYPE_UTLITY_FUNCTIONS_HPP

#include "type_info.hpp"
#include "../../Lexer/keywords.hpp"

namespace walnut {
namespace parser_types {

parser_types::PrimitiveType::BaseKind token_to_base_kind(const tokenizing::Token& token, bool& is_long_form);

bool is_long_form_type(std::string_view lexeme);

parser_types::PrimitiveType::BaseKind lexeme_to_base_kind(std::string_view lexeme);

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPE_UTLITY_FUNCTIONS_HPP