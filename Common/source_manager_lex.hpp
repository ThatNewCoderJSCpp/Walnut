#ifndef WALNUT_SOURCE_MANAGER_LEX_HPP
#define WALNUT_SOURCE_MANAGER_LEX_HPP

#include "source_manager.hpp"
#include "error_reporter.hpp"
#include "../Lexer/token_stream.hpp"

#include <vector>
#include <cstring>

namespace walnut {

inline tokenizing::TokenStream SourceManager::lex(FileId id, ErrorReporter& reporter) {
    Entry& e = m_files[id];

    if (!e.lexed) {
        const std::size_t cap = e.size + 1;  
        auto* buf = m_arena.alloc_array_uninit<tokenizing::Token>(cap); // No arena allocation between here and shrink_array_last
        tokenizing::Lexer lexer(e.data, reporter);
        std::size_t n = 0;

        for (tokenizing::Token tok = lexer.next(); ; tok = lexer.next()) {
            tok.file_id(id);
            const tokenizing::Token::Kind k = tok.kind();

            if (k == tokenizing::Token::Kind::End) {
                ::new (static_cast<void*>(buf + n++)) tokenizing::Token(tok);
                break;
            }

            if (k != tokenizing::Token::Kind::Comment && k != tokenizing::Token::Kind::LongComment) {
                ::new (static_cast<void*>(buf + n++)) tokenizing::Token(tok);
            }
        }

        m_arena.shrink_array_last(buf, cap, n); // Allocations allowed again
        e.tokens      = buf;
        e.token_count = n;
        e.lexed       = true;
    }

    return tokenizing::TokenStream(e.tokens, e.token_count);
}

} // namespace walnut

#endif // WALNUT_SOURCE_MANAGER_LEX_HPP