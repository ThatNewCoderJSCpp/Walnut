#ifndef WALNUT_DRIVER_HIGHLIGHT_HPP
#define WALNUT_DRIVER_HIGHLIGHT_HPP

#include "compiler.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace walnut {
namespace driver {

std::string highlight_json(CompilerPipeline& compiler);

struct CompletionRequest {
    enum class Mode { Name, Member, Arrow, Scope };
    Mode mode = Mode::Name;
    std::size_t start = 0;
    std::uint32_t line = 0;
    std::vector<std::string> qualifier;
};

std::string complete_json(CompilerPipeline& compiler, std::string_view placeholder, const CompletionRequest& request);

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_HIGHLIGHT_HPP
