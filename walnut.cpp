#include "compiler.hpp"
#include "Codegen/cpp_emitter.hpp"
#include "Common/file_kind.hpp"
#include "Driver/build_cache.hpp"
#include "Preprocessor/native_headers.hpp"
#include <algorithm>
#include <atomic>
#include <thread>

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

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void* module, wchar_t* filename, unsigned long size);
#endif

namespace {

namespace fs = std::filesystem;

enum class Command { Check, Emit, Build, Run, Clean };

enum class DebugMode { None, Walnut, Cpp };

struct Options {
    Command                                          command = Command::Check;
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
    bool                                             no_cache = false;
    bool                                             opt_set = false;
    DebugMode                                        debug = DebugMode::None;
    bool                                             line_map = false;
    std::string                                      line_map_path;
    bool                                             no_warnings = false;
    bool                                             report_timing = false;
    bool                                             verbose = false;
};

void print_usage(std::ostream& os) {
    os << "usage: walnut <command> [options] <main.wal> [more.wal ...] [C/C++ files] [-- program arguments]\n"
          "\n"
          "commands:\n"
          "  check   analyze the program and report errors and warnings\n"
          "  emit    write the generated C++ (to -o, or to standard output)\n"
          "  build   compile the program to an executable (default: the input name without .wal)\n"
          "  run     build the program and run it\n"
          "  clean   delete walnut's build cache\n"
          "\n"
          "options:\n"
          "  -o <path>          output path\n"
          "  -I <dir>           add an include directory for #include\n"
          "  -L <dir>, -l <lib> library search path and library for extern \"C\"/\"C++\" code\n"
          "  -D <name>[=value]  define a preprocessor macro\n"
          "  -O0 -O1 -O2 -O3    optimization level for the C++ compiler (default -O2, or -O0 with -g)\n"
          "  -g                 build for a debugger that steps through the .wal lines\n"
          "  --debug-cpp        build for a debugger that steps through the generated C++ (kept next to the program)\n"
          "  --line-map[=file]  write which .wal line each generated C++ line came from (to stdout without a file)\n"
          "  --cxx <compiler>   C++ compiler used by build and run (default: " WALNUT_DEFAULT_CXX ")\n"
          "  --cxx-flag <flag>  pass an extra flag to the C++ compiler\n"
          "  --runtime <dir>    directory containing walnut_runtime.hpp and the standard modules\n"
          "  -M <dir>           add a folder searched by 'import module.<name>' (also --module-dir)\n"
          "  --keep-cpp         keep the generated C++ next to the executable (builds without the cache)\n"
          "  --no-cache         build everything from scratch in a temporary folder, without the build cache\n"
          "  --tokens <file>    write the token list of the input to a .txt file\n"
          "  --ast <file>       write the syntax tree of the input to a .txt file\n"
          "  --errors <dest>    where errors go: stderr (default), stdout or a file path\n"
          "  --warnings <dest>  where warnings go: stderr (default), stdout or a file path\n"
          "  --no-warnings      suppress warnings\n"
          "  --time             report how long each compiler stage takes\n"
          "  -v, --verbose      print the C++ compiler command\n"
          "  -h, --help         show this help\n";
}

fs::path executable_dir() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    const unsigned long n = GetModuleFileNameW(nullptr, buf.data(), static_cast<unsigned long>(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

std::string relocate(const std::string& list) {
    if (!WALNUT_RELOCATABLE) return list;
    const fs::path base = executable_dir();
    std::string out;
    std::string cur;

    auto flush = [&] {
        if (cur.empty()) return;
        if (!out.empty()) out += ';';
        out += cur.rfind("../", 0) == 0 ? (base / fs::path(cur)).lexically_normal().string() : cur;
        cur.clear();
    };

    for (char c : list) {
        if (c == ';') flush();
        else cur += c;
    }

    flush();
    return out;
}

#ifdef _WIN32
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

std::vector<std::string> split_on(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != '\r' && c != '\n') cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool parse_args(int argc, char** argv, Options& o, int& exit_code) {
    o.runtime_dir    = relocate(o.runtime_dir);
    o.extra_includes = relocate(o.extra_includes);
    o.extra_libs     = relocate(o.extra_libs);
    o.optional_libs  = relocate(o.optional_libs);
    o.optional_archives = relocate(o.optional_archives);
    if (const char* env = std::getenv("WALNUT_RUNTIME_DIR")) if (*env) o.runtime_dir = env;
    if (const char* env = std::getenv("WALNUT_CXX")) if (*env) o.cxx = env;
    if (const char* env = std::getenv("WALNUT_MODULE_PATH")) if (*env) o.module_env_dirs = split_on(env, kPathListSeparator);

    exit_code = 0;
    if (argc < 2) { print_usage(std::cerr); exit_code = 2; return false; }
    const std::string cmd = argv[1];

    if (cmd == "-h" || cmd == "--help" || cmd == "help") { print_usage(std::cout); return false; }
    if (cmd == "check")      o.command = Command::Check;
    else if (cmd == "emit")  o.command = Command::Emit;
    else if (cmd == "build") o.command = Command::Build;
    else if (cmd == "run")   o.command = Command::Run;
    else if (cmd == "clean") { o.command = Command::Clean; return true; }
    else { std::cerr << "walnut: unknown command '" << cmd << "'\n"; print_usage(std::cerr); exit_code = 2; return false; }

    auto need = [&](int& i, const std::string& flag) -> const char* {
        if (i + 1 >= argc) { std::cerr << "walnut: option '" << flag << "' needs a value\n"; exit_code = 2; return nullptr; }
        return argv[++i];
    };

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "--") {
            for (int k = i + 1; k < argc; ++k) o.program_args.push_back(argv[k]);
            break;
        }

        if (a == "-o")                 { const char* v = need(i, a); if (!v) return false; o.output = v; }
        else if (a == "-I")            { const char* v = need(i, a); if (!v) return false; o.include_dirs.push_back(v); }
        else if (a.rfind("-I", 0) == 0) o.include_dirs.push_back(a.substr(2));
        else if (a == "-D" || a.rfind("-D", 0) == 0) {
            std::string def;
            if (a == "-D") { const char* v = need(i, a); if (!v) return false; def = v; }
            else def = a.substr(2);
            const auto eq = def.find('=');
            if (eq == std::string::npos) o.defines.emplace_back(def, "1");
            else o.defines.emplace_back(def.substr(0, eq), def.substr(eq + 1));
        }
        else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3" || a == "-Os") { o.opt = a; o.opt_set = true; }
        else if (a == "-g")            o.debug = DebugMode::Walnut;
        else if (a == "--debug-cpp")   { o.debug = DebugMode::Cpp; o.keep_cpp = true; }
        else if (a == "--line-map")    o.line_map = true;
        else if (a.rfind("--line-map=", 0) == 0) { o.line_map = true; o.line_map_path = a.substr(11); }
        else if (a == "--cxx")         { const char* v = need(i, a); if (!v) return false; o.cxx = v; }
        else if (a == "--cxx-flag")    { const char* v = need(i, a); if (!v) return false; o.cxx_flags.push_back(v); }
        else if (a == "--runtime")     { const char* v = need(i, a); if (!v) return false; o.runtime_dir = v; }
        else if (a == "-M" || a == "--module-dir") { const char* v = need(i, a); if (!v) return false; o.module_dirs.push_back(v); }
        else if (a == "--keep-cpp")    o.keep_cpp = true;
        else if (a == "--no-cache")    o.no_cache = true;
        else if (a == "--tokens")      { const char* v = need(i, a); if (!v) return false; o.tokens_path = v; }
        else if (a == "--ast")         { const char* v = need(i, a); if (!v) return false; o.ast_path = v; }
        else if (a == "--errors")      { const char* v = need(i, a); if (!v) return false; o.errors_to = v; }
        else if (a == "--warnings")    { const char* v = need(i, a); if (!v) return false; o.warnings_to = v; }
        else if (a == "--no-warnings") o.no_warnings = true;
        else if (a == "--time")        o.report_timing = true;
        else if (a == "-v" || a == "--verbose") o.verbose = true;
        else if (a == "-h" || a == "--help") { print_usage(std::cout); return false; }
        else if (a == "-L" || a == "-l") { const char* v = need(i, a); if (!v) return false; o.native_link.push_back(a + v); }
        else if (a.size() > 2 && (a.rfind("-L", 0) == 0 || a.rfind("-l", 0) == 0)) o.native_link.push_back(a);
        else if (!a.empty() && a[0] == '-') { std::cerr << "walnut: unknown option '" << a << "'\n"; exit_code = 2; return false; }
        else {
            const walnut::FileKind kind = walnut::classify_file(a);
            const std::string name = walnut::file_display_name(a);

            if (walnut::is_native_code(kind)) {
                o.native_inputs.push_back(a);
            } else if (kind == walnut::FileKind::Walnut) {
                if (o.input.empty()) o.input = a;
                else o.extra_inputs.push_back(a);
            } else if (kind == walnut::FileKind::WalnutHeader) {
                std::cerr << "walnut: '" << name << "' is a Walnut header; #include or import it from a .wal file instead of passing it to walnut\n";
                exit_code = 2;
                return false;
            } else if (walnut::is_native_header(kind)) {
                std::cerr << "walnut: '" << name << "' is a C or C++ header; name it in an extern block instead, as in 'extern \"C++\" from \"" << name << "\" { ... } end extern'\n";
                exit_code = 2;
                return false;
            } else {
                std::cerr << "walnut: '" << name << "' isn't a Walnut, C or C++ file; walnut takes .wal files, C and C++ source files (.c, .cpp, .cc, .cxx), object files (.o, .obj) and libraries (.a, .so, .dylib, .lib, .dll)\n";
                exit_code = 2;
                return false;
            }
        }
    }

    if (o.input.empty()) { std::cerr << "walnut: no input file\n"; exit_code = 2; return false; }

    for (const auto& [flag, path] : { std::pair<const char*, const std::string*>{ "--tokens", &o.tokens_path }, { "--ast", &o.ast_path } }) {
        if (path->empty()) continue;
        std::string ext = fs::path(*path).extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".txt") { std::cerr << "walnut: " << flag << " must name a .txt file (got '" << *path << "')\n"; exit_code = 2; return false; }
    }

