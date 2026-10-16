#include "Parser/Parsing/Main/parse_names.hpp"

namespace walnut {
namespace parsing {

std::string Parser::join_qualified_name(const std::vector<std::string_view>& parts) {
    std::string out;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "::";
        out.append(parts[i].data(), parts[i].size());
    }

    return out;
}

std::string_view Parser::expect_string_literal(const char* what) {
    using K = tokenizing::Token::Kind;

    if (current_token().kind() != K::String) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::String}, what);
    }

    validate_string_literal(current_token());
    std::string_view s = current_token().lexeme();
    advance();
    return s;
}

std::string_view Parser::parse_native_name(const char* what) {
    using K = tokenizing::Token::Kind;
    const tokenizing::Token at = current_token();
    std::string_view s = expect_string_literal(what);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    bool ok = !s.empty() && !(s.front() >= '0' && s.front() <= '9');

    for (std::size_t i = 0; ok && i < s.size(); ++i) {
        const char c = s[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (word) continue;
        if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') { ++i; continue; }
        if (c == '<' || c == '>' || c == ',' || c == ' ') continue;
        const std::size_t start = s.rfind("::", i) == std::string_view::npos ? 0 : s.rfind("::", i) + 2;
        if (s.substr(start, 8) == "operator" && i >= start + 8 && std::string_view("[]()+-*/%<>=!&|^~,").find(c) != std::string_view::npos) continue;
        ok = false;
    }

    if (!ok) throw ParserError::unexpected_token(reporter, at, {K::String}, "'" + std::string(s) + "' isn't a C++ name; write a name such as \"std::vector\", \"mylib::Grid\" or \"operator[]\"");
    return s;
}

std::vector<std::string_view> Parser::parse_qualified_name(const char* what, bool* is_global) {
    using K = tokenizing::Token::Kind;
    if (is_global) { *is_global = match(K::DoubleColon); }

    if (current_token().kind() != K::Identifier) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, what);
    }
    
    if (is_keyword(current_token().lexeme())) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Name cannot be a keyword");
    }

    std::vector<std::string_view> parts;
    parts.push_back(current_token().lexeme());
    advance();

    while (current_token().kind() == K::DoubleColon) {
        advance();

        if (current_token().kind() != K::Identifier) {
            throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected identifier after '::' in qualified name");
        }

        parts.push_back(current_token().lexeme());
        advance();
    }

    return parts;
}

} // namespace parsing
} // namespace walnut
