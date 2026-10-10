#ifndef WALNUT_SEMA_TYPE_IMPL_HPP
#define WALNUT_SEMA_TYPE_IMPL_HPP

#include "type.hpp"
#include "substitution.hpp"

#include "symbol.hpp"
#include "../Parser/Types/type_info.hpp"
#include "../Parser/Types/typename.hpp"
#include "../Parser/Types/dependent_types.hpp"

#include <algorithm>

namespace walnut {
namespace semantics {

inline Type* TypeContext::builtin(
    parser_types::PrimitiveType::BaseKind base,
    parser_types::LengthModifier length,
    bool is_unsigned, bool long_form, CV cv
) {
    return builtin_ranked(base, builtin_width(base, length, long_form), is_unsigned, cv);
}

namespace {

CV cv_from_modifiers(const modifiers::RawModifiers& m) {
    CV cv;
    cv.is_const     = m.has(modifiers::RawModifiers::Const) || m.has(modifiers::RawModifiers::Constexpr);
    cv.is_volatile  = m.has(modifiers::RawModifiers::Volatile);
    cv.is_immutable = m.has(modifiers::RawModifiers::Immutable);
    return cv;
}

Symbol* strip_aliases(Symbol* s) {
    while (s) {
        if (s->is_imported && s->import_target) { s = s->import_target; continue; }

        if (s->kind == SymbolKind::TypeAlias && s->type && s->type->resolved) {
            s = s->type->resolved; continue;
        }

        break;
    }
    return s;
}

} // namespace

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_IMPL_HPP