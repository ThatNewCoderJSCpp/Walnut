#ifndef RAW_MODIFIERS_HPP
#define RAW_MODIFIERS_HPP

#include <cstdint>
#include <array>
#include "../Lexer/token_macro.hpp"

namespace walnut {

namespace nodes { struct ASTNode; }

namespace modifiers {

enum class DeclCategory : std::uint8_t {
    Variable = 0,
    Function,
    Array,
    VarArray,
    All
};

class RawModifiers {
public:
    enum Flag : std::uint16_t {
        None        = 0,
        Const       = 1 << 0,     
        Constexpr   = 1 << 1,
        Constinit   = 1 << 2,
        Hoisted     = 1 << 3,
        Inline      = 1 << 4,
        Static      = 1 << 5,
        Hidden      = 1 << 6,
        Local       = 1 << 7,
        Global      = 1 << 8,
        Extern      = 1 << 9,
        Immutable   = 1 << 10,  
        Volatile    = 1 << 11,
        ThreadLocal = 1 << 12, 
        Mutable     = 1 << 13,
        Friend      = 1 << 14   
    };

    static constexpr std::size_t MAX_MODIFIERS = 15;

private:
    std::uint8_t m_count = 0;
    std::uint16_t m_flags = 0;
    std::array<tokenizing::Token::Kind, MAX_MODIFIERS> m_source_order{};

public:
    constexpr RawModifiers() noexcept = default;
    constexpr bool has(Flag f) const noexcept { return (m_flags & f) != 0; }
    constexpr bool empty() const noexcept { return m_flags == 0; }
    constexpr bool is_modified() const noexcept { return m_flags != Flag::None; }
    constexpr std::uint16_t flags() const noexcept { return m_flags; }
    constexpr std::uint8_t count() const noexcept { return m_count; }
    constexpr void clear() noexcept { m_flags = 0; m_count = 0; }

public:
    constexpr bool only_has(std::uint16_t allowed) const noexcept { return (m_flags & ~allowed) == 0; }
    constexpr bool exactly(std::uint16_t required) const noexcept { return m_flags == required; }
    constexpr bool has_all(std::uint16_t required) const noexcept { return (m_flags & required) == required; }
    constexpr bool has_any(std::uint16_t mask) const noexcept { return (m_flags & mask) != 0; }

public:
    constexpr void add(Flag f) noexcept { m_flags |= static_cast<std::uint16_t>(f); }
    constexpr void add_mask(std::uint16_t mask) noexcept { m_flags |= mask; }
    constexpr void remove(Flag f) noexcept { if (f != None) { m_flags &= ~static_cast<std::uint16_t>(f); }}
    constexpr void remove_mask(std::uint16_t mask) noexcept { m_flags &= ~mask; }
    constexpr void add_token(tokenizing::Token::Kind k) noexcept { if (m_count < MAX_MODIFIERS) { m_source_order[m_count++] = k; }}
    constexpr void toggle(Flag f) noexcept { m_flags ^= f; }
    constexpr void set(Flag f, bool enable) noexcept { enable ? add(f) : remove(f); }

public:
    static const char* flag_name(Flag f) noexcept {
        switch (f) {
            case Flag::Const:       return "const";
            case Flag::Constexpr:   return "constexpr";
            case Flag::Constinit:   return "constinit";
            case Flag::Hoisted:     return "hoisted";
            case Flag::Inline:      return "inline";
            case Flag::Static:      return "static";
            case Flag::Hidden:      return "hidden";
            case Flag::Local:       return "local";
            case Flag::Global:      return "global";
            case Flag::Extern:      return "extern";
            case Flag::Immutable:   return "immutable";
            case Flag::Volatile:    return "volatile";
            case Flag::ThreadLocal: return "threadlocal";
            case Flag::Mutable:     return "mutable";
            case Flag::Friend:      return "friend";
            case Flag::None:        return "none";
        }
        return "unknown";
    }

    void write_to(std::ostream& os) const {
        if (empty()) {
            os << "<none> ";
            return;
        }

        static constexpr struct {
            Flag flag;
            const char* text;
        } table[] = {
            { Flag::Const,       "const "       },
            { Flag::Constexpr,   "constexpr "   },
            { Flag::Hoisted,     "hoisted "     },
            { Flag::Inline,      "inline "      },
            { Flag::Static,      "static "      },
            { Flag::Hidden,      "hidden "      },
            { Flag::Local,       "local "       },
            { Flag::Global,      "global "      },
            { Flag::Extern,      "extern "      },
            { Flag::Immutable,   "immutable "   },
            { Flag::Volatile,    "volatile "    },
            { Flag::Mutable,     "mutable "     },
            { Flag::Constinit,   "constinit "   },
            { Flag::ThreadLocal, "threadlocal " },
            { Flag::Friend,      "friend "      }
        };

        for (auto&& entry : table) { if (has(entry.flag)) { os << entry.text; }}
    }

