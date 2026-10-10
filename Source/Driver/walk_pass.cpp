#include "Driver/walk_pass.hpp"

namespace walnut {
namespace driver {

WalkPass::WalkPass(ErrorReporter& reporter, WarningReporter& warnings, semantics::TypeContext& types, Arena& arena, semantics::Instantiator& inst) noexcept : m_reporter(reporter)
    , m_warnings(warnings)
    , m_types(types)
    , m_ctx(arena, reporter)      
    , m_scopes(m_ctx)
    , m_resolver(m_ctx)
    , m_inst(inst)
{
    m_inst.build_scopes = [this](nodes::ASTNode* n, semantics::Scope* parent) {
        m_scopes.build_into(n, parent);
    };
    m_inst.resolve_names = [this](nodes::ASTNode* n, semantics::Scope* scope) {
        m_resolver.resolve_into(n, scope);
    };
}

void WalkPass::run(const FrontPass& front) {
    semantics::ThrowContext throws(m_types);

    for (FileId id : front.order()) {
        const UnitFrontResult* unit = front.result(id);
        if (!unit || !unit->ast || !unit->root) { continue; }
        ScopedFile _f(m_reporter, unit->file);
        ScopedFile _w(m_warnings, unit->file);
        semantics::TypeWalker walker(m_types, m_reporter, m_warnings, unit->root, &m_inst, &throws);
        walker.run(unit->ast);
        walker.type_pending_instances();
    }

    semantics::check_noexcept_contracts(throws, m_types, m_reporter);
}

} // namespace driver
} // namespace walnut
