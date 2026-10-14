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
    std::unordered_map<std::string, std::string> m_virtual;

public:
    static constexpr const char* LIBRARY_ROOT = "<walnut>";

    explicit SourceManager(Arena& arena) noexcept : m_arena(arena) {}
    tokenizing::TokenStream lex(FileId id, ErrorReporter& reporter);

    std::optional<FileId> load(const std::string& raw_path);
    void add_virtual(const std::string& path, std::string content) { m_virtual[path] = std::move(content); }
    bool has_virtual(const std::string& path) const { return m_virtual.count(path) != 0; }

    const char* data(FileId id) const noexcept { return m_files[id].data; }
    std::size_t size(FileId id) const noexcept { return m_files[id].size; }
    std::size_t file_count()    const noexcept { return m_files.size();   }
    
    const tokenizing::Token* cached_tokens(FileId id) const noexcept { return m_files[id].tokens; }
    std::size_t              cached_token_count(FileId id) const noexcept { return m_files[id].token_count; }
    bool                     is_lexed(FileId id) const noexcept { return m_files[id].lexed; }
    
    std::size_t line_count(FileId id) const noexcept;

    const std::string& absolute_path(FileId id) const noexcept { return m_files[id].absolute; }
    
    const std::string& relative_path(FileId id) const;

    const std::string& filename(FileId id) const noexcept { return m_files[id].filename; }

    std::optional<FileId> file_containing(const char* p) const noexcept {
        for (std::size_t i = 0; i < m_files.size(); ++i) {
            const Entry& e = m_files[i];
            if (p >= e.data && p <= e.data + e.size) return static_cast<FileId>(i);
        }
        return std::nullopt;
    }
    const std::string& path(FileId id)     const noexcept { return m_files[id].absolute; }
};

} // namespace walnut

#endif // WALNUT_SOURCE_MANAGER_HPP