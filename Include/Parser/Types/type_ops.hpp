#ifndef WALNUT_PARSER_TYPE_OPS_HPP
#define WALNUT_PARSER_TYPE_OPS_HPP

#include <cstdint>
#include <string_view>

namespace walnut {
namespace parser_types {

enum class TypeOp : std::uint8_t {
    Typeof = 0,
    Decltype,
    DiscardConst,
    DiscardVolatile,
    DiscardCv,
    DiscardRef,
    DiscardPointer,
    Decay,
    AddConst,
    AddVolatile,
    AddRef,
    AddRvalueRef,
    AddPointer,
    MakeSigned,
    MakeUnsigned,
    ElementType,
    UnderlyingType,
    ReturnType,
    CommonType,
    Conditional,
    Promote,
    Demote,
    IntFromFloat,
    FloatFromInt
};

enum class OverloadPolicy : std::uint8_t {
    Never = 0,
    Allowed,
    Warns
};

struct TypeOpInfo {
    std::string_view name;
    TypeOp           op;
    int              min_types;
    int              max_types;
    OverloadPolicy   overload = OverloadPolicy::Never;
};

inline constexpr TypeOpInfo kTypeOps[] = {
    { "discard_volatile", TypeOp::DiscardVolatile, 1, 1  },
    { "discard_cv",       TypeOp::DiscardCv,       1, 1  },
    { "discard_ref",      TypeOp::DiscardRef,      1, 1  },
    { "discard_pointer",  TypeOp::DiscardPointer,  1, 1  },
    { "decay",            TypeOp::Decay,           1, 1  },
    { "add_const",        TypeOp::AddConst,        1, 1  },
    { "add_volatile",     TypeOp::AddVolatile,     1, 1  },
    { "add_ref",          TypeOp::AddRef,          1, 1  },
    { "add_rvalue_ref",   TypeOp::AddRvalueRef,    1, 1  },
    { "add_pointer",      TypeOp::AddPointer,      1, 1  },
    { "make_signed",      TypeOp::MakeSigned,      1, 1, OverloadPolicy::Allowed },
    { "make_unsigned",    TypeOp::MakeUnsigned,    1, 1, OverloadPolicy::Allowed },
    { "element_type",     TypeOp::ElementType,     1, 1, OverloadPolicy::Allowed },
    { "underlying_type",  TypeOp::UnderlyingType,  1, 1, OverloadPolicy::Allowed },
    { "return_type",      TypeOp::ReturnType,      1, 1, OverloadPolicy::Allowed },
    { "common_type",      TypeOp::CommonType,      1, -1, OverloadPolicy::Allowed },
    { "conditional",      TypeOp::Conditional,     2, 2  },
    { "promote",          TypeOp::Promote,         1, 1, OverloadPolicy::Allowed },
    { "demote",           TypeOp::Demote,          1, 1, OverloadPolicy::Allowed },
    { "int_from_float",   TypeOp::IntFromFloat,    1, 1, OverloadPolicy::Allowed },
    { "float_from_int",   TypeOp::FloatFromInt,    1, 1, OverloadPolicy::Allowed },
};

inline const TypeOpInfo* find_type_op(std::string_view name) {
    for (const TypeOpInfo& t : kTypeOps) if (t.name == name) return &t;
    return nullptr;
}

inline const TypeOpInfo* find_builtin_type_op(std::string_view spelling) {
    if (spelling.size() <= 2 || spelling.substr(0, 2) != "__") return nullptr;
    return find_type_op(spelling.substr(2));
}

inline std::string_view type_op_name(TypeOp op) {
    switch (op) {
        case TypeOp::Typeof:       return "typeof";
        case TypeOp::Decltype:     return "decltype";
        case TypeOp::DiscardConst: return "discard_const";
        default: break;
    }

    for (const TypeOpInfo& t : kTypeOps) if (t.op == op) return t.name;
    return "type operation";
}

enum class TypeTrait : std::uint8_t {
    IsSame = 0,
    IsConst,
    IsVolatile,
    IsRef,
    IsLvalueRef,
    IsRvalueRef,
    IsPointer,
    IsArray,
    IsInteger,
    IsFloating,
    IsArithmetic,
    IsSigned,
    IsUnsigned,
    IsBool,
    IsChar,
    IsString,
    IsEnum,
    IsClass,
    IsVoid,
    IsFunction,
    IsDynamic,
    IsConvertible,
    IsBaseOf,
    IntegerRank,
    FloatRank,
    IsText,
    IsStr,
    IsNullPointer,
    IsSpecial,
    IsVariant,
    IsUnion,
    IsStruct,
    IsClosure,
    IsFixedArray,
    IsRuntimeArray,
    IsAbstract,
    IsPolymorphic,
    IsEmpty,
    IsNative,
    IntegerBits,
    FloatBits,
    FloatMantissaBits,
    FloatExponentBits,
    ArrayExtent,
    IsConstructible,
    IsNothrowConstructible,
    IsConstexprConstructible,
    IsAssignable,
    IsNothrowAssignable,
    IsConstexprAssignable,
    IsDestructible,
    IsNothrowDestructible
};

struct TypeTraitInfo {
    std::string_view name;
    TypeTrait        trait;
    int              arity;
    bool             gives_int = false;
    OverloadPolicy   overload  = OverloadPolicy::Never;
};

inline constexpr TypeTraitInfo kTypeTraits[] = {
    { "is_same",        TypeTrait::IsSame,        2 },
    { "is_const",       TypeTrait::IsConst,       1 },
    { "is_volatile",    TypeTrait::IsVolatile,    1 },
    { "is_ref",         TypeTrait::IsRef,         1 },
    { "is_lvalue_ref",  TypeTrait::IsLvalueRef,   1 },
    { "is_rvalue_ref",  TypeTrait::IsRvalueRef,   1 },
    { "is_pointer",     TypeTrait::IsPointer,     1, false, OverloadPolicy::Warns },
    { "is_array",       TypeTrait::IsArray,       1, false, OverloadPolicy::Warns },
    { "is_integer",     TypeTrait::IsInteger,     1, false, OverloadPolicy::Allowed },
    { "is_float",       TypeTrait::IsFloating,    1, false, OverloadPolicy::Allowed },
    { "is_floating",    TypeTrait::IsFloating,    1, false, OverloadPolicy::Allowed },
    { "is_arithmetic",  TypeTrait::IsArithmetic,  1, false, OverloadPolicy::Allowed },
    { "is_signed",      TypeTrait::IsSigned,      1, false, OverloadPolicy::Allowed },
    { "is_unsigned",    TypeTrait::IsUnsigned,    1, false, OverloadPolicy::Allowed },
    { "is_bool",        TypeTrait::IsBool,        1, false, OverloadPolicy::Warns },
    { "is_char",        TypeTrait::IsChar,        1, false, OverloadPolicy::Warns },
    { "is_string",      TypeTrait::IsString,      1, false, OverloadPolicy::Allowed },
    { "is_enum",        TypeTrait::IsEnum,        1 },
    { "is_class",       TypeTrait::IsClass,       1, false, OverloadPolicy::Warns },
    { "is_void",        TypeTrait::IsVoid,        1 },
    { "is_function",    TypeTrait::IsFunction,    1, false, OverloadPolicy::Warns },
    { "is_dynamic",     TypeTrait::IsDynamic,     1 },
    { "is_convertible", TypeTrait::IsConvertible, 2 },
    { "is_base_of",     TypeTrait::IsBaseOf,      2 },
    { "integer_rank",   TypeTrait::IntegerRank,   1, true, OverloadPolicy::Allowed },
    { "float_rank",     TypeTrait::FloatRank,     1, true, OverloadPolicy::Allowed },
    { "is_text",        TypeTrait::IsText,        1, false, OverloadPolicy::Allowed },
    { "is_str",         TypeTrait::IsStr,         1, false, OverloadPolicy::Allowed },
    { "is_null_pointer", TypeTrait::IsNullPointer, 1 },
    { "is_special",     TypeTrait::IsSpecial,     1 },
    { "is_variant",     TypeTrait::IsVariant,     1 },
    { "is_union",       TypeTrait::IsUnion,       1 },
    { "is_struct",      TypeTrait::IsStruct,      1 },
    { "is_closure",     TypeTrait::IsClosure,     1 },
    { "is_fixed_array", TypeTrait::IsFixedArray,  1 },
    { "is_runtime_array", TypeTrait::IsRuntimeArray, 1 },
    { "is_abstract",    TypeTrait::IsAbstract,    1 },
    { "is_polymorphic", TypeTrait::IsPolymorphic, 1 },
    { "is_empty",       TypeTrait::IsEmpty,       1 },
    { "is_native",      TypeTrait::IsNative,      1 },
    { "integer_bits",   TypeTrait::IntegerBits,   1, true, OverloadPolicy::Allowed },
    { "float_bits",     TypeTrait::FloatBits,     1, true, OverloadPolicy::Allowed },
    { "float_mantissa_bits", TypeTrait::FloatMantissaBits, 1, true, OverloadPolicy::Allowed },
    { "float_exponent_bits", TypeTrait::FloatExponentBits, 1, true, OverloadPolicy::Allowed },
    { "array_extent",   TypeTrait::ArrayExtent,   1, true },
    { "is_constructible",           TypeTrait::IsConstructible,          -1 },
    { "is_nothrow_constructible",   TypeTrait::IsNothrowConstructible,   -1 },
    { "is_constexpr_constructible", TypeTrait::IsConstexprConstructible, -1 },
    { "is_assignable",              TypeTrait::IsAssignable,             2 },
    { "is_nothrow_assignable",      TypeTrait::IsNothrowAssignable,      2 },
    { "is_constexpr_assignable",    TypeTrait::IsConstexprAssignable,    2 },
    { "is_destructible",            TypeTrait::IsDestructible,           1 },
    { "is_nothrow_destructible",    TypeTrait::IsNothrowDestructible,    1 },
};

inline bool is_lifecycle_trait(TypeTrait trait) {
    switch (trait) {
        case TypeTrait::IsConstructible: case TypeTrait::IsNothrowConstructible: case TypeTrait::IsConstexprConstructible:
        case TypeTrait::IsAssignable: case TypeTrait::IsNothrowAssignable: case TypeTrait::IsConstexprAssignable:
        case TypeTrait::IsDestructible: case TypeTrait::IsNothrowDestructible:
            return true;
        default:
            return false;
    }
}

inline const TypeTraitInfo* find_type_trait(std::string_view name) {
    for (const TypeTraitInfo& t : kTypeTraits) if (t.name == name) return &t;
    return nullptr;
}

inline bool type_trait_gives_int(TypeTrait trait) {
    for (const TypeTraitInfo& t : kTypeTraits) if (t.trait == trait) return t.gives_int;
    return false;
}

struct TypeQueryInfo {
    std::string_view name;
    bool             is_trait = false;
    TypeOp           op       = TypeOp::Typeof;
    TypeTrait        trait    = TypeTrait::IsSame;
    OverloadPolicy   overload = OverloadPolicy::Never;
    bool             gives_int = false;
};

inline bool find_type_query(std::string_view name, TypeQueryInfo& out) {
    if (const TypeOpInfo* o = find_type_op(name)) {
        out = TypeQueryInfo{ o->name, false, o->op, TypeTrait::IsSame, o->overload, false };
        return true;
    }

    for (const TypeTraitInfo& t : kTypeTraits) {
        if (t.name != name) continue;
        out = TypeQueryInfo{ t.name, true, TypeOp::Typeof, t.trait, t.overload, t.gives_int };
        return true;
    }

    return false;
}

inline OverloadPolicy type_query_overload_policy(std::string_view name) {
    TypeQueryInfo q;
    return find_type_query(name, q) ? q.overload : OverloadPolicy::Never;
}

inline std::string_view type_trait_name(TypeTrait trait) {
    for (const TypeTraitInfo& t : kTypeTraits) if (t.trait == trait) return t.name;
    return "type trait";
}

} // namespace parser_types
} // namespace walnut

#endif // WALNUT_PARSER_TYPE_OPS_HPP
