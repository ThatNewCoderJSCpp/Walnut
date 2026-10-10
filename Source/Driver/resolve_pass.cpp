#include "Driver/resolve_pass.hpp"

namespace walnut {
namespace driver {

void ResolvePass::run(const FrontPass& front) {
    for (FileId id : front.order()) {
        const UnitFrontResult* unit = front.result(id);
        if (!unit || !unit->ast || !unit->root) { continue; }
        ScopedFile _f(m_reporter, unit->file);
        m_resolver.run(unit->ast, unit->root);
    }
}

} // namespace driver
} // namespace walnut
