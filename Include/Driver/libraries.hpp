#ifndef WALNUT_DRIVER_LIBRARIES_HPP
#define WALNUT_DRIVER_LIBRARIES_HPP

#include "../Common/source_manager.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace walnut {
namespace build {

struct LibrarySource {
    std::string hash;
    std::string path;
};

struct LibraryManifest {
    std::filesystem::path      location;
    std::string                name;
    std::string                kind;
    std::string                walnut_version;
    std::string                target;
    std::string                safety;
    std::string                defines;
    bool                       cpp20 = false;
    std::vector<std::string>   binaries;
    std::vector<std::string>   runtime_files;
    std::vector<std::string>   link;
    std::vector<std::string>   std_modules;
    std::vector<LibrarySource> sources;

    std::filesystem::path dir() const { return location.parent_path(); }
    bool has_source(const std::string& hash) const;
};

struct LibraryAbi {
    std::string walnut_version;
    std::string target;
    std::string safety;
    std::string defines;
};

std::string content_hash(std::string_view text);

std::filesystem::path manifest_path_for(const std::filesystem::path& binary);

std::optional<LibraryManifest> read_manifest(const std::filesystem::path& path, std::string& error);

bool write_manifest(const LibraryManifest& m, const std::filesystem::path& path);

std::string abi_mismatch(const LibraryManifest& m, const LibraryAbi& abi);

class LibraryIndex {
public:
    explicit LibraryIndex(LibraryAbi abi) : m_abi(std::move(abi)) {}

    const LibraryManifest* add_explicit(const std::filesystem::path& manifest);
    void ignore(const std::filesystem::path& manifest);
    void never_prebuilt(const std::string& absolute_path);

    const LibraryManifest* owner_of(const SourceManager& sources, FileId file);
    std::string hash_of(const SourceManager& sources, FileId file);

    std::vector<const LibraryManifest*> used() const;
    const std::vector<std::string>& warnings() const { return m_warnings; }
    const std::vector<const LibraryManifest*>& explicit_libraries() const { return m_explicit_order; }
    bool explicitly_linked(const std::filesystem::path& binary) const;
    bool is_compatible(const LibraryManifest* m) const;

private:
    struct Entry {
        std::unique_ptr<LibraryManifest> manifest;
        bool                             compatible = true;
        bool                             used = false;
        bool                             warned = false;
    };

    LibraryAbi                                     m_abi;
    std::vector<std::unique_ptr<Entry>>            m_entries;
    std::vector<const LibraryManifest*>            m_explicit_order;
    std::map<std::string, std::vector<Entry*>>     m_by_dir;
    std::map<std::string, bool>                    m_scanned;
    std::vector<std::string>                       m_ignored;
    std::vector<std::string>                       m_inputs;
    std::vector<std::string>                       m_input_hashes;
    std::map<FileId, std::string>                  m_hashes;
    std::map<FileId, Entry*>                       m_owner;
    std::vector<std::string>                       m_warnings;

    Entry* load(const std::filesystem::path& manifest);
    void scan(const std::filesystem::path& dir);
};

} // namespace build
} // namespace walnut

#endif // WALNUT_DRIVER_LIBRARIES_HPP
