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

    static SemanticError unresolved_name(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Name");
        ss << "use of undeclared identifier '" << name << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError overload_missing_primary(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view decl_kind
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload");
        ss << "overloaded " << decl_kind << " '" << name << "' must mark exactly one declaration 'primary'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError overload_multiple_primary(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view decl_kind, std::size_t first_line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload");
        ss << "overloaded " << decl_kind << " '" << name << "' marks more than one 'primary'";
        if (first_line != 0) { ss << " (first on line " << first_line << ")"; }
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError duplicate_destructor(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::size_t first_line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Destructor");
        ss << "a type may declare only one destructor";
        if (first_line != 0) { ss << " (previous on line " << first_line << ")"; }
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError no_matching_overload(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload");
        ss << "no matching overload for call to '" << name << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError ambiguous_call(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload");
        ss << "ambiguous call to '" << name << "'; no single overload is the best match";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError invalid_cast(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view cast_kind, const std::string& from, const std::string& to
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Cast");
        ss << "invalid " << cast_kind << " from '" << from << "' to '" << to << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError not_convertible(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        const std::string& from, const std::string& to
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type");
        ss << "no implicit conversion from '" << from << "' to '" << to << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError no_common_type(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        const std::string& a, const std::string& b
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type");
        ss << "ternary operands have no common type ('" << a << "' and '" << b << "')";
        return make_and_report(reporter, file_id, line, ss.str());
    }
    
    static SemanticError not_assignable(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view what, bool is_const
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type");
        if (is_const) ss << "cannot " << what << " a const expression";
        else          ss << "cannot " << what << "; operand is not a modifiable lvalue";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError not_addressable(ErrorReporter& reporter, FileId file_id, std::size_t line) {
        std::stringstream ss;
        ss << build_error_header(line, "Type") << "cannot take the address of a non-lvalue expression";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError condition_not_bool(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "condition of type '" << type << "' is not contextually convertible to 'bool'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError bad_operand(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const std::string& type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "operator '" << op << "' cannot be applied to operand of type '" << type << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError ambiguous_defaults(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload")
           << "ambiguous defaulted parameters in '" << name
           << "': a defaulted parameter is shadowed by a later parameter of the same type";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError auto_needs_initializer(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "declaration of '" << name << "' with deduced type 'auto' requires an initializer";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError bad_operator_arity(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const char* why
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Overload")
           << "operator '" << op << "' has an illegal signature: " << why;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError incomplete_type(ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "'" << name << "' is an incomplete type (forward-declared but never defined)";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError redefinition(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, FileId prev_file, std::size_t prev_line
    ) {
        (void)prev_file;
        std::stringstream ss;
        ss << build_error_header(line, "Overload") << "redefinition of '" << name << "'";
        if (prev_line) ss << " (previously defined at line " << prev_line << ")";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError invalid_override(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const char* reason
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Override") << "'" << name << "' is marked 'override' but " << reason;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError missing_return_value(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Return")
           << "non-void function must return a value of type '" << type << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError return_value_in_void(
        ErrorReporter& reporter, FileId file_id, std::size_t line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Return")
           << "a void function must not return a value";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError misplaced_control(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view keyword, std::string_view allowed_in
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Control")
           << "'" << keyword << "' may only appear inside " << allowed_in;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError switch_not_integral(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "switch condition of type '" << type << "' must be of integral or enum type";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError enum_base_not_int(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "enum underlying type '" << type << "' must be an integer type";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError coroutine_outside_function(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view keyword
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Coroutine")
           << "'" << keyword << "' may only appear inside a function body";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError coroutine_mixed_return(
        ErrorReporter& reporter, FileId file_id, std::size_t line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Coroutine")
           << "a coroutine cannot use a value-returning 'return'; use 'co_return'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError coroutine_deduced_return(
        ErrorReporter& reporter, FileId file_id, std::size_t line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Coroutine")
           << "a coroutine may not have a deduced ('auto') return type";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError not_a_template(ErrorReporter& r, FileId f, std::size_t line, std::string_view name) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << "'" << name << "' is not a template but was given template arguments";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError template_arity(ErrorReporter& r, FileId f, std::size_t line, std::size_t want, std::size_t got) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << "wrong number of template arguments (expected " << want << ", got " << got << ")";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError template_arg_form(ErrorReporter& r, FileId f, std::size_t line, std::size_t index) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << "template argument " << (index + 1) << " has the wrong form (type vs value)";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError template_depth_exceeded(ErrorReporter& r, FileId f, std::size_t line, std::string_view name) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << "instantiation of '" << name << "' exceeded the recursion depth limit";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError template_unsupported(ErrorReporter& r, FileId f, std::size_t line, std::string_view what) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << what << " are not supported in template instantiation yet";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError static_assert_failed(ErrorReporter& r, FileId f, std::size_t line) {
        std::stringstream ss;
        ss << build_error_header(line, "Assertion");
        ss << "static assertion failed";
        return make_and_report(r, f, line, ss.str());
    }

    static SemanticError template_arg_out_of_range(ErrorReporter& r, FileId f, std::size_t line, std::size_t index, const std::string& value) {
        std::stringstream ss;
        ss << build_error_header(line, "Template");
        ss << "template argument " << (index + 1) << " (" << value << ") does not fit the parameter's declared type";
        return make_and_report(r, f, line, ss.str());
    }
};

} // namespace semantics
} // namespace walnut

#endif // SEMANTIC_ERROR_HPP