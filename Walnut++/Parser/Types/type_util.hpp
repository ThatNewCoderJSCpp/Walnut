#ifndef PARSER_TYPE_UTLITY_FUNCTIONS_HPP
#define PARSER_TYPE_UTLITY_FUNCTIONS_HPP

#include "type_info.hpp"
#include "../../Lexer/keywords.hpp"

namespace walnut {
namespace parser_types {

parser_types::PrimitiveType::BaseKind token_to_base_kind(const tokenizing::Token& token, bool& is_long_form) {
    using BK = parser_types::PrimitiveType::BaseKind;
    std::string_view lexeme = token.lexeme();
    is_long_form = false;

    switch (token.kind()) {
        case tokenizing::Token::Kind::IntegerDeclaration:
            is_long_form = (lexeme == "integer");
            return BK::Int;

        case tokenizing::Token::Kind::DecimalDeclaration:
            is_long_form = (lexeme == "double" || lexeme == "decimal" || lexeme == "floater");
            return BK::Float;

        case tokenizing::Token::Kind::BooleanDeclaration: return BK::Bool;
        case tokenizing::Token::Kind::CharacterDeclaration: return BK::Char;
        case tokenizing::Token::Kind::StringDeclaration: return BK::String;
        case tokenizing::Token::Kind::TextDeclaration: return BK::Text;
        case tokenizing::Token::Kind::VoidKeyword: return BK::Void;
        case tokenizing::Token::Kind::AutoDeclaration: return BK::Auto;
        case tokenizing::Token::Kind::DynamicDeclaration: return BK::Dynamic;
        default: return BK::Auto;
    }
}

bool is_long_form_type(std::string_view lexeme) { return lexeme == "integer" || lexeme == "double" || lexeme == "decimal" || lexeme == "floater"; }

parser_types::PrimitiveType::BaseKind lexeme_to_base_kind(std::string_view lexeme) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (lexeme == "int" || lexeme == "integer") return BK::Int;
    if (lexeme == "float" || lexeme == "dec" || lexeme == "double" || lexeme == "decimal" || lexeme == "floater") return BK::Float;
    if (lexeme == "bool" || lexeme == "boolean") return BK::Bool;
    if (lexeme == "char" || lexeme == "character") return BK::Char;
    if (lexeme == "string" || lexeme == "str") return BK::String;
    if (lexeme == "text") return BK::Text;
    if (lexeme == "void") return BK::Void;
    if (lexeme == "auto" || lexeme == "automatic") return BK::Auto;
    if (lexeme == "let" || lexeme == "var" || lexeme == "def" || lexeme == "define" || lexeme == "variable" || lexeme == "any") return BK::Dynamic;
    return BK::Void; 
}

} // namespace parser_types
} // namespace walnut

#endif // PARSER_TYPE_UTLITY_FUNCTIONS_HPP