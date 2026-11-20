#ifndef WALNUT_CLI_HPP
#define WALNUT_CLI_HPP

#include "compiler.hpp"
#include "Codegen/cpp_emitter.hpp"
#include "Common/file_kind.hpp"
#include "Driver/build_cache.hpp"
#include "Driver/libraries.hpp"
#include "Driver/toolchain.hpp"
#include "Semantics/compile_time_random.hpp"
#include "Driver/highlight.hpp"
#include "Preprocessor/native_headers.hpp"
#include "Lexer/special_names.hpp"
#include <algorithm>
#include <optional>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>

#include "walnut_library.hpp"
#if __has_include("walnut_config.hpp")
#include "walnut_config.hpp"
#endif

#ifndef WALNUT_DEFAULT_CXX
#define WALNUT_DEFAULT_CXX "c++"
#endif

#ifndef WALNUT_RUNTIME_DIR
#define WALNUT_RUNTIME_DIR "Runtime"
#endif

#ifndef WALNUT_RUNTIME_INCLUDES
#define WALNUT_RUNTIME_INCLUDES ""
#endif

#ifndef WALNUT_RUNTIME_LIBS
#define WALNUT_RUNTIME_LIBS ""
#endif

#ifndef WALNUT_RUNTIME_OPTIONAL_LIBS
#define WALNUT_RUNTIME_OPTIONAL_LIBS ""
#endif

#ifndef WALNUT_RUNTIME_OPTIONAL_ARCHIVES
#define WALNUT_RUNTIME_OPTIONAL_ARCHIVES ""
#endif

#ifndef WALNUT_RELOCATABLE
#define WALNUT_RELOCATABLE 0
#endif

#ifndef WALNUT_VERSION
#define WALNUT_VERSION "unknown"
#endif

#include <cctype>
#include <cstdint>
#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define WALNUT_HAS_DEMANGLE 1
#endif
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void* module, wchar_t* filename, unsigned long size);
#endif


