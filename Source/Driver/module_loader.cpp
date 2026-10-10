#include "Driver/module_loader.hpp"

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
                
            if (auto target = resolve_file(it.source, importer, decl->line)) {
                m_file_edges[importer].emplace(std::string(it.source), *target);
                enqueue(*target);
            }
        }
    }
}

std::optional<FileId> ModuleLoader::resolve_file(std::string_view raw, FileId importer, std::uint32_t line) {
    namespace fs = std::filesystem;
    const std::string path(raw);
    const fs::path base = fs::path(m_sm.absolute_path(importer)).parent_path();
    if (auto id = m_sm.load((base / fs::path(path)).string())) return id;
    if (auto id = m_sm.load(path))                             return id;
    m_reporter.report(ErrorPhase::Loader, importer, line, "cannot open imported file: " + path);
    return std::nullopt;
}

} // namespace driver
} // namespace walnut
