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

namespace {

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

void print_usage(std::ostream& os) {
    os << "usage: walnut <command> [options] <main.wal> [more.wal ...] [C/C++ files] [-- program arguments]\n"
          "\n"
          "commands:\n"
          "  check   analyze the program and report errors and warnings\n"
          "  emit    write the generated C++ (to -o, or to standard output)\n"
          "  build   compile the program to an executable without running it (default: the input name without .wal);\n"
          "          with -c, --static or --shared it makes an object file or a library instead\n"
          "  run     build the program and run it\n"
          "  clean   delete walnut's build cache\n"
          "  highlight  print what each name and operator in the file is, as JSON, for editors (with --stdin, the file's text is read from standard input)\n"
          "  complete   print the names that fit at --at LINE:COLUMN (from 1, in bytes), as JSON, for editors' autocomplete; also takes --stdin\n"
          "  library    list the library files built into walnut; 'library <file>' prints one, 'library --extract <dir>' writes them all to a folder;\n"
          "             'library --precompile [options]' compiles every standard library module into the build cache ahead of time;\n"
          "             'library --verify [programs.wal]' checks the standard library's safe code, including the templates the programs use\n"
          "  init       set up a C++ compiler for walnut (see 'walnut init --help')\n"
          "\n"
          "options:\n"
          "  -o <path>          output path\n"
          "  -I <dir>           add an include directory for #include\n"
          "  -L <dir>, -l <lib> library search path and library to link: a Walnut library, or one for extern \"C\"/\"C++\" code\n"
          "  -D <name>[=value]  define a preprocessor macro\n"
          "  -O0 -O1 -O2 -O3    optimization level for the C++ compiler (default -O2, or -O0 with -g)\n"
          "  -g                 build for a debugger that steps through the .wal lines\n"
          "  -u, --unbuffered   make the program flush cout and clog after every write (like python -u);\n"
          "                     setting WALNUT_UNBUFFERED=1 when the program runs does the same\n"
          "  --debug-cpp        build for a debugger that steps through the generated C++ (kept next to the program)\n"
          "  --line-map[=file]  write which .wal line each generated C++ line came from (to stdout without a file)\n"
          "  --cxx <compiler>   C++ compiler used by build and run (default: " WALNUT_DEFAULT_CXX ")\n"
          "  --cxx-flag <flag>  pass an extra flag to the C++ compiler\n"
          "  --runtime <dir>    directory containing walnut_runtime.hpp and the standard modules\n"
          "  --runtime-libs <l> the libraries every program links, separated by ';' (also WALNUT_RUNTIME_LIBS);\n"
          "                     point it at another platform's fizmo archives to cross-compile, e.g. for Android\n"
          "  --optional-libs <l>, --optional-archives <l>\n"
          "                     the libraries linked only when they're installed (also WALNUT_OPTIONAL_LIBS and\n"
          "                     WALNUT_OPTIONAL_ARCHIVES); an empty list links none\n"
          "  -M <dir>           add a folder searched by 'import module.<name>' (also --module-dir)\n"
          "\n"
          "libraries (with build; the inputs can be .wal and .walh files):\n"
          "  -c                 compile to one object file (default: <input>.o) instead of a program\n"
          "  --static, --lib    make a static library (default: lib<input>.a)\n"
          "  --shared           make a shared library (lib<input>.so, lib<input>.dylib, or <input>.dll with lib<input>.dll.a)\n"
          "                     each one also writes a .walnut file next to it; keep the two together. A program that\n"
          "                     links the library, or includes a header that sits next to them, uses the compiled code\n"
          "                     instead of compiling the library's Walnut code again\n"
          "  --lib-dir <folder> put the shared libraries a program uses in this folder next to the program, and make the\n"
          "                     program find them there (rpath on Linux and macOS, a private assembly on Windows)\n"
          "  --no-prebuilt-std  compile the standard library modules with the program instead of using their\n"
          "                     precompiled copies in the build cache\n"
          "  --keep-cpp         keep the generated C++ next to the executable (builds without the cache)\n"
          "\n"
          "dumps (with emit, which prints to standard output, or build, which writes <input>.pp.wal, .s, .ll or .ii):\n"
          "  -E, --preprocessed the Walnut source after Walnut's preprocessor: #include files pasted in, macros\n"
          "                     expanded and the branches #if left out removed\n"
          "  -S, --asm          the assembly the C++ compiler makes for the program, instead of C++\n"
          "  --llvm-ir          the LLVM IR instead (needs clang as the C++ compiler)\n"
          "  --preprocessed-cpp the generated C++ with the runtime header and everything it includes pasted in\n"
          "  --asm-syntax <s>   intel or att assembly syntax (x86 only; the compiler's default is att)\n"
          "  -fverbose-asm      let the compiler add comments to the assembly that name the variables\n"
          "  --asm-function <f> only the assembly or IR of the functions named f, such as main, area,\n"
          "                     shapes::area or Rect::area (repeatable; also matches template instances)\n"
          "                     dumps leave out the standard library's code unless --no-prebuilt-std is given\n"
          "  --no-cache         build everything from scratch in a temporary folder, without the build cache\n"
          "  --random-seed <s>  make std::random calls in constexpr code give the same values every build (also WALNUT_RANDOM_SEED);\n"
          "                     without it they come from the operating system's secure random source, new each build\n"
          "  --tokens <file>    write the token list of the input to a .txt file\n"
          "  --ast <file>       write the syntax tree of the input to a .txt file\n"
          "  --errors <dest>    where errors go: stderr (default), stdout or a file path\n"
          "  --warnings <dest>  where warnings go: stderr (default), stdout or a file path\n"
          "  --no-warnings      suppress warnings\n"
          "  --warn-unused      warn about local variables that are defined but never used in their scope\n"
          "                     (names starting with _ are left alone)\n"
          "\n"
          "safety (all off unless asked, since each one makes the program slower or bigger):\n"
          "  --check-bounds     stop with the .wal line when an index is outside memory made with new[]\n"
          "  --check-null       stop with the .wal line when a null or deleted pointer is used\n"
          "  --check-overflow   stop with the .wal line when integer +, -, * or negation overflows or a cast clamps;\n"
          "                     constants that overflow or clamp become compile errors instead of warnings\n"
          "  --check-dangling   stop with the .wal line when a pointer to a local variable is used after its function returned\n"
          "  --checks           all four checks above\n"
          "  --leak-check[=warn|error]  list memory from new that was never deleted when the program ends;\n"
          "                     with =error the program also exits with code 3 (WALNUT_LEAKS=off|warn|error overrides it)\n"
          "  --gc               a garbage collector frees objects made with new once nothing points to them;\n"
          "                     delete still works and frees them right away\n"
          "  --ownership        check at compile time for use after move or delete, double delete, leaks\n"
          "                     and pointers or references to variables that no longer exist\n"
          "  --safe             --checks, --leak-check and --ownership together\n"
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
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    std::error_code ec;
    const fs::path real = fs::canonical(buf, ec);
    return (ec ? fs::path(buf) : real).parent_path();
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
    if (const char* env = std::getenv("WALNUT_CXX")) if (*env) { o.cxx = env; o.cxx_from_user = true; }
    if (const char* env = std::getenv("WALNUT_MODULE_PATH")) if (*env) o.module_env_dirs = split_on(env, kPathListSeparator);
    if (const char* env = std::getenv("WALNUT_RUNTIME_LIBS")) o.extra_libs = env;
    if (const char* env = std::getenv("WALNUT_OPTIONAL_LIBS")) o.optional_libs = env;
    if (const char* env = std::getenv("WALNUT_OPTIONAL_ARCHIVES")) o.optional_archives = env;

    exit_code = 0;
    if (argc < 2) { print_usage(std::cerr); exit_code = 2; return false; }
    const std::string cmd = argv[1];

    if (const char* env = std::getenv("WALNUT_RANDOM_SEED")) if (*env) o.random_seed = env;
    if (cmd == "-h" || cmd == "--help" || cmd == "help") { print_usage(std::cout); return false; }
    if (cmd == "check")      o.command = Command::Check;
    else if (cmd == "emit")  o.command = Command::Emit;
    else if (cmd == "build") o.command = Command::Build;
    else if (cmd == "run")   o.command = Command::Run;
    else if (cmd == "clean") { o.command = Command::Clean; return true; }
    else if (cmd == "highlight") o.command = Command::Highlight;
    else if (cmd == "complete") o.command = Command::Complete;
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
        else if (a == "-u" || a == "--unbuffered") o.unbuffered = true;
        else if (a == "--stdin")       o.from_stdin = true;
        else if (a == "--at") {
            const char* v = need(i, a);
            if (!v) return false;
            const std::string at = v;
            const std::size_t colon = at.find(':');
            char* end = nullptr;
            const unsigned long line = std::strtoul(at.c_str(), &end, 10);
            const unsigned long column = colon == std::string::npos ? 1ul : std::strtoul(at.c_str() + colon + 1, nullptr, 10);
            if (line == 0 || column == 0) { std::cerr << "walnut: --at needs LINE:COLUMN, both from 1\n"; exit_code = 2; return false; }
            o.at_line = line;
            o.at_column = column;
        }
        else if (a == "--debug-cpp")   { o.debug = DebugMode::Cpp; o.keep_cpp = true; }
        else if (a == "--line-map")    o.line_map = true;
        else if (a.rfind("--line-map=", 0) == 0) { o.line_map = true; o.line_map_path = a.substr(11); }
        else if (a == "--cxx")         { const char* v = need(i, a); if (!v) return false; o.cxx = v; o.cxx_from_user = true; }
        else if (a == "--cxx-flag")    { const char* v = need(i, a); if (!v) return false; o.cxx_flags.push_back(v); }
        else if (a == "--runtime")     { const char* v = need(i, a); if (!v) return false; o.runtime_dir = v; }
        else if (a == "--runtime-libs") { const char* v = need(i, a); if (!v) return false; o.extra_libs = v; }
        else if (a == "--optional-libs") { const char* v = need(i, a); if (!v) return false; o.optional_libs = v; }
        else if (a == "--optional-archives") { const char* v = need(i, a); if (!v) return false; o.optional_archives = v; }
        else if (a == "-M" || a == "--module-dir") { const char* v = need(i, a); if (!v) return false; o.module_dirs.push_back(v); }
        else if (a == "--keep-cpp")    o.keep_cpp = true;
        else if (a == "--no-cache")    o.no_cache = true;
        else if (a == "--random-seed") { const char* v = need(i, a); if (!v) return false; o.random_seed = v; }
        else if (a == "--tokens")      { const char* v = need(i, a); if (!v) return false; o.tokens_path = v; }
        else if (a == "--ast")         { const char* v = need(i, a); if (!v) return false; o.ast_path = v; }
        else if (a == "--errors")      { const char* v = need(i, a); if (!v) return false; o.errors_to = v; }
        else if (a == "--warnings")    { const char* v = need(i, a); if (!v) return false; o.warnings_to = v; }
        else if (a == "--no-warnings") o.no_warnings = true;
        else if (a == "--time")        o.report_timing = true;
        else if (a == "--check-bounds")   o.safety.push_back("-DWALNUT_CHECK_BOUNDS");
        else if (a == "--check-null")     o.safety.push_back("-DWALNUT_CHECK_NULL");
        else if (a == "--check-overflow") { o.safety.push_back("-DWALNUT_CHECK_OVERFLOW"); o.overflow_errors = true; }
        else if (a == "--check-dangling") o.safety.push_back("-DWALNUT_CHECK_DANGLING");
        else if (a == "--checks")      { for (const char* d : { "-DWALNUT_CHECK_BOUNDS", "-DWALNUT_CHECK_NULL", "-DWALNUT_CHECK_OVERFLOW", "-DWALNUT_CHECK_DANGLING" }) o.safety.push_back(d); o.overflow_errors = true; }
        else if (a == "--leak-check" || a == "--leak-check=warn") o.safety.push_back("-DWALNUT_LEAK_CHECK");
        else if (a == "--leak-check=error") { o.safety.push_back("-DWALNUT_LEAK_CHECK"); o.safety.push_back("-DWALNUT_LEAK_ERROR"); }
        else if (a.rfind("--leak-check=", 0) == 0) { std::cerr << "walnut: --leak-check takes warn or error, not '" << a.substr(13) << "'\n"; exit_code = 2; return false; }
        else if (a == "--gc")          o.safety.push_back("-DWALNUT_GC");
        else if (a == "--ownership")   o.ownership = true;
        else if (a == "--warn-unused") o.warn_unused = true;
        else if (a == "--safe")        { for (const char* d : { "-DWALNUT_CHECK_BOUNDS", "-DWALNUT_CHECK_NULL", "-DWALNUT_CHECK_OVERFLOW", "-DWALNUT_CHECK_DANGLING", "-DWALNUT_LEAK_CHECK" }) o.safety.push_back(d); o.ownership = true; o.overflow_errors = true; }
        else if (a == "-v" || a == "--verbose") o.verbose = true;
        else if (a == "-c")            o.out_kind = Output::Object;
        else if (a == "--static" || a == "--lib") o.out_kind = Output::Static;
        else if (a == "--shared")      o.out_kind = Output::Shared;
        else if (a == "--lib-dir")     { const char* v = need(i, a); if (!v) return false; o.lib_dir = v; }
        else if (a == "--no-prebuilt-std") o.prebuilt_std = false;
        else if (a == "-S" || a == "--asm" || a == "-E" || a == "--preprocessed" || a == "--preprocessed-cpp" || a == "--llvm-ir") {
            Dump d = Dump::Preprocessed;
            if (a == "-S" || a == "--asm") d = Dump::Assembly;
            else if (a == "--llvm-ir") d = Dump::LlvmIr;
            else if (a == "--preprocessed-cpp") d = Dump::PreprocessedCpp;
            if (o.dump != Dump::None && o.dump != d) { std::cerr << "walnut: pick one of --preprocessed (-E), --preprocessed-cpp, --asm (-S) and --llvm-ir\n"; exit_code = 2; return false; }
            o.dump = d;
        }
        else if (a == "--asm-syntax" || a.rfind("--asm-syntax=", 0) == 0) {
            std::string v;
            if (a == "--asm-syntax") { const char* n = need(i, a); if (!n) return false; v = n; }
            else v = a.substr(13);
            if (v != "intel" && v != "att") { std::cerr << "walnut: --asm-syntax takes intel or att, not '" << v << "'\n"; exit_code = 2; return false; }
            o.asm_syntax = v;
        }
        else if (a == "-fverbose-asm") o.verbose_asm = true;
        else if (a == "--asm-function" || a.rfind("--asm-function=", 0) == 0) {
            std::string v;
            if (a == "--asm-function") { const char* n = need(i, a); if (!n) return false; v = n; }
            else v = a.substr(15);
            if (v.empty()) { std::cerr << "walnut: --asm-function needs a function name\n"; exit_code = 2; return false; }
            o.asm_functions.push_back(v);
        }
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
            } else if (kind == walnut::FileKind::WalnutHeader && (o.command == Command::Highlight || o.command == Command::Complete) && o.input.empty()) {
                o.input = a;
            } else if (kind == walnut::FileKind::WalnutHeader && o.out_kind != Output::Program) {
                if (o.input.empty()) o.input = a;
                else o.extra_inputs.push_back(a);
            } else if (kind == walnut::FileKind::WalnutHeader) {
                std::cerr << "walnut: '" << name << "' is a Walnut header; #include or import it from a .wal file instead of passing it to walnut (or build it into a library with -c, --static or --shared)\n";
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

    if (o.out_kind != Output::Program && o.command != Command::Build && o.command != Command::Emit && o.command != Command::Check) {
        std::cerr << "walnut: -c, --static and --shared go with 'walnut build' (or 'emit' and 'check'); a library can't be run\n";
        exit_code = 2;
        return false;
    }

    if (o.dump != Dump::None && o.command != Command::Emit && o.command != Command::Build) {
        std::cerr << "walnut: --preprocessed, --preprocessed-cpp, --asm and --llvm-ir go with 'walnut emit' or 'walnut build'\n";
        exit_code = 2;
        return false;
    }

    if (o.dump != Dump::Assembly && (!o.asm_syntax.empty() || o.verbose_asm)) {
        std::cerr << "walnut: --asm-syntax and -fverbose-asm only change assembly output; add --asm (or -S)\n";
        exit_code = 2;
        return false;
    }

    if (!o.asm_functions.empty() && o.dump != Dump::Assembly && o.dump != Dump::LlvmIr) {
        std::cerr << "walnut: --asm-function picks functions out of assembly or LLVM IR; add --asm (or -S) or --llvm-ir\n";
        exit_code = 2;
        return false;
    }

    if (o.input.empty() && o.out_kind == Output::Program && (o.command == Command::Build || o.command == Command::Run) && !o.native_inputs.empty()) {
        o.link_only = true;
    } else if (o.input.empty()) {
        std::cerr << "walnut: no input file\n";
        exit_code = 2;
        return false;
    }

    if (!o.lib_dir.empty()) {
        const fs::path d(o.lib_dir);
        if (d.is_absolute() || d.has_parent_path() || o.lib_dir == "." || o.lib_dir == "..") {
            std::cerr << "walnut: --lib-dir takes the name of one folder next to the program, such as 'libs'\n";
            exit_code = 2;
            return false;
        }
        for (char c : o.lib_dir) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') continue;
            std::cerr << "walnut: --lib-dir names may only use letters, digits, '.', '_' and '-'\n";
            exit_code = 2;
            return false;
        }
    }
    std::sort(o.safety.begin(), o.safety.end());
    o.safety.erase(std::unique(o.safety.begin(), o.safety.end()), o.safety.end());
    for (const std::string& d : o.safety) o.cxx_flags.push_back(d);

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

struct LibraryState {
    std::unique_ptr<walnut::build::LibraryIndex>       index;
    std::vector<const walnut::build::LibraryManifest*> used;
    std::vector<std::string>                           std_files;
    std::vector<std::string>                           loaded_std;
    std::vector<walnut::build::LibrarySource>          own_sources;
    std::vector<std::string>                           dropped_inputs;
    bool                                               std_prebuilt = false;
};

LibraryState g_lib;

bool is_library_path(const std::string& path) {
    return path.rfind(walnut::SourceManager::LIBRARY_ROOT, 0) == 0;
}

std::string library_relative(const std::string& path) {
    const std::string root = std::string(walnut::SourceManager::LIBRARY_ROOT) + "/";
    return path.rfind(root, 0) == 0 ? path.substr(root.size()) : path;
}

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

bool links_native_binaries(const Options& o) {
    for (const std::string& n : o.native_inputs) {
        const walnut::FileKind k = walnut::classify_file(n);
        if (k == walnut::FileKind::Object || k == walnut::FileKind::Library) return true;
    }
    for (const std::string& l : o.native_link) if (l.rfind("-l", 0) == 0) return true;
    return false;
}

walnut::codegen::LinkOptions::Output emitter_output(const Options& o) {
    if (o.out_kind == Output::Object) return walnut::codegen::LinkOptions::Output::Object;
    if (o.out_kind == Output::Static || o.out_kind == Output::Shared) return walnut::codegen::LinkOptions::Output::Library;
    return walnut::codegen::LinkOptions::Output::Program;
}

void configure_pipeline(walnut::CompilerPipeline& compiler, const Options& o) {
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
}

bool generate(const Options& o, std::string& cpp, walnut::codegen::SplitProgram* split = nullptr, bool std_prebuilt = false) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(fs::path(o.input).is_absolute() ? walnut::PathStyle::Absolute : walnut::PathStyle::Relative);
    configure_pipeline(compiler, o);
    if (o.no_warnings) compiler.suppress_warnings();
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    walnut::build::LibraryIndex& index = *g_lib.index;
    const bool any_unresolved = o.out_kind != Output::Program || links_native_binaries(o);
    compiler.allow_unresolved_externs([&compiler, &index, any_unresolved](walnut::FileId f) {
        return any_unresolved || index.owner_of(compiler.get_sources(), f) != nullptr;
    });
    if (!o.tokens_path.empty()) compiler.output_tokens(o.tokens_path);
    if (!o.ast_path.empty()) compiler.output_ast(o.ast_path);
    if (o.report_timing) compiler.report_timing();
    if (o.ownership) compiler.enable_ownership_checks();
    if (o.warn_unused) compiler.enable_unused_warnings();
    if (o.overflow_errors) compiler.enable_overflow_errors();
    if (!route_messages(compiler.error_reporter(), o.errors_to, "--errors")) return false;
    if (!route_messages(compiler.warning_reporter(), o.warnings_to, "--warnings")) return false;

