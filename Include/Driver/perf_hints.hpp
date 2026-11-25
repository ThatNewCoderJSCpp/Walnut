#ifndef WALNUT_DRIVER_PERF_HINTS_HPP
#define WALNUT_DRIVER_PERF_HINTS_HPP

#include "compiler.hpp"
#include "complexity.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace walnut {
namespace driver {

struct PerfHint {
    std::string   file;
    std::uint32_t line = 0;
    std::string   function;
    std::string   kind;
    std::string   message;
    std::string   before;
    std::string   after;
};

struct FunctionSpan {
    std::string   name;
    std::string   file;
    std::uint32_t first = 0;
    std::uint32_t last  = 0;
    std::size_t   statements = 0;
    bool          is_inline = false;
    bool          is_main = false;
};

struct FieldSpan {
    std::string   name;
    std::uint32_t line = 0;
};

struct RecordSpan {
    std::string            name;
    std::string            file;
    std::uint32_t          line = 0;
    std::vector<FieldSpan> fields;
};

struct PerfInfo {
    std::vector<PerfHint>           hints;
    std::vector<FunctionSpan>       functions;
    std::vector<RecordSpan>         records;
    std::vector<ComplexityEstimate> complexity;
};

PerfInfo performance_hints(CompilerPipeline& compiler, const std::function<bool(FileId)>& own_file);

} // namespace driver
} // namespace walnut

#endif // WALNUT_DRIVER_PERF_HINTS_HPP
