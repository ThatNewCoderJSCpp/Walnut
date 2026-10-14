#ifndef WALNUT_NATIVE_HEADERS_HPP
#define WALNUT_NATIVE_HEADERS_HPP

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace walnut {
namespace native {

struct HeaderScan {
    std::string                                      walnut_text;
    std::size_t                                      functions = 0;
    std::vector<std::pair<std::string, std::string>> skipped;
    std::vector<std::string>                         errors;
};

std::string libclang_location();

HeaderScan read_headers(const std::vector<std::string>& headers, const std::vector<std::string>& other_files, bool cpp, const std::vector<std::string>& clang_args);

void note_skipped(const std::string& name, const std::string& reason);

std::string skipped_reason(std::string_view name);

} // namespace native
} // namespace walnut

#endif // WALNUT_NATIVE_HEADERS_HPP
