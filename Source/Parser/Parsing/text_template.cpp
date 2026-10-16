#include "Parser/Parsing/base.hpp"
#include "Parser/Utility/parser_error.hpp"
#include "Lexer/token_stream.hpp"
#include "Lexer/escapes.hpp"

namespace walnut {
namespace parsing {

namespace {

std::size_t skip_quoted_raw(std::string_view raw, std::size_t i, char quote);

std::size_t skip_expression_raw(std::string_view raw, std::size_t i) {
    int depth = 1;

    while (i < raw.size()) {
        const char c = raw[i];
        if (c == '{') { ++depth; ++i; continue; }
        if (c == '}') { if (--depth == 0) return i; ++i; continue; }
        if (c == '"' || c == '\'' || c == '`') { i = skip_quoted_raw(raw, i, c); continue; }
        ++i;
    }

    return std::string_view::npos;
}

std::size_t skip_quoted_raw(std::string_view raw, std::size_t i, char quote) {
    ++i;

    while (i < raw.size() && raw[i] != quote) {
        if (raw[i] == '\\') { i += 2; continue; }

        if (quote == '`' && raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
            const std::size_t close = skip_expression_raw(raw, i + 2);
            if (close == std::string_view::npos) return raw.size();
            i = close + 1;
            continue;
        }

        ++i;
    }

    return i < raw.size() ? i + 1 : raw.size();
}

} // namespace

bool Parser::has_interpolation(std::string_view raw) {
    for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\\') { ++i; continue; }
        if (raw[i] == '$' && raw[i + 1] == '{') return true;
    }

    return false;
}

nodes::ASTNode* Parser::parse_text_template(const tokenizing::Token& tok) {
    using K = tokenizing::Token::Kind;
    const std::string_view raw = tok.lexeme();
    const std::uint32_t line = tok.line();
    std::vector<nodes::ASTNode*> parts;
    std::size_t chunk_start = 0;
    std::size_t i = 0;

    auto add_chunk = [&](std::size_t from, std::size_t to) {
        const std::string_view piece = raw.substr(from, to - from);
        tokenizing::EscapeError err = tokenizing::EscapeError::None;
        std::size_t at = 0;
        if (!tokenizing::validate_literal(piece, err, &at)) throw ParserError::invalid_escape_sequence(reporter, tok, tokenizing::escape_error_text(err), from + at);
        auto* lit = arena.alloc<nodes::Literal>(piece, K::TextLiteral, line);
        lit->file_id = tok.file_id();
        parts.push_back(lit);
    };

    while (i < raw.size()) {
        if (raw[i] == '\\') { i += 2; continue; }

        if (raw[i] != '$' || i + 1 >= raw.size() || raw[i + 1] != '{') { ++i; continue; }

        if (i > chunk_start || parts.empty()) add_chunk(chunk_start, i);
        const std::size_t open = i + 2;
        const std::size_t close = skip_expression_raw(raw, open);
        if (close == std::string_view::npos) throw ParserError::unexpected_token(reporter, tok, {K::RightCurly}, "'${' in a text literal needs a closing '}'");
        const std::string_view source = raw.substr(open, close - open);

        if (source.find_first_not_of(" \t\r\n") == std::string_view::npos) {
            throw ParserError::unexpected_token(reporter, tok, {K::Identifier}, "'${}' in a text literal needs an expression inside the braces");
        }

        char* buffer = arena.alloc_array_uninit<char>(source.size() + 1);
        std::copy(source.begin(), source.end(), buffer);
        buffer[source.size()] = '\0';
        tokenizing::TokenStream inner(buffer, reporter);

        for (std::size_t k = 0; k < inner.size(); ++k) {
            auto& t = const_cast<tokenizing::Token&>(inner.data()[k]);
            t.file_id(tok.file_id());
            t.line(line);
        }

        Parser sub(inner, reporter, arena);
        sub.m_node_order = m_node_order;
        nodes::ASTNode* value = sub.parse_expression();
        m_node_order = sub.m_node_order;

        if (sub.current_token().kind() != K::End) {
            throw ParserError::unexpected_token(reporter, sub.current_token(), {K::RightCurly}, "unexpected text after the expression in '${...}'");
        }

        parser_types::TypeInfo text_type;
        text_type.type = make<parser_types::PrimitiveType>(parser_types::PrimitiveType::BaseKind::Text);
        auto* cast = make<nodes::CastExpression>(nodes::CastExpression::CastKind::Static, text_type, value, line);
        cast->interpolation = true;
        parts.push_back(cast);
        i = close + 1;
        chunk_start = i;
    }

    if (chunk_start < raw.size()) add_chunk(chunk_start, raw.size());

    nodes::ASTNode* result = parts.front();
    for (std::size_t k = 1; k < parts.size(); ++k) result = make<nodes::BinaryExpression>(result, K::Plus, parts[k], line);
    return result;
}

} // namespace parsing
} // namespace walnut
