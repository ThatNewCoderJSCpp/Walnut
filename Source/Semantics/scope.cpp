#include "Semantics/scope.hpp"

namespace walnut {
namespace semantics {

Symbol* Scope::declare(Symbol* sym) {
    if (Symbol* existing = find_local(sym->name)) { return existing; }
    sym->owner = this;
    symbols.push_back(sym);
    return nullptr;
}

Symbol* Scope::resolve(std::string_view n) const {
    for (const Scope* s = this; s; s = s->parent) { if (Symbol* found = s->find_local(n)) { return found; }}
    return nullptr;
}

Symbol* Scope::find_member(std::string_view n) const {
    if (Symbol* s = find_local(n)) { return s; }
    for (Scope* b : bases) { if (b) { if (Symbol* s = b->find_member(n)) { return s; }}}
    return nullptr;
}

} // namespace semantics
} // namespace walnut