    if (o.no_warnings && !o.warnings_to.empty()) { std::cerr << "walnut: --warnings and --no-warnings cannot be used together\n"; exit_code = 2; return false; }
    return true;
}

int run_command(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

std::string quote(const std::string& s) {
#ifdef _WIN32
    std::string r = "\"";
    for (char c : s) { if (c == '"') r += '\\'; r += c; }
    return r + "\"";
#else
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
#endif
}

std::string cxx_cmd(const Options& o);

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int exit_status_of(int raw) {
#ifdef _WIN32
    return raw;
#else
    if (raw == -1) return 127;
    if (WIFEXITED(raw)) return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw)) return 128 + WTERMSIG(raw);
    return raw;
#endif
}

fs::path make_temp_dir() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    const fs::path base = fs::temp_directory_path();

    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ostringstream name;
        name << "walnut-" << std::hex << gen();
        fs::path p = base / name.str();
        std::error_code ec;
        if (fs::create_directory(p, ec)) return p;
    }

    throw std::runtime_error("could not create a temporary directory");
}

bool g_needs_cpp20 = false;
bool g_uses_native = false;
std::vector<std::string> g_native_from;
std::vector<walnut::codegen::NativeCode> g_native_code;

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

std::string capture_all(const std::string& cmd) {
#ifdef _WIN32
    FILE* pipe = _popen(("\"" + cmd + " 2>&1\"").c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

std::vector<std::string> native_header_args(const Options& o, bool cpp) {
#ifdef _WIN32
    const std::string null_file = "NUL";
#else
    const std::string null_file = "/dev/null";
#endif
    const std::string text = capture_all(cxx_cmd(o) + " -E -x " + (cpp ? "c++" : "c") + " -v " + null_file + " -o " + null_file);
    std::vector<std::string> args;
    std::istringstream lines(text);
    std::string line;
    bool inside = false;

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.find("search starts here:") != std::string::npos) { inside = true; continue; }
        if (line.rfind("End of search list.", 0) == 0) break;
        if (!inside || line.empty() || line[0] != ' ') continue;
        std::string dir = line.substr(line.find_first_not_of(' '));
        const std::size_t framework = dir.find(" (framework directory)");
        if (framework != std::string::npos) dir = dir.substr(0, framework);
        args.push_back("-isystem");
        args.push_back(dir);
    }

    if (!args.empty()) {
        args.insert(args.begin(), "-nostdinc");
        if (cpp) args.insert(args.begin() + 1, "-nostdinc++");
    }

    return args;
}