    try {
        compiler.compile();
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return false;
    }

    if (o.command == Command::Check) {
        if (!o.no_warnings) for (const std::string& w : index.warnings()) std::cerr << "walnut: warning: " << w << "\n";
        return true;
    }

    std::vector<walnut::codegen::CodegenUnit> units;
    for (const auto& u : compiler.analyzed_units()) units.push_back({ u.file, u.ast, u.root });
    walnut::codegen::CppEmitter emitter(compiler.mutable_types(), compiler.mutable_instantiator(), compiler.error_reporter());
    emitter.source_path = [&compiler](walnut::FileId f) { return compiler.get_sources().absolute_path(f); };
    const walnut::SourceManager& sources = compiler.get_sources();
    auto library_file = [&sources](walnut::FileId f) { return f < sources.file_count() && is_library_path(sources.absolute_path(f)); };
    const bool std_is_prebuilt = std_prebuilt || o.out_kind != Output::Program;
    std::vector<char> prebuilt(sources.file_count(), 0);
    g_lib.std_files.clear();
    g_lib.loaded_std.clear();
    g_lib.own_sources.clear();

    for (walnut::FileId f = 0; f < sources.file_count(); ++f) {
        const std::string& path = sources.absolute_path(f);
        const walnut::FileKind kind = walnut::classify_file(path);
        if (!walnut::is_walnut_file(kind)) continue;

        if (library_file(f)) {
            g_lib.loaded_std.push_back(library_relative(path));
            if (!std_is_prebuilt) continue;
            prebuilt[f] = 1;
            g_lib.std_files.push_back(library_relative(path));
        } else if (index.owner_of(sources, f)) {
            prebuilt[f] = 1;
        } else {
            g_lib.own_sources.push_back(walnut::build::LibrarySource{ index.hash_of(sources, f), path });
        }
    }

    if (!o.no_warnings) for (const std::string& w : index.warnings()) std::cerr << "walnut: warning: " << w << "\n";

    g_lib.used = index.used();
    g_lib.std_prebuilt = std_is_prebuilt;
    emitter.link.output = emitter_output(o);
    emitter.link.shared_definitions = o.out_kind != Output::Program || std_is_prebuilt || !g_lib.used.empty() || links_native_binaries(o);
    emitter.link.prebuilt = [prebuilt](walnut::FileId f) { return f < prebuilt.size() && prebuilt[f]; };
    emitter.link.pinned = [library_file, &o](walnut::FileId f) { return library_file(f) || o.out_kind != Output::Program; };
    emitter.link.file_key = [&index, &sources](walnut::FileId f) { return f < sources.file_count() ? index.hash_of(sources, f) : std::string(); };
    emitter.link.own_init = library_file;
    std::error_code runtime_ec;
    const std::string runtime_root = fs::weakly_canonical(fs::path(o.runtime_dir), runtime_ec).generic_string();
    auto tracked = std::make_shared<std::unordered_map<walnut::FileId, bool>>();
    emitter.tracks_lines = [&compiler, runtime_root, tracked](walnut::FileId f) {
        if (auto it = tracked->find(f); it != tracked->end()) return it->second;
        const std::string path = compiler.get_sources().absolute_path(f);
        bool user = !path.empty() && path.rfind(walnut::SourceManager::LIBRARY_ROOT, 0) != 0;
        if (user && !runtime_root.empty()) {
            std::error_code ec;
            const std::string p = fs::weakly_canonical(fs::path(path), ec).generic_string();
            if (!ec && p.size() > runtime_root.size() && p.compare(0, runtime_root.size(), runtime_root) == 0 && (p[runtime_root.size()] == '/' || runtime_root.back() == '/')) user = false;
        }
        tracked->emplace(f, user);
        return user;
    };
    emitter.unbuffered = o.unbuffered;
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
    std::cerr << "walnut: Walnut for Windows is built with llvm-mingw, so programs need that compiler too:\n"
              << "walnut:     'walnut init' downloads it (about 190 MB), or use the Walnut download that ends in -with-compiler\n";
#elif defined(__APPLE__)
    std::cerr << "walnut: on macOS, install Apple's Command Line Tools with 'xcode-select --install'\n";
#else
    std::cerr << "walnut: install one, for example 'sudo apt install g++' (Debian/Ubuntu), 'sudo dnf install gcc-c++' (Fedora) or 'sudo pacman -S gcc' (Arch)\n";
#endif
    std::cerr << "walnut: or pick a compiler with --cxx <compiler> or the WALNUT_CXX environment variable\n";
    std::cerr << "walnut: 'walnut init' sets one up for you\n";
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

#if defined(__APPLE__)
    std::string sdk = capture_command("xcrun --show-sdk-path 2>/dev/null");
    while (!sdk.empty() && (sdk.back() == '\n' || sdk.back() == '\r' || sdk.back() == ' ')) sdk.pop_back();
    if (!sdk.empty()) dirs.emplace_back(fs::path(sdk) / "usr" / "lib");
#endif


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

struct LibraryLink {
    std::vector<fs::path>    binaries;
    std::vector<std::string> args;
    std::vector<fs::path>    runtime_files;
};

LibraryLink walnut_library_links(const Options& o);
std::string rpath_args(const Options& o, const LibraryLink& libs);
bool build_std_objects(const Options& o, std::vector<fs::path>& objects);

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
            if (std::find(g_lib.dropped_inputs.begin(), g_lib.dropped_inputs.end(), n) != g_lib.dropped_inputs.end()) continue;
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

    std::vector<fs::path> std_objects;
    if (!build_std_objects(o, std_objects)) return false;
    for (const fs::path& obj : std_objects) cmd += " " + quote(obj.string());
    cmd += " -o " + quote(exe_path.string());
    const LibraryLink walnut_libs = walnut_library_links(o);
    for (const fs::path& b : walnut_libs.binaries) cmd += " " + quote(b.string());
    for (const std::string& a : walnut_libs.args) cmd += " " + quote(a);
    cmd += rpath_args(o, walnut_libs);
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

#if !defined(_WIN32) && !defined(__APPLE__)
        if (!present.empty()) cmd += " -Wl,--as-needed";
#endif
        for (const std::string& lib : present) cmd += " " + quote(lib);
#if !defined(_WIN32) && !defined(__APPLE__)
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
    fs::path p = fs::path(o.input.empty() && !o.native_inputs.empty() ? o.native_inputs.front() : o.input).filename();
    p.replace_extension("");
#ifdef _WIN32
    p.replace_extension(".exe");
#endif
    return p;
}

