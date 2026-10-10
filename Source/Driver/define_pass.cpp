#include "Driver/define_pass.hpp"

namespace walnut {
namespace driver {

void DefinePass::run(const FrontPass& front) {
    for (FileId id : front.order()) {
        const UnitFrontResult* unit = front.result(id);
        if (!unit || !unit->root) { continue; }
        ScopedFile _f(m_reporter, unit->file);
        m_validator.run(unit->root);
    }
}

} // namespace driver
} // namespace walnut