bool generate(const Options& o, std::string& cpp, walnut::codegen::SplitProgram* split = nullptr) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(fs::path(o.input).is_absolute() ? walnut::PathStyle::Absolute : walnut::PathStyle::Relative);
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    if (o.no_warnings) compiler.suppress_warnings();
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    if (!o.tokens_path.empty()) compiler.output_tokens(o.tokens_path);
    if (!o.ast_path.empty()) compiler.output_ast(o.ast_path);
    if (o.report_timing) compiler.report_timing();
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
    if (!route_messages(compiler.error_reporter(), o.errors_to, "--errors")) return false;
    if (!route_messages(compiler.warning_reporter(), o.warnings_to, "--warnings")) return false;

    try {
        compiler.compile();
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return false;
    }

    if (o.command == Command::Check) return true;
    std::vector<walnut::codegen::CodegenUnit> units;
    for (const auto& u : compiler.analyzed_units()) units.push_back({ u.file, u.ast, u.root });
    walnut::codegen::CppEmitter emitter(compiler.mutable_types(), compiler.mutable_instantiator(), compiler.error_reporter());
    emitter.source_path = [&compiler](walnut::FileId f) { return compiler.get_sources().absolute_path(f); };
    emitter.path_of_text = [&compiler](const char* p) {
        const std::optional<walnut::FileId> f = compiler.get_sources().file_containing(p);
        return f ? compiler.get_sources().absolute_path(*f) : std::string();
    };
    std::ostringstream out;
    bool ok = false;

    try {
        ok = split ? emitter.emit_split(units, *split) : emitter.emit(units, out);
    } catch (const std::exception& e) {
        compiler.error_reporter().report(walnut::ErrorPhase::Semantic, walnut::INVALID_FILE, 0, std::string("Internal compiler error during code generation: ") + e.what());
    }

    if (!ok || compiler.error_reporter().has_errors()) {
        compiler.error_reporter().flush();
        const std::size_t n = compiler.error_reporter().error_count();
        std::cerr << "walnut: code generation failed with " << n << (n == 1 ? " error" : " errors") << "\n";
        return false;
    }

    cpp = out.str();
    g_needs_cpp20 = emitter.uses_coroutines();
    g_uses_native = emitter.uses_native_externs();
    g_native_from = emitter.native_files();
    g_native_code = emitter.native_code();
    return true;
}

