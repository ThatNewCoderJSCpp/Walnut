#ifndef WALNUT_SEMANTICS_PRELUDE_HPP
#define WALNUT_SEMANTICS_PRELUDE_HPP

#include "scope.hpp"
#include "symbol.hpp"

namespace walnut {
namespace semantics {

enum class Intrinsic : std::uint8_t { None = 0, Print, Println, Input, ToString, ParseInt, ParseFloat };

const char* intrinsic_runtime_name(Intrinsic i);

Scope* prelude_scope();

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_PRELUDE_HPP
