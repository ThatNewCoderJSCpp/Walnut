#ifndef WALNUT_SEMA_MODIFIER_RULES_HPP
#define WALNUT_SEMA_MODIFIER_RULES_HPP

#include <cstdint>
#include <string>
#include "semantic_error.hpp"
#include "../Parser/nodes.hpp"
#include "../Parser/modifiers.hpp"

namespace walnut {
namespace semantics {

using RM = modifiers::RawModifiers;
using FQ = modifiers::FunctionQualifiers;

struct ExclusiveGroup { std::uint16_t mask; };            
struct RequiresRule   { std::uint16_t flag, required; };  
struct CrossRule      { std::uint16_t mod_flag, qual_flag; const char* why; };

inline constexpr std::uint16_t kValidRecordMods =
    RM::Constexpr | RM::Hoisted | RM::Local | RM::Global | RM::Hidden | RM::Friend;

inline constexpr std::uint16_t kValidFunctionMods =
    RM::Constexpr | RM::Hoisted | RM::Inline | RM::Hidden |
    RM::Local | RM::Global | RM::Extern | RM::Friend | RM::Static;

inline constexpr std::uint16_t kValidVariableMods =
    RM::Const | RM::Constexpr | RM::Constinit | RM::Hoisted | RM::Inline |
    RM::Static | RM::Hidden | RM::Local | RM::Global | RM::Extern |
    RM::Immutable | RM::Volatile | RM::ThreadLocal | RM::Mutable | RM::Friend;

inline constexpr std::uint16_t kValidEnumMods =
    RM::Hoisted | RM::Local | RM::Global | RM::Hidden | RM::Inline;

inline constexpr std::uint16_t kVirtualIntroducers = FQ::Virtual | FQ::Override;

inline constexpr ExclusiveGroup kModExclusive[] = {
    { RM::Const     | RM::Mutable | RM::Immutable },   
    { RM::Local     | RM::Global  | RM::Hidden    },   
    { RM::Static    | RM::Extern                  },   
    { RM::Constexpr | RM::Constinit               },   
    { RM::Constexpr | RM::Volatile                },
    { RM::Constexpr | RM::Mutable                 },
    { RM::Constexpr | RM::ThreadLocal             },
    { RM::Constexpr | RM::Extern                  },
    { RM::Constinit | RM::Mutable                 },
    { RM::Mutable   | RM::Static                  },
    { RM::Mutable   | RM::ThreadLocal             },
    { RM::Mutable   | RM::Extern                  },
    { RM::Immutable | RM::Volatile                },
};

inline constexpr RequiresRule kModRequires[] = {
    { RM::Constinit, RM::Static | RM::ThreadLocal },   
};

inline constexpr ExclusiveGroup kFuncExclusive[] = {
    { FQ::LValueRef | FQ::RValueRef },
    { FQ::Virtual   | FQ::Override  },   
    { FQ::Virtual   | FQ::Consteval },
    { FQ::Override  | FQ::Consteval },   
    { FQ::Consteval | FQ::Async     },
};

inline constexpr std::array<RequiresRule, 0> kFuncRequires{};

inline constexpr CrossRule kCrossExclusive[] = {
    { RM::Constexpr, FQ::Async, "constexpr functions cannot be async" }
};

inline int count_bits(std::uint16_t x) { int n = 0; while (x) { x &= x - 1; ++n; } return n; }

inline constexpr bool is_virtual_function(const FQ& q) noexcept { return q.has_any(kVirtualIntroducers); }

std::string mod_flag_list(std::uint16_t bits);

std::string fq_flag_list(std::uint16_t bits);

void check_modifier_conflicts(const RM& m, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind);

void check_qualifier_conflicts(const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind);

void check_cross_conflicts(const RM& m, const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind);

void check_declaration_modifiers(const RM& m, std::uint16_t valid, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind);

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_MODIFIER_RULES_HPP