std::string capture_command(const std::string& cmd) {
#ifdef _WIN32
    FILE* pipe = _popen(("\"" + cmd + " 2>NUL\"").c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

bool find_program(const std::string& name) {
    std::vector<std::string> names{ name };
#ifdef _WIN32
    if (fs::path(name).extension().empty()) names.push_back(name + ".exe");
#endif
    std::error_code ec;

    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        for (const std::string& n : names) if (fs::is_regular_file(n, ec)) return true;
        return false;
    }

    const char* path = std::getenv("PATH");
    if (!path) return false;

    for (const std::string& dir : split_on(path, kPathListSeparator)) {
        for (const std::string& n : names) if (fs::is_regular_file(fs::path(dir) / n, ec)) return true;
    }

    return false;
}

void explain_missing_compiler(const std::string& cxx) {
    std::cerr << "walnut: cannot find the C++ compiler '" << cxx << "'\n"
              << "walnut: Walnut turns programs into C++, so 'build' and 'run' need a C++17 compiler (GCC 9 or newer, or Clang 9 or newer)\n";
#ifdef _WIN32
    std::cerr << "walnut: install MSYS2 (https://www.msys2.org) and the compiler that matches this download:\n"
              << "walnut:     pacman -S mingw-w64-ucrt-x86_64-cc         (windows-ucrt64, in the UCRT64 shell)\n"
              << "walnut:     pacman -S mingw-w64-x86_64-cc              (windows-mingw64, in the MINGW64 shell)\n"
              << "walnut:     pacman -S mingw-w64-clang-aarch64-cc       (windows-clangarm64, in the CLANGARM64 shell)\n"
              << "walnut: then run walnut from that MSYS2 shell, or add its bin folder to PATH\n";
#else
    std::cerr << "walnut: install one, for example 'sudo apt install g++' (Debian/Ubuntu), 'sudo dnf install gcc-c++' (Fedora) or 'sudo pacman -S gcc' (Arch)\n";
#endif
    std::cerr << "walnut: or pick a compiler with --cxx <compiler> or the WALNUT_CXX environment variable\n";
}

std::vector<fs::path> library_dirs(const Options& o) {
    std::vector<fs::path> dirs;

    for (const std::string& l : o.native_link) {
        if (l.rfind("-L", 0) == 0 && l.size() > 2) dirs.emplace_back(l.substr(2));
    }

    const std::string out = capture_command(cxx_cmd(o) + " -print-search-dirs");
    std::istringstream lines(out);
    std::string line;

    while (std::getline(lines, line)) {
        if (line.rfind("libraries:", 0) != 0) continue;
        std::string list = line.substr(10);
        while (!list.empty() && (list.front() == ' ' || list.front() == '=')) list.erase(list.begin());
        for (const std::string& d : split_on(list, kPathListSeparator)) dirs.emplace_back(d);
    }


    return dirs;
}

bool library_present(const std::string& item, const std::vector<fs::path>& dirs) {
    std::error_code ec;
    if (fs::path(item).is_absolute()) return fs::exists(item, ec);
    if (item.rfind("-l", 0) != 0 || item.size() <= 2) return true;
    if (item.rfind("-l:", 0) == 0) {
        for (const fs::path& d : dirs) if (fs::exists(d / item.substr(3), ec)) return true;
        return false;
    }

    const std::string name = item.substr(2);
    const std::string files[] = { "lib" + name + ".so", "lib" + name + ".a", "lib" + name + ".dll.a", name + ".lib", "lib" + name + ".dylib", "lib" + name + ".tbd" };

    for (const fs::path& d : dirs) {
        for (const std::string& f : files) if (fs::exists(d / f, ec)) return true;
    }

    return false;
}

bool write_file(const fs::path& p, const std::string& content);

bool compile_cpp(const Options& o, const fs::path& cpp_path, const fs::path& exe_path) {
    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return false;
    }

    const std::string opt = (o.debug != DebugMode::None && !o.opt_set) ? "-O0" : o.opt;
    std::string cmd = cxx_cmd(o) + (g_needs_cpp20 ? " -std=c++20 " : " -std=c++17 ") + opt + (o.debug != DebugMode::None ? " -g" : "") + " -w";
    cmd += " -I" + quote(o.runtime_dir);
    for (const std::string& inc : split_list(o.extra_includes)) cmd += " -I" + quote(inc);
    for (const std::string& f : o.cxx_flags) cmd += " " + f;
    cmd += " " + quote(cpp_path.string());

    std::vector<std::string> natives;
    std::vector<fs::path> seen;
    std::vector<std::string> code_includes;

    for (std::size_t i = 0; i < g_native_code.size(); ++i) {
        const walnut::codegen::NativeCode& nc = g_native_code[i];
        const fs::path file = cpp_path.parent_path() / ("code" + std::to_string(i) + (nc.is_c ? ".c" : ".cpp"));
        std::string escaped;
        for (char ch : nc.path) { if (ch == '\\' || ch == '"') escaped += '\\'; escaped += ch; }
        if (!write_file(file, "#line " + std::to_string(nc.line) + " \"" + escaped + "\"\n" + nc.text + "\n")) return false;
        natives.push_back(file.string());
        seen.push_back(file);
        const fs::path dir = fs::path(nc.path).parent_path();
        if (!nc.path.empty() && nc.path.front() != '<' && std::find(code_includes.begin(), code_includes.end(), dir.string()) == code_includes.end()) code_includes.push_back(dir.string());
    }

    for (const std::string& inc : code_includes) cmd += " -iquote" + quote(inc);

    for (const std::vector<std::string>* list : std::initializer_list<const std::vector<std::string>*>{ &o.native_inputs, &g_native_from }) {
        for (const std::string& n : *list) {
            std::error_code ec;
            fs::path key = fs::weakly_canonical(n, ec);
            if (ec) key = fs::path(n);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            natives.push_back(n);
        }
    }

    for (std::size_t i = 0; i < natives.size(); ++i) {
        const std::string& n = natives[i];

        if (walnut::classify_file(n) != walnut::FileKind::CSource) {
            cmd += " " + quote(n);
            continue;
        }

        const fs::path obj = cpp_path.parent_path() / ("c" + std::to_string(i) + "_" + fs::path(n).stem().string() + ".o");
        std::string ccmd = cxx_cmd(o) + " -x c " + opt + (o.debug != DebugMode::None ? " -g" : "") + " -w -c " + quote(n) + " -o " + quote(obj.string());
        for (const std::string& inc : split_list(o.extra_includes)) ccmd += " -I" + quote(inc);
        for (const std::string& inc : code_includes) ccmd += " -iquote" + quote(inc);
        if (o.verbose) std::cerr << ccmd << "\n";

        if (exit_status_of(run_command(ccmd)) != 0) {
            std::cerr << "walnut: the C compiler failed on '" << n << "'\n";
            return false;
        }

        cmd += " " + quote(obj.string());
    }

    cmd += " -o " + quote(exe_path.string());
    for (const std::string& l : o.native_link) cmd += " " + quote(l);

    std::error_code ec;
    for (const std::string& lib : split_list(o.optional_archives)) if (fs::exists(lib, ec)) cmd += " " + quote(lib);
    for (const std::string& lib : split_list(o.extra_libs)) cmd += " " + quote(lib);

    std::vector<std::string> left_out;
    const std::vector<std::string> optional = split_list(o.optional_libs);

    if (!optional.empty()) {
        const std::vector<fs::path> dirs = library_dirs(o);
        std::vector<std::string> present;

        for (const std::string& lib : optional) {
            if (library_present(lib, dirs)) present.push_back(lib);
            else left_out.push_back(lib);
        }

#ifndef _WIN32
        if (!present.empty()) cmd += " -Wl,--as-needed";
#endif
        for (const std::string& lib : present) cmd += " " + quote(lib);
#ifndef _WIN32
        if (!present.empty()) cmd += " -Wl,--no-as-needed";
#endif
    }

    if (o.verbose) std::cerr << cmd << "\n";
    const int status = exit_status_of(run_command(cmd));

    if (status != 0) {
        std::cerr << "walnut: the C++ compiler failed on the generated code (exit status " << status << ")\n";

        if (!left_out.empty()) {
            std::cerr << "walnut: these libraries that fizmo can use are not installed, so they were left out:";
            for (const std::string& lib : left_out) std::cerr << " " << (lib.rfind("-l", 0) == 0 ? lib.substr(2) : lib);
            std::cerr << "\nwalnut: if the errors above are 'undefined reference' errors from fizmo, install the development packages for those libraries\n";
        }

        if (g_uses_native || !o.native_inputs.empty()) {
            std::cerr << "walnut: if the errors above mention an extern \"C\" or \"C++\" function, check that its declaration matches the C/C++ code and that the file or library defining it was passed to walnut\n";
            std::cerr << "walnut: otherwise this is a code generator bug; the generated C++ is at " << cpp_path.string() << "\n";
        } else {
            std::cerr << "walnut: this is a code generator bug; the generated C++ is at " << cpp_path.string() << "\n";
        }
        return false;
    }

    return true;
}

