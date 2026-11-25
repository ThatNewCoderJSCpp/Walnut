#include "walnut_cli.hpp"

namespace walnut_cli {

bool g_needs_cpp20 = false;

bool g_uses_native = false;

std::vector<std::string> g_native_from;

std::vector<walnut::codegen::NativeCode> g_native_code;

LibraryState g_lib;
walnut::driver::PerfInfo g_perf;

void print_usage(std::ostream& os) {
    os << "usage: walnut <command> [options] <main.wal> [more.wal ...] [C/C++ files] [-- program arguments]\n"
          "\n"
          "commands:\n"
          "  check   analyze the program and report errors and warnings\n"
          "  emit    write the generated C++ (to -o, or to standard output)\n"
          "  build   compile the program to an executable without running it (default: the input name without .wal);\n"
          "          with -c, --static or --shared it makes an object file or a library instead\n"
          "  run     build the program and run it\n"
          "  profile build the program with timing added, run it, and report its slowest lines and functions, with suggestions:\n"
          "          --top <n> rows in each table (default 10), --function <name> and --file <path> for a line-by-line breakdown\n"
          "          (and to keep only their suggestions), --report <file> for the report, --json <file> for editors,\n"
          "          --suggest-out <file> for the suggestions (a .diff or .patch file gets just the changes),\n"
          "          --annotate <file> for a copy of the program with each line's time, --apply to make the suggested\n"
          "          changes (each changed file is backed up as .bak), --no-suggest to leave suggestions out, --memory to also\n"
          "          count allocations (which lines allocate the most and what's still held at the end; it slows every allocation\n"
          "          down, so it's off unless asked), --complexity to also measure how each function grows with the size of its\n"
          "          arguments (slows every call down)\n"
          "  complexity  estimate how each function's time and memory grow with the size of its input (O(n), O(n^2), ...):\n"
          "          --run to also build and run the program and measure it, --function <name> and --file <path> to pick which,\n"
          "          --json <file> for editors\n"
          "  clean   delete walnut's build cache\n"
          "  highlight  print what each name and operator in the file is, as JSON, for editors (with --stdin, the file's text is read from standard input)\n"
          "  complete   print the names that fit at --at LINE:COLUMN (from 1, in bytes), as JSON, for editors' autocomplete; also takes --stdin\n"
          "  library    list the library files built into walnut; 'library <file>' prints one, 'library --extract <dir>' writes them all to a folder;\n"
          "             'library --precompile [options]' compiles every standard library module into the build cache ahead of time;\n"
          "             'library --precompile --into <dir>' also writes a copy to ship in a package's std-prebuilt folder;\n"
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
          "  --android <abi>    build for Android (arm64-v8a, x86_64, armeabi-v7a or x86) with the NDK's compiler and\n"
          "                     Walnut's Android kit; usually with --shared for an app's native library\n"
          "  --android-kit <d>  the folder of Walnut's Android kit (also WALNUT_ANDROID_KIT, or android-kit next to bin)\n"
          "  --ndk <dir>        the Android NDK (also ANDROID_NDK_HOME)\n"
          "  --android-api <n>  the oldest Android API level the program runs on (default: the kit's, 24)\n"
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
          "  --check-loops[=n]  stop with the .wal line when one run of a loop goes round more than n times\n"
          "                     (100000000 if n is left out; WALNUT_LOOP_LIMIT overrides it when the program runs)\n"
          "  --check-recursion[=n]  stop with the .wal line when calls are nested more than n deep, before the stack\n"
          "                     runs out (10000 if n is left out; WALNUT_DEPTH_LIMIT overrides it when the program runs)\n"
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

std::string android_abi_name(const std::string& a) {
    if (a == "arm64-v8a" || a == "arm64" || a == "aarch64") return "arm64-v8a";
    if (a == "armeabi-v7a" || a == "arm" || a == "armv7" || a == "armv7a") return "armeabi-v7a";
    if (a == "x86_64" || a == "x64") return "x86_64";
    if (a == "x86" || a == "i686") return "x86";
    return std::string();
}

std::string android_triple(const std::string& abi) {
    if (abi == "arm64-v8a") return "aarch64-linux-android";
    if (abi == "armeabi-v7a") return "armv7a-linux-androideabi";
    if (abi == "x86_64") return "x86_64-linux-android";
    return "i686-linux-android";
}

bool setup_android(Options& o) {
    const std::string abi = android_abi_name(o.android_abi);
    if (abi.empty()) {
        std::cerr << "walnut: --android takes an Android ABI: arm64-v8a, x86_64, armeabi-v7a or x86, not '" << o.android_abi << "'\n";
        return false;
    }
    o.android_abi = abi;
    if (o.command == Command::Run) {
        std::cerr << "walnut: an Android program can't run on this computer; use 'walnut build' and copy it to the device\n";
        return false;
    }

    std::error_code ec;
    std::string kit = o.android_kit;
    if (kit.empty()) if (const char* env = std::getenv("WALNUT_ANDROID_KIT")) kit = env;
    if (kit.empty() && WALNUT_RELOCATABLE) {
        const std::string beside = relocate("../android-kit");
        if (fs::is_directory(beside, ec)) kit = beside;
    }
    if (kit.empty()) {
        std::cerr << "walnut: --android needs Walnut's Android kit (Walnut-<version>-android-kit.zip from the Releases page); pass its folder with --android-kit <dir>, set WALNUT_ANDROID_KIT, or extract it into the Walnut folder as 'android-kit'\n";
        return false;
    }
    const fs::path abi_dir = fs::path(kit) / abi;
    const fs::path kit_file = abi_dir / "walnut-kit.txt";
    std::ifstream in(kit_file);
    if (!in) {
        std::cerr << "walnut: the Android kit in '" << kit << "' has nothing for " << abi << " (no " << kit_file.string() << ")\n";
        return false;
    }

    auto resolve = [&abi_dir](const std::string& list) {
        std::string out;
        for (const std::string& item : split_on(list, ';')) {
            std::string v = item;
            if (!v.empty() && v[0] != '-' && fs::path(v).is_relative()) v = (abi_dir / v).lexically_normal().string();
            out += (out.empty() ? "" : ";") + v;
        }
        return out;
    };

    int kit_api = 0;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "api") kit_api = std::atoi(value.c_str());
        else if (key == "runtime-libs") o.extra_libs = resolve(value);
        else if (key == "optional-archives") o.optional_archives = resolve(value);
        else if (key == "optional-libs") o.optional_libs = resolve(value);
    }
    const int api = o.android_api ? o.android_api : (kit_api ? kit_api : 24);
    if (kit_api && api < kit_api) {
        std::cerr << "walnut: the Android kit for " << abi << " needs API " << kit_api << " or newer, not " << api << "\n";
        return false;
    }

