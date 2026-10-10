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

    static SemanticError inaccessible_member(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view member, bool is_private, std::string_view record
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Access")
           << "'" << member << "' is a " << (is_private ? "private" : "protected") << " member of '" << record << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError constraints_not_satisfied(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name, const std::string& what
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Constraint")
           << "constraints not satisfied for '" << name << "'";
        if (!what.empty()) ss << ": " << what << " is not satisfied";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError bad_binary_operands(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view op, const std::string& lhs, const std::string& rhs
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type")
           << "operator '" << op << "' cannot be applied to operands of type '" << lhs << "' and '" << rhs << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError bad_reference_binding(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& ref, bool to_rvalue
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Type");
        if (to_rvalue) ss << "cannot bind non-const lvalue reference of type '" << ref << "' to an rvalue";
        else           ss << "cannot bind rvalue reference of type '" << ref << "' to an lvalue";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError constant_needs_initializer(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view keyword, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Constant")
           << keyword << " variable '" << name << "' requires an initializer";
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

    static SemanticError rethrow_outside_catch(ErrorReporter& reporter, FileId file_id, std::size_t line) {
        std::stringstream ss;
        ss << build_error_header(line, "Exception")
           << "a bare 'throw' may only appear inside a catch block";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError noexcept_violation(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& what
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Exception")
           << "'" << name << "' is declared 'noexcept' but its body may throw (" << what << ")";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError not_a_namespace(ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& name) {
        std::stringstream ss;
        ss << build_error_header(line, "Name") << "'" << name << "' is not a namespace";
        return make_and_report(reporter, file_id, line, ss.str());
    }

        static SemanticError template_arg_overflow(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::size_t index, const std::string& value, const std::string& param_type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "template argument " << index << " (" << value
           << ") does not fit in the parameter type '" << param_type << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError template_arg_underflow(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::size_t index, const std::string& value, const std::string& param_type
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "template argument " << index << " (" << value
           << ") underflows to zero in the parameter type '" << param_type << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError not_a_constant(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const char* why
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Constant")
           << "expression is not a valid constant: " << why;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError fallthrough_at_end(ErrorReporter& reporter, FileId file_id, std::size_t line) {
        std::stringstream ss;
        ss << build_error_header(line, "Switch")
           << "'fallthrough' cannot appear in the last case of a switch";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError fallthrough_not_last(ErrorReporter& reporter, FileId file_id, std::size_t line) {
        std::stringstream ss;
        ss << build_error_header(line, "Switch")
           << "'fallthrough' must be the last statement in its case";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError missing_required_modifier(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        std::string_view flag,             
        std::string_view required_list,    
        std::size_t required_count
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Modifier");
        ss << flag << " on " << decl_kind << " declaration requires "
           << (required_count > 1 ? "one of " : "") << required_list;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError modifier_rule_violation(
        ErrorReporter& reporter,
        FileId file_id,
        std::size_t line,
        std::string_view decl_kind,
        const char* why
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Modifier");
        ss << why << " (on " << decl_kind << " declaration)";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError specialization_without_primary(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "specialization of '" << name << "' has no primary template in scope";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError specialization_arity(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::size_t got, std::size_t want
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "specialization of '" << name << "' supplies " << got
           << " template argument" << (got == 1 ? "" : "s") << ", but the primary template declares " << want;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError partial_function_specialization(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "'" << name << "' is a partial specialization of a function template, which is not supported"
           << " (declare an overload instead)";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError specialization_needs_args(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "explicit specialization of '" << name << "' must write its template arguments"
           << " (e.g. '" << name << "<int>')";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError ambiguous_specialization(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view name
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "ambiguous partial specializations of '" << name
           << "'; no single specialization is more specialized than the others";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError specialization_default_param(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view param
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Template")
           << "parameter '" << param << "' of the specialization of '" << name
           << "' has a default argument; defaults belong on the primary template only";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError unexported_name(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view spelling, const std::string& unit,
        std::size_t export_count, std::string_view suggestion
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import");
        ss << "'" << name << "' is not exported by \"" << spelling << "\" (" << unit << ")";
        if (export_count == 0)        ss << "; that unit exports nothing";
        else if (!suggestion.empty()) ss << "; did you mean '" << suggestion << "'?";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError unresolved_import_source(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view spelling
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import")
           << "cannot resolve import source \"" << spelling << "\"";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError import_target_failed(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view spelling, const std::string& unit
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import")
           << "imported unit \"" << spelling << "\" (" << unit << ") failed to compile";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError missing_module(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, std::string_view spelling, const std::string& unit
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import")
           << "\"" << spelling << "\" (" << unit << ") declares no module '" << name << "'";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError export_undefined(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& unit
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import")
           << "'" << name << "' is exported by " << unit << " but resolves to no definition";
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError import_failure(
        ErrorReporter& reporter, FileId file_id, std::size_t line, std::string_view why
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import") << why;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError emit(
        ErrorReporter& reporter, FileId file_id, std::size_t line, const std::string& kind, const std::string& message
    ) {
        std::stringstream ss;
        ss << build_error_header(line, kind) << message;
        return make_and_report(reporter, file_id, line, ss.str());
    }

    static SemanticError duplicate_export(
        ErrorReporter& reporter, FileId file_id, std::size_t line,
        std::string_view name, const std::string& unit, std::size_t first_line
    ) {
        std::stringstream ss;
        ss << build_error_header(line, "Import")
           << "'" << name << "' is exported more than once by " << unit;
        if (first_line != 0) ss << " (first at line " << first_line << ")";
        return make_and_report(reporter, file_id, line, ss.str());
    }
};

} // namespace semantics
} // namespace walnut

#endif // SEMANTIC_ERROR_HPP