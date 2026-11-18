#ifndef WALNUT_SEMANTICS_TYPE_OPS_HPP
#define WALNUT_SEMANTICS_TYPE_OPS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "type.hpp"
#include "../Parser/Types/type_ops.hpp"
#include "../Common/arena_vector.hpp"
#include "../Parser/nodes.hpp"

namespace walnut {
namespace semantics {

Type* apply_type_op(TypeContext& ctx, parser_types::TypeOp op, const std::vector<Type*>& args, std::string& error);

Type* numeric_common_type(TypeContext& ctx, Type* a, Type* b);

std::optional<std::size_t> enumerator_count(Type* t);

nodes::EnumDeclaration* enum_declaration_of(Type* t);

Type* enum_underlying_type(TypeContext& ctx, Type* enum_type);

Type* widest_unsigned_int(TypeContext& ctx);

unsigned int enum_storage_bits(Type* underlying);

std::optional<std::int64_t> eval_type_trait(TypeContext& ctx, parser_types::TypeTrait trait, const std::vector<Type*>& args, std::string& error);

std::vector<Type*> trait_operands(TypeContext& ctx, const parser_types::TemplateArgument* first, Span<parser_types::TemplateArgument* const> extra);
bool trait_has_pack(Span<parser_types::TemplateArgument* const> extra);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_TYPE_OPS_HPP