    if (!o.cxx_from_user) {
        std::string ndk = o.ndk;
        for (const char* var : { "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK_LATEST_HOME", "ANDROID_NDK" }) {
            if (!ndk.empty()) break;
            if (const char* env = std::getenv(var)) if (*env) ndk = env;
        }
        if (ndk.empty()) {
            std::cerr << "walnut: --android needs the Android NDK; pass its folder with --ndk <dir> or set ANDROID_NDK_HOME\n";
            return false;
        }
#if defined(_WIN32)
        const char* host = "windows-x86_64";
        const std::string suffix = ".cmd";
#elif defined(__APPLE__)
        const char* host = "darwin-x86_64";
        const std::string suffix;
#else
        const char* host = "linux-x86_64";
        const std::string suffix;
#endif
        fs::path bin = fs::path(ndk) / "toolchains" / "llvm" / "prebuilt" / host / "bin";
        if (!fs::is_directory(bin, ec)) {
            for (const fs::directory_entry& e : fs::directory_iterator(fs::path(ndk) / "toolchains" / "llvm" / "prebuilt", ec)) {
                if (fs::is_directory(e.path() / "bin", ec)) { bin = e.path() / "bin"; break; }
            }
        }
        const fs::path cxx = bin / (android_triple(abi) + std::to_string(api) + "-clang++" + suffix);
        if (!fs::exists(cxx, ec)) {
            std::cerr << "walnut: the NDK in '" << ndk << "' has no compiler for " << abi << " at API " << api << " (" << cxx.string() << "); use a newer NDK or another --android-api\n";
            return false;
        }
        o.cxx = cxx.string();
        o.cxx_from_user = true;
    }

