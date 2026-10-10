#ifndef WALNUT_DRIVER_MODULE_LOADER_HPP
#define WALNUT_DRIVER_MODULE_LOADER_HPP

#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include "../Common/source_manager.hpp"
#include "../Common/error_reporter.hpp"
#include "../Common/arena_allocator.hpp"
#include "../Preprocessor/preprocessor.hpp"   
#include "../Parser/Parsing/base.hpp"            
#include "../Parser/nodes.hpp"

namespace walnut {
namespace driver {

struct ParsedUnit {
    FileId                 file = INVALID_FILE;
    nodes::BlockStatement* ast  = nullptr;  
};

class ModuleLoader {
public:
    ModuleLoader(
        SourceManager& sm,
        preprocessing::Preprocessor& pp,
        ErrorReporter& reporter,
        Arena& arena
    ) noexcept : m_sm(sm), m_pp(pp), m_reporter(reporter), m_arena(arena) {}

    void load(FileId root);

    const std::unordered_map<FileId, ParsedUnit>& units() const noexcept { return m_units; }
    const SourceManager& source_manager() const { return m_sm; }

    const ParsedUnit* unit(FileId f) const {
        const auto it = m_units.find(f);
        return it == m_units.end() ? nullptr : &it->second;
    }

    const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>& file_edges() const noexcept { return m_file_edges; }

private:
    SourceManager&               m_sm;
    preprocessing::Preprocessor& m_pp;
    ErrorReporter&               m_reporter;
    Arena&                       m_arena;

    std::unordered_set<FileId>             m_seen;   
    std::vector<FileId>                    m_work;   
    std::unordered_map<FileId, ParsedUnit> m_units;  
    std::unordered_map<FileId, std::unordered_map<std::string, FileId>> m_file_edges;

private:
    void enqueue(FileId f) {
        if (f == INVALID_FILE) return;
        if (m_seen.insert(f).second) m_work.push_back(f);   
    }

    void process(FileId f);

    nodes::BlockStatement* parse_unit(FileId f);

    void scan_imports(FileId importer, nodes::BlockStatement* ast);

    std::optional<FileId> resolve_file(std::string_view raw, FileId importer, std::uint32_t line);
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_MODULE_LOADER_HPP