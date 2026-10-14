#include "Driver/build_cache.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace walnut {
namespace build {

namespace fs = std::filesystem;

namespace {

std::uint64_t fnv(std::string_view text, std::uint64_t seed) {
    std::uint64_t h = seed;

    for (unsigned char c : text) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }

    return h;
}

std::int64_t stamp_of(const fs::path& p, std::uintmax_t& size, bool& ok) {
    std::error_code ec;
    size = fs::file_size(p, ec);
    if (ec) { ok = false; return 0; }
    const auto t = fs::last_write_time(p, ec);
    if (ec) { ok = false; return 0; }
    ok = true;
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}

std::string normal(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    return a.lexically_normal().generic_string();
}

bool under(const std::string& path, const std::string& dir) {
    if (dir.empty() || path.size() <= dir.size()) return false;
    return path.compare(0, dir.size(), dir) == 0 && (path[dir.size()] == '/' || dir.back() == '/');
}

} // namespace

std::string hash_text(std::string_view text) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << fnv(text, 0xcbf29ce484222325ULL) << std::setw(16) << fnv(text, 0x84222325cbf29ce4ULL ^ text.size());
    return os.str();
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path default_cache_root() {
    if (const char* env = std::getenv("WALNUT_CACHE_DIR")) if (*env) return fs::path(env);
#ifdef _WIN32
    if (const char* env = std::getenv("LOCALAPPDATA")) if (*env) return fs::path(env) / "walnut" / "cache";
#else
    if (const char* env = std::getenv("XDG_CACHE_HOME")) if (*env) return fs::path(env) / "walnut";
    if (const char* env = std::getenv("HOME")) if (*env) return fs::path(env) / ".cache" / "walnut";
#endif
    return fs::temp_directory_path() / "walnut-cache";
}

std::vector<std::string> parse_depfile(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;

    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == ':' && (text[i + 1] == ' ' || text[i + 1] == '\n' || text[i + 1] == '\r' || text[i + 1] == '\\')) { start = i + 1; break; }
    }

    std::string cur;

    for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];

        if (c == '\\' && i + 1 < text.size()) {
            const char n = text[i + 1];
            if (n == '\n') { ++i; if (!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
            if (n == '\r' && i + 2 < text.size() && text[i + 2] == '\n') { i += 2; if (!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
            if (n == ' ' || n == '#') { cur += n; ++i; continue; }
        }

        if (c == '$' && i + 1 < text.size() && text[i + 1] == '$') { cur += '$'; ++i; continue; }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }

        cur += c;
    }

    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_fresh(const fs::path& output, const fs::path& meta) {
    std::error_code ec;
    if (!fs::is_regular_file(output, ec) || !fs::is_regular_file(meta, ec)) return false;
    std::ifstream in(meta);
    if (!in) return false;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::uintmax_t size = 0;
        std::int64_t stamp = 0;
        if (!(ls >> size >> stamp)) return false;
        std::string path;
        std::getline(ls, path);
        if (!path.empty() && path.front() == '\t') path.erase(0, 1);
        std::uintmax_t now_size = 0;
        bool ok = false;
        const std::int64_t now = stamp_of(fs::path(path), now_size, ok);
        if (!ok || now != stamp || now_size != size) return false;
    }

    return true;
}

void record_dependencies(const fs::path& meta, const fs::path& depfile, const std::vector<fs::path>& extra, const std::vector<fs::path>& ignore_under) {
    std::vector<std::string> paths;
    if (!depfile.empty()) for (const std::string& d : parse_depfile(read_text(depfile))) paths.push_back(normal(d));
    for (const fs::path& e : extra) paths.push_back(normal(e));
    std::vector<std::string> ignored;
    for (const fs::path& i : ignore_under) ignored.push_back(normal(i));
    std::ostringstream os;
    std::vector<std::string> seen;

    for (const std::string& p : paths) {
        bool skip = false;
        for (const std::string& i : ignored) if (under(p, i)) skip = true;
        for (const std::string& s : seen) if (s == p) skip = true;
        if (skip) continue;
        seen.push_back(p);
        std::uintmax_t size = 0;
        bool ok = false;
        const std::int64_t stamp = stamp_of(fs::path(p), size, ok);
        if (!ok) continue;
        os << size << ' ' << stamp << '\t' << p << '\n';
    }

    const fs::path tmp = fs::path(meta.string() + ".tmp");
    { std::ofstream out(tmp, std::ios::binary); out << os.str(); }
    replace_file(tmp, meta);
}

void touch(const fs::path& p) {
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
}

bool replace_file(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    if (!ec) return true;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(from, ec);
    return true;
}

void prune(const fs::path& root, int max_age_days) {
    std::error_code ec;
    const fs::path marker = root / ".last-prune";
    const auto now = fs::file_time_type::clock::now();

    if (fs::exists(marker, ec)) {
        const auto last = fs::last_write_time(marker, ec);
        if (!ec && now - last < std::chrono::hours(24)) return;
    }

    fs::create_directories(root, ec);
    { std::ofstream m(marker); }
    const auto limit = std::chrono::hours(24) * max_age_days;

    for (const char* sub : { "obj", "exe", "pch" }) {
        const fs::path dir = root / sub;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> doomed;

        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path p = it->path();
            if (p.extension() == ".deps" || p.extension() == ".part") continue;
            fs::path used = p;
            if (!fs::is_directory(p, ec)) used = fs::path(p.string() + ".deps");
            if (p.extension() == ".o") used = p.parent_path() / (p.stem().string() + ".deps");
            const auto t = fs::last_write_time(used, ec);
            if (ec || now - t > limit) { ec.clear(); doomed.push_back(p); doomed.push_back(used); }
        }

        for (const fs::path& p : doomed) fs::remove_all(p, ec);
    }
}

} // namespace build
} // namespace walnut
