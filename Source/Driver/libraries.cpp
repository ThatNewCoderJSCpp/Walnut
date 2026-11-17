#include "Driver/libraries.hpp"
#include "Driver/build_cache.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace walnut {
namespace build {

namespace fs = std::filesystem;

namespace {

std::string normal_path(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::weakly_canonical(p, ec);
    if (ec) a = fs::absolute(p, ec);
    return a.lexically_normal().generic_string();
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool LibraryManifest::has_source(const std::string& hash) const {
    for (const LibrarySource& s : sources) if (s.hash == hash) return true;
    return false;
}

std::string content_hash(std::string_view text) {
    return hash_text(text).substr(0, 16);
}

fs::path manifest_path_for(const fs::path& binary) {
    std::string name = binary.filename().string();
    const std::string low = lower(name);

    if (low.size() > 6 && low.compare(low.size() - 6, 6, ".dll.a") == 0) {
        name.resize(name.size() - 6);
    } else {
        for (const char* ext : { ".a", ".so", ".dylib", ".dll", ".lib", ".o", ".obj" }) {
            const std::string e = ext;
            if (low.size() > e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0) { name.resize(name.size() - e.size()); break; }
        }
    }

    return binary.parent_path() / (name + ".walnut");
}

std::optional<LibraryManifest> read_manifest(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot read " + path.string(); return std::nullopt; }
    LibraryManifest m;
    m.location = path;
    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string value = space == std::string::npos ? std::string() : line.substr(space + 1);

        if (first) {
            if (key != "walnut-library") { error = path.string() + " isn't a Walnut library description"; return std::nullopt; }
            if (value != "1") { error = path.string() + " was written by a newer walnut (format " + value + ")"; return std::nullopt; }
            first = false;
            continue;
        }

        if (key == "name") m.name = value;
        else if (key == "kind") m.kind = value;
        else if (key == "walnut") m.walnut_version = value;
        else if (key == "target") m.target = value;
        else if (key == "safety") m.safety = value == "-" ? std::string() : value;
        else if (key == "defines") m.defines = value == "-" ? std::string() : value;
        else if (key == "cpp") m.cpp20 = value == "20";
        else if (key == "binary") m.binaries.push_back(value);
        else if (key == "runtime") m.runtime_files.push_back(value);
        else if (key == "link") m.link.push_back(value);
        else if (key == "std") m.std_modules.push_back(value);
        else if (key == "source") {
            const std::size_t gap = value.find(' ');
            if (gap == std::string::npos) continue;
            m.sources.push_back(LibrarySource{ value.substr(0, gap), value.substr(gap + 1) });
        }
    }

    if (first) { error = path.string() + " is empty"; return std::nullopt; }
    return m;
}

bool write_manifest(const LibraryManifest& m, const fs::path& path) {
    std::ostringstream os;
    os << "walnut-library 1\n";
    os << "name " << m.name << "\n";
    os << "kind " << m.kind << "\n";
    os << "walnut " << m.walnut_version << "\n";
    os << "target " << m.target << "\n";
    os << "safety " << (m.safety.empty() ? std::string("-") : m.safety) << "\n";
    os << "defines " << (m.defines.empty() ? std::string("-") : m.defines) << "\n";
    os << "cpp " << (m.cpp20 ? "20" : "17") << "\n";
    for (const std::string& b : m.binaries) os << "binary " << b << "\n";
    for (const std::string& r : m.runtime_files) os << "runtime " << r << "\n";
    for (const std::string& l : m.link) os << "link " << l << "\n";
    for (const std::string& s : m.std_modules) os << "std " << s << "\n";
    for (const LibrarySource& s : m.sources) os << "source " << s.hash << " " << s.path << "\n";
    const fs::path tmp = fs::path(path.string() + ".part");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << os.str();
        if (!out) return false;
    }
    return replace_file(tmp, path);
}

std::string abi_mismatch(const LibraryManifest& m, const LibraryAbi& abi) {
    if (m.walnut_version != abi.walnut_version) return "it was built by walnut " + m.walnut_version + ", and this is walnut " + abi.walnut_version;
    if (!m.target.empty() && !abi.target.empty() && m.target != abi.target) return "it was built for " + m.target + ", and this build is for " + abi.target;
    if (m.safety != abi.safety) {
        auto shown = [](const std::string& s) { return s.empty() ? std::string("no safety checks") : "safety checks " + s; };
        return "it was built with " + shown(m.safety) + ", and this build uses " + shown(abi.safety);
    }
    if (m.defines != abi.defines) {
        auto shown = [](const std::string& s) { return s.empty() ? std::string("no -D macros") : "-D " + s; };
        return "it was built with " + shown(m.defines) + ", and this build uses " + shown(abi.defines);
    }
    return std::string();
}

auto LibraryIndex::load(const fs::path& manifest) -> Entry* {
    const std::string key = normal_path(manifest);
    for (const std::unique_ptr<Entry>& e : m_entries) if (normal_path(e->manifest->location) == key) return e.get();
    if (std::find(m_ignored.begin(), m_ignored.end(), key) != m_ignored.end()) return nullptr;
    std::string error;
    std::optional<LibraryManifest> m = read_manifest(manifest, error);

    if (!m) {
        m_warnings.push_back(error);
        return nullptr;
    }

    auto entry = std::make_unique<Entry>();
    entry->manifest = std::make_unique<LibraryManifest>(std::move(*m));
    const std::string why = abi_mismatch(*entry->manifest, m_abi);

    if (!why.empty()) {
        entry->compatible = false;
        m_warnings.push_back("the library " + manifest.filename().string() + " can't be used: " + why + "; its Walnut code is compiled from source instead");
        entry->warned = true;
    } else {
        for (const std::string& b : entry->manifest->binaries) {
            std::error_code ec;
            if (fs::exists(entry->manifest->dir() / b, ec)) continue;
            entry->compatible = false;
            m_warnings.push_back("the library " + manifest.filename().string() + " can't be used: " + b + " is missing next to it; its Walnut code is compiled from source instead");
            entry->warned = true;
            break;
        }
    }

    m_entries.push_back(std::move(entry));
    return m_entries.back().get();
}

void LibraryIndex::scan(const fs::path& dir) {
    const std::string key = normal_path(dir);
    if (m_scanned[key]) return;
    m_scanned[key] = true;
    std::error_code ec;

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".walnut") continue;
        if (!it->is_regular_file(ec)) continue;
        Entry* e = load(it->path());
        if (e && e->manifest->kind != "object") m_by_dir[key].push_back(e);
    }
}