bool write_file(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    if (!f) { std::cerr << "walnut: cannot write '" << p.string() << "'\n"; return false; }
    f << content;
    return static_cast<bool>(f);
}

fs::path default_exe_path(const Options& o) {
    fs::path p = fs::path(o.input).filename();
    p.replace_extension("");
#ifdef _WIN32
    p.replace_extension(".exe");
#endif
    return p;
}

struct LineMapEntry {
    std::size_t cpp_first = 0;
    std::size_t cpp_last  = 0;
    std::string wal_path;
    std::size_t wal_line  = 0;
};

std::string finalize_cpp(const std::string& raw, const Options& o, const std::string& cpp_name, std::vector<LineMapEntry>& map) {
    const bool keep_markers = o.debug == DebugMode::Walnut;
    std::string escaped_name;
    for (char c : cpp_name) { if (c == '\\' || c == '"') escaped_name += '\\'; escaped_name += c; }

    std::string out;
    out.reserve(raw.size());
    std::size_t count = 0;
    bool have = false;
    std::string cur_directive;
    std::string cur_path;
    std::size_t cur_line = 0;
    std::size_t since = 0;

    auto emit = [&](const std::string& line) { out += line; out += '\n'; ++count; };

    std::size_t pos = 0;

    while (pos < raw.size()) {
        std::size_t end = raw.find('\n', pos);
        if (end == std::string::npos) end = raw.size();
        const std::string line = raw.substr(pos, end - pos);
        pos = end + 1;

        if (line.rfind("#line ", 0) == 0) {
            const std::size_t q1 = line.find('"');
            const std::size_t q2 = line.rfind('"');
            cur_line = static_cast<std::size_t>(std::strtoull(line.c_str() + 6, nullptr, 10));
            cur_path.clear();
            for (std::size_t i = q1 + 1; q1 != std::string::npos && i < q2; ++i) { if (line[i] == '\\' && i + 1 < q2) ++i; cur_path += line[i]; }
            cur_directive = line;
            have = true;
            since = 0;
            if (keep_markers) emit(line);
            continue;
        }

        if (line == walnut::codegen::CppEmitter::kLineReset) {
            have = false;
            if (keep_markers) emit("#line " + std::to_string(count + 2) + " \"" + escaped_name + "\"");
            continue;
        }

        if (keep_markers && have && since > 0) emit(cur_directive);
        emit(line);
        ++since;

        if (have) {
            if (!map.empty() && map.back().cpp_last + 1 == count && map.back().wal_line == cur_line && map.back().wal_path == cur_path) map.back().cpp_last = count;
            else map.push_back(LineMapEntry{ count, count, cur_path, cur_line });
        }
    }

    return out;
}

bool write_line_map(const Options& o, const std::string& cpp_name, const std::vector<LineMapEntry>& map) {
    if (!o.line_map) return true;
    std::ostringstream ss;

    for (const LineMapEntry& e : map) {
        ss << cpp_name << ":" << e.cpp_first;
        if (e.cpp_last != e.cpp_first) ss << "-" << e.cpp_last;
        ss << " -> " << e.wal_path << ":" << e.wal_line << "\n";
    }

    if (o.line_map_path.empty()) { std::cout << ss.str(); return true; }
    return write_file(o.line_map_path, ss.str());
}

std::string cxx_cmd(const Options& o) {
    return quote(o.cxx);
}

std::string compile_flags(const Options& o) {
    const std::string opt = (o.debug != DebugMode::None && !o.opt_set) ? "-O0" : o.opt;
    std::string flags = std::string(g_needs_cpp20 ? "-std=c++20 " : "-std=c++17 ") + opt + (o.debug != DebugMode::None ? " -g" : "") + " -w";
    flags += " -I" + quote(o.runtime_dir);
    for (const std::string& inc : split_list(o.extra_includes)) flags += " -I" + quote(inc);
    for (const std::string& f : o.cxx_flags) flags += " " + f;
    return flags;
}

std::string c_flags(const Options& o) {
    const std::string opt = (o.debug != DebugMode::None && !o.opt_set) ? "-O0" : o.opt;
    std::string flags = "-x c " + opt + (o.debug != DebugMode::None ? " -g" : "") + " -w";
    for (const std::string& inc : split_list(o.extra_includes)) flags += " -I" + quote(inc);
    return flags;
}

