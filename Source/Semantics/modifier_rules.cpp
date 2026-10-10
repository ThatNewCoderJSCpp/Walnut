#include "Semantics/modifier_rules.hpp"

namespace walnut {
namespace semantics {

std::string mod_flag_list(std::uint16_t bits) {
    std::string out;

    for (std::uint16_t b = 1; b; b = std::uint16_t(b << 1)) {
        if (bits & b) {
            if (!out.empty()) out += ", ";
            out += '\''; out += RM::flag_name(static_cast<RM::Flag>(b)); out += '\'';
        }
    }

    return out;
}

std::string fq_flag_list(std::uint16_t bits) {
    std::string out;

    for (std::uint16_t b = 1; b; b = std::uint16_t(b << 1)) {
        if (bits & b) {
            if (!out.empty()) out += ", ";
            out += '\''; out += FQ::flag_name(static_cast<FQ::Flag>(b)); out += '\'';
        }
    } 

    return out;
}

void check_modifier_conflicts(const RM& m, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    for (const auto& g : kModExclusive) {
        if (count_bits(m.flags() & g.mask) > 1) {
            SemanticError::conflicting_modifiers(
                r, 
                site->file_id, site->line, 
                kind, 
                mod_flag_list(m.flags() & g.mask)
            );
        }
    }

    for (const auto& rq : kModRequires) {                                 
        if ((m.flags() & rq.flag) && (m.flags() & rq.required) == 0) {
            SemanticError::missing_required_modifier(
                r, 
                site->file_id, site->line, 
                kind,
                mod_flag_list(rq.flag), mod_flag_list(rq.required),
                std::size_t(count_bits(rq.required))
            );
        }
    }
}

void check_qualifier_conflicts(const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    if (q.has(FQ::Mutable)) SemanticError::modifier_rule_violation(r, site->file_id, site->line, kind, "only lambdas can be 'mutable'");

    for (const auto& g : kFuncExclusive) {
        if (count_bits(q.flags() & g.mask) > 1) { 
            SemanticError::conflicting_modifiers(
                r, 
                site->file_id, site->line, 
                kind, 
                fq_flag_list(q.flags() & g.mask)
            );
        }
    }

    for (const auto& rq : kFuncRequires) {                                 
        if ((q.flags() & rq.flag) && (q.flags() & rq.required) == 0) {
            SemanticError::missing_required_modifier(
                r, 
                site->file_id, site->line, kind,
                fq_flag_list(rq.flag), fq_flag_list(rq.required),
                std::size_t(count_bits(rq.required))
            );
        }
    }
}

void check_cross_conflicts(const RM& m, const FQ& q, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    for (const auto& c : kCrossExclusive) {
        if ((m.flags() & c.mod_flag) && (q.flags() & c.qual_flag)) {
            SemanticError::modifier_rule_violation(
                r, 
                site->file_id, site->line, 
                kind, 
                c.why
            );
        }
    }
}

void check_declaration_modifiers(const RM& m, std::uint16_t valid, ErrorReporter& r, const nodes::ASTNode* site, std::string_view kind) {
    if (std::uint16_t bad = std::uint16_t(m.flags() & ~valid)) SemanticError::invalid_modifiers(r, site->file_id, site->line, kind, mod_flag_list(bad), std::size_t(count_bits(bad)));
    check_modifier_conflicts(m, r, site, kind);
}

} // namespace semantics
} // namespace walnut
