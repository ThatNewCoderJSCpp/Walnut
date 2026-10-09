#ifndef WALNUT_DRIVER_WALK_PASS_HPP
#define WALNUT_DRIVER_WALK_PASS_HPP

#include "front_pass.hpp"

#include "../Common/error_reporter.hpp"
#include "../Semantics/type_impl.hpp"
#include "../Semantics/type_walker.hpp"
#include "../Semantics/instantiator.hpp"

namespace walnut {
namespace driver {

class WalkPass {
public:
    WalkPass(ErrorReporter& reporter, WarningReporter& warnings, semantics::TypeContext& types, Arena& arena, semantics::Instantiator& inst) noexcept
        : m_reporter(reporter)
        , m_warnings(warnings)
        , m_types(types)
        , m_ctx(arena, reporter)      
        , m_scopes(m_ctx)
        , m_inst(inst)
    {
        m_inst.build_scopes = [this](nodes::ASTNode* n, semantics::Scope* parent) {
            m_scopes.build_into(n, parent);
        };
    }

    void run(const FrontPass& front) {
        semantics::ThrowContext throws(m_types);

        for (FileId id : front.order()) {
            const UnitFrontResult* unit = front.result(id);
            if (!unit || !unit->ast || !unit->root) { continue; }
            ScopedFile _f(m_reporter, unit->file);
            ScopedFile _w(m_warnings, unit->file);
            semantics::TypeWalker walker(m_types, m_reporter, m_warnings, unit->root, &m_inst, &throws);
            walker.run(unit->ast);
        }

        semantics::check_noexcept_contracts(throws, m_types, m_reporter);
    }

private:
    ErrorReporter&             m_reporter;
    WarningReporter&           m_warnings;
    semantics::TypeContext&    m_types;
    semantics::AnalysisContext m_ctx;      
    semantics::ScopeBuilder    m_scopes;
    semantics::Instantiator&   m_inst;
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_WALK_PASS_HPP