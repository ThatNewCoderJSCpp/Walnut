#ifndef WALNUT_DRIVER_RESOLVE_PASS_HPP
#define WALNUT_DRIVER_RESOLVE_PASS_HPP

#include "front_pass.hpp"   

#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Semantics/name_resolver.hpp"

namespace walnut {
namespace driver {

class ResolvePass {
public:
    ResolvePass(ErrorReporter& reporter, Arena& arena) noexcept : m_ctx(arena, reporter), m_reporter(reporter), m_resolver(m_ctx) {}

    void run(const FrontPass& front) {
        for (FileId id : front.order()) {
            const UnitFrontResult* unit = front.result(id);
            if (!unit || !unit->ast || !unit->root) { continue; }
            ScopedFile _f(m_reporter, unit->file);
            m_resolver.run(unit->ast, unit->root);
        }
    }

private:
    semantics::AnalysisContext m_ctx;       
    ErrorReporter&             m_reporter;
    semantics::NameResolver    m_resolver;
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_RESOLVE_PASS_HPP