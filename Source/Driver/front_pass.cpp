#include "Driver/front_pass.hpp"

namespace walnut {
namespace driver {

void FrontPass::run(const std::unordered_map<FileId, ParsedUnit>& units) {
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

void FrontPass::process(const ParsedUnit& unit) {
    ScopedFile _f(m_reporter, unit.file);
    semantics::AnalysisContext ctx(m_arena, m_reporter);
    semantics::ScopeBuilder builder(ctx);
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

} // namespace driver
} // namespace walnut
