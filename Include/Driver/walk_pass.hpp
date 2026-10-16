#ifndef WALNUT_DRIVER_WALK_PASS_HPP
#define WALNUT_DRIVER_WALK_PASS_HPP

#include "front_pass.hpp"

#include "../Common/error_reporter.hpp"
#include "../Semantics/type_impl.hpp"
#include "../Semantics/type_walker.hpp"
#include "../Semantics/instantiator.hpp"
#include "../Semantics/name_resolver.hpp"

namespace walnut {
namespace driver {

class WalkPass {
public:
    WalkPass(ErrorReporter& reporter, WarningReporter& warnings, semantics::TypeContext& types, Arena& arena, semantics::Instantiator& inst) noexcept
;

    ~WalkPass();

    WalkPass(const WalkPass&) = delete;
    WalkPass& operator=(const WalkPass&) = delete;

    void run(const FrontPass& front);

private:
    void check_inline_cycles(const FrontPass& front);

    void check_externs(const FrontPass& front);

    std::string native_type_problem(semantics::Type* t, bool is_return, bool cpp);

    bool same_signature(const nodes::FunctionDeclaration* a, const nodes::FunctionDeclaration* b);

private:
    ErrorReporter&             m_reporter;
    WarningReporter&           m_warnings;
    semantics::TypeContext&    m_types;
    semantics::AnalysisContext m_ctx;      
    semantics::ScopeBuilder    m_scopes;
    semantics::NameResolver    m_resolver;
    semantics::Instantiator&   m_inst;
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_WALK_PASS_HPP