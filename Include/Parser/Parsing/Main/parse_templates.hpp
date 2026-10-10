#ifndef PARSE_TEMPLATE_HPP
#define PARSE_TEMPLATE_HPP

#include "../base.hpp"

namespace walnut {
namespace parsing {

static inline std::string_view template_value_repr(const nodes::ASTNode* n) {
    if (!n) return {};
    if (const auto* id = nodes::node_cast<nodes::Identifier>(n))           return id->get_name();
    if (const auto* lit = nodes::node_cast<nodes::Literal>(n))             return lit->value;
    if (const auto* q   = nodes::node_cast<nodes::QualifiedIdentifier>(n)) return q->simple_name();
    return std::string_view("<expr>");
}

} // namespace parsing
} // namespace walnut

#endif // PARSE_TEMPLATE_HPP