#ifndef SEMANTIC_ERROR_HPP
#define SEMANTIC_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <cstdint>
#include "../Common/error_reporter.hpp"

namespace walnut {
namespace semantics {

class SemanticError : public std::runtime_error {
private:
    static std::string build_error_header(std::size_t line, const std::string& error_type) {
        std::stringstream ss;
        ss << error_type << " Error on line " << line << ": ";
        return ss.str();
    }

    static SemanticError make_and_report(ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& message) {
        reporter.report(ErrorPhase::Semantic, file_id, line, message);
        return SemanticError(message);
    }

    SemanticError(const std::string& message) : std::runtime_error(message) {}

public:
    static SemanticError redeclaration(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view name,
        FileId previous_file = INVALID_FILE,
        std::size_t previous_line = 0   // 0 == unknown
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Redeclaration");
        ss << "Redeclaration of '" << name << "'";

        if (previous_line != 0) {
            ss << " (previously declared ";

            if (previous_file != INVALID_FILE && previous_file != file_id) {
                const std::string where = reporter.display_path(previous_file);
                if (!where.empty()) { ss << "in " << where << " "; }
            }

            ss << "on line " << previous_line << ")";
        }

        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError import_export_not_global(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        bool is_import
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Scope");
        ss << (is_import ? "'import'" : "'export'") << " declarations are only allowed at global scope";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError invalid_modifiers(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag_list,
        std::size_t count
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Modifier");
        ss << "invalid modifier" << (count > 1 ? "s " : " ") << flag_list << " on " << decl_kind << " declaration";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError conflicting_modifiers(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag_list
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Modifier");
        ss << "conflicting modifiers " << flag_list << " on " << decl_kind << " declaration (at most one may be used)";
        return make_and_report(reporter, file_id, line, ss.str());
    }
};

} // namespace semantics
} // namespace walnut

#endif // SEMANTIC_ERROR_HPP