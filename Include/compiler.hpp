#ifndef COMPILER_HPP
#define COMPILER_HPP

#include "Parser/parser.hpp"

#include "Semantics/instantiator.hpp"

#include "Common/error_reporter.hpp"
#include "Common/source_manager_lex.hpp"
#include "Common/compiler_warning.hpp"
#include "Common/scoped_timer.hpp"
#include "Common/file_kind.hpp"

#include "Preprocessor/preprocessor.hpp"

#include "Driver/module_loader.hpp"
#include "Driver/front_pass.hpp"
#include "Semantics/scope_builder.hpp"
#include "Driver/linker.hpp"
#include "Driver/resolve_pass.hpp"
#include "Driver/define_pass.hpp"
#include "Driver/walk_pass.hpp" 

#include <fstream>
#include <string>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <chrono>

namespace walnut {

inline void merge_library_scope(semantics::Scope* into, semantics::Scope* from) {
    for (semantics::Symbol* sym : from->symbols) {
        if (!sym || sym->visibility == semantics::Visibility::Hidden) continue;
        semantics::Symbol* have = into->find_local(sym->name);
        if (!have) { into->symbols.push_back(sym); continue; }
        if (have == sym) continue;
        const bool namespaces = have->kind == semantics::SymbolKind::Namespace && sym->kind == semantics::SymbolKind::Namespace;
        if (namespaces && have->inner_scope && sym->inner_scope) merge_library_scope(have->inner_scope, sym->inner_scope);
    }
}

class CompilerPipeline {
private:
    std::string input_file;
    Arena ast_arena;         
    SourceManager sources;    
    ErrorReporter reporter;
    WarningReporter warnings;
    semantics::TypeContext types;
    semantics::Instantiator instantiator;

    bool m_output_tokens = false;
    bool m_output_ast = false;
    std::string tokens_output_path;  
    std::string ast_output_path;

    bool m_report_timing = false;
    std::vector<std::string> m_include_dirs{ "include" };
    std::vector<std::string> m_module_dirs;
    std::vector<std::pair<std::string, std::string>> m_defines;
    std::vector<std::string> m_extra_inputs;
    std::function<std::vector<std::string>(bool)> m_native_args;

public:
    struct AnalyzedUnit {
        FileId                 file = INVALID_FILE;
        nodes::BlockStatement* ast  = nullptr;
        semantics::Scope*      root = nullptr;
    };

private:
    std::vector<AnalyzedUnit> m_units;
    FileId                    m_main_file = INVALID_FILE;
    std::unordered_map<FileId, std::unordered_map<std::string, FileId>> m_file_imports;

public:
    explicit CompilerPipeline(
        std::string input,
        std::size_t arena_block_size = 128 * 1024
    )
        : input_file(std::move(input))
        , ast_arena(arena_block_size)
        , sources(ast_arena)
        , reporter(ErrorOutput::StdErr)
        , warnings(ErrorOutput::StdErr)
        , types(ast_arena)
        , instantiator(ast_arena, types, reporter, warnings)
    {
        reporter.set_sources(&sources);
        warnings.set_sources(&sources);
    }

    const ErrorReporter& get_reporter() const noexcept { return reporter; }
    const WarningReporter& get_warnings() const noexcept { return warnings; }
    const Arena& get_arena() const noexcept { return ast_arena; }
    const SourceManager& get_sources() const noexcept { return sources; }
    const std::vector<AnalyzedUnit>& analyzed_units() const noexcept { return m_units; }
    const std::unordered_map<FileId, std::unordered_map<std::string, FileId>>& file_imports() const noexcept { return m_file_imports; }
    FileId main_file() const noexcept { return m_main_file; }
    semantics::TypeContext& mutable_types() noexcept { return types; }
    semantics::Instantiator& mutable_instantiator() noexcept { return instantiator; }
    ErrorReporter& error_reporter() noexcept { return reporter; }
    WarningReporter& warning_reporter() noexcept { return warnings; }
    const semantics::TypeContext& get_types() const noexcept { return types; }
    const semantics::Instantiator& get_instantiator() const noexcept { return instantiator; }

public:
    CompilerPipeline& enable_token_output() {
        m_output_tokens = true;
        return *this;
    }

    CompilerPipeline& output_tokens(std::string path) {
        m_output_tokens = true;
        tokens_output_path = std::move(path);
        return *this;
    }

    CompilerPipeline& enable_ast_output() {
        m_output_ast = true;
        return *this;
    }

    CompilerPipeline& output_ast(std::string path) {
        m_output_ast = true;
        ast_output_path = std::move(path);
        return *this;
    }

    CompilerPipeline& report_to_stderr() {
        reporter.set_mode(reporter.mode() | ErrorOutput::StdErr);
        return *this;
    }

    CompilerPipeline& warn_to_stderr() {
        warnings.set_mode(warnings.mode() | ErrorOutput::StdErr);
        return *this;
    }