fs::path exe_output_path(const Options& o) {
    if (o.output.empty()) return default_exe_path(o);
    fs::path p(o.output);
#ifdef _WIN32
    if (!p.has_extension()) p += ".exe";
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
    const bool keep_markers = o.debug == DebugMode::Walnut || !o.safety.empty();
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
#if !defined(_WIN32)
    if (o.out_kind == Output::Shared || o.out_kind == Output::Static) flags += " -fPIC";
#endif
    flags += " -I" + quote(o.runtime_dir);
    for (const std::string& inc : split_list(o.extra_includes)) flags += " -I" + quote(inc);
    for (const std::string& f : o.cxx_flags) flags += " " + f;
    return flags;
}

std::string c_flags(const Options& o) {
    const std::string opt = (o.debug != DebugMode::None && !o.opt_set) ? "-O0" : o.opt;
    std::string flags = "-x c " + opt + (o.debug != DebugMode::None ? " -g" : "") + " -w";
#if !defined(_WIN32)
    if (o.out_kind == Output::Shared || o.out_kind == Output::Static) flags += " -fPIC";
#endif
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

#if !defined(_WIN32) && !defined(__APPLE__)
    if (!present.empty()) out.args += " -Wl,--as-needed";
#endif
    for (const std::string& lib : present) {
        out.args += " " + quote(lib);
        if (fs::path(lib).is_absolute() && fs::exists(lib, ec)) out.files.emplace_back(lib);
    }
#if !defined(_WIN32) && !defined(__APPLE__)
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
    fs::path              ignore;
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
                if (j.ok) walnut::build::record_dependencies(j.meta, j.depfile, j.extra_deps, j.ignore.empty() ? std::vector<fs::path>{ tmp } : std::vector<fs::path>{ tmp, j.ignore });
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

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    return s;
}

std::string target_triple(const Options& o) {
    static std::string cached;
    static std::string cached_for;
    if (cached_for == o.cxx) return cached;
    cached_for = o.cxx;
    cached = trimmed(capture_command(cxx_cmd(o) + " -dumpmachine"));
    return cached;
}

walnut::build::LibraryAbi current_abi(const Options& o) {
    walnut::build::LibraryAbi abi;
    abi.walnut_version = WALNUT_VERSION;
    abi.target = (o.command == Command::Build || o.command == Command::Run) && find_program(o.cxx) ? target_triple(o) : std::string();
    std::vector<std::string> checks;
    for (const std::string& d : o.safety) {
        std::string name = d.rfind("-DWALNUT_", 0) == 0 ? d.substr(9) : d;
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        checks.push_back(name);
    }
    std::sort(checks.begin(), checks.end());
    for (const std::string& c : checks) abi.safety += (abi.safety.empty() ? "" : ",") + c;
    std::vector<std::string> defines;
    for (const auto& [name, value] : o.defines) defines.push_back(name + "=" + value);
    std::sort(defines.begin(), defines.end());
    defines.erase(std::unique(defines.begin(), defines.end()), defines.end());
    for (const std::string& d : defines) abi.defines += (abi.defines.empty() ? "" : ",") + d;
    return abi;
}

const walnut_library::File* library_file_named(const std::string& rel) {
    for (const walnut_library::File& f : walnut_library::files) if (f.path && rel == f.path) return &f;
    return nullptr;
}

std::string std_module_import(const std::string& rel) {
    if (rel == "core.walh" || rel == "optional.walh") return std::string();
    std::string dotted = fs::path(rel).replace_extension("").generic_string();
    for (char& c : dotted) if (c == '/') c = '.';
    return "import module." + dotted + " as walnut_std_unit;\n";
}

bool generate_std_unit(const Options& base, const std::string& rel, const fs::path& dir, walnut::codegen::SplitProgram& prog, std::vector<walnut::codegen::NativeCode>& native_code, std::vector<std::string>& native_files, bool& cpp20) {
    Options o = base;
    o.input = (dir / "unit.wal").generic_string();
    o.extra_inputs.clear();
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(walnut::PathStyle::Absolute);
    configure_pipeline(compiler, o);
    compiler.suppress_warnings();
    compiler.allow_unresolved_externs([](walnut::FileId) { return true; });
    compiler.set_input_text(std_module_import(rel));

    try {
        compiler.compile();
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return false;
    }

    if (compiler.error_reporter().has_errors()) {
        compiler.error_reporter().flush();
        return false;
    }

    std::vector<walnut::codegen::CodegenUnit> units;
    for (const auto& u : compiler.analyzed_units()) units.push_back({ u.file, u.ast, u.root });
    walnut::codegen::CppEmitter emitter(compiler.mutable_types(), compiler.mutable_instantiator(), compiler.error_reporter());
    const walnut::SourceManager& sources = compiler.get_sources();
    emitter.source_path = [&sources](walnut::FileId f) { return sources.absolute_path(f); };
    emitter.tracks_lines = [](walnut::FileId) { return false; };
    emitter.path_of_text = [&sources](const char* p) {
        const std::optional<walnut::FileId> f = sources.file_containing(p);
        return f ? sources.absolute_path(*f) : std::string();
    };
    walnut::build::LibraryIndex keys(walnut::build::LibraryAbi{});
    auto library_file = [&sources](walnut::FileId f) { return f < sources.file_count() && is_library_path(sources.absolute_path(f)); };
    emitter.link.output = walnut::codegen::LinkOptions::Output::Library;
    emitter.link.shared_definitions = true;
    emitter.link.prebuilt = [&sources, library_file, rel](walnut::FileId f) { return library_file(f) && library_relative(sources.absolute_path(f)) != rel; };
    emitter.link.pinned = [](walnut::FileId) { return true; };
    emitter.link.file_key = [&keys, &sources](walnut::FileId f) { return f < sources.file_count() ? keys.hash_of(sources, f) : std::string(); };
    emitter.link.own_init = library_file;
    bool ok = false;

    try {
        ok = emitter.emit_split(units, prog);
    } catch (const std::exception& e) {
        compiler.error_reporter().report(walnut::ErrorPhase::Semantic, walnut::INVALID_FILE, 0, std::string("Internal compiler error during code generation: ") + e.what());
    }

    if (!ok || compiler.error_reporter().has_errors()) {
        compiler.error_reporter().flush();
        return false;
    }

    native_code = emitter.native_code();
    native_files = emitter.native_files();
    cpp20 = emitter.uses_coroutines();
    return true;
}

std::string native_code_text(const walnut::codegen::NativeCode& nc) {
    std::string escaped;
    for (char ch : nc.path) { if (ch == '\\' || ch == '"') escaped += '\\'; escaped += ch; }
    return "#line " + std::to_string(nc.line) + " \"" + escaped + "\"\n" + nc.text + "\n";
}

struct PendingUnit {
    fs::path              dir;
    fs::path              staging;
    std::vector<fs::path> objects;
};

std::vector<const walnut::build::LibraryManifest*> linked_manifests() {
    std::vector<const walnut::build::LibraryManifest*> out = g_lib.used;
    if (!g_lib.index) return out;
    for (const walnut::build::LibraryManifest* m : g_lib.index->explicit_libraries()) {
        if (g_lib.index->is_compatible(m) && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    }
    return out;
}

std::vector<std::string> wanted_std_units() {
    std::vector<std::string> wanted = g_lib.std_prebuilt ? g_lib.std_files : std::vector<std::string>{};

    for (const walnut::build::LibraryManifest* m : linked_manifests()) {
        for (const std::string& rel : m->std_modules) {
            if (std::find(wanted.begin(), wanted.end(), rel) != wanted.end()) continue;
            if (!g_lib.std_prebuilt && std::find(g_lib.loaded_std.begin(), g_lib.loaded_std.end(), rel) != g_lib.loaded_std.end()) continue;
            wanted.push_back(rel);
        }
    }

    return wanted;
}

bool prepare_std_units(const Options& o, ObjectSet& set, std::vector<CompileJob>& jobs, std::vector<PendingUnit>& pending) {
    namespace wb = walnut::build;
    const std::vector<std::string> wanted = wanted_std_units();

    std::error_code ec;

    for (const std::string& rel : wanted) {
        const walnut_library::File* lib = library_file_named(rel);
        if (!lib) continue;
        const std::string text(reinterpret_cast<const char*>(lib->data), lib->size);
        const std::string key = wb::hash_text("std\n" + set.id + "\n" + set.flags + "\n" + current_abi(o).defines + "\n" + rel + "\n" + wb::content_hash(text));
        const fs::path dir = set.root / "std" / key;
        const fs::path list = dir / "objects.txt";

        if (fs::is_regular_file(list, ec)) {
            std::vector<fs::path> objs;
            bool complete = true;
            std::istringstream lines(wb::read_text(list));
            std::string line;
            while (std::getline(lines, line)) {
                if (line.empty()) continue;
                const fs::path obj = dir / line;
                if (!wb::is_fresh(obj, dir / (obj.stem().string() + ".deps"))) { complete = false; break; }
                objs.push_back(obj);
            }
            if (complete) {
                wb::touch(dir);
                for (const fs::path& obj : objs) set.std_objects.push_back(obj);
                continue;
            }
        }

        walnut::codegen::SplitProgram prog;
        std::vector<walnut::codegen::NativeCode> code;
        std::vector<std::string> files;
        bool cpp20 = false;
        const fs::path staging = fs::path(dir.string() + ".part-" + set.tmp.filename().string());
        fs::remove_all(staging, ec);
        fs::create_directories(staging / "src", ec);

        if (!generate_std_unit(o, rel, staging, prog, code, files, cpp20)) {
            std::cerr << "walnut: precompiling the standard library module '" << rel << "' failed; --no-prebuilt-std compiles it with the program instead\n";
            return false;
        }

        std::string flags = set.flags;
        if (cpp20 && flags.find("-std=c++17") != std::string::npos) flags.replace(flags.find("-std=c++17"), 10, "-std=c++20");
        std::vector<LineMapEntry> unused;
        PendingUnit unit;
        unit.dir = dir;
        unit.staging = staging;
        if (!write_file(staging / "src" / "program.hpp", finalize_cpp(prog.header, o, (staging / "src" / "program.hpp").string(), unused))) return false;

        auto add = [&](const std::string& name, const std::string& command_part, const fs::path& src) {
            const fs::path obj = staging / (fs::path(name).stem().string() + ".o");
            CompileJob j;
            j.label = "the standard library module " + rel;
            j.output = obj;
            j.ignore = staging;
            j.part = set.tmp / (key + "_" + fs::path(name).stem().string() + ".o.part");
            j.meta = staging / (fs::path(name).stem().string() + ".deps");
            j.depfile = set.tmp / (key + "_" + fs::path(name).stem().string() + ".d");
            j.log = set.tmp / (key + "_" + fs::path(name).stem().string() + ".log");
            j.command = command_part + " -c " + quote(src.string()) + " -o " + quote(j.part.string()) + " -MMD -MF " + quote(j.depfile.string());
            jobs.push_back(std::move(j));
            unit.objects.push_back(dir / obj.filename());
        };

        for (const auto& [name, body] : prog.sources) {
            const fs::path src = staging / "src" / name;
            if (!write_file(src, finalize_cpp(body, o, src.string(), unused))) return false;
            add(name, cxx_cmd(o) + " " + (cpp20 ? std::string() : set.pch_flag) + flags, src);
        }

        for (std::size_t i = 0; i < code.size(); ++i) {
            const std::string name = "code" + std::to_string(i) + (code[i].is_c ? ".c" : ".cpp");
            const fs::path src = staging / "src" / name;
            if (!write_file(src, native_code_text(code[i]))) return false;
            add(name, code[i].is_c ? cxx_cmd(o) + " " + c_flags(o) : cxx_cmd(o) + " " + flags, src);
        }

        for (const std::string& n : files) {
            const walnut::FileKind kind = walnut::classify_file(n);
            if (kind != walnut::FileKind::CSource && kind != walnut::FileKind::CppSource) continue;
            add("native_" + fs::path(n).stem().string() + (kind == walnut::FileKind::CSource ? ".c" : ".cpp"), kind == walnut::FileKind::CSource ? cxx_cmd(o) + " " + c_flags(o) : cxx_cmd(o) + " " + flags, fs::path(n));
        }

        for (const fs::path& obj : unit.objects) set.std_objects.push_back(obj);
        pending.push_back(std::move(unit));
    }

    return true;
}

void finish_std_units(const std::vector<PendingUnit>& pending) {
    for (const PendingUnit& u : pending) {
        std::string list;
        for (const fs::path& obj : u.objects) list += obj.filename().string() + "\n";
        write_file(u.staging / "objects.txt", list);
        std::error_code ec;
        fs::remove_all(u.dir, ec);
        ec.clear();
        fs::rename(u.staging, u.dir, ec);
        if (ec) fs::remove_all(u.staging, ec);
    }
}

void discard_std_units(const std::vector<PendingUnit>& pending) {
    for (const PendingUnit& u : pending) {
        std::error_code ec;
        fs::remove_all(u.staging, ec);
    }
}

bool build_std_objects(const Options& o, std::vector<fs::path>& objects) {
    namespace wb = walnut::build;
    if (wanted_std_units().empty()) return true;
    ObjectSet set;
    set.root = wb::default_cache_root();
    std::error_code ec;
    fs::create_directories(set.root / "std", ec);
    set.tmp = make_temp_dir();
    std::string id = capture_command(cxx_cmd(o) + " --version");
    id = id.substr(0, id.find('\n'));
    set.clang = id.find("clang") != std::string::npos;
    set.id = o.cxx + "\n" + id + "\n" + __DATE__ " " __TIME__;
    set.flags = compile_flags(o);
    std::vector<CompileJob> jobs;
    std::vector<PendingUnit> pending;
    if (!prepare_std_units(o, set, jobs, pending)) { discard_std_units(pending); return false; }
    if (!run_jobs(jobs, o, set.tmp)) { discard_std_units(pending); return false; }
    finish_std_units(pending);
    objects = set.std_objects;
    fs::remove_all(set.tmp, ec);
    return true;
}

std::string single_translation_unit(const walnut::codegen::SplitProgram& prog) {
    const std::string prelude = "#include \"walnut_runtime.hpp\"\n#include \"program.hpp\"\n";
    std::string out = "#include \"walnut_runtime.hpp\"\n" + prog.header;

    for (const auto& [name, text] : prog.sources) {
        out += "\n";
        out += text.rfind(prelude, 0) == 0 ? text.substr(prelude.size()) : text;
    }

    return out;
}

bool compile_objects(const Options& o, const walnut::codegen::SplitProgram& prog, ObjectSet& set, bool single_tu, bool with_std = true) {
    namespace wb = walnut::build;

    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return false;
    }

    set.root = wb::default_cache_root();
    std::error_code ec;
    for (const char* sub : { "obj", "exe", "pch", "std" }) fs::create_directories(set.root / sub, ec);
    wb::prune(set.root, 30);

    set.tmp = make_temp_dir();
    std::string id = capture_command(cxx_cmd(o) + " --version");
    id = id.substr(0, id.find('\n'));
    set.clang = id.find("clang") != std::string::npos;
    set.id = o.cxx + "\n" + id + "\n" + __DATE__ " " __TIME__;
    set.flags = compile_flags(o);
    const std::string& flags = set.flags;
    const fs::path& root = set.root;
    const fs::path& tmp = set.tmp;

    {
        const std::string key = wb::hash_text("pch\n" + set.id + "\n" + flags);
        const fs::path dir = root / "pch" / key;
        const fs::path file = dir / (set.clang ? "walnut_runtime.pch" : "walnut_runtime.hpp.gch");
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
            set.pch_flag = set.clang ? "-include-pch " + quote(file.string()) + " " : "-I" + quote(dir.string()) + " ";
        }
    }

    const fs::path pch_file = set.pch_flag.empty() ? fs::path() : root / "pch" / wb::hash_text("pch\n" + set.id + "\n" + flags) / (set.clang ? "walnut_runtime.pch" : "walnut_runtime.hpp.gch");
    std::vector<CompileJob> jobs;
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
        set.objects.push_back(obj);
        set.object_keys.push_back(key);
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
    std::vector<fs::path> extra;
    if (!pch_file.empty()) extra.push_back(pch_file);

    if (single_tu) {
        const fs::path src = tmp / "object.cpp";
        const std::string body = finalize_cpp(single_translation_unit(prog), o, src.string(), unused);
        if (!write_file(src, body)) return false;
        add_job("the generated C++", wb::hash_text("object\n" + set.id + "\n" + flags + "\n" + body), cxx_cmd(o) + " " + set.pch_flag + flags + iquotes, src, extra);
    } else {
        for (const auto& [name, text] : prog.sources) {
            const fs::path src = tmp / name;
            const std::string body = finalize_cpp(text, o, src.string(), unused);
            if (!write_file(src, body)) return false;
            const std::string key = wb::hash_text("tu\n" + set.id + "\n" + flags + "\n" + header + "\n" + body);
            add_job("the generated C++ (" + name + ")", key, cxx_cmd(o) + " " + set.pch_flag + flags + iquotes, src, extra);
        }
    }

    for (std::size_t i = 0; i < g_native_code.size(); ++i) {
        const walnut::codegen::NativeCode& nc = g_native_code[i];
        const fs::path src = tmp / ("code" + std::to_string(i) + (nc.is_c ? ".c" : ".cpp"));
        const std::string text = native_code_text(nc);
        if (!write_file(src, text)) return false;
        const std::string part = nc.is_c ? cxx_cmd(o) + " " + c_flags(o) + iquotes : cxx_cmd(o) + " " + flags + iquotes;
        add_job("the code block in " + nc.path, wb::hash_text("code\n" + set.id + "\n" + part + "\n" + text), part, src, {});
    }

    std::vector<fs::path> seen;

    for (const std::vector<std::string>* list : std::initializer_list<const std::vector<std::string>*>{ &o.native_inputs, &g_native_from }) {
        for (const std::string& n : *list) {
            if (std::find(g_lib.dropped_inputs.begin(), g_lib.dropped_inputs.end(), n) != g_lib.dropped_inputs.end()) continue;
            fs::path key_path = fs::weakly_canonical(n, ec);
            if (ec) key_path = fs::absolute(n, ec);
            if (std::find(seen.begin(), seen.end(), key_path) != seen.end()) continue;
            seen.push_back(key_path);
            const walnut::FileKind kind = walnut::classify_file(n);

            if (kind != walnut::FileKind::CSource && kind != walnut::FileKind::CppSource) {
                set.link_inputs.push_back(key_path);
                continue;
            }

            const std::string part = kind == walnut::FileKind::CSource ? cxx_cmd(o) + " " + c_flags(o) + iquotes : cxx_cmd(o) + " " + flags + iquotes;
            add_job(n, wb::hash_text("native\n" + set.id + "\n" + part + "\n" + key_path.string() + "\n" + wb::read_text(n)), part, key_path, {});
        }
    }

    std::vector<PendingUnit> pending;
    if (with_std && !prepare_std_units(o, set, jobs, pending)) {
        discard_std_units(pending);
        return false;
    }

    if (!run_jobs(jobs, o, tmp)) {
        discard_std_units(pending);
        report_compile_failure(o, LinkLibs{}, tmp);
        return false;
    }

    finish_std_units(pending);
    return true;
}

