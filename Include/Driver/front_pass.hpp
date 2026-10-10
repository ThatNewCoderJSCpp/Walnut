#ifndef WALNUT_DRIVER_FRONT_PASS_HPP
#define WALNUT_DRIVER_FRONT_PASS_HPP

#include <vector>
#include <algorithm>
#include <unordered_map>

#include "module_loader.hpp"                

#include "../Common/source_manager.hpp"      
#include "../Common/arena_allocator.hpp"
#include "../Common/error_reporter.hpp"
#include "../Parser/nodes.hpp"

#include "../Semantics/scope.hpp"            
#include "../Semantics/scope_builder.hpp"   
#include "../Semantics/import_resolver.hpp" 

namespace walnut {
namespace driver {

struct UnitFrontResult {
    FileId                              file    = INVALID_FILE;
    nodes::BlockStatement*              ast     = nullptr;
    semantics::Scope*                   root    = nullptr;   
    std::vector<semantics::ExportEntry> exports;             
};

class FrontPass {
public:
    FrontPass(ErrorReporter& reporter, Arena& arena) noexcept : m_reporter(reporter), m_arena(arena) {}

    void run(const std::unordered_map<FileId, ParsedUnit>& units);

    const std::unordered_map<FileId, UnitFrontResult>& results() const noexcept { return m_results; }
    const std::vector<FileId>& order() const noexcept { return m_order; }

    const UnitFrontResult* result(FileId f) const {
        const auto it = m_results.find(f);
        return it == m_results.end() ? nullptr : &it->second;
    }

private:
    ErrorReporter& m_reporter;
    Arena&         m_arena;

    std::unordered_map<FileId, UnitFrontResult> m_results;
    std::vector<FileId>                         m_order;

private:
    void process(const ParsedUnit& unit);
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_FRONT_PASS_HPP