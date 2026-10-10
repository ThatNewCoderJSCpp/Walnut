#ifndef WALNUT_SEMANTICS_PRELUDE_HPP
#define WALNUT_SEMANTICS_PRELUDE_HPP

#include "scope.hpp"
#include "symbol.hpp"

namespace walnut {
namespace semantics {

enum class Intrinsic : std::uint8_t { None = 0, Print, Println, Input, ToString, ParseInt, ParseFloat };

inline const char* intrinsic_runtime_name(Intrinsic i) {
    switch (i) {
        case Intrinsic::Print:   return "walnut_rt::print";
        case Intrinsic::Println: return "walnut_rt::println";
        case Intrinsic::Input:   return "walnut_rt::input";
        case Intrinsic::ToString:   return "walnut_rt::to_display";
        case Intrinsic::ParseInt:   return "walnut_rt::parse_int";
        case Intrinsic::ParseFloat: return "walnut_rt::parse_float";
        default:                 return "";
    }
}

inline Scope* prelude_scope() {
    static Scope* scope = [] {
        auto* s = new Scope(Scope::Kind::Module, nullptr);
        auto add = [s](const char* name, Intrinsic which) {
            auto* sym = new Symbol(name, SymbolKind::Function, nullptr);
            sym->visibility = Visibility::Global;
            sym->is_hoisted = true;
            sym->intrinsic  = static_cast<std::uint8_t>(which);
            s->declare(sym);
        };
        add("print",   Intrinsic::Print);
        add("println", Intrinsic::Println);
        add("input",   Intrinsic::Input);
        add("to_string",   Intrinsic::ToString);
        add("parse_int",   Intrinsic::ParseInt);
        add("parse_float", Intrinsic::ParseFloat);
        return s;
    }();
    return scope;
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_PRELUDE_HPP
