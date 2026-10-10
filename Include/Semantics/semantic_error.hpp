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

    static SemanticError make_and_report(ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& message);

    SemanticError(const std::string& message) : std::runtime_error(message) {}

public:
    static SemanticError redeclaration(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view name,
        FileId previous_file = INVALID_FILE,
        std::size_t previous_line = 0   // 0 == unknown
    );

    static SemanticError import_export_not_global(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        bool is_import
    );

    static SemanticError invalid_modifiers(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag_list,
        std::size_t count
    );

    static SemanticError conflicting_modifiers(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag_list
    );

    static SemanticError unresolved_name(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view name
    );

    static SemanticError duplicate_destructor(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::size_t first_line
    );

    static SemanticError no_matching_overload(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError ambiguous_call(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError invalid_cast(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view cast_kind, const std::string& from, const std::string& to
    );

    static SemanticError not_convertible(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        const std::string& from, const std::string& to
    );

    static SemanticError no_common_type(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        const std::string& a, const std::string& b
    );
    
    static SemanticError not_assignable(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view what, bool is_const
    );

    static SemanticError not_addressable(ErrorReporter& reporter, FileId file_id, std::size_t line);

    static SemanticError condition_not_bool(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    );

    static SemanticError bad_operand(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const std::string& type
    );

    static SemanticError ambiguous_defaults(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError inaccessible_member(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view member, bool is_private, std::string_view record
    );

    static SemanticError constraints_not_satisfied(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name, const std::string& what
    );

    static SemanticError bad_binary_operands(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const std::string& lhs, const std::string& rhs
    );

    static SemanticError bad_reference_binding(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& ref, bool to_rvalue
    );

    static SemanticError constant_needs_initializer(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view keyword, std::string_view name
    );

    static SemanticError auto_needs_initializer(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError bad_operator_arity(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const char* why
    );

    static SemanticError incomplete_type(ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name);

    static SemanticError redefinition(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, FileId prev_file, std::size_t prev_line
    );

    static SemanticError invalid_override(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const char* reason
    );

    static SemanticError missing_return_value(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    );

    static SemanticError return_value_in_void(
        ErrorReporter& reporter, FileId file_id, std::size_t line
    );

    static SemanticError misplaced_control(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view keyword, std::string_view allowed_in
    );

    static SemanticError switch_not_integral(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    );

    static SemanticError enum_base_not_int(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& type
    );

    static SemanticError coroutine_outside_function(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view keyword
    );

    static SemanticError coroutine_deduced_return(
        ErrorReporter& reporter, FileId file_id, std::size_t line
    );

    static SemanticError not_a_template(ErrorReporter& r, FileId f, std::size_t line, std::string_view name);

    static SemanticError template_arity(ErrorReporter& r, FileId f, std::size_t line, std::size_t want, std::size_t got);

    static SemanticError template_arg_form(ErrorReporter& r, FileId f, std::size_t line, std::size_t index);

    static SemanticError template_depth_exceeded(ErrorReporter& r, FileId f, std::size_t line, std::string_view name);

    static SemanticError template_unsupported(ErrorReporter& r, FileId f, std::size_t line, std::string_view what);

    static SemanticError static_assert_failed(ErrorReporter& r, FileId f, std::size_t line);

    static SemanticError template_arg_out_of_range(ErrorReporter& r, FileId f, std::size_t line, std::size_t index, const std::string& value);

    static SemanticError rethrow_outside_catch(ErrorReporter& reporter, FileId file_id, std::size_t line);

    static SemanticError noexcept_violation(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& what
    );

    static SemanticError not_a_namespace(ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& name);

        static SemanticError template_arg_overflow(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::size_t index, const std::string& value, const std::string& param_type
    );

    static SemanticError template_arg_underflow(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::size_t index, const std::string& value, const std::string& param_type
    );

    static SemanticError not_a_constant(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const char* why
    );

    static SemanticError fallthrough_at_end(ErrorReporter& reporter, FileId file_id, std::size_t line);

    static SemanticError fallthrough_not_last(ErrorReporter& reporter, FileId file_id, std::size_t line);

    static SemanticError missing_required_modifier(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag,             
        std::string_view required_list,    
        std::size_t required_count
    );

    static SemanticError modifier_rule_violation(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        const char* why
    );

    static SemanticError specialization_without_primary(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError specialization_arity(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::size_t got, std::size_t want
    );

    static SemanticError partial_function_specialization(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError specialization_needs_args(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError ambiguous_specialization(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    );

    static SemanticError specialization_default_param(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view param
    );

    static SemanticError unexported_name(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view spelling, const std::string& unit,
        std::size_t export_count, std::string_view suggestion
    );

    static SemanticError unresolved_import_source(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view spelling
    );

    static SemanticError import_target_failed(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view spelling, const std::string& unit
    );

    static SemanticError missing_module(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view spelling, const std::string& unit
    );

    static SemanticError export_undefined(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& unit
    );

    static SemanticError import_failure(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view why
    );

    static SemanticError emit(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& kind, const std::string& message
    );

    static SemanticError duplicate_export(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& unit, std::size_t first_line
    );
};

} // namespace semantics
} // namespace walnut

#endif // SEMANTIC_ERROR_HPP