    CompilerPipeline& report_to_file(const std::string& path = "output/errors.txt") {
        if (reporter.mode() & ErrorOutput::File) { std::cerr << "Warning: overwriting previous error output path\n"; }
        std::filesystem::path p(path);
        if (p.has_parent_path()) { std::filesystem::create_directories(p.parent_path()); }
        reporter.set_output_path(path);
        reporter.set_mode(reporter.mode() | ErrorOutput::File);
        return *this;
    }

    CompilerPipeline& warn_to_file(const std::string& path = "output/warnings.txt") {
        if (warnings.mode() & ErrorOutput::File) { std::cerr << "Warning: overwriting previous warning output path\n"; }
        std::filesystem::path p(path);
        if (p.has_parent_path()) { std::filesystem::create_directories(p.parent_path()); }
        warnings.set_output_path(path);
        warnings.set_mode(warnings.mode() | ErrorOutput::File);
        return *this;
    }

    CompilerPipeline& report_silent() {
        reporter.set_mode(ErrorOutput::Silent);
        return *this;
    }

    CompilerPipeline& warn_silent() {
        warnings.set_mode(ErrorOutput::Silent);
        return *this;
    }

    CompilerPipeline& set_path_style(PathStyle style) {
        reporter.set_path_style(style);
        warnings.set_path_style(style);
        return *this;
    }

    CompilerPipeline& report_timing(bool on = true) { m_report_timing = on; return *this; }
    CompilerPipeline& add_include_dir(std::string dir) { m_include_dirs.push_back(std::move(dir)); return *this; }
    CompilerPipeline& add_module_dir(std::string dir) { m_module_dirs.push_back(std::move(dir)); return *this; }
    CompilerPipeline& add_library_file(const std::string& path, std::string content) { sources.add_virtual(std::string(SourceManager::LIBRARY_ROOT) + "/" + path, std::move(content)); return *this; }
    CompilerPipeline& define_macro(std::string name, std::string value = "1") { m_defines.emplace_back(std::move(name), std::move(value)); return *this; }
    CompilerPipeline& suppress_warnings() { warnings.set_enabled(false); return *this; }
    CompilerPipeline& add_input(std::string path) { m_extra_inputs.push_back(std::move(path)); return *this; }
    CompilerPipeline& set_native_header_args(std::function<std::vector<std::string>(bool)> provider) { m_native_args = std::move(provider); return *this; }
    CompilerPipeline& set_input_text(std::string text) { sources.add_virtual(std::filesystem::path(input_file).lexically_normal().generic_string(), std::move(text)); return *this; }
    std::optional<FileId> load_input() { return sources.load(input_file); }
    bool had_front_end_errors() const { return front_end_failed(); }

private:
    bool front_end_failed() const {
        return reporter.count(ErrorPhase::Lexer) || reporter.count(ErrorPhase::Parser)
            || reporter.count(ErrorPhase::Preprocessor) || reporter.count(ErrorPhase::Loader);
    }

    void run_semantic_passes(const driver::FrontPass& front) {
        if (front_end_failed()) return;
        driver::ResolvePass resolver(reporter, ast_arena);
        resolver.run(front);

        driver::DefinePass definitions(reporter, types);
        definitions.run(front);

        driver::WalkPass walker(reporter, warnings, types, ast_arena, instantiator);
        walker.run(front);
    }

    std::vector<std::string> m_missing_native;

    void check_native_files(const std::vector<nodes::ASTNode*>& stmts) {
        namespace fs = std::filesystem;

        for (nodes::ASTNode* s : stmts) {
            if (!s) continue;

            if (s->kind == nodes::ASTNode::Kind::NamespaceDeclaration) {
                auto* ns = static_cast<nodes::NamespaceDeclaration*>(s);
                if (ns->get_body()) check_native_files(ns->get_body()->statements);
                continue;
            }

            const nodes::ASTNode* f = s;
            if (f->kind == nodes::ASTNode::Kind::TemplateDeclaration) f = static_cast<const nodes::TemplateDeclaration*>(f)->get_declaration();
            if (!f) continue;
            const modifiers::RawModifiers* found = nullptr;
            if (f->kind == nodes::ASTNode::Kind::FunctionDeclaration) found = &static_cast<const nodes::FunctionDeclaration*>(f)->get_modifiers();
            else if (f->kind == nodes::ASTNode::Kind::RecordDeclaration && static_cast<const nodes::RecordDeclaration*>(f)->m_native) found = &static_cast<const nodes::RecordDeclaration*>(f)->m_mods;
            if (!found) continue;
            const modifiers::RawModifiers& mods = *found;
            if (!mods.native_file_count() || FileId(f->file_id) >= sources.file_count()) continue;
            const fs::path base = fs::path(sources.absolute_path(FileId(f->file_id))).parent_path();

            for (std::uint8_t i = 0; i < mods.native_file_count(); ++i) {
                const std::string file(mods.native_file(i));
                if (is_system_header(file)) continue;
                std::error_code ec;
                if (fs::is_regular_file(base / file, ec)) continue;
                const std::string key = (base / file).generic_string();
                if (std::find(m_missing_native.begin(), m_missing_native.end(), key) != m_missing_native.end()) continue;
                m_missing_native.push_back(key);
                reporter.report(ErrorPhase::Semantic, FileId(f->file_id), f->line, "Extern Error on line " + std::to_string(f->line) + ": cannot find '" + file + "', named in 'from'; the path is relative to the folder of the file that names it (" + base.generic_string() + ")");
            }
        }
    }

