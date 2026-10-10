#include "Semantics/semantic_warning.hpp"

namespace walnut {
namespace semantics {

void SemanticWarning::emit(WarningReporter& warnings, FileId file_id, std::size_t line, std::string_view kind, std::string_view detail) {
    std::stringstream ss;
    ss << build_header(line, kind) << detail;
    warnings.report(ErrorPhase::Semantic, file_id, line, ss.str());
}

} // namespace semantics
} // namespace walnut
