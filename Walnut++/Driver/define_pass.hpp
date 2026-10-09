#ifndef WALNUT_DRIVER_DEFINE_PASS_HPP
#define WALNUT_DRIVER_DEFINE_PASS_HPP

#include "front_pass.hpp"

#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Semantics/type_impl.hpp"            
#include "../Semantics/definition_validator.hpp"

namespace walnut {
namespace driver {

class DefinePass {
public:
    DefinePass(ErrorReporter& reporter, semantics::TypeContext& types) noexcept : m_reporter(reporter), m_validator(types, reporter) {}

    void run(const FrontPass& front) {
        for (FileId id : front.order()) {
            const UnitFrontResult* unit = front.result(id);
            if (!unit || !unit->root) { continue; }
            ScopedFile _f(m_reporter, unit->file);
            m_validator.run(unit->root);
        }
    }

private:
    ErrorReporter&                 m_reporter;
    semantics::DefinitionValidator m_validator;
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_DEFINE_PASS_HPP