const LibraryManifest* LibraryIndex::add_explicit(const fs::path& manifest) {
    Entry* e = load(manifest);
    if (!e) return nullptr;
    if (std::find(m_explicit_order.begin(), m_explicit_order.end(), e->manifest.get()) == m_explicit_order.end()) m_explicit_order.push_back(e->manifest.get());
    return e->manifest.get();
}

void LibraryIndex::ignore(const fs::path& manifest) {
    m_ignored.push_back(normal_path(manifest));
}

void LibraryIndex::never_prebuilt(const std::string& absolute_path) {
    m_inputs.push_back(normal_path(absolute_path));
    const std::string text = read_text(absolute_path);
    if (!text.empty()) m_input_hashes.push_back(content_hash(text));
}

bool LibraryIndex::explicitly_linked(const fs::path& binary) const {
    const std::string want = normal_path(binary);
    for (const LibraryManifest* m : m_explicit_order) {
        for (const std::string& b : m->binaries) if (normal_path(m->dir() / b) == want) return true;
    }
    return false;
}

bool LibraryIndex::is_compatible(const LibraryManifest* m) const {
    for (const std::unique_ptr<Entry>& e : m_entries) if (e->manifest.get() == m) return e->compatible;
    return false;
}

std::string LibraryIndex::hash_of(const SourceManager& sources, FileId file) {
    if (auto it = m_hashes.find(file); it != m_hashes.end()) return it->second;
    const std::string h = content_hash(std::string_view(sources.data(file), sources.size(file)));
    m_hashes[file] = h;
    return h;
}

const LibraryManifest* LibraryIndex::owner_of(const SourceManager& sources, FileId file) {
    if (file == INVALID_FILE || file >= sources.file_count()) return nullptr;
    if (auto it = m_owner.find(file); it != m_owner.end()) return it->second ? it->second->manifest.get() : nullptr;
    m_owner[file] = nullptr;
    const std::string& path = sources.absolute_path(file);
    if (path.empty() || path.rfind(SourceManager::LIBRARY_ROOT, 0) == 0) return nullptr;
    if (std::find(m_inputs.begin(), m_inputs.end(), normal_path(path)) != m_inputs.end()) return nullptr;
    const std::string hash = hash_of(sources, file);
    Entry* found = nullptr;

    for (const std::unique_ptr<Entry>& e : m_entries) {
        if (std::find(m_explicit_order.begin(), m_explicit_order.end(), e->manifest.get()) == m_explicit_order.end()) continue;
        if (e->manifest->has_source(hash)) { found = e.get(); break; }
    }

    if (!found) {
        const fs::path dir = fs::path(path).parent_path();
        scan(dir);
        for (Entry* e : m_by_dir[normal_path(dir)]) if (e->manifest->has_source(hash)) { found = e; break; }
    }

    if (found) {
        for (const std::string& h : m_input_hashes) if (found->manifest->has_source(h)) { found = nullptr; break; }
    }

    if (!found || !found->compatible) return nullptr;
    found->used = true;
    m_owner[file] = found;
    return found->manifest.get();
}

std::vector<const LibraryManifest*> LibraryIndex::used() const {
    std::vector<const LibraryManifest*> out;
    for (const std::unique_ptr<Entry>& e : m_entries) if (e->used && e->compatible) out.push_back(e->manifest.get());
    return out;
}

} // namespace build
} // namespace walnut