struct LinkLibs {
    std::string              args;
    std::vector<fs::path>    files;
    std::vector<std::string> left_out;
};

LinkLibs link_libraries(const Options& o) {
    LinkLibs out;
    std::error_code ec;
    for (const std::string& l : o.native_link) out.args += " " + quote(l);

    for (const std::string& lib : split_list(o.optional_archives)) {
        if (!fs::exists(lib, ec)) continue;
        out.args += " " + quote(lib);
        out.files.emplace_back(lib);
    }

    for (const std::string& lib : split_list(o.extra_libs)) {
        out.args += " " + quote(lib);
        if (fs::path(lib).is_absolute() && fs::exists(lib, ec)) out.files.emplace_back(lib);
    }

    const std::vector<std::string> optional = split_list(o.optional_libs);
    if (optional.empty()) return out;
    const std::vector<fs::path> dirs = library_dirs(o);
    std::vector<std::string> present;

    for (const std::string& lib : optional) {
        if (library_present(lib, dirs)) present.push_back(lib);
        else out.left_out.push_back(lib);
    }

#ifndef _WIN32
    if (!present.empty()) out.args += " -Wl,--as-needed";
#endif
    for (const std::string& lib : present) {
        out.args += " " + quote(lib);
        if (fs::path(lib).is_absolute() && fs::exists(lib, ec)) out.files.emplace_back(lib);
    }
#ifndef _WIN32
    if (!present.empty()) out.args += " -Wl,--no-as-needed";
#endif
    return out;
}

struct CompileJob {
    std::string           label;
    std::string           command;
    fs::path              output;
    fs::path              part;
    fs::path              meta;
    fs::path              depfile;
    fs::path              log;
    std::vector<fs::path> extra_deps;
    bool                  ok = false;
};

void report_compile_failure(const Options& o, const LinkLibs& libs, const fs::path& kept) {
    if (!libs.left_out.empty()) {
        std::cerr << "walnut: these libraries that fizmo can use are not installed, so they were left out:";
        for (const std::string& lib : libs.left_out) std::cerr << " " << (lib.rfind("-l", 0) == 0 ? lib.substr(2) : lib);
        std::cerr << "\nwalnut: if the errors above are 'undefined reference' errors from fizmo, install the development packages for those libraries\n";
    }

    if (g_uses_native || !o.native_inputs.empty() || !g_native_code.empty()) {
        std::cerr << "walnut: if the errors above mention an extern \"C\" or \"C++\" function, check that its declaration matches the C/C++ code and that the file or library defining it was passed to walnut\n";
        std::cerr << "walnut: otherwise this is a code generator bug; the generated C++ is in " << kept.string() << "\n";
    } else {
        std::cerr << "walnut: this is a code generator bug; the generated C++ is in " << kept.string() << "\n";
    }
}

bool run_jobs(std::vector<CompileJob>& jobs, const Options& o, const fs::path& tmp) {
    if (jobs.empty()) return true;
    for (const CompileJob& j : jobs) if (o.verbose) std::cerr << j.command << "\n";
    std::atomic<std::size_t> next{ 0 };
    const std::size_t workers = std::max<std::size_t>(1, std::min<std::size_t>(jobs.size(), std::max(1u, std::thread::hardware_concurrency())));
    std::vector<std::thread> threads;

    for (std::size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&]() {
            for (std::size_t i = next++; i < jobs.size(); i = next++) {
                CompileJob& j = jobs[i];
                j.ok = exit_status_of(run_command(j.command + " >" + quote(j.log.string()) + " 2>&1")) == 0;
                if (!j.ok) continue;
                std::error_code ec;
                fs::create_directories(j.output.parent_path(), ec);
                j.ok = walnut::build::replace_file(j.part, j.output);
                if (j.ok) walnut::build::record_dependencies(j.meta, j.depfile, j.extra_deps, { tmp });
            }
        });
    }

    for (std::thread& t : threads) t.join();
    bool all = true;

    for (const CompileJob& j : jobs) {
        const std::string text = walnut::build::read_text(j.log);
        if (!text.empty()) std::cerr << text;
        if (!j.ok) { all = false; std::cerr << "walnut: compiling " << j.label << " failed\n"; }
    }

    return all;
}