LibraryLink walnut_library_links(const Options& o) {
    LibraryLink out;
    std::error_code ec;
    std::vector<fs::path> inputs;

    for (const std::string& n : o.native_inputs) {
        fs::path p = fs::weakly_canonical(n, ec);
        if (ec) p = fs::absolute(n, ec);
        inputs.push_back(p);
    }

    for (const walnut::build::LibraryManifest* m : linked_manifests()) {
        for (const std::string& b : m->binaries) {
            fs::path p = fs::weakly_canonical(m->dir() / b, ec);
            if (ec) p = m->dir() / b;
            if (std::find(inputs.begin(), inputs.end(), p) != inputs.end()) continue;
            if (std::find(out.binaries.begin(), out.binaries.end(), p) != out.binaries.end()) continue;
            out.binaries.push_back(p);
        }

        for (const std::string& l : m->link) {
            if (std::find(out.args.begin(), out.args.end(), l) != out.args.end()) continue;
            if (!l.empty() && l[0] != '-') {
                fs::path p = fs::weakly_canonical(l, ec);
                if (ec) p = fs::path(l);
                if (std::find(inputs.begin(), inputs.end(), p) != inputs.end()) continue;
                if (std::find(out.binaries.begin(), out.binaries.end(), p) != out.binaries.end()) continue;
                out.binaries.push_back(p);
                continue;
            }
            out.args.push_back(l);
        }

        for (const std::string& r : m->runtime_files) {
            fs::path p = fs::weakly_canonical(m->dir() / r, ec);
            if (ec) p = m->dir() / r;
            if (std::find(out.runtime_files.begin(), out.runtime_files.end(), p) == out.runtime_files.end()) out.runtime_files.push_back(p);
        }
    }

    return out;
}

std::string rpath_args(const Options& o, const LibraryLink& libs) {
    std::string out;
#if defined(_WIN32)
    (void)o;
    (void)libs;
#else
    std::vector<std::string> dirs;
    for (const fs::path& r : libs.runtime_files) {
        const std::string d = r.parent_path().string();
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
    }
    if (dirs.empty() && o.lib_dir.empty()) return out;
#if defined(__APPLE__)
    const std::string origin = "@executable_path";
#else
    const std::string origin = "$ORIGIN";
#endif
    if (!o.lib_dir.empty()) out += " " + quote("-Wl,-rpath," + origin + "/" + o.lib_dir);
    out += " " + quote("-Wl,-rpath," + origin);
    for (const std::string& d : dirs) out += " " + quote("-Wl,-rpath," + d);
#endif
    return out;
}

std::string find_tool(const Options& o, const std::vector<std::string>& names) {
    const fs::path cxx(o.cxx);
    std::string prefix;
    const std::string file = cxx.filename().string();
    for (const char* tail : { "clang++", "g++", "c++", "clang", "gcc" }) {
        const std::string t = tail;
        const std::size_t at = file.rfind(t);
        if (at != std::string::npos && at > 0) { prefix = file.substr(0, at); break; }
    }
    std::error_code ec;

    for (const std::string& n : names) {
        std::vector<std::string> tries;
        if (!prefix.empty()) tries.push_back(prefix + n);
        tries.push_back(n);
        for (const std::string& t : tries) {
            if (cxx.has_parent_path()) {
                for (const char* ext : { "", ".exe" }) {
                    const fs::path p = cxx.parent_path() / (t + ext);
                    if (fs::is_regular_file(p, ec)) return p.string();
                }
            }
            if (find_program(t)) return t;
        }
    }

    return std::string();
}

std::string find_archiver(const Options& o) {
    if (const char* env = std::getenv("WALNUT_AR")) if (*env) return env;
    std::string ar = find_tool(o, { "ar", "llvm-ar" });
    if (!ar.empty()) return ar;
    const std::string printed = trimmed(capture_command(cxx_cmd(o) + " -print-prog-name=ar"));
    std::error_code ec;
    if (!printed.empty() && (fs::is_regular_file(printed, ec) || find_program(printed))) return printed;
    return std::string();
}

std::string windows_resource_object(const Options& o, const fs::path& tmp) {
    if (o.lib_dir.empty()) return std::string();
    const std::string windres = find_tool(o, { "windres", "llvm-windres" });
    if (windres.empty()) {
        std::cerr << "walnut: --lib-dir needs windres (or llvm-windres) next to the C++ compiler to give the program its manifest\n";
        return "!";
    }
    const fs::path manifest = tmp / "program.manifest";
    const fs::path rc = tmp / "program.rc";
    const fs::path obj = tmp / "program_manifest.o";
    write_file(manifest,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
        "  <dependency>\n"
        "    <dependentAssembly>\n"
        "      <assemblyIdentity type=\"win32\" name=\"" + o.lib_dir + "\" version=\"1.0.0.0\"/>\n"
        "    </dependentAssembly>\n"
        "  </dependency>\n"
        "</assembly>\n");
    std::string escaped;
    for (char c : manifest.generic_string()) { if (c == '\\' || c == '"') escaped += '\\'; escaped += c; }
    write_file(rc, "1 24 \"" + escaped + "\"\n");
    const std::string cmd = quote(windres) + " " + quote(rc.string()) + " -O coff -o " + quote(obj.string());
    if (o.verbose) std::cerr << cmd << "\n";
    if (exit_status_of(run_command(cmd)) != 0) {
        std::cerr << "walnut: windres failed to make the program's manifest for --lib-dir\n";
        return "!";
    }
    return obj.string();
}

bool place_runtime_files(const Options& o, const fs::path& exe, const LibraryLink& libs) {
    if (libs.runtime_files.empty()) return true;
    std::error_code ec;
    const fs::path base = exe.has_parent_path() ? exe.parent_path() : fs::path(".");
#if defined(_WIN32)
    const fs::path dir = o.lib_dir.empty() ? base : base / o.lib_dir;
#else
    if (o.lib_dir.empty()) return true;
    const fs::path dir = base / o.lib_dir;
#endif
    fs::create_directories(dir, ec);

    for (const fs::path& r : libs.runtime_files) {
        fs::copy_file(r, dir / r.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) { std::cerr << "walnut: cannot copy " << r.string() << " to " << dir.string() << ": " << ec.message() << "\n"; return false; }
    }

#if defined(_WIN32)
    if (!o.lib_dir.empty()) {
        std::string m = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
                        "  <assemblyIdentity type=\"win32\" name=\"" + o.lib_dir + "\" version=\"1.0.0.0\"/>\n";
        for (const fs::path& r : libs.runtime_files) m += "  <file name=\"" + r.filename().string() + "\"/>\n";
        m += "</assembly>\n";
        if (!write_file(dir / (o.lib_dir + ".manifest"), m)) return false;
    }
#endif
    return true;
}