    void report_internal_error(const std::string& what) {
        reporter.report(ErrorPhase::Semantic, INVALID_FILE, 0, "Internal compiler error: " + what);
    }

public:
    void compile() {
        ScopedTimer _total("Total runtime", m_report_timing);

        auto main_id = sources.load(input_file);

        if (!main_id) {
            reporter.report(ErrorPhase::Lexer, INVALID_FILE, "Could not open input file: " + input_file);
            reporter.flush();
            throw std::runtime_error("Could not open input file: " + input_file);
        }

        reporter.set_current_file(*main_id);
        warnings.set_current_file(*main_id);
        const char* source = sources.data(*main_id);

        if (m_output_tokens) {
            std::string tokens_path;

            if (!tokens_output_path.empty()) {
                tokens_path = tokens_output_path;
            } else {
                std::filesystem::create_directories("output");
                tokens_path = "output/" + std::filesystem::path(input_file).filename().string() + ".tokens";
            }

            const std::filesystem::path tokens_dir = std::filesystem::path(tokens_path).parent_path();
            if (!tokens_dir.empty()) std::filesystem::create_directories(tokens_dir);
            tokenizing::display_all_tokens(source, tokens_path);
        }

        nodes::BlockStatement* ast = nullptr;

        try {
            preprocessing::Preprocessor pp(sources, reporter, warnings, ast_arena);
            for (const auto& dir : m_include_dirs) pp.add_include_dir(dir);
            for (const auto& [name, value] : m_defines) pp.define(name, value);
            if (m_native_args) pp.set_native_args(m_native_args);
            driver::ModuleLoader loader(sources, pp, reporter, ast_arena);
            for (const auto& dir : m_module_dirs) loader.add_module_dir(dir);
            std::vector<FileId> roots{ *main_id };

            for (const std::string& extra : m_extra_inputs) {
                auto extra_id = sources.load(extra);
                if (!extra_id) { reporter.report(ErrorPhase::Loader, INVALID_FILE, 0, "Could not open input file: " + extra); continue; }
                roots.push_back(*extra_id);
            }
            std::optional<FileId> core_id;

            const std::string core_path = std::string(SourceManager::LIBRARY_ROOT) + "/core.walh";

            if (sources.has_virtual(core_path)) {
                core_id = sources.load(core_path);
                if (core_id) roots.push_back(*core_id);
            }

            loader.load_all(roots);
            m_file_imports = loader.file_edges();

            if (const driver::ParsedUnit* root = loader.unit(*main_id)) { ast = root->ast; }
            driver::FrontPass front(reporter, ast_arena);
            front.run(loader.units());

            std::vector<FileId> library_units;
            if (core_id) library_units.push_back(*core_id);

            for (const auto& [id, unit] : loader.units()) {
                if (core_id && id == *core_id) continue;
                if (sources.absolute_path(id).rfind(SourceManager::LIBRARY_ROOT, 0) == 0) library_units.push_back(id);
            }

            for (FileId id : library_units) {
                if (const driver::UnitFrontResult* lib = front.result(id); lib && lib->root) merge_library_scope(semantics::prelude_scope(), lib->root);
            }
            semantics::attach_orphan_specializations(reporter);
            m_units.clear();
            m_main_file = *main_id;
            for (FileId id : front.order()) {
                if (const driver::UnitFrontResult* r = front.result(id)) m_units.push_back(AnalyzedUnit{ r->file, r->ast, r->root });
            }
            driver::Linker linker(reporter, ast_arena, loader.source_manager());
            linker.run(front.results(), loader.file_edges());
            run_semantic_passes(front);
            for (const AnalyzedUnit& u : m_units) if (u.ast) check_native_files(u.ast->statements);
        } catch (const parsing::ParserError&) {
        } catch (const std::exception& e) {
            report_internal_error(e.what());
        } catch (...) {
            report_internal_error("unknown exception");
        }

        if (ast && m_output_ast) {
            std::string ast_path = ast_output_path.empty() ? "output/parsed_tree.txt" : ast_output_path;
            const std::filesystem::path ast_dir = std::filesystem::path(ast_path).parent_path();
            if (!ast_dir.empty()) std::filesystem::create_directories(ast_dir);
            std::ofstream ast_out(ast_path);
            if (ast_out) { ast->print(ast_out, 0); }
        }

        warnings.flush();

        if (reporter.has_errors()) {
            reporter.flush();
            const std::size_t num_errors = reporter.error_count();
            const std::string num_err_str = std::to_string(num_errors);

            throw std::runtime_error(
                "Compilation failed with " + num_err_str + (num_errors == 1 ? " error" : " errors")
            );
        }
    }
};

} // namespace walnut

#endif // COMPILER_HPP