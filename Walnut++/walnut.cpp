#include "compiler.hpp"
#include "Codegen/cpp_emitter.hpp"

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

namespace {

namespace fs = std::filesystem;

enum class Command { Check, Emit, Build, Run };

struct Options {
    Command                                          command = Command::Check;
    std::string                                      input;
    std::string                                      output;
    std::vector<std::string>                         include_dirs;
    std::vector<std::pair<std::string, std::string>> defines;
    std::string                                      cxx = WALNUT_DEFAULT_CXX;
    std::string                                      opt = "-O2";
    std::string                                      runtime_dir = WALNUT_RUNTIME_DIR;
    std::string                                      extra_includes = WALNUT_RUNTIME_INCLUDES;
    std::vector<std::string>                         cxx_flags;
    std::vector<std::string>                         program_args;
    bool                                             keep_cpp = false;
    bool                                             no_warnings = false;
    bool                                             verbose = false;
};

void print_usage(std::ostream& os) {
    os << "usage: walnut <command> [options] <file.wal> [-- program arguments]\n"
          "\n"
          "commands:\n"
          "  check   analyze the program and report errors and warnings\n"
          "  emit    write the generated C++ (to -o, or to standard output)\n"
          "  build   compile the program to an executable (default: the input name without .wal)\n"
          "  run     build the program into a temporary directory and run it\n"
          "\n"
          "options:\n"
          "  -o <path>          output path\n"
          "  -I <dir>           add an include directory for #include\n"
          "  -D <name>[=value]  define a preprocessor macro\n"
          "  -O0 -O1 -O2 -O3    optimization level for the C++ compiler (default -O2)\n"
          "  --cxx <compiler>   C++ compiler used by build and run (default: " WALNUT_DEFAULT_CXX ")\n"
          "  --cxx-flag <flag>  pass an extra flag to the C++ compiler\n"
          "  --runtime <dir>    directory containing walnut_runtime.hpp\n"
          "  --keep-cpp         keep the generated C++ next to the executable\n"
          "  --no-warnings      suppress warnings\n"
          "  -v, --verbose      print the C++ compiler command\n"
          "  -h, --help         show this help\n";
}

bool parse_args(int argc, char** argv, Options& o, int& exit_code) {
    exit_code = 0;
    if (argc < 2) { print_usage(std::cerr); exit_code = 2; return false; }
    const std::string cmd = argv[1];

    if (cmd == "-h" || cmd == "--help" || cmd == "help") { print_usage(std::cout); return false; }
    if (cmd == "check")      o.command = Command::Check;
    else if (cmd == "emit")  o.command = Command::Emit;
    else if (cmd == "build") o.command = Command::Build;
    else if (cmd == "run")   o.command = Command::Run;
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
        else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3" || a == "-Os") o.opt = a;
        else if (a == "--cxx")         { const char* v = need(i, a); if (!v) return false; o.cxx = v; }
        else if (a == "--cxx-flag")    { const char* v = need(i, a); if (!v) return false; o.cxx_flags.push_back(v); }
        else if (a == "--runtime")     { const char* v = need(i, a); if (!v) return false; o.runtime_dir = v; }
        else if (a == "--keep-cpp")    o.keep_cpp = true;
        else if (a == "--no-warnings") o.no_warnings = true;
        else if (a == "-v" || a == "--verbose") o.verbose = true;
        else if (a == "-h" || a == "--help") { print_usage(std::cout); return false; }
        else if (!a.empty() && a[0] == '-') { std::cerr << "walnut: unknown option '" << a << "'\n"; exit_code = 2; return false; }
        else if (o.input.empty()) o.input = a;
        else { std::cerr << "walnut: more than one input file given ('" << o.input << "' and '" << a << "')\n"; exit_code = 2; return false; }
    }

    if (o.input.empty()) { std::cerr << "walnut: no input file\n"; exit_code = 2; return false; }
    if (const char* env = std::getenv("WALNUT_RUNTIME_DIR")) if (*env) o.runtime_dir = env;
    if (const char* env = std::getenv("WALNUT_CXX")) if (*env) o.cxx = env;
    return true;
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

bool generate(const Options& o, std::string& cpp) {
    walnut::CompilerPipeline compiler(o.input);
    compiler.set_path_style(fs::path(o.input).is_absolute() ? walnut::PathStyle::Absolute : walnut::PathStyle::Relative);
    for (const auto& d : o.include_dirs) compiler.add_include_dir(d);
    for (const auto& [name, value] : o.defines) compiler.define_macro(name, value);
    if (o.no_warnings) compiler.suppress_warnings();

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
    std::ostringstream out;
    bool ok = false;

    try {
        ok = emitter.emit(units, out);
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
    return true;
}

bool compile_cpp(const Options& o, const fs::path& cpp_path, const fs::path& exe_path) {
    std::string cmd = quote(o.cxx) + (g_needs_cpp20 ? " -std=c++20 " : " -std=c++17 ") + o.opt + " -w";
    cmd += " -I" + quote(o.runtime_dir);
    for (const std::string& inc : split_list(o.extra_includes)) cmd += " -I" + quote(inc);
    for (const std::string& f : o.cxx_flags) cmd += " " + f;
    cmd += " " + quote(cpp_path.string()) + " -o " + quote(exe_path.string());
    if (o.verbose) std::cerr << cmd << "\n";
    const int status = exit_status_of(std::system(cmd.c_str()));

    if (status != 0) {
        std::cerr << "walnut: the C++ compiler failed on the generated code (exit status " << status << ")\n";
        std::cerr << "walnut: this is a code generator bug; the generated C++ is at " << cpp_path.string() << "\n";
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

int run(const Options& o) {
    if (!fs::exists(o.input)) { std::cerr << "walnut: cannot open '" << o.input << "'\n"; return 1; }
    std::string cpp;
    if (!generate(o, cpp)) return 1;
    if (o.command == Command::Check) return 0;

    if (o.command == Command::Emit) {
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
        if (!write_file(cpp_path, cpp)) return 1;
        const bool ok = compile_cpp(o, cpp_path, exe);
        if (!tmp.empty() && ok) { std::error_code ec; fs::remove_all(tmp, ec); }
        return ok ? 0 : 1;
    }

    const fs::path tmp = make_temp_dir();
    const fs::path cpp_path = tmp / "program.cpp";
    const fs::path exe = tmp / "program";
    if (!write_file(cpp_path, cpp)) return 1;
    if (!compile_cpp(o, cpp_path, exe)) return 1;
    std::string cmd = quote(exe.string());
    for (const std::string& a : o.program_args) cmd += " " + quote(a);
    std::cout.flush();
    const int status = exit_status_of(std::system(cmd.c_str()));
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
