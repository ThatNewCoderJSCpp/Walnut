#ifndef COMPILER_HPP
#define COMPILER_HPP

#include "Parser/parser.hpp"

#include "Semantics/instantiator.hpp"

#include "Common/error_reporter.hpp"
#include "Common/source_manager_lex.hpp"
#include "Common/compiler_warning.hpp"
#include "Common/scoped_timer.hpp"

#include "Preprocessor/preprocessor.hpp"

#include "Driver/module_loader.hpp"
#include "Driver/front_pass.hpp"
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
    std::vector<std::pair<std::string, std::string>> m_defines;

public:
    explicit CompilerPipeline(
        std::string input,
        std::size_t arena_block_size = 128 * 1024
    )
        : input_file(std::move(input))
        , ast_arena(arena_block_size)
        , sources(ast_arena)
        , reporter(input_file, ErrorOutput::StdErr)
        , warnings(input_file, ErrorOutput::StdErr)
        , types(ast_arena)
        , instantiator(ast_arena, types, reporter)
    {
        reporter.set_sources(&sources);
        warnings.set_sources(&sources);
    }

    const ErrorReporter& get_reporter() const noexcept { return reporter; }
    const WarningReporter& get_warnings() const noexcept { return warnings; }
    const Arena& get_arena() const noexcept { return ast_arena; }
    const SourceManager& get_sources() const noexcept { return sources; }
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
    CompilerPipeline& define_macro(std::string name, std::string value = "1") { m_defines.emplace_back(std::move(name), std::move(value)); return *this; }
    CompilerPipeline& suppress_warnings() { warnings.set_enabled(false); return *this; }

private:
    void run_semantic_passes(const driver::FrontPass& front) {
        if (reporter.has_errors()) return;
        driver::ResolvePass resolver(reporter, ast_arena);
        resolver.run(front);

        if (reporter.has_errors()) return;
        driver::DefinePass definitions(reporter, types);
        definitions.run(front);

        if (reporter.has_errors()) return;
        driver::WalkPass walker(reporter, types, ast_arena, instantiator);
        walker.run(front);
    }

public:
    void compile() {
        ScopedTimer _total("Total runtime", m_report_timing);

        auto main_id = sources.load(input_file);

        if (!main_id) {
            reporter.report(ErrorPhase::Lexer, 0, "Could not open input file: " + input_file);
            reporter.flush();
            throw std::runtime_error("Could not open input file: " + input_file);
        }

        reporter.set_current_file(*main_id);
        warnings.set_current_file(*main_id);
        const char* source = sources.data(*main_id);

    #ifdef WALNUT_DEBUG
        if (m_output_tokens) {
            std::string tokens_path;

            if (!tokens_output_path.empty()) {
                tokens_path = tokens_output_path;
            } else {
                std::filesystem::create_directories("output");
                tokens_path = "output/" + std::filesystem::path(input_file).filename().string() + ".tokens";
            }

            std::filesystem::create_directories(std::filesystem::path(tokens_path).parent_path());
            tokenizing::display_all_tokens(source, tokens_path);
        }
    #endif

        nodes::BlockStatement* ast = nullptr;

        try {
            preprocessing::Preprocessor pp(sources, reporter, warnings, ast_arena);
            for (const auto& dir : m_include_dirs) pp.add_include_dir(dir);
            for (const auto& [name, value] : m_defines) pp.define(name, value);
            driver::ModuleLoader loader(sources, pp, reporter, ast_arena);
            loader.load(*main_id);
            if (const driver::ParsedUnit* root = loader.unit(*main_id)) { ast = root->ast; }
            driver::FrontPass front(reporter, ast_arena);
            front.run(loader.units());
            driver::Linker linker(reporter, ast_arena);
            linker.run(front.results(), loader.file_edges());
            run_semantic_passes(front);
            driver::WalkPass walker(reporter, types, ast_arena, instantiator);
            walker.run(front);
        } catch (...) {}

    #ifdef WALNUT_DEBUG
        if (ast && m_output_ast) {
            std::string ast_path = ast_output_path.empty() ? "output/parsed_tree.txt" : ast_output_path;
            std::filesystem::create_directories(std::filesystem::path(ast_path).parent_path());
            std::ofstream ast_out(ast_path);
            if (ast_out) { ast->print(ast_out, 0); }
        }
    #endif

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