    o.native_link.push_back("-static-libstdc++");
    o.native_link.push_back("-Wl,-z,max-page-size=16384");
    return true;
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
    else if (cmd == "profile") { o.command = Command::Run; o.profile = true; }
    else if (cmd == "complexity") { o.command = Command::Check; o.complexity = true; }
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
        else if (a == "--check-loops" || a.rfind("--check-loops=", 0) == 0 || a == "--check-recursion" || a.rfind("--check-recursion=", 0) == 0) {
            const bool loops = a.rfind("--check-loops", 0) == 0;
            const std::size_t eq = a.find('=');
            std::string n = loops ? "100000000" : "10000";
            if (eq != std::string::npos) {
                n = a.substr(eq + 1);
                const bool digits = !n.empty() && n.size() <= 18 && n.find_first_not_of("0123456789") == std::string::npos && n.find_first_not_of('0') != std::string::npos;
                if (!digits) { std::cerr << "walnut: " << a.substr(0, eq) << " takes a whole number above 0, not '" << n << "'\n"; exit_code = 2; return false; }
            }
            o.safety.push_back(std::string(loops ? "-DWALNUT_CHECK_LOOPS=" : "-DWALNUT_CHECK_RECURSION=") + n + "ULL");
        }
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
        else if (a == "--android")     { const char* v = need(i, a); if (!v) return false; o.android_abi = v; }
        else if (a == "--android-kit") { const char* v = need(i, a); if (!v) return false; o.android_kit = v; }
        else if (a == "--ndk")         { const char* v = need(i, a); if (!v) return false; o.ndk = v; }
        else if (a == "--android-api") {
            const char* v = need(i, a);
            if (!v) return false;
            o.android_api = std::atoi(v);
            if (o.android_api < 21) { std::cerr << "walnut: --android-api takes an Android API level, 21 or newer, such as 26\n"; exit_code = 2; return false; }
        }
        else if (a == "--no-prebuilt-std") o.prebuilt_std = false;
        else if (o.profile && (a == "--top" || a.rfind("--top=", 0) == 0)) {
            std::string v;
            if (a == "--top") { const char* n = need(i, a); if (!n) return false; v = n; }
            else v = a.substr(6);
            const int n = std::atoi(v.c_str());
            if (n <= 0 || v.find_first_not_of("0123456789") != std::string::npos) { std::cerr << "walnut: --top takes a whole number above 0, not '" << v << "'\n"; exit_code = 2; return false; }
            o.profile_top = n;
        }
        else if ((o.profile || o.complexity) && a == "--function") { const char* v = need(i, a); if (!v) return false; o.profile_functions.push_back(v); }
        else if ((o.profile || o.complexity) && a == "--file")     { const char* v = need(i, a); if (!v) return false; o.profile_files.push_back(v); }
        else if ((o.profile || o.complexity) && a == "--json")     { const char* v = need(i, a); if (!v) return false; o.profile_json = v; }
        else if (o.profile && a == "--report")   { const char* v = need(i, a); if (!v) return false; o.profile_report = v; }
        else if (o.profile && a == "--suggest-out") { const char* v = need(i, a); if (!v) return false; o.profile_suggest_out = v; }
        else if (o.profile && a == "--annotate") { const char* v = need(i, a); if (!v) return false; o.profile_annotate = v; }
        else if (o.profile && a == "--no-suggest") o.profile_suggest = false;
        else if (o.profile && a == "--apply")    o.profile_apply = true;
        else if (o.profile && a == "--no-memory") o.profile_memory = false;
        else if (o.profile && a == "--memory") o.profile_memory = true;
        else if (o.profile && a == "--complexity") o.profile_complexity = true;
        else if (o.complexity && (a == "--run" || a == "--measure")) o.complexity_run = true;
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
    if (!o.android_abi.empty() && !setup_android(o)) {
        exit_code = 2;
        return false;
    }
    if (o.android_abi.empty() && (!o.android_kit.empty() || !o.ndk.empty() || o.android_api != 0)) {
        std::cerr << "walnut: --android-kit, --ndk and --android-api go with --android <abi>\n";
        exit_code = 2;
        return false;
    }