bool build_with_cache(const Options& o, const walnut::codegen::SplitProgram& prog, fs::path& exe_out, LibraryLink& libs_out) {
    namespace wb = walnut::build;
    ObjectSet set;
    if (!compile_objects(o, prog, set, false)) return false;
    std::error_code ec;
    const fs::path& root = set.root;
    const fs::path& tmp = set.tmp;
    const LibraryLink walnut_libs = walnut_library_links(o);
    libs_out = walnut_libs;
    const LinkLibs libs = link_libraries(o);
    std::string extra_args;
    for (const fs::path& b : walnut_libs.binaries) extra_args += " " + quote(b.string());
    for (const std::string& a : walnut_libs.args) extra_args += " " + quote(a);
    extra_args += rpath_args(o, walnut_libs);
#ifdef _WIN32
    const std::string resource = windows_resource_object(o, tmp);
    if (resource == "!") return false;
    if (!resource.empty()) extra_args += " " + quote(resource);
#endif
    std::string link_key = "link\n" + set.id + "\n" + libs.args + "\n" + extra_args + "\n" + o.lib_dir;
    for (const std::string& k : set.object_keys) link_key += "\n" + k;
    for (const fs::path& p : set.link_inputs) link_key += "\n" + p.string();
    for (const fs::path& p : set.std_objects) link_key += "\n" + p.string();
#ifdef _WIN32
    const std::string exe_name = wb::hash_text(link_key) + ".exe";
#else
    const std::string exe_name = wb::hash_text(link_key);
#endif
    const fs::path exe = root / "exe" / exe_name;
    const fs::path exe_meta = root / "exe" / (exe_name + ".deps");
    std::vector<fs::path> link_deps = set.objects;
    for (const fs::path& p : set.link_inputs) link_deps.push_back(p);
    for (const fs::path& p : set.std_objects) link_deps.push_back(p);
    for (const fs::path& p : walnut_libs.binaries) link_deps.push_back(p);
    for (const fs::path& p : libs.files) link_deps.push_back(p);

    if (!wb::is_fresh(exe, exe_meta)) {
        const fs::path part = tmp / ("program.part" + exe.extension().string());
        std::string cmd = cxx_cmd(o) + (g_needs_cpp20 ? " -std=c++20" : " -std=c++17") + (o.debug != DebugMode::None ? " -g" : "");
        for (const fs::path& obj : set.objects) cmd += " " + quote(obj.string());
        for (const fs::path& obj : set.std_objects) cmd += " " + quote(obj.string());
        for (const fs::path& in : set.link_inputs) cmd += " " + quote(in.string());
        cmd += " -o " + quote(part.string()) + extra_args + libs.args;
#ifdef _WIN32
        if (o.managed_cxx && std::find(o.cxx_flags.begin(), o.cxx_flags.end(), "-shared") == o.cxx_flags.end()) cmd += " -static";
#endif
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

bool build_with_cache(const Options& o, const walnut::codegen::SplitProgram& prog, fs::path& exe_out) {
    LibraryLink libs;
    return build_with_cache(o, prog, exe_out, libs);
}

int precompile_std(const Options& o) {
    g_lib.index = std::make_unique<walnut::build::LibraryIndex>(current_abi(o));
    g_lib.std_prebuilt = true;
    g_lib.std_files.clear();
    for (const walnut_library::File& f : walnut_library::files) {
        if (f.path && walnut::classify_file(f.path) == walnut::FileKind::WalnutHeader) g_lib.std_files.push_back(f.path);
    }

    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return 1;
    }

    std::vector<fs::path> objects;
    if (!build_std_objects(o, objects)) return 1;
    std::cout << "walnut: the " << g_lib.std_files.size() << " standard library modules are precompiled for " << o.cxx << " (" << objects.size() << " object files in " << (walnut::build::default_cache_root() / "std").string() << ")\n";
    return 0;
}

fs::path library_output_path(const Options& o) {
    const std::string stem = o.input.empty() ? std::string("out") : fs::path(o.input).stem().string();
    fs::path p = o.output.empty() ? fs::path() : fs::path(o.output);

    if (o.out_kind == Output::Object) {
        if (p.empty()) p = stem + ".o";
        else if (!p.has_extension()) p += ".o";
    } else if (o.out_kind == Output::Static) {
        if (p.empty()) p = "lib" + stem + ".a";
        else if (!p.has_extension()) p += ".a";
    } else {
#if defined(_WIN32)
        if (p.empty()) p = stem + ".dll";
        else if (!p.has_extension()) p += ".dll";
#elif defined(__APPLE__)
        if (p.empty()) p = "lib" + stem + ".dylib";
        else if (!p.has_extension()) p += ".dylib";
#else
        if (p.empty()) p = "lib" + stem + ".so";
        else if (!p.has_extension()) p += ".so";
#endif
    }

    return p;
}

std::string relative_to(const fs::path& path, const fs::path& base) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, base, ec);
    if (ec || rel.empty()) return fs::path(path).generic_string();
    return rel.generic_string();
}

bool build_library(const Options& o, const walnut::codegen::SplitProgram& prog) {
    namespace wb = walnut::build;
    const bool object = o.out_kind == Output::Object;
    ObjectSet set;
    if (!compile_objects(o, prog, set, object, o.out_kind == Output::Shared)) return false;
    std::error_code ec;
    const fs::path out = library_output_path(o);
    const fs::path out_abs = fs::absolute(out, ec);
    if (out_abs.has_parent_path()) fs::create_directories(out_abs.parent_path(), ec);
    const LibraryLink walnut_libs = walnut_library_links(o);
    std::vector<fs::path> own = set.objects;
    std::vector<fs::path> archived_inputs;

    for (const fs::path& in : set.link_inputs) {
        if (walnut::classify_file(in.string()) == walnut::FileKind::Object) archived_inputs.push_back(in);
    }

    wb::LibraryManifest m;
    m.name = out.stem().string();
    if (m.name.rfind("lib", 0) == 0 && o.out_kind != Output::Object) m.name = m.name.substr(3);
    m.walnut_version = WALNUT_VERSION;
    const wb::LibraryAbi abi = current_abi(o);
    m.target = abi.target;
    m.safety = abi.safety;
    m.defines = abi.defines;
    m.cpp20 = g_needs_cpp20;
    const fs::path manifest = wb::manifest_path_for(out_abs);
    const fs::path base = manifest.parent_path();

    if (object) {
        m.kind = "object";
        std::vector<fs::path> parts = own;
        for (const fs::path& in : archived_inputs) parts.push_back(in);

        if (parts.size() == 1) {
            fs::remove(out_abs, ec);
            fs::copy_file(parts[0], out_abs, fs::copy_options::overwrite_existing, ec);
            if (ec) { std::cerr << "walnut: cannot write '" << out.string() << "': " << ec.message() << "\n"; return false; }
        } else {
            std::string cmd = cxx_cmd(o) + " -r -nostdlib";
            for (const fs::path& p : parts) cmd += " " + quote(p.string());
            cmd += " -o " + quote(out_abs.string());
            if (o.verbose) std::cerr << cmd << "\n";
            if (exit_status_of(run_command(cmd)) != 0) {
                std::cerr << "walnut: combining the object files into '" << out.string() << "' failed; the extern \"C\"/\"C++\" code makes more than one object file, and this linker can't merge them (use --static instead)\n";
                return false;
            }
        }

        m.binaries.push_back(relative_to(out_abs, base));
    } else if (o.out_kind == Output::Static) {
        m.kind = "static";
        const std::string ar = find_archiver(o);
        if (ar.empty()) {
            std::cerr << "walnut: --static needs an archiver ('ar' or 'llvm-ar'); none was found next to the C++ compiler or on PATH (set WALNUT_AR to pick one)\n";
            return false;
        }
        fs::remove(out_abs, ec);
        const fs::path rsp = set.tmp / "archive.rsp";
        std::string list;
        for (const fs::path& p : own) list += quote(p.string()) + "\n";
        for (const fs::path& p : archived_inputs) list += quote(p.string()) + "\n";
        if (!write_file(rsp, list)) return false;
        const std::string cmd = quote(ar) + " rcs " + quote(out_abs.string()) + " @" + quote(rsp.string());
        if (o.verbose) std::cerr << cmd << "\n";
        if (exit_status_of(run_command(cmd)) != 0) {
            std::cerr << "walnut: making the archive '" << out.string() << "' failed\n";
            return false;
        }
        m.binaries.push_back(relative_to(out_abs, base));
    } else {
        m.kind = "shared";
        const LinkLibs libs = link_libraries(o);
        std::string cmd = cxx_cmd(o) + (g_needs_cpp20 ? " -std=c++20" : " -std=c++17") + (o.debug != DebugMode::None ? " -g" : "");
#if defined(__APPLE__)
        cmd += " -dynamiclib -Wl,-install_name,@rpath/" + out_abs.filename().string();
#else
        cmd += " -shared";
#endif
        for (const fs::path& obj : own) cmd += " " + quote(obj.string());
        for (const fs::path& obj : set.std_objects) cmd += " " + quote(obj.string());
        for (const fs::path& in : set.link_inputs) cmd += " " + quote(in.string());
        for (const fs::path& b : walnut_libs.binaries) cmd += " " + quote(b.string());
        for (const std::string& a : walnut_libs.args) cmd += " " + quote(a);
        cmd += rpath_args(o, walnut_libs);
        cmd += " -o " + quote(out_abs.string());
#if defined(_WIN32)
        fs::path implib = out_abs.parent_path() / ("lib" + out_abs.stem().string() + ".dll.a");
        cmd += " " + quote("-Wl,--out-implib," + implib.string()) + " -Wl,--export-all-symbols";
        if (o.managed_cxx) cmd += " -static";
#elif !defined(__APPLE__)
        cmd += " " + quote("-Wl,-soname," + out_abs.filename().string());
#endif
        cmd += libs.args;
        if (o.verbose) std::cerr << cmd << "\n";
        if (exit_status_of(run_command(cmd)) != 0) {
            std::cerr << "walnut: linking the shared library '" << out.string() << "' failed\n";
            report_compile_failure(o, libs, set.tmp);
            return false;
        }
#if defined(_WIN32)
        m.binaries.push_back(relative_to(implib, base));
#else
        m.binaries.push_back(relative_to(out_abs, base));
#endif
        m.runtime_files.push_back(relative_to(out_abs, base));
        for (const fs::path& r : walnut_libs.runtime_files) m.runtime_files.push_back(r.string());
    }

    if (o.out_kind != Output::Shared) {
        for (const fs::path& b : walnut_libs.binaries) m.link.push_back(b.string());
        for (const std::string& a : walnut_libs.args) m.link.push_back(a);
        for (const fs::path& r : walnut_libs.runtime_files) m.runtime_files.push_back(r.string());

        for (const fs::path& in : set.link_inputs) {
            if (walnut::classify_file(in.string()) == walnut::FileKind::Library) m.link.push_back(in.string());
        }

        for (const std::string& l : o.native_link) {
            if (l.rfind("-L", 0) == 0) m.link.push_back("-L" + fs::absolute(l.substr(2), ec).string());
            else m.link.push_back(l);
        }

        m.std_modules = g_lib.std_files;
        for (const wb::LibraryManifest* u : linked_manifests()) {
            for (const std::string& s : u->std_modules) if (std::find(m.std_modules.begin(), m.std_modules.end(), s) == m.std_modules.end()) m.std_modules.push_back(s);
        }
    }

    for (const wb::LibrarySource& src : g_lib.own_sources) m.sources.push_back(wb::LibrarySource{ src.hash, relative_to(src.path, base) });

    if (!wb::write_manifest(m, manifest)) {
        std::cerr << "walnut: cannot write '" << manifest.string() << "'\n";
        return false;
    }

    fs::remove_all(set.tmp, ec);
    return true;
}

int highlight(const Options& o) {
    std::string text;

    if (o.from_stdin) {
        std::ostringstream in;
        in << std::cin.rdbuf();
        text = in.str();
    }

    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(walnut::PathStyle::Absolute);
    compiler.report_silent();
    compiler.warn_silent();
    if (o.ownership) compiler.enable_ownership_checks();
    if (o.warn_unused) compiler.enable_unused_warnings();
    if (o.overflow_errors) compiler.enable_overflow_errors();
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
    if (o.from_stdin) compiler.set_input_text(std::move(text));

    try {
        compiler.compile();
    } catch (const std::exception&) {
    }

    std::cout << walnut::driver::highlight_json(compiler);
    std::cout.flush();
    return 0;
}

bool completion_ident(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

std::string completion_pass(const Options& o, std::string text, std::string_view placeholder, const walnut::driver::CompletionRequest& request) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(walnut::PathStyle::Absolute);
    compiler.report_silent();
    compiler.warn_silent();
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& d : o.module_dirs) compiler.add_module_dir(d);
    for (const auto& d : o.module_env_dirs) compiler.add_module_dir(d);
    compiler.add_module_dir(o.runtime_dir);
    for (const walnut_library::File& f : walnut_library::files) if (f.path) compiler.add_library_file(f.path, std::string(reinterpret_cast<const char*>(f.data), f.size));
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    compiler.set_native_header_args([&o](bool cpp) { return native_header_args(o, cpp); });
    compiler.set_input_text(std::move(text));
    try {
        compiler.compile();
    } catch (const std::exception&) {
    }
    if (compiler.had_front_end_errors()) return std::string();
    return walnut::driver::complete_json(compiler, placeholder, request);
}

int complete(const Options& o) {
    std::string text;
    if (o.from_stdin) {
        std::ostringstream in;
        in << std::cin.rdbuf();
        text = in.str();
    } else {
        std::ifstream in(o.input, std::ios::binary);
        if (!in) { std::cerr << "walnut: cannot read '" << o.input << "'\n"; return 1; }
        std::ostringstream all;
        all << in.rdbuf();
        text = all.str();
    }
    if (o.at_line == 0) { std::cerr << "walnut: complete needs --at LINE:COLUMN\n"; return 2; }

    std::size_t line_start = 0;
    for (std::size_t l = 1; l < o.at_line && line_start <= text.size(); ++l) {
        const std::size_t nl = text.find('\n', line_start);
        if (nl == std::string::npos) { line_start = text.size() + 1; break; }
        line_start = nl + 1;
    }
    if (line_start > text.size()) line_start = text.size();
    std::size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) line_end = text.size();
    const std::size_t caret = std::min(line_start + o.at_column - 1, line_end);
    std::size_t start = caret;
    while (start > line_start && completion_ident(text[start - 1])) --start;
    std::size_t end = caret;
    while (end < line_end && completion_ident(text[end])) ++end;

    walnut::driver::CompletionRequest request;
    request.start = start - line_start;
    request.line = static_cast<std::uint32_t>(o.at_line);
    std::size_t before = start;
    auto ends_with = [&](std::string_view s) { return before >= line_start + s.size() && std::string_view(text).substr(before - s.size(), s.size()) == s; };
    if (ends_with("::")) request.mode = walnut::driver::CompletionRequest::Mode::Scope;
    else if (ends_with("->")) request.mode = walnut::driver::CompletionRequest::Mode::Arrow;
    else if (ends_with(".") && !ends_with("..")) request.mode = walnut::driver::CompletionRequest::Mode::Member;
    if (request.mode != walnut::driver::CompletionRequest::Mode::Name) {
        std::size_t q = before - (request.mode == walnut::driver::CompletionRequest::Mode::Member ? 1 : 2);
        std::vector<std::string> parts;
        while (q > line_start) {
            std::size_t w = q;
            while (w > line_start && completion_ident(text[w - 1])) --w;
            if (w == q) break;
            parts.insert(parts.begin(), text.substr(w, q - w));
            if (request.mode != walnut::driver::CompletionRequest::Mode::Scope || w < line_start + 2 || text.compare(w - 2, 2, "::") != 0) break;
            q = w - 2;
        }
        request.qualifier = std::move(parts);
    }

    const std::string placeholder = "__walnut_complete__";
    bool rest_blank = true;
    for (std::size_t i = end; i < line_end; ++i) rest_blank = rest_blank && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r');
    std::string patched = text.substr(0, start) + placeholder + (rest_blank ? ";" : "") + text.substr(end);
    std::string json = completion_pass(o, patched, placeholder, request);
    if (json.empty() && !rest_blank) {
        patched = text.substr(0, start) + placeholder + ";" + text.substr(line_end);
        json = completion_pass(o, patched, placeholder, request);
    }
    if (json.empty()) {
        std::string blank = text;
        for (std::size_t i = line_start; i < line_end; ++i) if (blank[i] != '\r') blank[i] = ' ';
        json = completion_pass(o, blank, placeholder, request);
    }
    if (json.empty()) json = "{\"start\":" + std::to_string(request.start) + ",\"mode\":\"none\",\"items\":[]}\n";
    std::cout << json;
    std::cout.flush();
    return 0;
}

