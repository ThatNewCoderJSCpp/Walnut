#include "Driver/module_loader.hpp"
#include "Common/file_kind.hpp"

namespace walnut {
namespace driver {

void ModuleLoader::load(FileId root) {
    if (root == INVALID_FILE) return;
    enqueue(root);

    while (!m_work.empty()) {
        const FileId f = m_work.back();
        m_work.pop_back();
        process(f);
    }
}

void ModuleLoader::process(FileId f) {
    ScopedFile _f(m_reporter, f);
    nodes::BlockStatement* ast = parse_unit(f);
    m_units.emplace(f, ParsedUnit{ f, ast });
    if (ast) { scan_imports(f, ast); }
}

nodes::BlockStatement* ModuleLoader::parse_unit(FileId f) {
    tokenizing::TokenStream stream = m_pp.preprocess(f);
    parsing::Parser parser(stream, m_reporter, m_arena);
    return parser.parse_program();
}

void ModuleLoader::scan_imports(FileId importer, nodes::BlockStatement* ast) {
    for (nodes::ASTNode* stmt : ast->get_statements()) {
        if (stmt->kind != nodes::ASTNode::Kind::ImportExportDeclaration) continue;
        auto* decl = static_cast<nodes::ImportExportDeclaration*>(stmt);
        if (!decl->is_import()) continue;

        for (const auto& it : decl->get_items()) {
            if (!it.has_source) continue;   
                
            const std::optional<FileId> target = it.from_module_path ? resolve_module(it.module_parts, importer, decl->line) : resolve_file(it.source, importer, decl->line);

            if (target) {
                m_file_edges[importer].emplace(std::string(it.source), *target);
                enqueue(*target);
            }
        }
    }
}

std::optional<FileId> ModuleLoader::resolve_module(const std::vector<std::string_view>& parts, FileId importer, std::uint32_t line) {
    namespace fs = std::filesystem;
    fs::path rel;
    std::string dotted;

    for (std::string_view p : parts) {
        rel /= std::string(p);
        if (!dotted.empty()) dotted += '.';
        dotted += p;
    }

    std::error_code ec;

    for (const std::string& dir : m_module_dirs) {
        for (const char* ext : { ".walh", ".wal" }) {
            fs::path candidate = fs::path(dir) / rel;
            candidate += ext;
            if (fs::is_regular_file(candidate, ec)) if (auto id = m_sm.load(candidate.string())) return id;
        }
    }

    for (const char* ext : { ".walh", ".wal" }) {
        const std::string candidate = std::string(SourceManager::LIBRARY_ROOT) + "/" + rel.generic_string() + ext;
        if (m_sm.has_virtual(candidate)) if (auto id = m_sm.load(candidate)) return id;
    }

    std::string where;
    for (const std::string& dir : m_module_dirs) where += (where.empty() ? "" : ", ") + dir;
    if (where.empty()) where = "(no module folders are set)";
    m_reporter.report(ErrorPhase::Loader, importer, line, "cannot find module '" + dotted + "': looked for '" + rel.generic_string() + ".walh' and '" + rel.generic_string() + ".wal' in " + where);
    return std::nullopt;
}

std::optional<FileId> ModuleLoader::resolve_file(std::string_view raw, FileId importer, std::uint32_t line) {
    namespace fs = std::filesystem;
    const std::string path(raw);

    if (!is_walnut_file(classify_file(path))) {
        m_reporter.report(ErrorPhase::Loader, importer, line, not_walnut_reason(path, "import"));
        return std::nullopt;
    }

    const fs::path base = fs::path(m_sm.absolute_path(importer)).parent_path();
    if (auto id = m_sm.load((base / fs::path(path)).string())) return id;
    if (auto id = m_sm.load(path))                             return id;
    m_reporter.report(ErrorPhase::Loader, importer, line, "cannot open imported file: " + path);
    return std::nullopt;
}

} // namespace driver
} // namespace walnut
