#ifndef WALNUT_TOOLCHAIN_HPP
#define WALNUT_TOOLCHAIN_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace walnut {
namespace toolchain {

struct Pinned {
    std::string name;
    std::string description;
    std::string url;
    std::string sha256;
    std::string archive_root;
    std::string compiler;
    std::vector<std::string> path_dirs;
    std::uint64_t size = 0;
};

std::optional<Pinned> pinned();

std::filesystem::path root();

std::filesystem::path install_dir(const Pinned& p);

std::optional<std::filesystem::path> installed_compiler();

std::vector<std::filesystem::path> installed_path_dirs();

void add_to_path(const std::vector<std::filesystem::path>& dirs);

std::string sha256_bytes(const std::string& data);

std::string sha256_file(const std::filesystem::path& file);

bool interactive();

bool ask_yes(const std::string& question, bool assume_yes);

struct InitOptions {
    bool yes = false;
    bool force = false;
    bool remove = false;
    bool status = false;
    bool print_compiler = false;
    std::string current_cxx;
    bool cxx_from_user = false;
};

int init(const InitOptions& options);

} // namespace toolchain
} // namespace walnut

#endif // WALNUT_TOOLCHAIN_HPP