bool build_with_cache(const Options& o, const walnut::codegen::SplitProgram& prog, fs::path& exe_out) {
    namespace wb = walnut::build;

    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return false;
    }

    const fs::path root = wb::default_cache_root();
    std::error_code ec;
    for (const char* sub : { "obj", "exe", "pch" }) fs::create_directories(root / sub, ec);
    wb::prune(root, 30);

    const fs::path tmp = make_temp_dir();
    std::string id = capture_command(cxx_cmd(o) + " --version");
    id = id.substr(0, id.find('\n'));
    const bool clang = id.find("clang") != std::string::npos;
    id = o.cxx + "\n" + id + "\n" + __DATE__ " " __TIME__;
    const std::string flags = compile_flags(o);
    std::string pch_flag;
    fs::path pch_file;

    {
        const std::string key = wb::hash_text("pch\n" + id + "\n" + flags);
        const fs::path dir = root / "pch" / key;
        const fs::path file = dir / (clang ? "walnut_runtime.pch" : "walnut_runtime.hpp.gch");
        const fs::path meta = dir / "deps";
        bool ready = wb::is_fresh(file, meta);

        if (!ready) {
            fs::create_directories(dir, ec);
            std::vector<CompileJob> pch(1);
            pch[0].label = "the precompiled runtime header";
            pch[0].output = file;
            pch[0].part = tmp / "runtime.pch.part";
            pch[0].meta = meta;
            pch[0].depfile = tmp / "runtime.pch.d";
            pch[0].log = tmp / "runtime.pch.log";
            pch[0].command = cxx_cmd(o) + " -x c++-header " + flags + " " + quote((fs::path(o.runtime_dir) / "walnut_runtime.hpp").string()) + " -o " + quote(pch[0].part.string()) + " -MMD -MF " + quote(pch[0].depfile.string());
            if (o.verbose) std::cerr << pch[0].command << "\n";
            ready = exit_status_of(run_command(pch[0].command + " >" + quote(pch[0].log.string()) + " 2>&1")) == 0 && wb::replace_file(pch[0].part, file);
            if (ready) wb::record_dependencies(meta, pch[0].depfile, {}, { tmp });
        }

        if (ready) {
            wb::touch(dir);
            pch_file = file;
            pch_flag = clang ? "-include-pch " + quote(file.string()) + " " : "-I" + quote(dir.string()) + " ";
        }
    }

    std::vector<CompileJob> jobs;
    std::vector<fs::path> objects;
    std::vector<std::string> object_keys;
    std::vector<fs::path> link_inputs;
    std::vector<std::string> code_includes;

    for (const walnut::codegen::NativeCode& nc : g_native_code) {
        const fs::path dir = fs::path(nc.path).parent_path();
        if (!nc.path.empty() && nc.path.front() != '<' && std::find(code_includes.begin(), code_includes.end(), dir.string()) == code_includes.end()) code_includes.push_back(dir.string());
    }

    std::string iquotes;
    for (const std::string& inc : code_includes) iquotes += " -iquote" + quote(inc);

    auto add_job = [&](const std::string& label, const std::string& key, const std::string& compiler_part, const fs::path& src, const std::vector<fs::path>& extra) {
        const fs::path obj = root / "obj" / (key + ".o");
        const fs::path meta = root / "obj" / (key + ".deps");
        objects.push_back(obj);
        object_keys.push_back(key);
        if (wb::is_fresh(obj, meta)) { wb::touch(meta); return; }
        CompileJob j;
        j.label = label;
        j.output = obj;
        j.part = tmp / (key + ".o.part");
        j.meta = meta;
        j.depfile = tmp / (key + ".d");
        j.log = tmp / (key + ".log");
        j.extra_deps = extra;
        j.command = compiler_part + " -c " + quote(src.string()) + " -o " + quote(j.part.string()) + " -MMD -MF " + quote(j.depfile.string());
        jobs.push_back(std::move(j));
    };

    std::vector<LineMapEntry> unused;
    const fs::path header_path = tmp / "program.hpp";
    const std::string header = finalize_cpp(prog.header, o, header_path.string(), unused);
    if (!write_file(header_path, header)) return false;

    for (const auto& [name, text] : prog.sources) {
        const fs::path src = tmp / name;
        const std::string body = finalize_cpp(text, o, src.string(), unused);
        if (!write_file(src, body)) return false;
        const std::string key = wb::hash_text("tu\n" + id + "\n" + flags + "\n" + header + "\n" + body);
        std::vector<fs::path> extra;
        if (!pch_file.empty()) extra.push_back(pch_file);
        add_job("the generated C++ (" + name + ")", key, cxx_cmd(o) + " " + pch_flag + flags + iquotes, src, extra);
    }

    for (std::size_t i = 0; i < g_native_code.size(); ++i) {
        const walnut::codegen::NativeCode& nc = g_native_code[i];
        const fs::path src = tmp / ("code" + std::to_string(i) + (nc.is_c ? ".c" : ".cpp"));
        std::string escaped;
        for (char ch : nc.path) { if (ch == '\\' || ch == '"') escaped += '\\'; escaped += ch; }
        const std::string text = "#line " + std::to_string(nc.line) + " \"" + escaped + "\"\n" + nc.text + "\n";
        if (!write_file(src, text)) return false;
        const std::string part = nc.is_c ? cxx_cmd(o) + " " + c_flags(o) + iquotes : cxx_cmd(o) + " " + flags + iquotes;
        add_job("the code block in " + nc.path, wb::hash_text("code\n" + id + "\n" + part + "\n" + text), part, src, {});
    }

    std::vector<fs::path> seen;

    for (const std::vector<std::string>* list : std::initializer_list<const std::vector<std::string>*>{ &o.native_inputs, &g_native_from }) {
        for (const std::string& n : *list) {
            fs::path key_path = fs::weakly_canonical(n, ec);
            if (ec) key_path = fs::absolute(n, ec);
            if (std::find(seen.begin(), seen.end(), key_path) != seen.end()) continue;
            seen.push_back(key_path);
            const walnut::FileKind kind = walnut::classify_file(n);

            if (kind != walnut::FileKind::CSource && kind != walnut::FileKind::CppSource) {
                link_inputs.push_back(key_path);
                continue;
            }

            const std::string part = kind == walnut::FileKind::CSource ? cxx_cmd(o) + " " + c_flags(o) + iquotes : cxx_cmd(o) + " " + flags + iquotes;
            add_job(n, wb::hash_text("native\n" + id + "\n" + part + "\n" + key_path.string() + "\n" + wb::read_text(n)), part, key_path, {});
        }
    }

    if (!run_jobs(jobs, o, tmp)) {
        report_compile_failure(o, LinkLibs{}, tmp);
        return false;
    }

    const LinkLibs libs = link_libraries(o);
    std::string link_key = "link\n" + id + "\n" + libs.args;
    for (const std::string& k : object_keys) link_key += "\n" + k;
    for (const fs::path& p : link_inputs) link_key += "\n" + p.string();
#ifdef _WIN32
    const std::string exe_name = wb::hash_text(link_key) + ".exe";
#else
    const std::string exe_name = wb::hash_text(link_key);