namespace walnut_cli {

namespace fs = std::filesystem;

enum class Command { Check, Emit, Build, Run, Clean, Highlight, Complete };

enum class DebugMode { None, Walnut, Cpp };

enum class Output { Program, Object, Static, Shared };

enum class Dump { None, Assembly, LlvmIr, Preprocessed, PreprocessedCpp };

struct Options {
    Command                                          command = Command::Check;
    Output                                           out_kind = Output::Program;
    bool                                             prebuilt_std = true;
    bool                                             link_only = false;
    std::string                                      lib_dir;
    std::string                                      android_abi;
    std::string                                      android_kit;
    std::string                                      ndk;
    int                                              android_api = 0;
    Dump                                             dump = Dump::None;
    std::string                                      asm_syntax;
    bool                                             verbose_asm = false;
    std::vector<std::string>                         asm_functions;
    std::string                                      input;
    std::vector<std::string>                         extra_inputs;
    std::vector<std::string>                         native_inputs;
    std::vector<std::string>                         native_link;
    std::string                                      output;
    std::vector<std::string>                         include_dirs;
    std::vector<std::string>                         module_dirs;
    std::vector<std::string>                         module_env_dirs;
    std::vector<std::pair<std::string, std::string>> defines;
    std::string                                      cxx = WALNUT_DEFAULT_CXX;
    bool                                             cxx_from_user = false;
    bool                                             managed_cxx = false;
    std::string                                      opt = "-O2";
    std::string                                      runtime_dir = WALNUT_RUNTIME_DIR;
    std::string                                      extra_includes = WALNUT_RUNTIME_INCLUDES;
    std::string                                      extra_libs = WALNUT_RUNTIME_LIBS;
    std::string                                      optional_libs = WALNUT_RUNTIME_OPTIONAL_LIBS;
    std::string                                      optional_archives = WALNUT_RUNTIME_OPTIONAL_ARCHIVES;
    std::vector<std::string>                         cxx_flags;
    std::vector<std::string>                         program_args;
    std::string                                      tokens_path;
    std::string                                      ast_path;
    std::string                                      errors_to;
    std::string                                      warnings_to;
    bool                                             keep_cpp = false;
    bool                                             unbuffered = false;
    bool                                             from_stdin = false;
    std::size_t                                      at_line = 0;
    std::size_t                                      at_column = 0;
    bool                                             no_cache = false;
    std::string                                      random_seed;
    bool                                             opt_set = false;
    DebugMode                                        debug = DebugMode::None;
    bool                                             line_map = false;
    std::string                                      line_map_path;
    bool                                             no_warnings = false;
    bool                                             report_timing = false;
    bool                                             verbose = false;
    bool                                             ownership = false;
    bool                                             warn_unused = false;
    bool                                             overflow_errors = false;
    std::vector<std::string>                         safety;
};

#ifdef _WIN32

constexpr char kPathListSeparator = ';';

#else

constexpr char kPathListSeparator = ':';

#endif

struct LibraryState {
    std::unique_ptr<walnut::build::LibraryIndex>       index;
    std::vector<const walnut::build::LibraryManifest*> used;
    std::vector<std::string>                           std_files;
    std::vector<std::string>                           loaded_std;
    std::vector<walnut::build::LibrarySource>          own_sources;
    std::vector<std::string>                           dropped_inputs;
    bool                                               std_prebuilt = false;
};

template <class Reporter>
bool route_messages(Reporter& r, const std::string& dest, const char* flag) {
    if (dest.empty() || dest == "stderr") return true;

    if (dest == "stdout") {
        r.set_mode(walnut::ErrorOutput::StdOut);
        return true;
    }

    const fs::path p(dest);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream truncate(p, std::ios::trunc);

    if (!truncate) {
        std::cerr << "walnut: cannot write " << flag << " file '" << dest << "'\n";
        return false;
    }

    r.set_mode(walnut::ErrorOutput::File);
    r.set_output_path(dest);
    return true;
}

struct LibraryLink {
    std::vector<fs::path>    binaries;
    std::vector<std::string> args;
    std::vector<fs::path>    runtime_files;
};

struct LineMapEntry {
    std::size_t cpp_first = 0;
    std::size_t cpp_last  = 0;
    std::string wal_path;
    std::size_t wal_line  = 0;
};

struct LinkLibs {
    std::string              args;
    std::vector<fs::path>    files;
    std::vector<std::string> left_out;
};

struct CompileJob {
    std::string           label;
    std::string           command;
    fs::path              output;
    fs::path              part;
    fs::path              meta;
    fs::path              depfile;
    fs::path              log;
    std::vector<fs::path> extra_deps;
    fs::path              ignore;
    bool                  ok = false;
};

struct ObjectSet {
    fs::path                 root;
    fs::path                 tmp;
    std::string              id;
    std::string              flags;
    std::string              pch_flag;
    bool                     clang = false;
    std::vector<fs::path>    objects;
    std::vector<std::string> object_keys;
    std::vector<fs::path>    link_inputs;
    std::vector<fs::path>    std_objects;
};

struct PendingUnit {
    fs::path              dir;
    fs::path              staging;
    std::vector<fs::path> objects;
};

struct DumpBlock {
    std::string           symbol;
    std::vector<std::string> lines;
};

void print_usage(std::ostream& os);
fs::path executable_dir();
std::string relocate(const std::string& list);
std::vector<std::string> split_on(const std::string& s, char sep);
std::string android_abi_name(const std::string& a);
std::string android_triple(const std::string& abi);
bool setup_android(Options& o);
bool parse_args(int argc, char** argv, Options& o, int& exit_code);
int run_command(const std::string& cmd);
std::string quote(const std::string& s);
std::vector<std::string> split_list(const std::string& s);
int exit_status_of(int raw);
fs::path make_temp_dir();
extern bool g_needs_cpp20;
extern bool g_uses_native;
extern std::vector<std::string> g_native_from;
extern std::vector<walnut::codegen::NativeCode> g_native_code;
extern LibraryState g_lib;
bool is_library_path(const std::string& path);
std::string library_relative(const std::string& path);
std::string capture_all(const std::string& cmd);
std::vector<std::string> native_header_args(const Options& o, bool cpp);
bool links_native_binaries(const Options& o);
walnut::codegen::LinkOptions::Output emitter_output(const Options& o);
void configure_pipeline(walnut::CompilerPipeline& compiler, const Options& o);
bool generate(const Options& o, std::string& cpp, walnut::codegen::SplitProgram* split = nullptr, bool std_prebuilt = false);
std::string capture_command(const std::string& cmd);
fs::path locate_program(const std::string& name);
std::string compiler_query(const Options& o, const std::string& args);
bool find_program(const std::string& name);
void explain_missing_compiler(const std::string& cxx);
std::vector<fs::path> library_dirs(const Options& o);
bool library_present(const std::string& item, const std::vector<fs::path>& dirs);
bool compile_cpp(const Options& o, const fs::path& cpp_path, const fs::path& exe_path);
bool write_file(const fs::path& p, const std::string& content);
fs::path default_exe_path(const Options& o);
fs::path exe_output_path(const Options& o);
std::string finalize_cpp(const std::string& raw, const Options& o, const std::string& cpp_name, std::vector<LineMapEntry>& map);
bool write_line_map(const Options& o, const std::string& cpp_name, const std::vector<LineMapEntry>& map);
std::string cxx_cmd(const Options& o);
std::string compile_flags(const Options& o);
std::string c_flags(const Options& o);
LinkLibs link_libraries(const Options& o);
void report_compile_failure(const Options& o, const LinkLibs& libs, const fs::path& kept);
bool run_jobs(std::vector<CompileJob>& jobs, const Options& o, const fs::path& tmp);
std::string trimmed(std::string s);
std::string target_triple(const Options& o);
walnut::build::LibraryAbi current_abi(const Options& o);
std::string object_set_id(const Options& o, const std::string& compiler_version);
const walnut_library::File* library_file_named(const std::string& rel);
std::string std_module_import(const std::string& rel);
bool generate_std_unit(const Options& base, const std::string& rel, const fs::path& dir, walnut::codegen::SplitProgram& prog, std::vector<walnut::codegen::NativeCode>& native_code, std::vector<std::string>& native_files, bool& cpp20);
std::string native_code_text(const walnut::codegen::NativeCode& nc);
std::vector<const walnut::build::LibraryManifest*> linked_manifests();
std::vector<std::string> wanted_std_units();
std::string package_root_text();
fs::path shipped_std_dir();
std::string std_unit_flags(const std::string& flags);
std::string std_unit_key(const Options& o, const ObjectSet& set, const std::string& rel, const std::string& text, bool shipped);
bool use_shipped_std_unit(const Options& o, ObjectSet& set, const std::string& rel, const std::string& text);
bool prepare_std_units(const Options& o, ObjectSet& set, std::vector<CompileJob>& jobs, std::vector<PendingUnit>& pending);
void finish_std_units(const std::vector<PendingUnit>& pending);
void discard_std_units(const std::vector<PendingUnit>& pending);
bool build_std_objects(const Options& o, std::vector<fs::path>& objects, const fs::path& ship = fs::path());
std::string single_translation_unit(const walnut::codegen::SplitProgram& prog);
bool compile_objects(const Options& o, const walnut::codegen::SplitProgram& prog, ObjectSet& set, bool single_tu, bool with_std = true);
LibraryLink walnut_library_links(const Options& o);
std::string rpath_args(const Options& o, const LibraryLink& libs);
std::string find_tool(const Options& o, const std::vector<std::string>& names);
std::string find_archiver(const Options& o);
std::string windows_resource_object(const Options& o, const fs::path& tmp);
bool place_runtime_files(const Options& o, const fs::path& exe, const LibraryLink& libs);
bool build_with_cache(const Options& o, const walnut::codegen::SplitProgram& prog, fs::path& exe_out, LibraryLink& libs_out);
bool build_with_cache(const Options& o, const walnut::codegen::SplitProgram& prog, fs::path& exe_out);
int precompile_std(const Options& o, const fs::path& ship = fs::path());
fs::path library_output_path(const Options& o);
std::string relative_to(const fs::path& path, const fs::path& base);
bool build_library(const Options& o, const walnut::codegen::SplitProgram& prog);
int highlight(const Options& o);
bool completion_ident(char c);
std::string completion_pass(const Options& o, std::string text, std::string_view placeholder, const walnut::driver::CompletionRequest& request);
int complete(const Options& o);
bool load_project_names(const Options& o);
std::string demangled(const std::string& symbol);
std::string without_parameters(const std::string& name);
std::string sanitize_name(const std::string& s);
std::string decode_walnut_name(const std::string& text, std::size_t& i);
std::string readable_name(const std::string& symbol);
bool function_matches(const std::string& query, const std::string& symbol);
std::vector<DumpBlock> assembly_functions(const std::string& text);
std::vector<DumpBlock> llvm_functions(const std::string& text);
std::string filter_dump(const Options& o, const std::string& text, bool& ok);
std::string display_path(const std::string& path);
int dump_walnut_preprocessed(const Options& o);
int dump_program(const Options& o);
void register_explicit_libraries(Options& o);
int finish_program(const Options& o, const fs::path& exe, const LibraryLink& libs);
int run(const Options& in);

} // namespace walnut_cli

#endif // WALNUT_CLI_HPP
