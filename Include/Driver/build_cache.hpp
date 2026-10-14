#ifndef WALNUT_BUILD_CACHE_HPP
#define WALNUT_BUILD_CACHE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace walnut {
namespace build {

std::string hash_text(std::string_view text);

std::string read_text(const std::filesystem::path& p);

std::filesystem::path default_cache_root();

bool is_fresh(const std::filesystem::path& output, const std::filesystem::path& meta);

void record_dependencies(const std::filesystem::path& meta, const std::filesystem::path& depfile, const std::vector<std::filesystem::path>& extra, const std::vector<std::filesystem::path>& ignore_under);

std::vector<std::string> parse_depfile(const std::string& text);

void touch(const std::filesystem::path& p);

void prune(const std::filesystem::path& root, int max_age_days);

bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to);

} // namespace build
} // namespace walnut

#endif // WALNUT_BUILD_CACHE_HPP
