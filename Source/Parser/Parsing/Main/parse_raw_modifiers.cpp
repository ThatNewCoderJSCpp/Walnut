#include "Parser/Parsing/Main/parse_raw_modifiers.hpp"
#include "Common/file_kind.hpp"
#include <algorithm>
#include <vector>

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

                if (current_token().kind() == tokenizing::Token::Kind::String) {
                    validate_string_literal(current_token());
                    std::string_view lang = current_token().lexeme();
                    if (lang.size() >= 2 && lang.front() == '"' && lang.back() == '"') lang = lang.substr(1, lang.size() - 2);

                    if (lang == "C") raw.set_linkage(modifiers::RawModifiers::Linkage::C);
                    else if (lang == "C++") raw.set_linkage(modifiers::RawModifiers::Linkage::Cpp);
                    else throw ParserError::invalid_literal(reporter, current_token(), "extern only understands \"C\" and \"C++\"");

                    advance();
                }

                if (current_token().kind() == tokenizing::Token::Kind::FromKeyword) {
                    if (raw.linkage() == modifiers::RawModifiers::Linkage::Walnut) {
                        throw ParserError::invalid_declaration(reporter, current_token(), "'from' names the C or C++ file that defines these functions, so it needs a language first, as in 'extern \"C\" from \"file.c\"'");
                    }

                    advance();
                    std::vector<std::string_view> files;

                    for (;;) {
                        if (current_token().kind() != tokenizing::Token::Kind::String) {
                            throw ParserError::invalid_declaration(reporter, current_token(), "Expected a file name in quotes after 'from', as in 'extern \"C\" from \"file.c\"'");
                        }

                        validate_string_literal(current_token());
                        std::string_view file = current_token().lexeme();
                        if (file.size() >= 2 && file.front() == '"' && file.back() == '"') file = file.substr(1, file.size() - 2);
                        const FileKind fk = classify_file(file);

                        if (is_walnut_file(fk)) {
                            throw ParserError::invalid_declaration(reporter, current_token(), "'" + std::string(file) + "' is a Walnut file; use import or #include for Walnut code, and extern \"C\" or extern \"C++\" only for C and C++ files");
                        }

                        if (!is_native_header(fk) && !is_native_code(fk)) {
                            throw ParserError::invalid_declaration(reporter, current_token(), "'" + std::string(file) + "' isn't a C or C++ file; 'from' takes C and C++ headers (.h, .hpp, .hh, .hxx), source files (.c, .cpp, .cc, .cxx), object files (.o, .obj) and libraries (.a, .so, .dylib, .lib, .dll)");
                        }

                        if (std::find(files.begin(), files.end(), file) == files.end()) files.push_back(file);
                        advance();
                        if (current_token().kind() != tokenizing::Token::Kind::Comma) break;
                        advance();
                    }

                    if (files.size() > 255) throw ParserError::invalid_declaration(reporter, current_token(), "An extern can name at most 255 files");
                    std::string_view* stored = arena.alloc_array_uninit<std::string_view>(files.size());
                    for (std::size_t i = 0; i < files.size(); ++i) ::new (static_cast<void*>(stored + i)) std::string_view(files[i]);
                    raw.set_native_files(stored, static_cast<std::uint8_t>(files.size()));
                }
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

            case tokenizing::Token::Kind::PublicKeyword:
                if (lookahead().kind() != tokenizing::Token::Kind::LazyKeyword) return raw;
                if (raw.lazy_public()) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.set_lazy_public(true);
                advance();
                break;

            case tokenizing::Token::Kind::LazyKeyword:
                if (raw.has(modifiers::RawModifiers::Lazy)) throw ParserError::duplicate_type_modifiers(reporter, current_token());
                raw.add(modifiers::RawModifiers::Lazy);
                raw.add_token(kind);
                advance();
                if (current_token().kind() == tokenizing::Token::Kind::Identifier && current_token().lexeme() == "threadsafe") {
                    raw.add(modifiers::RawModifiers::LazyThreadsafe);
                    advance();
                }
                break;

                
            default:
                return raw;
        }
    }
}

} // namespace parsing
} // namespace walnut
