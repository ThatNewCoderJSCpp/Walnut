#ifndef SEMANTIC_WARNING_HPP
#define SEMANTIC_WARNING_HPP

#include <string>
#include <string_view>
#include <sstream>
#include <cstdint>
#include "../Common/compiler_warning.hpp"

namespace walnut {
namespace semantics {

struct SemanticWarning {
private:
    static std::string build_header(std::size_t line, std::string_view kind) {
        std::stringstream ss;
        ss << kind << " Warning on line " << line << ": ";
        return ss.str();
    }

public:
    static void emit(WarningReporter& warnings, FileId file_id, std::size_t line, std::string_view kind, std::string_view detail) {
        std::stringstream ss;
        ss << build_header(line, kind) << detail;
        warnings.report(ErrorPhase::Semantic, file_id, line, ss.str());
    }
};

} // namespace semantics
} // namespace walnut

#endif // SEMANTIC_WARNING_HPP