bool load_project_names(const Options& o) {
    namespace wt = walnut::tokenizing;
    wt::clear_project_names();
    const std::string file = wt::find_project_file(o.input);
    if (file.empty()) return true;
    const wt::ProjectNames names = wt::read_project_names(file);
    wt::use_project_names(names);
    if (names.problems.empty() || o.command == Command::Highlight || o.command == Command::Complete) return true;
    std::error_code ec;
    std::string shown = fs::relative(file, fs::current_path(ec), ec).generic_string();
    if (ec || shown.empty() || shown.rfind("..", 0) == 0) shown = file;
    for (const wt::ProjectNameProblem& p : names.problems) {
        std::cerr << "[Project] " << shown;
        if (p.line > 0) std::cerr << ":" << p.line;
        std::cerr << ": " << p.message << "\n";
    }
    std::cerr << "walnut: " << shown << " has " << names.problems.size() << (names.problems.size() == 1 ? " problem" : " problems") << " in its 'names' section\n";
    return false;
}

std::string demangled(const std::string& symbol) {
    std::string name = symbol;
    if (name.rfind("__Z", 0) == 0) name.erase(0, 1);
#if defined(WALNUT_HAS_DEMANGLE)
    if (name.rfind("_Z", 0) == 0) {
        int status = 0;
        char* out = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
        if (out && status == 0) {
            std::string result = out;
            std::free(out);
            return result;
        }
        std::free(out);
    }
#endif
    return symbol;
}

std::string without_parameters(const std::string& name) {
    int depth = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '<') ++depth;
        else if (name[i] == '>') --depth;
        else if (name[i] == '(' && depth == 0 && i > 0) return name.substr(0, i);
    }
    return name;
}

std::string sanitize_name(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(c);
        else if (c == '_') out += "_u";
        else if (c == ' ') continue;
        else { out += '_'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string decode_walnut_name(const std::string& text, std::size_t& i) {
    auto hex = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'A' && c <= 'F'); };
    std::string out;

    while (i < text.size()) {
        const char c = text[i];
        if (std::isalnum(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
        if (c != '_') break;
        if (i + 1 < text.size() && text[i + 1] == 'u') { out += '_'; i += 2; continue; }
        if (i + 2 < text.size() && hex(text[i + 1]) && hex(text[i + 2])) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 3;
            continue;
        }
        break;
    }

    return out;
}

std::string readable_name(const std::string& symbol) {
    std::string name = without_parameters(demangled(symbol));
    if (name.rfind("wlu::", 0) != 0) return name;
    name = name.substr(5);
    std::size_t i = 2;
    const bool instance = name.find("_I_") != std::string::npos;

    if (name.rfind("f_", 0) == 0) return decode_walnut_name(name, i) + (instance ? " (template instance)" : "");

    if (name.rfind("R_", 0) == 0) {
        const std::string record = decode_walnut_name(name, i);
        const std::size_t sep = name.find("::");
        if (sep == std::string::npos) return record;
        const std::string rest = name.substr(sep + 2);
        if (rest.rfind("m_", 0) == 0) {
            std::size_t j = 2;
            return record + "::" + decode_walnut_name(rest, j) + (instance ? " (template instance)" : "");
        }
        const std::size_t colon = record.rfind("::");
        const std::string last = colon == std::string::npos ? record : record.substr(colon + 2);
        if (rest.rfind("~", 0) == 0) return record + "::~" + last;
        if (rest.rfind("R_", 0) == 0) return record + "::" + last;
        return record + "::" + rest;
    }

    return "wlu::" + name;
}

bool function_matches(const std::string& query, const std::string& symbol) {
    const std::string full = without_parameters(demangled(symbol));
    if (full == query || symbol == query || symbol == "_" + query) return true;
    std::string name = full;
    if (name.rfind("wlu::", 0) == 0) name = name.substr(5);
    const std::size_t split = query.rfind("::");
    const std::string whole = sanitize_name(query);

    if (name.rfind("f_" + whole + "_", 0) == 0) return true;
    if (name.rfind("f_", 0) == 0 && name.find("_3A_3A" + whole + "_") != std::string::npos) return true;

    if (split != std::string::npos) {
        const std::string owner = sanitize_name(query.substr(0, split));
        const std::string member = query.substr(split + 2);
        const std::size_t sep = name.find("::");
        if (sep != std::string::npos && name.rfind("R_", 0) == 0) {
            const std::string record = name.substr(0, sep);
            const std::string rest = name.substr(sep + 2);
            const bool owner_ok = record.rfind("R_" + owner, 0) == 0 || record.find("_3A_3A" + owner) != std::string::npos;
            const std::size_t last = query.rfind("::", split - 1);
            const std::string owner_last = query.substr(last == std::string::npos || split == 0 ? 0 : last + 2, split - (last == std::string::npos || split == 0 ? 0 : last + 2));
            const bool special = (rest == record && member == owner_last) || (rest == "~" + record && member == "~" + owner_last);
            const bool member_ok = rest.rfind("m_" + sanitize_name(member) + "_", 0) == 0 || special;
            if (owner_ok && member_ok) return true;
        }
    }

    return false;
}

struct DumpBlock {
    std::string           symbol;
    std::vector<std::string> lines;
};

std::vector<DumpBlock> assembly_functions(const std::string& text) {
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    auto trim = [](const std::string& x) {
        std::size_t a = 0;
        while (a < x.size() && (x[a] == ' ' || x[a] == '\t')) ++a;
        return x.substr(a);
    };

    auto label_of = [](const std::string& line) -> std::string {
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == ';') return std::string();
        std::size_t end = 0;
        if (line[0] == '"') {
            end = line.find('"', 1);
            if (end == std::string::npos || end + 1 >= line.size() || line[end + 1] != ':') return std::string();
            return line.substr(1, end - 1);
        }
        while (end < line.size() && line[end] != ':' && line[end] != ' ' && line[end] != '\t') ++end;
        if (end == 0 || end >= line.size() || line[end] != ':') return std::string();
        return line.substr(0, end);
    };

    std::vector<std::string> typed;
    for (const std::string& raw : lines) {
        const std::string l = trim(raw);
        if (l.rfind(".type", 0) == 0 && (l.find("@function") != std::string::npos || l.find("%function") != std::string::npos)) {
            std::string sym = trim(l.substr(5));
            sym = sym.substr(0, sym.find(','));
            if (!sym.empty() && sym.front() == '"') sym = sym.substr(1, sym.size() - 2);
            typed.push_back(sym);
        } else if (l.rfind(".def", 0) == 0) {
            const std::size_t type = l.find(".type");
            if (type == std::string::npos || trim(l.substr(type + 5)).rfind("32", 0) != 0) continue;
            std::string sym = trim(l.substr(4));
            sym = sym.substr(0, sym.find(';'));
            typed.push_back(sym);
        }
    }

    auto is_function = [&](std::size_t i, const std::string& sym) {
        if (sym.rfind(".L", 0) == 0 || (sym.size() > 1 && sym[0] == 'L' && (sym.rfind("Ltmp", 0) == 0 || sym.rfind("LBB", 0) == 0 || sym.rfind("Lfunc", 0) == 0))) return false;
        if (std::find(typed.begin(), typed.end(), sym) != typed.end()) return true;
        for (std::size_t j = i + 1; j < lines.size() && j <= i + 4; ++j) if (trim(lines[j]).rfind(".cfi_startproc", 0) == 0) return true;
        return false;
    };

    std::vector<DumpBlock> out;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string sym = label_of(lines[i]);
        if (sym.empty() || !is_function(i, sym)) continue;
        DumpBlock b;
        b.symbol = sym;
        std::size_t j = i;

        for (; j < lines.size(); ++j) {
            if (j > i) {
                const std::string other = label_of(lines[j]);
                if (!other.empty() && is_function(j, other)) break;
            }
            b.lines.push_back(lines[j]);
            const std::string l = trim(lines[j]);
            if (l.rfind(".size", 0) == 0 && l.find(sym) != std::string::npos) { ++j; break; }
            if (l.rfind(".cfi_endproc", 0) == 0) {
                for (std::size_t k = j + 1; k < lines.size() && k <= j + 3; ++k) {
                    const std::string n = trim(lines[k]);
                    if (n.rfind(".size", 0) == 0 && n.find(sym) != std::string::npos) { b.lines.push_back(lines[k]); j = k; break; }
                    if (!label_of(lines[k]).empty()) break;
                }
                ++j;
                break;
            }
        }

        out.push_back(std::move(b));
        i = j - 1;
    }

    return out;
}

std::vector<DumpBlock> llvm_functions(const std::string& text) {
    std::vector<DumpBlock> out;
    std::istringstream in(text);
    std::string line;
    DumpBlock* open = nullptr;

    while (std::getline(in, line)) {
        if (!open && line.rfind("define ", 0) == 0) {
            const std::size_t at = line.find('@');
            if (at == std::string::npos) continue;
            std::size_t end = at + 1;
            std::string sym;
            if (end < line.size() && line[end] == '"') {
                const std::size_t close = line.find('"', end + 1);
                sym = line.substr(end + 1, close - end - 1);
            } else {
                while (end < line.size() && line[end] != '(') ++end;
                sym = line.substr(at + 1, end - at - 1);
            }
            out.push_back(DumpBlock{ sym, {} });
            open = &out.back();
        }

        if (open) {
            open->lines.push_back(line);
            if (line == "}") open = nullptr;
        }
    }

    return out;
}

std::string filter_dump(const Options& o, const std::string& text, bool& ok) {
    ok = true;
    if (o.asm_functions.empty()) return text;
    const std::vector<DumpBlock> blocks = o.dump == Dump::LlvmIr ? llvm_functions(text) : assembly_functions(text);
    const std::string comment = o.dump == Dump::LlvmIr ? "; " : (target_triple(o).rfind("aarch64", 0) == 0 || target_triple(o).rfind("arm", 0) == 0 ? "// " : "# ");
    std::string out;

    for (const std::string& q : o.asm_functions) {
        std::vector<const DumpBlock*> found;
        for (const DumpBlock& b : blocks) if (function_matches(q, b.symbol)) found.push_back(&b);

        if (found.empty()) {
            for (const DumpBlock& b : blocks) {
                if (demangled(b.symbol).find(q) != std::string::npos) found.push_back(&b);
            }
        }

        if (found.empty()) {
            std::cerr << "walnut: no function named '" << q << "' in the " << (o.dump == Dump::LlvmIr ? "LLVM IR" : "assembly");
            std::vector<std::string> names;
            for (const DumpBlock& b : blocks) {
                const std::string raw = without_parameters(demangled(b.symbol));
                if (raw.rfind("wlu::f_", 0) != 0 && raw.rfind("wlu::R_", 0) != 0) continue;
                const std::string n = readable_name(b.symbol);
                if (n.rfind("std::", 0) == 0) continue;
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
            }
            if (!names.empty()) {
                std::cerr << "; the program's functions are:\n";
                for (std::size_t i = 0; i < names.size() && i < 40; ++i) std::cerr << "  " << names[i] << "\n";
                if (names.size() > 40) std::cerr << "  ... and " << (names.size() - 40) << " more\n";
            } else {
                std::cerr << "\n";
            }
            std::cerr << "walnut: it may have been inlined everywhere it's used; -O0 keeps every function\n";
            ok = false;
            continue;
        }

        for (const DumpBlock* b : found) {
            out += comment + readable_name(b->symbol) + "\n";
            out += comment + demangled(b->symbol) + "\n";
            for (const std::string& l : b->lines) out += l + "\n";
            out += "\n";
        }
    }

    return out;
}

std::string display_path(const std::string& path) {
    if (is_library_path(path)) return path;
    std::error_code ec;
    const fs::path rel = fs::relative(path, fs::current_path(ec), ec);
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) return rel.generic_string();
    return fs::path(path).generic_string();
}

class WalnutSourcePrinter {
public:
    explicit WalnutSourcePrinter(const walnut::SourceManager& sources) : m_sources(sources) {}

