#include "walnut_cli.hpp"

namespace walnut_cli {

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
    cached = trimmed(compiler_query(o, "-dumpmachine"));
    return cached;
}

std::string object_set_id(const Options& o, const std::string& compiler_version) {
    return o.cxx + "\n" + compiler_version + "\n" + __DATE__ " " __TIME__ + "\n" + walnut_library::content_hash;
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

std::string single_translation_unit(const walnut::codegen::SplitProgram& prog) {
    const std::string prelude = "#include \"walnut_runtime.hpp\"\n#include \"program.hpp\"\n";
    std::string out = "#include \"walnut_runtime.hpp\"\n" + prog.header;

    for (const auto& [name, text] : prog.sources) {
        out += "\n";
        out += text.rfind(prelude, 0) == 0 ? text.substr(prelude.size()) : text;
    }

    return out;
}

bool compile_objects(const Options& o, const walnut::codegen::SplitProgram& prog, ObjectSet& set, bool single_tu, bool with_std) {
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
    std::string id = compiler_query(o, "--version");
    id = id.substr(0, id.find('\n'));
    set.clang = id.find("clang") != std::string::npos;
    set.id = object_set_id(o, id);
    set.flags = compile_flags(o);
    const std::string& flags = set.flags;
    const fs::path& root = set.root;
    const fs::path& tmp = set.tmp;

    {
        const std::string key = wb::hash_key("pch\n" + set.id + "\n" + flags);
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

    const fs::path pch_file = set.pch_flag.empty() ? fs::path() : root / "pch" / wb::hash_key("pch\n" + set.id + "\n" + flags) / (set.clang ? "walnut_runtime.pch" : "walnut_runtime.hpp.gch");
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
        add_job("the generated C++", wb::hash_key("object\n" + set.id + "\n" + flags + "\n" + body), cxx_cmd(o) + " " + set.pch_flag + flags + iquotes, src, extra);
    } else {
        for (const auto& [name, text] : prog.sources) {
            const fs::path src = tmp / name;
            const std::string body = finalize_cpp(text, o, src.string(), unused);
            if (!write_file(src, body)) return false;
            const std::string key = wb::hash_key("tu\n" + set.id + "\n" + flags + "\n" + header + "\n" + body);
            add_job("the generated C++ (" + name + ")", key, cxx_cmd(o) + " " + set.pch_flag + flags + iquotes, src, extra);
        }
    }

    for (std::size_t i = 0; i < g_native_code.size(); ++i) {
        const walnut::codegen::NativeCode& nc = g_native_code[i];
        const fs::path src = tmp / ("code" + std::to_string(i) + (nc.is_c ? ".c" : ".cpp"));
        const std::string text = native_code_text(nc);
        if (!write_file(src, text)) return false;
        const std::string part = nc.is_c ? cxx_cmd(o) + " " + c_flags(o) + iquotes : cxx_cmd(o) + " " + flags + iquotes;
        add_job("the code block in " + nc.path, wb::hash_key("code\n" + set.id + "\n" + part + "\n" + text), part, src, {});
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
            add_job(n, wb::hash_key("native\n" + set.id + "\n" + part + "\n" + key_path.string() + "\n" + wb::read_text(n)), part, key_path, {});
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
    const std::string origin = o.android_abi.empty() ? "@executable_path" : "$ORIGIN";
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
    const std::string printed = trimmed(compiler_query(o, "-print-prog-name=ar"));
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
    const std::string exe_name = wb::hash_key(link_key) + ".exe";
#else
    const std::string exe_name = wb::hash_key(link_key);
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

int precompile_std(const Options& o, const fs::path& ship) {
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
    if (!build_std_objects(o, objects, ship)) return 1;
    std::cout << "walnut: the " << g_lib.std_files.size() << " standard library modules are precompiled for " << o.cxx << " (" << objects.size() << " object files)\n";
    if (!ship.empty()) std::cout << "walnut: a copy to ship in a Walnut package is in " << ship.string() << "\n";
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

} // namespace walnut_cli