    std::sort(o.safety.begin(), o.safety.end());
    o.safety.erase(std::unique(o.safety.begin(), o.safety.end()), o.safety.end());
    for (const std::string& d : o.safety) o.cxx_flags.push_back(d);
    if (o.complexity && o.complexity_run) {
        o.command = Command::Run;
        o.profile = true;
        o.profile_complexity = true;
        o.profile_suggest = false;
    }
    if (o.profile) o.cxx_flags.push_back("-DWALNUT_PROFILE");
    if (o.profile && o.profile_memory) o.cxx_flags.push_back("-DWALNUT_PROFILE_MEMORY");
    if (o.profile && o.profile_complexity) o.cxx_flags.push_back("-DWALNUT_PROFILE_COMPLEXITY");

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

bool is_library_path(const std::string& path) {
    return path.rfind(walnut::SourceManager::LIBRARY_ROOT, 0) == 0;
}

std::string library_relative(const std::string& path) {
    const std::string root = std::string(walnut::SourceManager::LIBRARY_ROOT) + "/";
    return path.rfind(root, 0) == 0 ? path.substr(root.size()) : path;
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

bool generate(const Options& o, std::string& cpp, walnut::codegen::SplitProgram* split, bool std_prebuilt) {
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

    if (o.profile || o.complexity) {
        const walnut::SourceManager& srcs = compiler.get_sources();
        g_perf = walnut::driver::performance_hints(compiler, [&srcs, &index](walnut::FileId f) {
            if (f >= srcs.file_count()) return false;
            const std::string& path = srcs.absolute_path(f);
            return walnut::is_walnut_file(walnut::classify_file(path)) && !is_library_path(path) && index.owner_of(srcs, f) == nullptr;
        });
    }

    if (o.command == Command::Check) {
        if (!o.no_warnings) for (const std::string& w : index.warnings()) std::cerr << "walnut: warning: " << w << "\n";
        if (o.complexity) return complexity_report(o) == 0;
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

fs::path locate_program(const std::string& name) {
    std::vector<std::string> names{ name };
#ifdef _WIN32
    if (fs::path(name).extension().empty()) names.push_back(name + ".exe");
#endif
    std::error_code ec;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        for (const std::string& n : names) if (fs::is_regular_file(n, ec)) return fs::path(n);
        return fs::path();
    }
    const char* path = std::getenv("PATH");
    if (!path) return fs::path();
    for (const std::string& dir : split_on(path, kPathListSeparator)) {
        for (const std::string& n : names) if (fs::is_regular_file(fs::path(dir) / n, ec)) return fs::path(dir) / n;
    }
    return fs::path();
}

std::string compiler_query(const Options& o, const std::string& args) {
    static std::unordered_map<std::string, std::string> memo;
    const std::string memo_key = o.cxx + "\n" + args;
    if (auto it = memo.find(memo_key); it != memo.end()) return it->second;

    const fs::path exe = locate_program(o.cxx);
    std::error_code ec;
    std::string stamp;
    if (!exe.empty()) {
        const auto when = fs::last_write_time(exe, ec);
        const auto size = fs::file_size(exe, ec);
        if (!ec) stamp = exe.string() + "\n" + std::to_string(static_cast<long long>(when.time_since_epoch().count())) + "\n" + std::to_string(static_cast<unsigned long long>(size));
    }

    fs::path file;
    if (!stamp.empty()) {
        file = walnut::build::default_cache_root() / "compiler" / (walnut::build::hash_key("query\n" + memo_key + "\n" + stamp) + ".txt");
        if (fs::is_regular_file(file, ec)) {
            std::string cached = walnut::build::read_text(file);
            memo.emplace(memo_key, cached);
            return cached;
        }
    }

    std::string out = capture_command(cxx_cmd(o) + " " + args);
    memo.emplace(memo_key, out);
    if (!file.empty() && !out.empty()) {
        fs::create_directories(file.parent_path(), ec);
        const fs::path part = fs::path(file.string() + ".part");
        {
            std::ofstream f(part, std::ios::binary);
            f << out;
        }
        fs::rename(part, file, ec);
        if (ec) fs::remove(part, ec);
    }
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

    const std::string out = compiler_query(o, "-print-search-dirs");
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

} // namespace walnut_cli
