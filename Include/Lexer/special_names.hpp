#ifndef WALNUT_LEXER_SPECIAL_NAMES_HPP
#define WALNUT_LEXER_SPECIAL_NAMES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace walnut {
namespace tokenizing {

struct ProjectName {
    std::string name;
    std::uint32_t code = 0;
    std::size_t line = 0;
};

struct ProjectNameProblem {
    std::size_t line = 0;
    std::string message;
};

struct ProjectNames {
    std::string path;
    std::vector<ProjectName> names;
    std::vector<ProjectNameProblem> problems;
};

bool special_character(std::string_view name, std::uint32_t& code);

std::string special_character_suggestion(std::string_view name);

std::vector<std::string_view> special_character_names(std::string_view prefix);

bool builtin_special_character(std::string_view name, std::uint32_t& code);

std::vector<std::string_view> builtin_special_character_names(std::string_view prefix);

std::size_t special_name_distance(std::string_view a, std::string_view b);

bool special_names_alike(char a, char b);

const char* project_file_name();

std::string find_project_file(const std::string& from);

ProjectNames parse_project_names(std::string_view text, const std::string& path);

ProjectNames read_project_names(const std::string& path);

void use_project_names(const ProjectNames& names);

void clear_project_names();

const std::vector<ProjectName>& project_names();

} // namespace tokenizing
} // namespace walnut

#endif // WALNUT_LEXER_SPECIAL_NAMES_HPP
