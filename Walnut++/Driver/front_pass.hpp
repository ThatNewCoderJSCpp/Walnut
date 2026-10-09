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

    void run(const std::unordered_map<FileId, ParsedUnit>& units) {
        std::vector<FileId> order;
        order.reserve(units.size());
        for (const auto& kv : units) order.push_back(kv.first);
        std::sort(order.begin(), order.end());

        for (FileId id : order) {
            const ParsedUnit& unit = units.at(id);
            if (!unit.ast) continue;          
            process(unit);
        }
    }

    const std::unordered_map<FileId, UnitFrontResult>& results() const noexcept { return m_results; }
    const std::vector<FileId>& order() const noexcept { return m_order; }

    const UnitFrontResult* result(FileId f) const {
        const auto it = m_results.find(f);
        return it == m_results.end() ? nullptr : &it->second;
    }

    void enforce_primary_constructor(bool enable) { m_enforce_prim = enable; }

private:
    ErrorReporter& m_reporter;
    Arena&         m_arena;

    bool m_enforce_prim = false;

    std::unordered_map<FileId, UnitFrontResult> m_results;
    std::vector<FileId>                         m_order;

private:
    void process(const ParsedUnit& unit) {
        ScopedFile _f(m_reporter, unit.file);
        semantics::AnalysisContext ctx(m_arena, m_reporter);
        semantics::ScopeBuilder builder(ctx);
        if (m_enforce_prim) { builder.set_enforce_primary(true); }
        semantics::Scope* root = builder.build(unit.ast);
        semantics::ImportResolver resolver(ctx);
        resolver.run(unit.ast);
        UnitFrontResult res;
        res.file    = unit.file;
        res.ast     = unit.ast;
        res.root    = root;
        res.exports = resolver.exports();
        m_results.emplace(unit.file, std::move(res));
        m_order.push_back(unit.file);
    }
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_FRONT_PASS_HPP