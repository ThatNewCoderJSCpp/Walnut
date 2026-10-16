#include "Driver/module_loader.hpp"
#include "Common/file_kind.hpp"

namespace walnut {
namespace driver {

void ModuleLoader::load(FileId root) {
    load_all({ root });
}

void ModuleLoader::load_all(const std::vector<FileId>& roots) {
    const std::size_t first = m_order.size();
    for (FileId r : roots) if (r != INVALID_FILE) enqueue(r);

    while (!m_work.empty()) {
        const FileId f = m_work.back();
        m_work.pop_back();
        discover(f);
    }

    for (std::size_t i = first; i < m_order.size(); ++i) {
        const FileId f = m_order[i];
        ScopedFile _f(m_reporter, f);
        nodes::BlockStatement* ast = parse_tokens(f, m_reporter);
        m_units.emplace(f, ParsedUnit{ f, ast });
        if (ast) link_imports(f, ast);
        m_tokens.erase(f);
    }

    if (!m_work.empty()) load_all({});
}

void ModuleLoader::discover(FileId f) {
    ScopedFile _f(m_reporter, f);
    tokenizing::TokenStream stream = m_pp.preprocess(f);
    m_tokens[f].assign(stream.begin(), stream.end());
    m_order.push_back(f);
    const std::vector<tokenizing::Token>& toks = m_tokens[f];
    parsing::Parser::prescan_names(toks.data(), toks.size(), m_names);

    std::vector<tokenizing::Token> copy = toks;
    tokenizing::TokenStream view(copy);
    ErrorReporter quiet;
    parsing::Parser parser(view, quiet, m_arena);

    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].kind() != tokenizing::Token::Kind::ImportKeyword) continue;
        nodes::ASTNode* decl = nullptr;
        try { decl = parser.parse_import_at(i); } catch (...) { decl = nullptr; }
        if (decl && decl->kind == nodes::ASTNode::Kind::ImportExportDeclaration) discover_import(f, static_cast<nodes::ImportExportDeclaration*>(decl));
    }
}

nodes::BlockStatement* ModuleLoader::parse_tokens(FileId f, ErrorReporter& reporter) {
    std::vector<tokenizing::Token> copy = m_tokens[f];
    tokenizing::TokenStream stream(copy);
    parsing::Parser parser(stream, reporter, m_arena);
    parser.share_names(&m_names);
    return parser.parse_program();
}

void ModuleLoader::discover_import(FileId importer, nodes::ImportExportDeclaration* decl) {
    if (!decl->is_import()) return;
    auto& resolved = m_resolved[importer];

    for (const auto& it : decl->get_items()) {
        if (!it.has_source) continue;
        const std::optional<FileId> target = it.from_module_path ? resolve_module(it.module_parts, importer, decl->line) : resolve_file(it.source, importer, decl->line);
        resolved.emplace_back(std::string(it.source), target);
        if (target) enqueue(*target);
    }
}

void ModuleLoader::link_imports(FileId importer, nodes::BlockStatement* ast) {
    const auto found = m_resolved.find(importer);

    for (nodes::ASTNode* stmt : ast->get_statements()) {
        if (stmt->kind != nodes::ASTNode::Kind::ImportExportDeclaration) continue;
        auto* decl = static_cast<nodes::ImportExportDeclaration*>(stmt);
        if (!decl->is_import()) continue;

        for (const auto& it : decl->get_items()) {
            if (!it.has_source) continue;
            std::optional<FileId> target;
            bool known = false;

            if (found != m_resolved.end()) {
                for (const auto& [src, id] : found->second) {
                    if (src == it.source) { target = id; known = true; break; }
                }
            }

            if (!known) {
                target = it.from_module_path ? resolve_module(it.module_parts, importer, decl->line) : resolve_file(it.source, importer, decl->line);
                if (target && m_seen.insert(*target).second) {
                    m_work.push_back(*target);
                }
            }

            if (target) {
                if (it.from_module_path && m_sm.absolute_path(*target).rfind(SourceManager::LIBRARY_ROOT, 0) == 0) const_cast<nodes::ImportExportItem&>(it).library_module = true;
                m_file_edges[importer].emplace(std::string(it.source), *target);
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
