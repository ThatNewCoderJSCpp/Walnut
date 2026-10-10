#include "Common/source_manager.hpp"
#include <algorithm>

namespace walnut {

std::optional<FileId> SourceManager::load(const std::string& raw_path) {
    if (auto it = m_by_raw.find(raw_path); it != m_by_raw.end()) return it->second;  
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path abs_path = fs::absolute(raw_path, ec).lexically_normal();  
    std::string abs = ec ? raw_path : abs_path.string();

    if (auto it = m_by_path.find(abs); it != m_by_path.end()) {
        m_by_raw.emplace(raw_path, it->second);
        return it->second;
    }

    std::ifstream in(raw_path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const std::streamoff end = in.tellg();
    if (end < 0) return std::nullopt;
    const std::size_t n = static_cast<std::size_t>(end);
    in.seekg(0, std::ios::beg);
    char* buf = static_cast<char*>(m_arena.alloc_raw(n + 1, alignof(char)));
    if (n > 0 && !in.read(buf, static_cast<std::streamsize>(n))) return std::nullopt;
    buf[n] = '\0';
    const FileId id = static_cast<FileId>(m_files.size());
    Entry e;
    e.data     = buf;
    e.size     = n;
    e.absolute = abs;
    e.filename = fs::path(raw_path).filename().string();
    m_files.push_back(std::move(e));
    m_by_path.emplace(abs, id);
    m_by_raw.emplace(raw_path, id);
    return id;
}

auto SourceManager::line_count(FileId id) const noexcept -> std::size_t {
    const Entry& e = m_files[id];
    return static_cast<std::size_t>(std::count(e.data, e.data + e.size, '\n')) + 1;
}

const std::string& SourceManager::relative_path(FileId id) const {   
    const Entry& e = m_files[id];

    if (!e.relative_ready) {
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(e.absolute, ec);
        e.relative       = (ec || rel.empty()) ? e.absolute : rel.string();
        e.relative_ready = true;
    }

    return e.relative;
}

} // namespace walnut
