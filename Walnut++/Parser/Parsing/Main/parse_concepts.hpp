#ifndef PARSE_CONCEPTS_HPP
#define PARSE_CONCEPTS_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::ASTNode* Parser::parse_concept_declaration() {
    using K = tokenizing::Token::Kind;
    const std::size_t line = current_token().line();
    expect(K::ConceptKeyword, "Expected 'concept'");

    if (current_token().kind() != K::Identifier) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Expected a concept name after 'concept'");
    }

    std::string_view name = current_token().lexeme();

    if (is_keyword(name)) {
        throw ParserError::unexpected_token(reporter, current_token(), {K::Identifier}, "Concept name cannot be a keyword");
    }

    advance();
    register_concept_name(name);
    register_template_name(name);

    if (!match(K::Equal)) {
        throw ParserError::missing_token(reporter, current_token(), K::Equal, "Expected '=' after concept name");
    }

    nodes::ASTNode* constraint = parse_expression();
    return make<nodes::ConceptDeclaration>(name, constraint, static_cast<std::uint32_t>(line));
}

bool Parser::looks_like_constrained_param() {
    using K = tokenizing::Token::Kind;
    auto tok = [&](std::size_t i) -> const tokenizing::Token& { return i == 0 ? current_token() : lookahead(i); };
    std::size_t i = 0;
    if (tok(0).kind() == K::DoubleColon) { i = 1; }          
    if (tok(i).kind() != K::Identifier) { return false; }
    std::string_view leaf = tok(i).lexeme();                 
    ++i;

    while (tok(i).kind() == K::DoubleColon && tok(i + 1).kind() == K::Identifier) {
        leaf = tok(i + 1).lexeme();
        i += 2;
    }

    return is_known_concept(leaf);
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_CONCEPTS_HPP