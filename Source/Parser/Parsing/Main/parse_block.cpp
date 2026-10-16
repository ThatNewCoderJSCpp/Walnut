#include "Parser/Parsing/Main/parse_block.hpp"

namespace walnut {
namespace parsing {

nodes::BlockStatement* Parser::parse_block(bool require_body) {
    expect(tokenizing::Token::Kind::LeftCurly);
    ++nesting_depth;
    check_nesting_depth(current_token(), "block");
    nodes::BlockStatement* block = make<nodes::BlockStatement>();
    bool has_statements = false;
    
    while (current_token().kind() != tokenizing::Token::Kind::RightCurly && current_token().kind() != tokenizing::Token::Kind::End) {
        const std::size_t mark = m_spliced.size();
        m_list_level = true;
        block->add_statement(parse_statement());
        m_list_level = false;
        drain_spliced(block, mark);
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