    friend std::ostream& operator<<(std::ostream& os, const RawModifiers& mods) {
        mods.write_to(os);
        return os;
    }
};

struct FunctionQualifiers {
    enum Flag : std::uint16_t {
        None      = 0,
        Const     = 1 << 0,
        LValueRef = 1 << 1,
        RValueRef = 1 << 2,
        Explicit  = 1 << 3,
        Virtual   = 1 << 4,
        Override  = 1 << 5,
        Noexcept  = 1 << 6,
        Consteval = 1 << 7,
        Nodiscard = 1 << 8,
        Async     = 1 << 9,
        Overload  = 1 << 10,
        Mutable   = 1 << 11
    };

private:
    std::uint16_t    m_flags = Flag::None; 
    std::string_view m_nodiscard_message;
    nodes::ASTNode*  m_noexcept_expr = nullptr;

public:
    constexpr FunctionQualifiers() noexcept = default;
    constexpr bool has(Flag f) const noexcept { return (m_flags & f) != 0; }
    constexpr bool empty() const noexcept { return m_flags == 0; }
    constexpr bool is_qualified() const noexcept { return m_flags != Flag::None; }
    constexpr std::uint16_t flags() const noexcept { return m_flags; }
    constexpr void clear() noexcept { m_flags = 0; }
    void set_noexcept_expr(nodes::ASTNode* e) noexcept { m_noexcept_expr = e; add(Flag::Noexcept); }
    nodes::ASTNode* noexcept_expr() const noexcept { return m_noexcept_expr; }
    bool has_conditional_noexcept() const noexcept { return has(Noexcept) && m_noexcept_expr != nullptr; }
    void set_nodiscard(std::string_view msg = {}) noexcept { add(Nodiscard); m_nodiscard_message = msg; }
    std::string_view nodiscard_message() const noexcept { return m_nodiscard_message; }
    bool has_nodiscard_message() const noexcept { return has(Nodiscard) && !m_nodiscard_message.empty(); }

public:
    constexpr bool only_has(std::uint16_t allowed) const noexcept { return (m_flags & ~allowed) == 0; }
    constexpr bool exactly(std::uint16_t required) const noexcept { return m_flags == required; }
    constexpr bool has_all(std::uint16_t required) const noexcept { return (m_flags & required) == required; }
    constexpr bool has_any(std::uint16_t mask)     const noexcept { return (m_flags & mask) != 0; }

public:
    constexpr void add(Flag f) noexcept { m_flags |= static_cast<std::uint16_t>(f); }
    constexpr void add_mask(std::uint16_t mask) noexcept { m_flags |= mask; }
    constexpr void remove(Flag f) noexcept { if (f != Flag::None) { m_flags &= ~static_cast<std::uint16_t>(f); }}
    constexpr void remove_mask(std::uint16_t mask) noexcept { m_flags &= ~mask; }
    constexpr void toggle(Flag f) noexcept { m_flags ^= f; }
    constexpr void set(Flag f, bool enable) noexcept { enable ? add(f) : remove(f); }

public:
    void write_to(std::ostream& os) const {
        if (empty()) {
            os << "none ";
            return;
        }

        static constexpr struct {
            Flag flag;
            const char* text;
        } table[] = {
            { Flag::Const,     "const "     },
            { Flag::Consteval, "consteval " },
            { Flag::Explicit,  "explicit "  },
            { Flag::LValueRef, "& "         },
            { Flag::Noexcept,  "noexcept "  },
            { Flag::Override,  "override "  },
            { Flag::RValueRef, "&& "        },
            { Flag::Virtual,   "virtual "   },
            { Flag::Async,     "async "     },
            { Flag::Overload,  "overload "  },
            { Flag::Mutable,   "mutable "   }
        };

        for (auto&& entry : table) { if (has(entry.flag)) { os << entry.text; }}

        if (has(Flag::Nodiscard)) {
            if (m_nodiscard_message.empty()) { os << "nodiscard "; }
            else { os << "nodiscard(\"" << m_nodiscard_message << "\") "; }
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const FunctionQualifiers& mods) {
        mods.write_to(os);
        return os;
    }

    static const char* flag_name(Flag f) noexcept {
        switch (f) {
            case Flag::Const:     return "const";
            case Flag::Consteval: return "consteval";
            case Flag::Explicit:  return "explicit";
            case Flag::LValueRef: return "&";
            case Flag::Noexcept:  return "noexcept";
            case Flag::Override:  return "override";
            case Flag::RValueRef: return "&&";
            case Flag::Virtual:   return "virtual";
            case Flag::Async:     return "async";
            case Flag::Overload:  return "overload";
            case Flag::Mutable:   return "mutable";
            case Flag::Nodiscard: return "nodiscard";
            case Flag::None:      return "none";
        }
        return "unknown";
    }
};

} // namespace modifiers
} // namespace walnut

#endif // RAW_MODIFIERS_HPP