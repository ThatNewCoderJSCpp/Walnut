#ifndef OTHER_PARSING_HPP
#define OTHER_PARSING_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

nodes::BlockStatement* Parser::parse_block(bool require_body) {
    expect(tokenizing::Token::Kind::LeftCurly);
    ++nesting_depth;
    check_nesting_depth(current_token(), "block");
    nodes::BlockStatement* block = make<nodes::BlockStatement>();
    bool has_statements = false;
    
    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        block->add_statement(parse_statement());
        has_statements = true;
    }
    
    if (!has_statements && require_body) { throw ParserError::empty_block(reporter, current_token(), "code"); }
    if (current_token().kind() == tokenizing::Token::Kind::End) { throw ParserError::unexpected_end_of_input(reporter, current_token(), "block"); }
    expect(tokenizing::Token::Kind::RightCurly);
    --nesting_depth;
    return block;
}

} // namespace parsing
} // namespace walnut

#endif // OTHER_PARSING_HPP