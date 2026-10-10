#include "Parser/modifiers.hpp"

namespace walnut {
namespace modifiers {

const char* RawModifiers::flag_name(Flag f) noexcept {
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

void RawModifiers::write_to(std::ostream& os) const {
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

void FunctionQualifiers::write_to(std::ostream& os) const {
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

const char* FunctionQualifiers::flag_name(Flag f) noexcept {
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

} // namespace modifiers
} // namespace walnut
