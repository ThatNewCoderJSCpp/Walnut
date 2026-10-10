#ifndef PARSE_UNIFIED_MODIFIERS_HPP
#define PARSE_UNIFIED_MODIFIERS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

modifiers::RawModifiers Parser::parse_raw_modifiers() {
    modifiers::RawModifiers raw;
    
    while (true) {
        tokenizing::Token::Kind kind = current_token().kind();
        
        switch (kind) {
            case tokenizing::Token::Kind::ConstantDeclaration:
                if (raw.has(modifiers::RawModifiers::Const)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Const);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::ConstexprKeyword:
                if (raw.has(modifiers::RawModifiers::Constexpr)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Constexpr);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::ConstinitKeyword:
                if (raw.has(modifiers::RawModifiers::Constinit)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Constinit);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::FriendKeyword:
                if (raw.has(modifiers::RawModifiers::Friend)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Friend);
                raw.add_token(kind);
                advance();
                break;

            case tokenizing::Token::Kind::HoistKeyword:
                if (raw.has(modifiers::RawModifiers::Hoisted)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Hoisted);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::InlineKeyword:
                if (raw.has(modifiers::RawModifiers::Inline)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Inline);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::StaticKeyword:
                if (raw.has(modifiers::RawModifiers::Static)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Static);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::HiddenKeyword:
                if (raw.has(modifiers::RawModifiers::Hidden)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Hidden);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::LocalKeyword:
                if (raw.has(modifiers::RawModifiers::Local)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Local);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::GlobalKeyword:
                if (raw.has(modifiers::RawModifiers::Global)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Global);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::ExternKeyword:
                if (raw.has(modifiers::RawModifiers::Extern)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Extern);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::VolatileKeyword:
                if (raw.has(modifiers::RawModifiers::Volatile)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Volatile);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::ThreadLocalKeyword:
                if (raw.has(modifiers::RawModifiers::ThreadLocal)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::ThreadLocal);
                raw.add_token(kind);
                advance();
                break;
                
            case tokenizing::Token::Kind::MutableKeyword:
                if (raw.has(modifiers::RawModifiers::Mutable)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Mutable);
                raw.add_token(kind);
                advance();
                break;

            case tokenizing::Token::Kind::ImmutableKeyword:
                if (raw.has(modifiers::RawModifiers::Immutable)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Immutable);
                raw.add_token(kind);
                advance();
                break;
                
            default:
                return raw;
        }
    }
}

} // namespace walnut
} // namespace parsing

#endif // PARSE_UNIFIED_MODIFIERS_HPP