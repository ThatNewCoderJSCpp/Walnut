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
    { FQ::Virtual   | FQ::Consteval },
    { FQ::Consteval | FQ::Async     },
    { FQ::Primary   | FQ::Overload  },
};

inline constexpr RequiresRule kFuncRequires[] = {
    { FQ::Override, FQ::Virtual },
};

inline constexpr CrossRule kCrossExclusive[] = {
    { RM::Constexpr, FQ::Async, "constexpr functions cannot be async" }
};

inline int count_bits(std::uint16_t x) { int n = 0; while (x) { x &= x - 1; ++n; } return n; }

inline std::string mod_flag_list(std::uint16_t bits) {
    std::string out;
    for (std::uint16_t b = 1; b; b = std::uint16_t(b << 1)) if (bits & b) { if (!out.empty()) out += ", "; out += RM::flag_name(static_cast<RM::Flag>(b)); }
    return out;
}

inline std::string fq_flag_list(std::uint16_t bits) {
    std::string out;
    for (std::uint16_t b = 1; b; b = std::uint16_t(b << 1)) if (bits & b) { if (!out.empty()) out += ", "; out += FQ::flag_name(static_cast<FQ::Flag>(b)); }
    return out;
}

inline void check_modifier_conflicts(const RM& m, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    for (const auto& g : kModExclusive)
        if (count_bits(m.flags() & g.mask) > 1) SemanticError::conflicting_modifiers(r, site->file_id, site->line, kind, mod_flag_list(m.flags() & g.mask));

    for (const auto& rq : kModRequires)
        if ((m.flags() & rq.flag) && (m.flags() & rq.required) == 0) SemanticError::invalid_modifiers(r, site->file_id, site->line, kind, mod_flag_list(rq.flag), 1);
}

inline void check_qualifier_conflicts(const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    for (const auto& g : kFuncExclusive)
        if (count_bits(q.flags() & g.mask) > 1) SemanticError::conflicting_modifiers(r, site->file_id, site->line, kind, fq_flag_list(q.flags() & g.mask));

    for (const auto& rq : kFuncRequires)
        if ((q.flags() & rq.flag) && (q.flags() & rq.required) == 0) SemanticError::invalid_modifiers(r, site->file_id, site->line, kind, fq_flag_list(rq.flag), 1);
}

inline void check_cross_conflicts(const RM& m, const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    for (const auto& c : kCrossExclusive)
        if ((m.flags() & c.mod_flag) && (q.flags() & c.qual_flag)) SemanticError::conflicting_modifiers(r, site->file_id, site->line, kind, c.why);
}

inline void check_declaration_modifiers(const RM& m, std::uint16_t valid, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    if (std::uint16_t bad = std::uint16_t(m.flags() & ~valid)) SemanticError::invalid_modifiers(r, site->file_id, site->line, kind, mod_flag_list(bad), std::size_t(count_bits(bad)));
    check_modifier_conflicts(m, r, site, kind);
}

}} // namespace walnut::semantics

#endif // WALNUT_SEMA_MODIFIER_RULES_HPP