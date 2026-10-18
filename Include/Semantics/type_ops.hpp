#ifndef WALNUT_SEMANTICS_TYPE_OPS_HPP
#define WALNUT_SEMANTICS_TYPE_OPS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "type.hpp"
#include "../Parser/Types/type_ops.hpp"

namespace walnut {
namespace semantics {

Type* apply_type_op(TypeContext& ctx, parser_types::TypeOp op, const std::vector<Type*>& args, std::string& error);

std::optional<std::size_t> enumerator_count(Type* t);

std::optional<std::int64_t> eval_type_trait(TypeContext& ctx, parser_types::TypeTrait trait, const std::vector<Type*>& args, std::string& error);

std::vector<Type*> trait_operands(TypeContext& ctx, const parser_types::TemplateArgument* first, const std::vector<parser_types::TemplateArgument*>& extra);
bool trait_has_pack(const std::vector<parser_types::TemplateArgument*>& extra);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_TYPE_OPS_HPP