    std::string print(const walnut::CompilerPipeline::PreprocessedFile& f) {
        m_out.clear();
        m_stack.clear();
        m_prev = nullptr;
        m_line_start = true;
        std::size_t mark = 0;
        m_out += "// " + display_path(m_sources.absolute_path(f.file)) + "\n";

        for (std::size_t i = 0; i <= f.tokens.size(); ++i) {
            while (mark < f.marks.size() && f.marks[mark].at == i) {
                const walnut::preprocessing::FileMark& m = f.marks[mark++];
                if (m.enter) enter(m.file);
                else leave();
            }

            if (i == f.tokens.size()) break;
            const walnut::tokenizing::Token& t = f.tokens[i];
            if (t.is(walnut::tokenizing::Token::Kind::End)) continue;
            put(t);
        }

        if (!m_out.empty() && m_out.back() != '\n') m_out += '\n';
        return m_out;
    }

private:
    using Token = walnut::tokenizing::Token;
    using Kind = Token::Kind;

    struct Region {
        walnut::FileId file = walnut::INVALID_FILE;
        std::uint32_t  line = 0;
        std::string    indent;
    };

    const walnut::SourceManager&                           m_sources;
    std::string                                            m_out;
    std::vector<Region>                                    m_stack;
    std::map<walnut::FileId, std::vector<std::size_t>>     m_line_starts;
    bool                                                   m_line_start = true;
    const Token*                                           m_prev = nullptr;
    bool                                                   m_prev_original = false;

    void newline() {
        if (!m_out.empty() && m_out.back() != '\n') m_out += '\n';
        m_line_start = true;
        m_prev = nullptr;
    }

    void enter(walnut::FileId file) {
        if (!m_stack.empty()) {
            newline();
            m_out += m_stack.back().indent + "// begin " + display_path(m_sources.absolute_path(file)) + "\n";
        }
        m_stack.push_back(Region{ file, 0, std::string() });
        m_line_start = true;
        m_prev = nullptr;
    }

    void leave() {
        if (m_stack.empty()) return;
        const walnut::FileId file = m_stack.back().file;
        m_stack.pop_back();
        if (m_stack.empty()) return;
        newline();
        m_out += m_stack.back().indent + "// end " + display_path(m_sources.absolute_path(file)) + "\n";
        m_line_start = true;
        m_prev = nullptr;
    }

    const std::vector<std::size_t>& line_starts(walnut::FileId file) {
        auto it = m_line_starts.find(file);
        if (it != m_line_starts.end()) return it->second;
        std::vector<std::size_t> starts{ 0 };
        const char* data = m_sources.data(file);
        const std::size_t size = m_sources.size(file);
        for (std::size_t i = 0; i < size; ++i) if (data[i] == '\n') starts.push_back(i + 1);
        return m_line_starts.emplace(file, std::move(starts)).first->second;
    }

    bool in_buffer(const Token& t, walnut::FileId file) const {
        if (file == walnut::INVALID_FILE || file >= m_sources.file_count()) return false;
        const char* data = m_sources.data(file);
        return t.data() >= data && t.data() + t.length() <= data + m_sources.size(file);
    }

    std::uint32_t line_at(walnut::FileId file, const char* p) {
        const std::vector<std::size_t>& starts = line_starts(file);
        const std::size_t offset = static_cast<std::size_t>(p - m_sources.data(file));
        return static_cast<std::uint32_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
    }

    bool original(const Token& t, walnut::FileId region) {
        if (t.file_id() != region || !in_buffer(t, region)) return false;
        return line_at(region, t.data()) == t.line() || line_at(region, t.data() + t.length()) == t.line();
    }

    std::string indentation(walnut::FileId file, const char* p) {
        const std::vector<std::size_t>& starts = line_starts(file);
        const char* data = m_sources.data(file);
        const std::size_t offset = static_cast<std::size_t>(p - data);
        const std::size_t start = *(std::upper_bound(starts.begin(), starts.end(), offset) - 1);
        std::string out;
        for (std::size_t i = start; i < offset && (data[i] == ' ' || data[i] == '\t'); ++i) out += data[i];
        return out;
    }

    static std::string spelling(const Token& t) {
        const std::string text(t.lexeme());
        if (t.is(Kind::String)) return "\"" + text + "\"";
        if (t.is(Kind::Character)) return "'" + text + "'";
        if (t.is(Kind::TextLiteral)) return "`" + text + "`";
        if (t.is(Kind::RawCode)) return "code {" + text + "}";
        return text;
    }

    static bool word_like(const std::string& s) {
        return !s.empty() && (std::isalnum(static_cast<unsigned char>(s.back())) || s.back() == '_' || s.back() == '"' || s.back() == '\'' || s.back() == '`');
    }

    static bool needs_space(const Token& prev, const Token& cur) {
        const std::string a = spelling(prev);
        const std::string b = spelling(cur);
        for (const char* tight : { "(", "[", ".", "::", "->", "~", "?.", "@" }) if (a == tight) return false;
        for (const char* tight : { ")", "]", ",", ";", ".", "::", "->", "?.", "...", "++", "--" }) if (b == tight) return false;
        if ((b == "(" || b == "[") && (prev.is(Kind::Identifier) || a == ")" || a == "]" || a == ">")) return false;
        if ((a == "++" || a == "--" || a == "!") && word_like(b)) return false;
        return true;
    }

    void put(const Token& t) {
        if (m_stack.empty()) return;
        Region& region = m_stack.back();
        const bool orig = original(t, region.file);

        if (t.line() > region.line) {
            const bool first = region.line == 0;
            const std::uint32_t gap = first ? 0 : t.line() - region.line;
            newline();
            if (gap > 1) m_out += '\n';
            region.line = t.line();
        }

        if (m_line_start) {
            region.indent = orig ? indentation(region.file, t.data()) : region.indent;
            m_out += region.indent;
        } else if (m_prev) {
            bool done = false;
            if (orig && m_prev_original && m_prev->data() + m_prev->length() <= t.data() && in_buffer(*m_prev, region.file)) {
                const char* from = m_prev->data() + m_prev->length();
                const char* to = t.data();
                if (t.is(Kind::String) || t.is(Kind::Character) || t.is(Kind::TextLiteral)) --to;
                if (m_prev->is(Kind::String) || m_prev->is(Kind::Character) || m_prev->is(Kind::TextLiteral)) ++from;
                if (from <= to) {
                    std::string gap(from, to);
                    if (gap.find_first_not_of(" \t") == std::string::npos) { m_out += gap; done = true; }
                }
            }
            if (!done && needs_space(*m_prev, t)) m_out += ' ';
        }

        m_out += spelling(t);
        m_line_start = false;
        m_prev = &t;
        m_prev_original = orig;
    }
};

int dump_walnut_preprocessed(const Options& o) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(fs::path(o.input).is_absolute() ? walnut::PathStyle::Absolute : walnut::PathStyle::Relative);
    configure_pipeline(compiler, o);
    if (o.no_warnings) compiler.suppress_warnings();
    for (const std::string& extra : o.extra_inputs) compiler.add_input(extra);
    if (!route_messages(compiler.error_reporter(), o.errors_to, "--errors")) return 1;
    if (!route_messages(compiler.warning_reporter(), o.warnings_to, "--warnings")) return 1;
    std::vector<walnut::CompilerPipeline::PreprocessedFile> files;

    try {
        files = compiler.preprocess_only();
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return 1;
    }

    if (compiler.error_reporter().has_errors() || files.empty()) return 1;
    WalnutSourcePrinter printer(compiler.get_sources());
    std::string text;

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i) text += "\n";
        text += printer.print(files[i]);
    }

    fs::path out;
    if (!o.output.empty()) out = o.output;
    else if (o.command == Command::Build) out = fs::path(o.input).filename().replace_extension(".pp.wal");
    if (out.empty()) { std::cout << text; return 0; }
    return write_file(out, text) ? 0 : 1;
}

int dump_program(const Options& o) {
    std::string raw;
    if (!generate(o, raw, nullptr, o.prebuilt_std)) return 1;

    if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
        std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
        return 1;
    }

    if (!find_program(o.cxx)) {
        explain_missing_compiler(o.cxx);
        return 1;
    }

    if (o.dump == Dump::LlvmIr && capture_command(cxx_cmd(o) + " --version").find("clang") == std::string::npos) {
        std::cerr << "walnut: --llvm-ir needs clang as the C++ compiler (" << o.cxx << " isn't clang); pass --cxx clang++\n";
        return 1;
    }

    if (o.asm_syntax == "intel") {
        const std::string t = target_triple(o);
        if (!t.empty() && t.rfind("x86_64", 0) != 0 && t.rfind("i386", 0) != 0 && t.rfind("i486", 0) != 0 && t.rfind("i586", 0) != 0 && t.rfind("i686", 0) != 0 && t.rfind("amd64", 0) != 0) {
            std::cerr << "walnut: --asm-syntax only applies to x86, and this compiler makes code for " << t << "\n";
            return 1;
        }
    }

    const fs::path tmp = make_temp_dir();
    const fs::path cpp_path = tmp / "program.cpp";
    std::vector<LineMapEntry> map;
    if (!write_file(cpp_path, finalize_cpp(raw, o, cpp_path.string(), map))) return 1;
    const char* ext = o.dump == Dump::Assembly ? ".s" : (o.dump == Dump::LlvmIr ? ".ll" : ".ii");
    const fs::path out_tmp = tmp / (std::string("program") + ext);
    std::string cmd = cxx_cmd(o) + " " + compile_flags(o);

    if (o.dump == Dump::Assembly) {
        cmd += " -S";
        if (o.asm_syntax == "intel") cmd += " -masm=intel";
        else if (o.asm_syntax == "att") cmd += " -masm=att";
        if (o.verbose_asm) cmd += " -fverbose-asm";
    } else if (o.dump == Dump::LlvmIr) {
        cmd += " -S -emit-llvm";
    } else {
        cmd += " -E";
    }

    cmd += " " + quote(cpp_path.string()) + " -o " + quote(out_tmp.string());
    if (o.verbose) std::cerr << cmd << "\n";

    if (exit_status_of(run_command(cmd)) != 0) {
        std::cerr << "walnut: the C++ compiler failed to make the " << (o.dump == Dump::Assembly ? "assembly" : (o.dump == Dump::LlvmIr ? "LLVM IR" : "preprocessed C++")) << "; the generated C++ is in " << tmp.string() << "\n";
        return 1;
    }

    bool ok = true;
    const std::string text = filter_dump(o, walnut::build::read_text(out_tmp), ok);
    std::error_code ec;
    if (!ok && text.empty()) { fs::remove_all(tmp, ec); return 1; }
    fs::path out;

    if (!o.output.empty()) out = o.output;
    else if (o.command == Command::Build) out = fs::path(o.input).filename().replace_extension(ext);

    int status = ok ? 0 : 1;
    if (out.empty()) std::cout << text;
    else if (!write_file(out, text)) status = 1;
    fs::remove_all(tmp, ec);
    return status;
}

void register_explicit_libraries(Options& o) {
    namespace wb = walnut::build;
    std::error_code ec;
    std::vector<fs::path> manifests;

    auto candidates = [](const fs::path& binary) {
        std::vector<fs::path> out{ wb::manifest_path_for(binary) };
        const std::string stem = out[0].stem().string();
        if (stem.rfind("lib", 0) == 0) out.push_back(out[0].parent_path() / (stem.substr(3) + ".walnut"));
        else out.push_back(out[0].parent_path() / ("lib" + stem + ".walnut"));
        return out;
    };

    for (const std::string& n : o.native_inputs) {
        const walnut::FileKind kind = walnut::classify_file(n);
        if (kind != walnut::FileKind::Object && kind != walnut::FileKind::Library) continue;
        for (const fs::path& m : candidates(fs::path(n))) {
            if (!fs::is_regular_file(m, ec)) continue;
            const wb::LibraryManifest* lib = g_lib.index->add_explicit(m);
            if (lib && !g_lib.index->is_compatible(lib)) g_lib.dropped_inputs.push_back(n);
            break;
        }
    }

    std::vector<fs::path> dirs;
    for (const std::string& l : o.native_link) if (l.rfind("-L", 0) == 0 && l.size() > 2) dirs.emplace_back(l.substr(2));
    std::vector<std::string> kept;

    for (const std::string& l : o.native_link) {
        bool drop = false;

        if (l.rfind("-l", 0) == 0 && l.size() > 2 && l.rfind("-l:", 0) != 0) {
            const std::string name = l.substr(2);
            for (const fs::path& d : dirs) {
                bool found = false;
                for (const fs::path& m : { d / ("lib" + name + ".walnut"), d / (name + ".walnut") }) {
                    if (!fs::is_regular_file(m, ec)) continue;
                    if (g_lib.index->add_explicit(m)) drop = true;
                    found = true;
                    break;
                }
                if (found) break;
            }
        }

        if (!drop) kept.push_back(l);
    }

    o.native_link = kept;
}

