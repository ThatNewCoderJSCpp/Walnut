#include "walnut_cli.hpp"

namespace walnut_cli {

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

std::string package_root_text() {
    if (!WALNUT_RELOCATABLE) return std::string();
    std::string root = (executable_dir() / "..").lexically_normal().string();
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    return root;
}

fs::path shipped_std_dir() {
    if (!WALNUT_RELOCATABLE) return fs::path();
    return fs::path(relocate("../std-prebuilt"));
}

std::string std_unit_flags(const std::string& flags) {
    std::string out = flags;
    const std::size_t at = out.find("-std=c++20");
    if (at != std::string::npos) out.replace(at, 10, "-std=c++17");
    return out;
}

std::string std_unit_key(const Options& o, const ObjectSet& set, const std::string& rel, const std::string& text, bool shipped) {
    namespace wb = walnut::build;
    std::string id = set.id;
    std::string flags = std_unit_flags(set.flags);
    if (shipped) {
        id = id.substr(id.find('\n') + 1);
        const std::string root = package_root_text();
        if (!root.empty()) {
            for (std::size_t at = flags.find(root); at != std::string::npos; at = flags.find(root, at + 8)) flags.replace(at, root.size(), "<walnut>");
        }
    }
    return wb::hash_key(std::string(shipped ? "shipped-std\n" : "std\n") + id + "\n" + flags + "\n" + current_abi(o).defines + "\n" + rel + "\n" + wb::content_hash(text));
}

bool use_shipped_std_unit(const Options& o, ObjectSet& set, const std::string& rel, const std::string& text) {
    const fs::path shipped = shipped_std_dir();
    std::error_code ec;
    if (shipped.empty() || !fs::is_directory(shipped, ec)) return false;
    const fs::path dir = shipped / std_unit_key(o, set, rel, text, true);
    const fs::path list = dir / "objects.txt";
    if (!fs::is_regular_file(list, ec)) return false;
    std::vector<fs::path> objs;
    std::istringstream lines(walnut::build::read_text(list));
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (!fs::is_regular_file(dir / line, ec)) return false;
        objs.push_back(dir / line);
    }
    if (objs.empty()) return false;
    for (const fs::path& obj : objs) set.std_objects.push_back(obj);
    return true;
}

bool prepare_std_units(const Options& o, ObjectSet& set, std::vector<CompileJob>& jobs, std::vector<PendingUnit>& pending) {
    namespace wb = walnut::build;
    const std::vector<std::string> wanted = wanted_std_units();

    std::error_code ec;

    for (const std::string& rel : wanted) {
        const walnut_library::File* lib = library_file_named(rel);
        if (!lib) continue;
        const std::string text(reinterpret_cast<const char*>(lib->data), lib->size);
        if (use_shipped_std_unit(o, set, rel, text)) continue;
        const std::string key = std_unit_key(o, set, rel, text, false);
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

        std::string flags = std_unit_flags(set.flags);
        if (cpp20 && flags.find("-std=c++17") != std::string::npos) flags.replace(flags.find("-std=c++17"), 10, "-std=c++20");
        const std::string pch = flags == set.flags ? set.pch_flag : std::string();
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
            add(name, cxx_cmd(o) + " " + pch + flags, src);
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

bool build_std_objects(const Options& o, std::vector<fs::path>& objects, const fs::path& ship) {
    namespace wb = walnut::build;
    if (wanted_std_units().empty()) return true;
    ObjectSet set;
    set.root = wb::default_cache_root();
    std::error_code ec;
    fs::create_directories(set.root / "std", ec);
    set.tmp = make_temp_dir();
    std::string id = compiler_query(o, "--version");
    id = id.substr(0, id.find('\n'));
    set.clang = id.find("clang") != std::string::npos;
    set.id = o.cxx + "\n" + id + "\n" + __DATE__ " " __TIME__ + "\n" + walnut_library::content_hash;
    set.flags = compile_flags(o);
    std::vector<CompileJob> jobs;
    std::vector<PendingUnit> pending;
    if (!prepare_std_units(o, set, jobs, pending)) { discard_std_units(pending); return false; }
    if (!run_jobs(jobs, o, set.tmp)) { discard_std_units(pending); return false; }
    finish_std_units(pending);
    objects = set.std_objects;
    fs::remove_all(set.tmp, ec);
    if (ship.empty()) return true;

    for (const std::string& rel : wanted_std_units()) {
        const walnut_library::File* lib = library_file_named(rel);
        if (!lib) continue;
        const std::string text(reinterpret_cast<const char*>(lib->data), lib->size);
        const fs::path from = set.root / "std" / std_unit_key(o, set, rel, text, false);
        const fs::path to = ship / std_unit_key(o, set, rel, text, true);
        if (!fs::is_regular_file(from / "objects.txt", ec)) {
            std::cerr << "walnut: the standard library module '" << rel << "' wasn't precompiled, so it can't be copied to " << ship.string() << "\n";
            return false;
        }
        fs::create_directories(to, ec);
        std::istringstream lines(walnut::build::read_text(from / "objects.txt"));
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            fs::copy_file(from / line, to / line, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "walnut: can't copy " << (from / line).string() << " to " << to.string() << ": " << ec.message() << "\n";
                return false;
            }
        }
        fs::copy_file(from / "objects.txt", to / "objects.txt", fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
    }
    return true;
}

} // namespace walnut_cli
