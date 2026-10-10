#ifndef WALNUT_AST_NODES_HPP
#define WALNUT_AST_NODES_HPP

#include "Nodes/main_nodes.hpp"
#include "Nodes/expression_nodes.hpp"
#include "Nodes/statement_nodes.hpp"
#include "Nodes/function_nodes.hpp"
#include "Nodes/record_nodes.hpp"
#include "Nodes/requires_expression_node.hpp"

#include "Nodes/node_children.hpp"

namespace walnut {
namespace nodes {

void print_node(const ASTNode* node, std::ostream& os, std::size_t indent);

} // namespace nodes
} // namespace walnut

#endif // WALNUT_AST_NODES_HPP