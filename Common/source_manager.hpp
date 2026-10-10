#ifndef WALNUT_SOURCE_MANAGER_HPP
#define WALNUT_SOURCE_MANAGER_HPP

#include "arena_allocator.hpp"   

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>

namespace walnut {

class ErrorReporter; 

namespace tokenizing {
    class Token;
    class TokenStream;
}

using FileId = std::uint32_t;
inline constexpr FileId INVALID_FILE = static_cast<FileId>(-1);

class SourceManager {
    struct Entry {
        const char* data;        // NUL-terminated, owned by the arena
        std::size_t size;        // byte length, excluding the trailing '\0'
        std::string absolute;    // canonical absolute path 
        mutable std::string relative; // path relative to the current working directory
        mutable bool        relative_ready = false;    
        std::string filename;    // bare filename, no directory
        tokenizing::Token* tokens      = nullptr;
        std::size_t        token_count = 0;
        bool               lexed       = false;
    };

    Arena& m_arena;                                  
    std::vector<Entry> m_files; // index == FileId
    std::unordered_map<std::string, FileId> m_by_path;
    std::unordered_map<std::string, FileId> m_by_raw;

public:
    explicit SourceManager(Arena& arena) noexcept : m_arena(arena) {}
    tokenizing::TokenStream lex(FileId id, ErrorReporter& reporter);

    std::optional<FileId> load(const std::string& raw_path) {
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

    const char* data(FileId id) const noexcept { return m_files[id].data; }
    std::size_t size(FileId id) const noexcept { return m_files[id].size; }
    std::size_t file_count()    const noexcept { return m_files.size();   }
    
    const tokenizing::Token* cached_tokens(FileId id) const noexcept { return m_files[id].tokens; }
    std::size_t              cached_token_count(FileId id) const noexcept { return m_files[id].token_count; }
    bool                     is_lexed(FileId id) const noexcept { return m_files[id].lexed; }
    
    std::size_t line_count(FileId id) const noexcept {
        const Entry& e = m_files[id];
        return static_cast<std::size_t>(std::count(e.data, e.data + e.size, '\n')) + 1;
    }

    const std::string& absolute_path(FileId id) const noexcept { return m_files[id].absolute; }
    
    const std::string& relative_path(FileId id) const {   
        const Entry& e = m_files[id];

        if (!e.relative_ready) {
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(e.absolute, ec);
            e.relative       = (ec || rel.empty()) ? e.absolute : rel.string();
            e.relative_ready = true;
        }

        return e.relative;
    }

    const std::string& filename(FileId id) const noexcept { return m_files[id].filename; }
    const std::string& path(FileId id)     const noexcept { return m_files[id].absolute; }
};

} // namespace walnut

#endif // WALNUT_SOURCE_MANAGER_HPP