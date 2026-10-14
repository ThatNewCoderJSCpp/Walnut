#include "Semantics/prelude.hpp"

namespace walnut {
namespace semantics {

Scope* prelude_scope() {
    static Scope* scope = new Scope(Scope::Kind::Module, nullptr);
    return scope;
}

} // namespace semantics
} // namespace walnut
