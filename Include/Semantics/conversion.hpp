#ifndef WALNUT_SEMA_CONVERT_HPP
#define WALNUT_SEMA_CONVERT_HPP

#include <optional>
#include "type_impl.hpp"
#include "scope.hpp"

namespace walnut {
namespace semantics {

enum class ConversionRank : std::uint8_t {
    Exact = 0,    // identical canonical type
    Promotion,    // same-family same-sign widening
    Conversion,   // int->float widening, pointer->bool, table Implicit
    UserDefined,  // operator T()  
    None          // not implicitly convertible 
};


#define WALNUT_BUILTIN_CAST_TABLE(X) \
    X(Char, Int,  Explicit) \
    X(Int,  Char, Forbidden)

enum class CastMode : std::uint8_t { Forbidden = 0, Explicit, Implicit };

struct BaseCastRule { parser_types::PrimitiveType::BaseKind from, to; CastMode mode; };

inline constexpr BaseCastRule kBaseCastRules[] = {
#define WALNUT_X(F,T,M) { parser_types::PrimitiveType::BaseKind::F, parser_types::PrimitiveType::BaseKind::T, CastMode::M },
    WALNUT_BUILTIN_CAST_TABLE(WALNUT_X)
#undef WALNUT_X
};

CastMode base_cast_mode(parser_types::PrimitiveType::BaseKind f, parser_types::PrimitiveType::BaseKind t);

struct NumericProfile {
    enum class Family : std::uint8_t { Int = 0, Float } family;
    bool is_unsigned;
    int  width;
};

std::optional<NumericProfile> numeric_profile(const BuiltinType* b);

ConversionRank rank_numeric(const NumericProfile& s, const NumericProfile& d);

ConversionRank rank_builtin(Type* src, Type* dst);

ConversionRank rank_user_defined(
    Type* src, Type* dst, TypeContext& ctx,
    bool allow_explicit = false, Symbol** chosen = nullptr
);

bool cv_at_least(const CV& have, const CV& want);

ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx);

Scope* record_scope_of(Type* t, TypeContext& ctx);

bool scope_derives_from(const Scope* s, const Scope* base, int depth = 0);

bool is_derived_record(Type* derived, Type* base, TypeContext& ctx);

bool unsized_array_accepts(Type* src, Type* dst, TypeContext& ctx);

ConversionRank rank_reference_binding(Type* src, ReferenceType* dst, TypeContext& ctx);

ConversionRank rank_conversion(Type* src, Type* dst, TypeContext& ctx);

bool is_static_castable(Type* src, Type* dst, TypeContext& ctx);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_CONVERSION_HPP