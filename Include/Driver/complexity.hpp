#ifndef WALNUT_DRIVER_COMPLEXITY_HPP
#define WALNUT_DRIVER_COMPLEXITY_HPP

#include "compiler.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace walnut {
namespace driver {

struct Growth {
    int  power = 0;
    int  logs = 0;
    bool exponential = false;

    Growth times(const Growth& o) const;
    bool below(const Growth& o) const;
    Growth larger(const Growth& o) const;
    std::string text() const;
};

struct ComplexityReason {
    std::uint32_t line = 0;
    std::string   text;
};

struct ComplexityEstimate {
    std::string                    name;
    std::string                    file;
    std::uint32_t                  first = 0;
    std::uint32_t                  last = 0;
    Growth                         time;
    Growth                         space;
    std::vector<ComplexityReason>  time_reasons;
    std::vector<ComplexityReason>  space_reasons;
    bool                           recursive = false;
};

std::vector<ComplexityEstimate> estimate_complexity(CompilerPipeline& compiler, const std::function<bool(FileId)>& own_file);

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_COMPLEXITY_HPP
