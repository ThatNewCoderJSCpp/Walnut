#ifndef TOKEN_TYPE_CHECKERS_HPP
#define TOKEN_TYPE_CHECKERS_HPP

#include "../../Lexer/token_macro.hpp"

namespace walnut {

constexpr bool is_basic_arithmetic_operator(const tokenizing::Token& token) noexcept {
    return token.is_one_of(
        tokenizing::Token::Kind::Plus,          
        tokenizing::Token::Kind::Minus,         
        tokenizing::Token::Kind::Asterisk,      
        tokenizing::Token::Kind::Slash,         
        tokenizing::Token::Kind::Percent,     
        tokenizing::Token::Kind::DoubleAsterisk 
    );
}

constexpr bool is_declaration_token(const tokenizing::Token& token) noexcept {
    return token.is_one_of(
        tokenizing::Token::Kind::IntegerDeclaration,
        tokenizing::Token::Kind::DecimalDeclaration,
        tokenizing::Token::Kind::StringDeclaration,
        tokenizing::Token::Kind::BooleanDeclaration,
        tokenizing::Token::Kind::CharacterDeclaration,
        tokenizing::Token::Kind::LongDeclaration,
        tokenizing::Token::Kind::ShortDeclaration,
        tokenizing::Token::Kind::UnsignedDeclaration,
        tokenizing::Token::Kind::TextDeclaration,
        tokenizing::Token::Kind::AutoDeclaration,
        tokenizing::Token::Kind::DynamicDeclaration
    );
}

constexpr bool is_declaration_token(tokenizing::Token::Kind token) noexcept {
    return  token == tokenizing::Token::Kind::IntegerDeclaration ||
            token == tokenizing::Token::Kind::DecimalDeclaration ||
            token == tokenizing::Token::Kind::StringDeclaration ||
            token == tokenizing::Token::Kind::BooleanDeclaration ||
            token == tokenizing::Token::Kind::CharacterDeclaration ||
            token == tokenizing::Token::Kind::LongDeclaration ||
            token == tokenizing::Token::Kind::ShortDeclaration ||
            token == tokenizing::Token::Kind::UnsignedDeclaration ||
            token == tokenizing::Token::Kind::TextDeclaration ||
            token == tokenizing::Token::Kind::AutoDeclaration ||
            token == tokenizing::Token::Kind::DynamicDeclaration;
}

constexpr bool is_variable_modifier(const tokenizing::Token& token) noexcept {
    return token.is_one_of(
        tokenizing::Token::Kind::ConstantDeclaration,
        tokenizing::Token::Kind::HoistKeyword,
        tokenizing::Token::Kind::GlobalKeyword,
        tokenizing::Token::Kind::LocalKeyword,
        tokenizing::Token::Kind::HiddenKeyword,
        tokenizing::Token::Kind::ConstexprKeyword,
        tokenizing::Token::Kind::ThreadLocalKeyword,
        tokenizing::Token::Kind::VolatileKeyword,
        tokenizing::Token::Kind::ExternKeyword,
        tokenizing::Token::Kind::ConstinitKeyword,
        tokenizing::Token::Kind::ImmutableKeyword,
        tokenizing::Token::Kind::InlineKeyword,
        tokenizing::Token::Kind::StaticKeyword,
        tokenizing::Token::Kind::MutableKeyword,
        tokenizing::Token::Kind::AlignasKeyword,
        tokenizing::Token::Kind::FriendKeyword
    );
}

constexpr bool is_variable_modifier(tokenizing::Token::Kind token) noexcept { 
    return token == tokenizing::Token::Kind::ConstantDeclaration ||
            token == tokenizing::Token::Kind::HoistKeyword ||
            token == tokenizing::Token::Kind::GlobalKeyword ||
            token == tokenizing::Token::Kind::LocalKeyword ||
            token == tokenizing::Token::Kind::HiddenKeyword ||
            token == tokenizing::Token::Kind::ConstexprKeyword ||
            token == tokenizing::Token::Kind::ThreadLocalKeyword ||
            token == tokenizing::Token::Kind::VolatileKeyword ||
            token == tokenizing::Token::Kind::ExternKeyword ||
            token == tokenizing::Token::Kind::ConstinitKeyword ||
            token == tokenizing::Token::Kind::ImmutableKeyword ||
            token == tokenizing::Token::Kind::InlineKeyword ||
            token == tokenizing::Token::Kind::StaticKeyword ||
            token == tokenizing::Token::Kind::MutableKeyword ||
            token == tokenizing::Token::Kind::AlignasKeyword ||
            token == tokenizing::Token::Kind::FriendKeyword;
}

constexpr bool is_base_type_token(tokenizing::Token::Kind kind) {
    switch (kind) {
        case tokenizing::Token::Kind::IntegerDeclaration:
        case tokenizing::Token::Kind::DecimalDeclaration:
        case tokenizing::Token::Kind::BooleanDeclaration:
        case tokenizing::Token::Kind::CharacterDeclaration:
        case tokenizing::Token::Kind::StringDeclaration:
        case tokenizing::Token::Kind::TextDeclaration:
        case tokenizing::Token::Kind::VoidKeyword:
        case tokenizing::Token::Kind::AutoDeclaration:
        case tokenizing::Token::Kind::DynamicDeclaration:
            return true;
        default:
            return false;
    }
}

} // namespace walnut

#endif // TOKEN_TYPE_CHECKERS_HPP