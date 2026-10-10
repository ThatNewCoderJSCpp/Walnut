#ifndef WALNUT_DRIVER_LINKER_HPP
#define WALNUT_DRIVER_LINKER_HPP

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cctype>

#include "front_pass.hpp"
#include "module_loader.hpp"

#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Parser/nodes.hpp"

#include "../Semantics/scope.hpp"
#include "../Semantics/symbol.hpp"
#include "../Semantics/resolve.hpp"
#include "../Semantics/import_resolver.hpp"  
#include "../Semantics/semantic_error.hpp"

namespace walnut {
namespace driver {

class Linker {
public:
    Linker(ErrorReporter& reporter, Arena& arena, const SourceManager& manager) noexcept : m_reporter(reporter), m_arena(arena), m_sm(manager) {}

    void run(const std::unordered_map<FileId, UnitFrontResult>& results, const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>& file_edges);

private:
    using IK = nodes::ImportExportItem::Kind;

    struct Pending {
        FileId                                importer;
        const nodes::ImportExportItem*        item;
        const nodes::ImportExportDeclaration* decl;  
    };

    ErrorReporter&       m_reporter;
    Arena&               m_arena;
    const SourceManager& m_sm;
    const std::unordered_map<FileId, UnitFrontResult>*                         m_results    = nullptr;
    const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>* m_file_edges = nullptr;

private:
    const UnitFrontResult* unit(FileId f) const {
        auto it = m_results->find(f);
        return it == m_results->end() ? nullptr : &it->second;
    }

    void report(const Pending& p, std::string_view msg) {
        semantics::SemanticError::import_failure(m_reporter, p.importer, item_line(p), msg);
    }

    std::vector<Pending> collect();

    std::string unit_label(FileId f) const {
        std::string p = m_reporter.display_path(f);
        return p.empty() ? "<unknown unit>" : p;
    }

    static const std::vector<std::string_view>& local_parts(const nodes::ImportExportItem& it);

    static std::vector<std::string_view> export_public_parts(const semantics::ExportEntry& e);

    static std::vector<std::string_view> export_origin_parts(const semantics::ExportEntry& e);

    static std::string join_parts(const std::vector<std::string_view>& parts);

    static std::string lowered(std::string_view s);

    static std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t max);

    static std::string nearest_export(const UnitFrontResult& tgt, const std::vector<std::string_view>& want);

    const semantics::ExportEntry* find_export(const UnitFrontResult& tgt, const std::vector<std::string_view>& name, IK kind);

    semantics::Symbol* resolve_export(const UnitFrontResult& tgt, const semantics::ExportEntry& e, bool& defer);

    bool try_bind(const Pending& p);

    bool bind_named(const Pending& p, const nodes::ImportExportItem& it, semantics::Symbol* ph, const UnitFrontResult& tgt);

    bool bind_file(const Pending& p, semantics::Symbol* ph, const UnitFrontResult& tgt);

    static std::size_t item_line(const Pending& p) { return p.item->line ? p.item->line : p.decl->line; }
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_LINKER_HPP