#endif
    const fs::path exe = root / "exe" / exe_name;
    const fs::path exe_meta = root / "exe" / (exe_name + ".deps");
    std::vector<fs::path> link_deps = objects;
    for (const fs::path& p : link_inputs) link_deps.push_back(p);
    for (const fs::path& p : libs.files) link_deps.push_back(p);

    if (!wb::is_fresh(exe, exe_meta)) {
        const fs::path part = tmp / ("program.part" + exe.extension().string());
        std::string cmd = cxx_cmd(o) + (g_needs_cpp20 ? " -std=c++20" : " -std=c++17") + (o.debug != DebugMode::None ? " -g" : "");
        for (const fs::path& obj : objects) cmd += " " + quote(obj.string());
        for (const fs::path& in : link_inputs) cmd += " " + quote(in.string());
        cmd += " -o " + quote(part.string()) + libs.args;
        if (o.verbose) std::cerr << cmd << "\n";
        const int status = exit_status_of(run_command(cmd));

        if (status != 0 || !wb::replace_file(part, exe)) {
            std::cerr << "walnut: linking the program failed (exit status " << status << ")\n";
            report_compile_failure(o, libs, tmp);
            return false;
        }

        wb::record_dependencies(exe_meta, {}, link_deps, {});
    }

    wb::touch(exe_meta);
    exe_out = exe;
    fs::remove_all(tmp, ec);
    return true;
}

int run(const Options& o) {
    if (o.command == Command::Clean) {
        const fs::path root = walnut::build::default_cache_root();
        std::error_code ec;
        const std::uintmax_t removed = fs::remove_all(root, ec);
        if (ec) { std::cerr << "walnut: could not delete the build cache at " << root.string() << ": " << ec.message() << "\n"; return 1; }
        std::cout << "walnut: deleted the build cache at " << root.string() << " (" << removed << " files)\n";
        return 0;
    }

    if (!fs::exists(o.input)) { std::cerr << "walnut: cannot open '" << o.input << "'\n"; return 1; }
    const bool cached = (o.command == Command::Build || o.command == Command::Run) && !o.no_cache && !o.keep_cpp && !o.line_map && o.debug != DebugMode::Cpp;
    std::string raw;
    walnut::codegen::SplitProgram split;
    if (!generate(o, raw, cached ? &split : nullptr)) return 1;
    if (o.command == Command::Check) return 0;
    std::vector<LineMapEntry> map;

    if (cached) {
        if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
            std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
            return 1;
        }

        fs::path exe;
        if (!build_with_cache(o, split, exe)) return 1;

        if (o.command == Command::Build) {
            const fs::path out = o.output.empty() ? default_exe_path(o) : fs::path(o.output);
            std::error_code ec;
            if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
            fs::remove(out, ec);
            fs::copy_file(exe, out, fs::copy_options::overwrite_existing, ec);
            if (ec) { std::cerr << "walnut: cannot write '" << out.string() << "': " << ec.message() << "\n"; return 1; }
            fs::permissions(out, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
            return 0;
        }

        std::string cmd = quote(exe.string());
        for (const std::string& a : o.program_args) cmd += " " + quote(a);
        std::cout.flush();
        return exit_status_of(run_command(cmd));
    }

    if (o.command == Command::Emit) {
        const std::string name = o.output.empty() ? std::string("generated.cpp") : fs::absolute(o.output).string();
        const std::string cpp = finalize_cpp(raw, o, name, map);
        if (!write_line_map(o, name, map)) return 1;
        if (o.output.empty()) { std::cout << cpp; return 0; }
        return write_file(o.output, cpp) ? 0 : 1;
    }

    if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
        std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
        return 1;
    }

    if (o.command == Command::Build) {
        const fs::path exe = o.output.empty() ? default_exe_path(o) : fs::path(o.output);
        fs::path cpp_path;
        fs::path tmp;

        if (o.keep_cpp) {
            cpp_path = exe;
            cpp_path += ".cpp";
        } else {
            tmp = make_temp_dir();
            cpp_path = tmp / "program.cpp";
        }

        if (exe.has_parent_path()) { std::error_code ec; fs::create_directories(exe.parent_path(), ec); }
        const std::string cpp = finalize_cpp(raw, o, fs::absolute(cpp_path).string(), map);
        if (!write_line_map(o, fs::absolute(cpp_path).string(), map)) return 1;
        if (!write_file(cpp_path, cpp)) return 1;
        const bool ok = compile_cpp(o, cpp_path, exe);
        if (!tmp.empty() && ok) { std::error_code ec; fs::remove_all(tmp, ec); }
        return ok ? 0 : 1;
    }

    const fs::path tmp = make_temp_dir();
    const fs::path cpp_path = tmp / "program.cpp";
    const fs::path exe = tmp / "program";
    const std::string cpp = finalize_cpp(raw, o, cpp_path.string(), map);
    if (!o.keep_cpp && o.line_map) std::cerr << "walnut: the line map refers to a temporary file; add --keep-cpp to keep it\n";
    if (!write_line_map(o, cpp_path.string(), map)) return 1;
    if (!write_file(cpp_path, cpp)) return 1;
    if (!compile_cpp(o, cpp_path, exe)) return 1;
    std::string cmd = quote(exe.string());
    for (const std::string& a : o.program_args) cmd += " " + quote(a);
    std::cout.flush();
    const int status = exit_status_of(run_command(cmd));
    std::error_code ec;
    if (!o.keep_cpp) fs::remove_all(tmp, ec);
    else std::cerr << "walnut: generated C++ kept at " << cpp_path.string() << "\n";
    return status;
}

}

int main(int argc, char** argv) {
    Options o;
    int code = 0;
    if (!parse_args(argc, argv, o, code)) return code;

    try {
        return run(o);
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return 1;
    }
}