int finish_program(const Options& o, const fs::path& exe, const LibraryLink& libs) {
    if (o.command == Command::Build) {
        const fs::path out = exe_output_path(o);
        std::error_code ec;
        if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
        fs::remove(out, ec);
        fs::copy_file(exe, out, fs::copy_options::overwrite_existing, ec);
        if (ec) { std::cerr << "walnut: cannot write '" << out.string() << "': " << ec.message() << "\n"; return 1; }
        fs::permissions(out, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        return place_runtime_files(o, out, libs) ? 0 : 1;
    }

#if defined(_WIN32)
    std::vector<fs::path> dll_dirs;
    for (const fs::path& r : libs.runtime_files) if (std::find(dll_dirs.begin(), dll_dirs.end(), r.parent_path()) == dll_dirs.end()) dll_dirs.push_back(r.parent_path());
    if (!dll_dirs.empty()) walnut::toolchain::add_to_path(dll_dirs);
#endif
    std::string cmd = quote(exe.string());
    for (const std::string& a : o.program_args) cmd += " " + quote(a);
    std::cout.flush();
    return exit_status_of(run_command(cmd));
}

int run(const Options& in) {
    Options o = in;

    if (o.command == Command::Clean) {
        const fs::path root = walnut::build::default_cache_root();
        std::error_code ec;
        const std::uintmax_t removed = fs::remove_all(root, ec);
        if (ec) { std::cerr << "walnut: could not delete the build cache at " << root.string() << ": " << ec.message() << "\n"; return 1; }
        std::cout << "walnut: deleted the build cache at " << root.string() << " (" << removed << " files)\n";
        return 0;
    }

    if (o.command == Command::Highlight) {
        if (!load_project_names(o)) return 1;
        return highlight(o);
    }

    if (o.command == Command::Complete) {
        if (!load_project_names(o)) return 1;
        return complete(o);
    }

    g_lib.index = std::make_unique<walnut::build::LibraryIndex>(current_abi(o));
    register_explicit_libraries(o);

    if (o.link_only) {
        for (const std::string& w : g_lib.index->warnings()) std::cerr << "walnut: warning: " << w << "\n";
        g_lib.used = g_lib.index->explicit_libraries();
        g_lib.used.erase(std::remove_if(g_lib.used.begin(), g_lib.used.end(), [](const walnut::build::LibraryManifest* m) { return !g_lib.index->is_compatible(m); }), g_lib.used.end());
        g_lib.std_prebuilt = true;
        for (const walnut::build::LibraryManifest* m : g_lib.used) if (m->cpp20) g_needs_cpp20 = true;

        for (const std::string& n : o.native_inputs) {
            const walnut::FileKind kind = walnut::classify_file(n);
            if (kind == walnut::FileKind::CSource || kind == walnut::FileKind::CppSource) {
                std::cerr << "walnut: '" << n << "': linking without a .wal file only takes object files and libraries\n";
                return 1;
            }
        }

        fs::path exe;
        LibraryLink libs;
        if (!build_with_cache(o, walnut::codegen::SplitProgram{}, exe, libs)) return 1;
        return finish_program(o, exe, libs);
    }

    if (!load_project_names(o)) return 1;
    if (!fs::exists(o.input)) { std::cerr << "walnut: cannot open '" << o.input << "'\n"; return 1; }
    g_lib.index->never_prebuilt(fs::absolute(o.input).string());
    for (const std::string& extra : o.extra_inputs) g_lib.index->never_prebuilt(fs::absolute(extra).string());
    if (o.dump == Dump::Preprocessed) return dump_walnut_preprocessed(o);
    if (o.dump != Dump::None) return dump_program(o);

    if (o.out_kind != Output::Program && o.command == Command::Build) {
        g_lib.index->ignore(walnut::build::manifest_path_for(fs::absolute(library_output_path(o))));
        std::string raw;
        walnut::codegen::SplitProgram split;
        if (!generate(o, raw, &split, true)) return 1;

        if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
            std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
            return 1;
        }

        return build_library(o, split) ? 0 : 1;
    }

    const bool cached = (o.command == Command::Build || o.command == Command::Run) && !o.no_cache && !o.keep_cpp && !o.line_map && o.debug != DebugMode::Cpp;
    std::string raw;
    walnut::codegen::SplitProgram split;
    if (!generate(o, raw, cached ? &split : nullptr, cached && o.prebuilt_std)) return 1;
    if (o.command == Command::Check) return 0;
    std::vector<LineMapEntry> map;

    if (cached) {
        if (!fs::exists(fs::path(o.runtime_dir) / "walnut_runtime.hpp")) {
            std::cerr << "walnut: runtime header not found in '" << o.runtime_dir << "' (use --runtime or WALNUT_RUNTIME_DIR)\n";
            return 1;
        }

        fs::path exe;
        LibraryLink libs;
        if (!build_with_cache(o, split, exe, libs)) return 1;
        return finish_program(o, exe, libs);
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
        const fs::path exe = exe_output_path(o);
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
        if (ok && !place_runtime_files(o, exe, walnut_library_links(o))) return 1;
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
#if defined(_WIN32)
    {
        std::vector<fs::path> dll_dirs;
        for (const fs::path& r : walnut_library_links(o).runtime_files) if (std::find(dll_dirs.begin(), dll_dirs.end(), r.parent_path()) == dll_dirs.end()) dll_dirs.push_back(r.parent_path());
        if (!dll_dirs.empty()) walnut::toolchain::add_to_path(dll_dirs);
    }
#endif
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

bool offer_init(Options& o);
void use_installed_toolchain(Options& o);

int verify_library(const Options& base, const std::vector<std::string>& programs) {
    std::vector<std::pair<std::string, std::string>> inputs;
    std::string all;
    std::size_t n = 0;

    for (const walnut_library::File& f : walnut_library::files) {
        if (!f.path || walnut::classify_file(f.path) != walnut::FileKind::WalnutHeader) continue;
        const std::string imp = std::string(f.path) == "core.walh" || std::string(f.path) == "optional.walh" ? std::string() : std::string(f.path);
        if (imp.empty()) continue;
        std::string dotted = fs::path(imp).replace_extension("").generic_string();
        for (char& c : dotted) if (c == '/') c = '.';
        all += "import module." + dotted + " as walnut_verify_" + std::to_string(n++) + ";\n";
    }

    all += "function main() -> int {\n    return 0;\n} end function main\n";
    inputs.emplace_back((fs::temp_directory_path() / "walnut_verify_all.wal").generic_string(), all);
    for (const std::string& p : programs) inputs.emplace_back(p, std::string());
    std::size_t safety = 0;
    std::size_t other = 0;
    std::vector<std::string> seen;

    for (const auto& [path, text] : inputs) {
        Options o = base;
        o.input = path;
        walnut::CompilerPipeline compiler(o.input);
        compiler.set_path_style(walnut::PathStyle::Relative);
        configure_pipeline(compiler, o);
        compiler.suppress_warnings();
        compiler.verify_library();
        compiler.allow_unresolved_externs([](walnut::FileId) { return true; });
        compiler.error_reporter().set_mode(walnut::ErrorOutput::Silent);
        if (!text.empty()) compiler.set_input_text(text);

        try {
            compiler.compile();
        } catch (const std::exception& e) {
            if (text.empty()) std::cerr << "walnut: " << path << ": " << e.what() << "\n";
        }

        const walnut::SourceManager& sources = compiler.get_sources();

        for (const walnut::CompilerError& e : compiler.error_reporter().errors()) {
            const bool library = e.file_id < sources.file_count() && is_library_path(sources.absolute_path(e.file_id));
            const bool is_safety = e.message.find("Safety Error") != std::string::npos || e.message.find("safe code") != std::string::npos;
            const std::string where = compiler.error_reporter().display_path(e.file_id) + ":" + std::to_string(e.line);
            const std::string key = where + ": " + e.message;
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);

            if (library && is_safety) {
                std::cerr << key << "\n";
                ++safety;
            } else if (library) {
                ++other;
                if (o.verbose) std::cerr << "(not safety) " << key << "\n";
            } else if (!text.empty() || o.verbose) {
                std::cerr << "(in " << path << ") " << key << "\n";
            }
        }
    }

    std::cout << "walnut: " << safety << " safety " << (safety == 1 ? "problem" : "problems") << " in the standard library";
    if (other) std::cout << " (and " << other << " other errors from checking methods no program called; -v lists them)";
    std::cout << "\n";
    return safety ? 1 : 0;
}

int library_command(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[2]) == "--verify") {
        std::vector<std::string> args{ argv[0], "check" };
        std::vector<std::string> programs;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            if (walnut::classify_file(a) == walnut::FileKind::Walnut) programs.push_back(a);
            else args.push_back(a);
        }
        args.push_back("walnut_verify.wal");
        std::vector<char*> raw;
        for (std::string& a : args) raw.push_back(a.data());
        Options o;
        int code = 0;
        if (!parse_args(static_cast<int>(raw.size()), raw.data(), o, code)) return code;
        return verify_library(o, programs);
    }

    if (argc >= 3 && std::string(argv[2]) == "--precompile") {
        std::vector<std::string> args{ argv[0], "build" };
        for (int i = 3; i < argc; ++i) args.push_back(argv[i]);
        args.push_back("walnut_precompile.wal");
        std::vector<char*> raw;
        for (std::string& a : args) raw.push_back(a.data());
        Options o;
        int code = 0;
        if (!parse_args(static_cast<int>(raw.size()), raw.data(), o, code)) return code;
        use_installed_toolchain(o);
        if (!offer_init(o)) {
            explain_missing_compiler(o.cxx);
            return 1;
        }
        return precompile_std(o);
    }

    if (argc < 3) {
        for (const walnut_library::File& f : walnut_library::files) if (f.path) std::cout << f.path << "\n";
        return 0;
    }
    const std::string a = argv[2];
    if (a == "--extract") {
        if (argc < 4) { std::cerr << "walnut: '--extract' needs a folder\n"; return 2; }
        const fs::path dir = argv[3];
        std::error_code ec;
        for (const walnut_library::File& f : walnut_library::files) {
            if (!f.path) continue;
            const fs::path target = dir / fs::path(f.path);
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) { std::cerr << "walnut: can't write " << target.string() << "\n"; return 1; }
            out.write(reinterpret_cast<const char*>(f.data), static_cast<std::streamsize>(f.size));
        }
        return 0;
    }
    std::string want = a;
    const std::string root = "<walnut>/";
    if (want.rfind(root, 0) == 0) want = want.substr(root.size());
    for (const walnut_library::File& f : walnut_library::files) {
        if (!f.path || want != f.path) continue;
        std::cout.write(reinterpret_cast<const char*>(f.data), static_cast<std::streamsize>(f.size));
        return 0;
    }
    std::cerr << "walnut: '" << a << "' isn't a library file; 'walnut library' lists them\n";
    return 1;
}

void use_installed_toolchain(Options& o) {
    if (o.cxx_from_user) return;
    const std::optional<fs::path> managed = walnut::toolchain::installed_compiler();
    if (!managed) return;
    o.cxx = managed->string();
    o.managed_cxx = true;
    walnut::toolchain::add_to_path(walnut::toolchain::installed_path_dirs());
}

void print_init_usage(std::ostream& os) {
    os << "usage: walnut init [--yes] [--force] | --status | --print-compiler | --remove\n"
          "\n"
          "Sets up a C++ compiler for walnut, which builds programs by compiling the C++ it generates.\n"
          "On Windows it downloads a pinned compiler that matches this walnut, checks its SHA-256, and unpacks it into\n"
          "a folder of its own (" << walnut::toolchain::root().string() << "); nothing else on the system changes.\n"
          "On Linux it installs the distribution's g++ with its package manager (asking for your password), and on\n"
          "macOS it opens Apple's installer for the Command Line Tools.\n"
          "\n"
          "  -y, --yes          don't ask before downloading or installing\n"
          "  --force            install walnut's own compiler even when another one is already on PATH\n"
          "  --status           show which compiler walnut uses and its version\n"
          "  --print-compiler   print only the path of the compiler walnut uses (for editors and build tools)\n"
          "  --remove           delete the compiler walnut init downloaded\n"
          "  --cxx <compiler>   check this compiler instead (with --status or --print-compiler)\n"
          "\n"
          "walnut picks its compiler in this order: --cxx, then WALNUT_CXX, then the one walnut init downloaded,\n"
          "then " WALNUT_DEFAULT_CXX " from PATH. WALNUT_TOOLCHAIN_DIR puts the downloaded compiler somewhere else.\n";
}

int init_command(int argc, char** argv) {
    Options o;
    walnut::toolchain::InitOptions io;
    if (const char* env = std::getenv("WALNUT_CXX")) if (*env) { o.cxx = env; o.cxx_from_user = true; }
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-y" || a == "--yes") io.yes = true;
        else if (a == "--force") io.force = true;
        else if (a == "--remove") io.remove = true;
        else if (a == "--status") io.status = true;
        else if (a == "--print-compiler") io.print_compiler = true;
        else if (a == "--cxx" && i + 1 < argc) { o.cxx = argv[++i]; o.cxx_from_user = true; }
        else if (a == "-h" || a == "--help") { print_init_usage(std::cout); return 0; }
        else { std::cerr << "walnut: unknown option '" << a << "' for init\n"; print_init_usage(std::cerr); return 2; }
    }
    use_installed_toolchain(o);
    io.current_cxx = o.cxx;
    io.cxx_from_user = o.cxx_from_user;
    return walnut::toolchain::init(io);
}

bool offer_init(Options& o) {
    if (o.cxx_from_user || find_program(o.cxx) || !walnut::toolchain::interactive()) return true;
    std::cerr << "walnut: Walnut builds programs with a C++ compiler, and there isn't one ('" << o.cxx << "' isn't on PATH)\n";
    walnut::toolchain::InitOptions io;
    io.current_cxx = o.cxx;
    if (walnut::toolchain::init(io) != 0) return false;
    use_installed_toolchain(o);
    if (!o.managed_cxx) {
        for (const char* c : { "c++", "g++", "clang++" }) {
            if (find_program(o.cxx)) break;
            if (find_program(c)) o.cxx = c;
        }
    }
    return find_program(o.cxx);
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "library") return library_command(argc, argv);
    if (argc >= 2 && std::string(argv[1]) == "init") return init_command(argc, argv);
    Options o;
    int code = 0;
    if (!parse_args(argc, argv, o, code)) return code;
    use_installed_toolchain(o);
    if ((o.command == Command::Build || o.command == Command::Run) && !offer_init(o)) {
        explain_missing_compiler(o.cxx);
        return 1;
    }
    if (!o.random_seed.empty()) walnut::semantics::set_compile_time_random_seed(o.random_seed);

    try {
        return run(o);
    } catch (const std::exception& e) {
        std::cerr << "walnut: " << e.what() << "\n";
        return 1;
    }
}
