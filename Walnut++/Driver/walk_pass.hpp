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
    WalkPass(ErrorReporter& reporter, semantics::TypeContext& types, Arena& arena, semantics::Instantiator& inst) noexcept
        : m_reporter(reporter)
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
        for (FileId id : front.order()) {
            const UnitFrontResult* unit = front.result(id);
            if (!unit || !unit->ast || !unit->root) { continue; }
            m_reporter.set_current_file(unit->file);
            semantics::TypeWalker walker(m_types, m_reporter, unit->root, &m_inst);
            walker.run(unit->ast);
        }
    }

private:
    ErrorReporter&             m_reporter;
    semantics::TypeContext&    m_types;
    semantics::AnalysisContext m_ctx;      
    semantics::ScopeBuilder    m_scopes;
    semantics::Instantiator&   m_inst;
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_WALK_PASS_HPP