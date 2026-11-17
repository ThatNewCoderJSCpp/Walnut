#include "Semantics/prelude.hpp"

namespace walnut {
namespace semantics {

Scope* prelude_scope() {
    static Scope* scope = new Scope(Scope::Kind::Module, nullptr);
    return scope;
}

void reset_prelude_scope() {
    Scope* scope = prelude_scope();
    scope->symbols.clear();
    scope->using_directives.clear();
    scope->bases.clear();
    scope->children.clear();
}

} // namespace semantics
} // namespace walnut
