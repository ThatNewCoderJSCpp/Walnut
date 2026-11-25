#include "Cli/walnut_cli.hpp"

namespace walnut_cli {

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
    return run_program(o, cmd);
}

int run_program(const Options& o, const std::string& cmd) {
    if (!o.profile) {
        std::cout.flush();
        return exit_status_of(run_command(cmd));
    }
    const fs::path dir = make_temp_dir();
    const fs::path data = dir / "profile.tsv";
#if defined(_WIN32)
    _putenv_s("WALNUT_PROFILE_OUT", data.string().c_str());
#else
    setenv("WALNUT_PROFILE_OUT", data.string().c_str(), 1);
#endif
    std::cout.flush();
    const int status = exit_status_of(run_command(cmd));
    const int result = profile_report(o, status, data);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return result;
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
    const int status = run_program(o, cmd);
    std::error_code ec;
    if (!o.keep_cpp) fs::remove_all(tmp, ec);
    else std::cerr << "walnut: generated C++ kept at " << cpp_path.string() << "\n";
    return status;
}

} // namespace walnut_cli

using namespace walnut_cli;

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
        fs::path ship;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--into") {
                if (i + 1 >= argc) { std::cerr << "walnut: '--into' needs a folder\n"; return 2; }
                ship = argv[++i];
                continue;
            }
            args.push_back(argv[i]);
        }
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
        return precompile_std(o, ship);
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
    os << "usage: walnut init [--yes] [--force] [--no-precompile] | --status | --print-compiler | --remove\n"
          "\n"
          "Sets up a C++ compiler for walnut, which builds programs by compiling the C++ it generates.\n"
          "On Windows it downloads a pinned compiler that matches this walnut, checks its SHA-256, and unpacks it into\n"
          "a folder of its own (" << walnut::toolchain::root().string() << "); nothing else on the system changes.\n"
          "On Linux it installs the distribution's g++ with its package manager (asking for your password), in Termux\n"
          "on Android it installs Clang with pkg, and on macOS it opens Apple's installer for the Command Line Tools.\n"
          "\n"
          "  -y, --yes          don't ask before downloading or installing\n"
          "  --force            install walnut's own compiler even when another one is already on PATH\n"
          "  --status           show which compiler walnut uses and its version\n"
          "  --print-compiler   print only the path of the compiler walnut uses (for editors and build tools)\n"
          "  --remove           delete the compiler walnut init downloaded\n"
          "  --cxx <compiler>   check this compiler instead (with --status or --print-compiler)\n"
          "  --no-precompile    don't precompile the standard library afterwards\n"
          "\n"
          "Once there's a compiler, walnut init precompiles the standard library for it (like walnut library --precompile),\n"
          "so the first build of a program doesn't have to compile the std modules it uses.\n"
          "\n"
          "walnut picks its compiler in this order: --cxx, then WALNUT_CXX, then the one walnut init downloaded,\n"
          "then " WALNUT_DEFAULT_CXX " from PATH. WALNUT_TOOLCHAIN_DIR puts the downloaded compiler somewhere else.\n";
}

int precompile_after_init(const Options& chosen) {
    std::vector<std::string> args{ "walnut", "build" };
    if (chosen.cxx_from_user) { args.push_back("--cxx"); args.push_back(chosen.cxx); }
    args.push_back("walnut_precompile.wal");
    std::vector<char*> raw;
    for (std::string& a : args) raw.push_back(a.data());
    Options o;
    int code = 0;
    if (!parse_args(static_cast<int>(raw.size()), raw.data(), o, code)) return code;
    use_installed_toolchain(o);
    if (!o.managed_cxx && !find_program(o.cxx)) {
        for (const char* c : { "c++", "g++", "clang++" }) {
            if (find_program(c)) { o.cxx = c; break; }
        }
    }
    bool ready = find_program(o.cxx);
#if defined(__APPLE__)
    if (ready && !o.cxx_from_user && std::system("xcode-select -p >/dev/null 2>&1") != 0) ready = false;
#endif
    if (!ready) {
        std::cerr << "walnut: run 'walnut library --precompile' once a compiler is on PATH to precompile the standard library\n";
        return 0;
    }
    std::cout << "walnut: precompiling the standard library for " << o.cxx << " (this takes a minute the first time)\n";
    if (precompile_std(o) != 0) {
        std::cerr << "walnut: the compiler is set up, but precompiling the standard library failed; programs still build, and 'walnut library --precompile' tries again\n";
    }
    return 0;
}

int init_command(int argc, char** argv) {
    Options o;
    bool no_precompile = false;
    walnut::toolchain::InitOptions io;
    if (const char* env = std::getenv("WALNUT_CXX")) if (*env) { o.cxx = env; o.cxx_from_user = true; }
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-y" || a == "--yes") io.yes = true;
        else if (a == "--force") io.force = true;
        else if (a == "--remove") io.remove = true;
        else if (a == "--status") io.status = true;
        else if (a == "--print-compiler") io.print_compiler = true;
        else if (a == "--no-precompile") no_precompile = true;
        else if (a == "--cxx" && i + 1 < argc) { o.cxx = argv[++i]; o.cxx_from_user = true; }
        else if (a == "-h" || a == "--help") { print_init_usage(std::cout); return 0; }
        else { std::cerr << "walnut: unknown option '" << a << "' for init\n"; print_init_usage(std::cerr); return 2; }
    }
    use_installed_toolchain(o);
    io.current_cxx = o.cxx;
    io.cxx_from_user = o.cxx_from_user;
    const int result = walnut::toolchain::init(io);
    if (result != 0 || no_precompile || io.status || io.print_compiler || io.remove) return result;
    return precompile_after_init(o);
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
#if defined(__ANDROID__)
    if (!std::getenv("TMPDIR")) {
        std::error_code ec;
        const char* prefix = std::getenv("PREFIX");
        fs::path tmp = prefix && *prefix ? fs::path(prefix) / "tmp" : fs::path();
        if (tmp.empty() || !fs::is_directory(tmp, ec)) {
            const char* home = std::getenv("HOME");
            tmp = fs::path(home && *home ? home : ".") / ".cache" / "walnut-tmp";
            fs::create_directories(tmp, ec);
        }
        setenv("TMPDIR", tmp.string().c_str(), 1);
    }
#endif
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
