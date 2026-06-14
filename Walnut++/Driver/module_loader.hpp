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

    void load(FileId root) {
        if (root == INVALID_FILE) return;
        enqueue(root);

        while (!m_work.empty()) {
            const FileId f = m_work.back();
            m_work.pop_back();
            process(f);
        }
    }

    const std::unordered_map<FileId, ParsedUnit>& units() const noexcept { return m_units; }

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

    void process(FileId f) {
        nodes::BlockStatement* ast = parse_unit(f);
        m_units.emplace(f, ParsedUnit{ f, ast });
        if (ast) { scan_imports(f, ast); }
    }

    nodes::BlockStatement* parse_unit(FileId f) {
        tokenizing::TokenStream stream = m_pp.preprocess(f);
        parsing::Parser parser(stream, m_reporter, m_arena);
        return parser.parse_program();
    }

    void scan_imports(FileId importer, nodes::BlockStatement* ast) {
        for (nodes::ASTNode* stmt : ast->get_statements()) {
            if (stmt->kind != nodes::ASTNode::Kind::ImportExportDeclaration) continue;
            auto* decl = static_cast<nodes::ImportExportDeclaration*>(stmt);
            if (!decl->is_import()) continue;

            for (const auto& it : decl->get_items()) {
                if (!it.has_source) continue;   
                
                if (auto target = resolve_file(it.source, importer, decl->line)) {
                    m_file_edges[importer].emplace(std::string(it.source), *target);
                    enqueue(*target);
                }
            }
        }
    }

    std::optional<FileId> resolve_file(std::string_view raw, FileId importer, std::uint32_t line) {
        namespace fs = std::filesystem;
        const std::string path(raw);
        const fs::path base = fs::path(m_sm.absolute_path(importer)).parent_path();
        if (auto id = m_sm.load((base / fs::path(path)).string())) return id;
        if (auto id = m_sm.load(path))                             return id;
        m_reporter.report(ErrorPhase::Loader, line, "cannot open imported file: " + path);
        return std::nullopt;
    }
};

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_MODULE_LOADER_HPP