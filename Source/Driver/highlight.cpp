#include "Driver/highlight.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Semantics/const_evaluator.hpp"
#include "Semantics/scope.hpp"
#include "Semantics/symbol.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace driver {

namespace {

namespace N = nodes;
namespace S = semantics;
using K = N::ASTNode::Kind;

enum Modifier : std::uint32_t {
    Declaration       = 1u << 0,
    Overloaded        = 1u << 1,
    TemplateQualifier = 1u << 2
};

constexpr std::string_view modifier_names[] = { "declaration", "overloaded", "templateQualifier" };

const S::Symbol* follow(const S::Symbol* s) {
    for (int i = 0; s && i < 16; ++i) {
        if (s->kind == S::SymbolKind::Import && s->import_target) { s = s->import_target; continue; }
        break;
    }
    return s;
}

bool in_record(const S::Symbol* s) {
    return s && s->owner && s->owner->kind == S::Scope::Kind::Record;
}

std::string_view kind_of(const S::Symbol* s) {
    s = follow(s);
    if (!s) return {};

    switch (s->kind) {
        case S::SymbolKind::Variable:      return in_record(s) ? "property" : "variable";
        case S::SymbolKind::Parameter:     return "parameter";
        case S::SymbolKind::Function:
            if (s->decl && s->decl->kind == K::OperatorFunctionDeclaration) return "operator";
            return in_record(s) ? "method" : "function";
        case S::SymbolKind::Type:
            if (s->decl && s->decl->kind == K::RecordDeclaration && static_cast<const N::RecordDeclaration*>(s->decl)->m_form != N::RecordDeclaration::Form::Class) return "struct";
            return "class";
        case S::SymbolKind::TypeAlias:     return "type";
        case S::SymbolKind::Enum:          return "enum";
        case S::SymbolKind::EnumConstant:  return "enumMember";
        case S::SymbolKind::Namespace:
        case S::SymbolKind::Module:        return "namespace";
        case S::SymbolKind::Concept:       return "concept";
        case S::SymbolKind::TemplateParam: return "typeParameter";
        case S::SymbolKind::Import:        return "namespace";
    }

    return {};
}

bool word_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

std::string json_string(std::string_view s) {
    std::string out = "\"";

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }

    return out + "\"";
}

std::string type_text(S::Type* t) {
    return t ? S::type_str_of(t) : std::string();
}

std::string type_text(const parser_types::TypeInfo& t, S::TypeContext* types = nullptr) {
    if (t.canonical) return S::type_str_of(t.canonical);
    if (!t.has_type()) return std::string();

    if (types) {
        S::Type* c = types->canonicalize(t);
        const std::string text = type_text(c);
        if (c && text.find("<error") == std::string::npos) return text;
    }

    std::ostringstream os;
    t.write_to(os);
    return os.str();
}

std::string string_literal(std::string_view s) {
    std::string out = "\"";

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '$':  out += "\\$";  break;
            default:   out += c;
        }
    }

    return out + "\"";
}

unsigned float_bits(S::Type* t) {
    if (!t || !t->is_builtin()) return 0;
    const auto* b = static_cast<S::BuiltinType*>(t);
    if (b->base() != parser_types::PrimitiveType::BaseKind::Float) return 0;
    return bit_width_of_rank(b->width());
}

std::string float_text(const WideFloat& f, unsigned bits) {
    const unsigned binary = bits ? float_digits_of_bits(bits) : 0;
    const long long most = binary ? static_cast<long long>(binary * 0.30103) + 3 : 17;

    for (long long digits = 1; digits < most; ++digits) {
        const std::string text = f.to_string(digits);
        bool ok = false;
        const WideFloat back = wf_parse(text, ok);
        if (!ok) continue;
        WideFloat rounded = back;
        if (bits && !wf_round_to_width(back, bits, rounded)) rounded = back;
        if (wf_same_bits(rounded, f)) return text;
    }

    return f.to_string(most);
}

std::string value_text(const S::ConstValue& v, S::Type* t) {
    switch (v.kind) {
        case S::ConstValue::Kind::Int: {
            if (!v.i) return std::string();
            std::string text = v.i->to_string();
            const std::string ts = type_text(t);

            if ((ts == "char" || ts == "const char") && !v.i->is_negative() && S::ConstTable::fits(*v.i, 7, false)) {
                const long long code = std::stoll(text);
                if (code >= 0x20 && code < 0x7F && code != '\'' && code != '\\') return "'" + std::string(1, static_cast<char>(code)) + "' (" + text + ")";
            }

            return text;
        }
        case S::ConstValue::Kind::Float:   return v.f ? float_text(*v.f, float_bits(t)) : std::string();
        case S::ConstValue::Kind::Bool:    return v.b ? "true" : "false";
        case S::ConstValue::Kind::Special: return S::special_spelling(v.sp);
        case S::ConstValue::Kind::String:  return v.s ? string_literal(*v.s) : std::string();
        case S::ConstValue::Kind::None:    break;
    }

    return std::string();
}

std::string joined(const std::vector<std::string_view>& parts) {
    std::string out;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += "::";
        out += parts[i];
    }

    return out;
}

std::string without_const(std::string t) {
    if (t.rfind("const ", 0) == 0) t.erase(0, 6);
    return t;
}

std::string prefix(const modifiers::RawModifiers& m) {
    using RM = modifiers::RawModifiers;
    std::string out;
    if (m.has(RM::Static))    out += "static ";
    if (m.has(RM::Inline))    out += "inline ";
    if (m.has(RM::Lazy))      out += "lazy ";
    if (m.has(RM::Constexpr)) out += "constexpr ";
    return out;
}

std::string signature(const N::FunctionDeclaration* f, S::TypeContext* types = nullptr) {
    if (f->variable_template) {
        std::string t = type_text(f->m_return_type, types);
        if (f->m_modifiers.has(modifiers::RawModifiers::Constexpr)) t = without_const(t);
        return prefix(f->m_modifiers) + (t.empty() ? std::string() : t + ": ") + joined(f->m_name_parts);
    }

    std::string out = prefix(f->m_modifiers) + "function " + joined(f->m_name_parts) + "(";

    if (f->m_parameters) {
        bool first = true;

        for (const N::FunctionParameter* p : f->m_parameters->m_params) {
            if (!p) continue;
            if (!first) out += ", ";
            first = false;
            out += type_text(p->m_type, types);

            if (p->is_variadic()) {
                out += "...";
                if (p->m_variadic_length != static_cast<std::size_t>(-1) && p->m_variadic_length > 1) out += "[" + std::to_string(p->m_variadic_length) + "]";
            }

            if (!p->m_name.empty()) out += ": " + std::string(p->m_name);
            if (p->m_initializer) out += " = ...";
        }
    }

    out += ")";
    const std::string ret = type_text(f->m_return_type, types);
    if (!ret.empty()) out += " -> " + ret;
    return out;
}

class Collector {
public:
    Collector(const char* begin, std::size_t size, S::ConstEvaluator* eval = nullptr, S::TypeContext* types = nullptr) : m_begin(begin), m_end(begin + size), m_eval(eval), m_types(types) {
        m_line_starts.push_back(0);
        for (std::size_t i = 0; i < size; ++i) if (begin[i] == '\n') m_line_starts.push_back(i + 1);
    }

    bool inside(const char* p, std::size_t n = 1) const { return p && p >= m_begin && p + n <= m_end; }

    void walk_instantiation(N::ASTNode* n) {
        m_in_instantiation = true;
        walk(n);
        m_in_instantiation = false;
    }

    void walk(N::ASTNode* n) {
        if (!n || !m_seen.insert(n).second) return;

        switch (n->kind) {
            case K::Identifier: {
                auto* id = static_cast<N::Identifier*>(n);

                if (id->name != "self" && id->name != "this") {
                    use(id->name, id->resolved);
                    describe(id->name, id->resolved, n, 1);
                } else {
                    expression_hover(id->name, n, 1);
                }
                break;
            }

            case K::QualifiedIdentifier: {
                auto* q = static_cast<N::QualifiedIdentifier*>(n);
                if (q->parts().empty()) break;
                use(q->parts().back(), q->resolved);
                describe(q->parts().back(), q->resolved, n, 1);
                qualifiers(q->parts(), q->parts().size() - 1, q->resolved);
                break;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<N::BinaryExpression*>(n);
                if (b->resolved) op(b->at, b->op);
                expression_hover(op_spelling(b->at, b->op), n, 1);
                break;
            }

            case K::UnaryExpression: {
                auto* u = static_cast<N::UnaryExpression*>(n);
                if (u->resolved) op(u->at, u->op);
                expression_hover(op_spelling(u->at, u->op), n, 1);
                break;
            }

            case K::TernaryExpression:
                expression_hover(op_text_at(n->at, "?"), n, 1);
                break;

            case K::TypeQuery: {
                auto* t = static_cast<N::TypeQueryExpression*>(n);
                const std::string_view word = word_at(n->at);
                std::string denoted;

                if ((t->get_op() == N::TypeQueryExpression::Op::Typeof || t->get_op() == N::TypeQueryExpression::Op::Decltype) && t->get_operand()) {
                    const parser_types::TemplateArgument* a = t->get_operand();
                    if (a->is_type()) denoted = type_text(a->type, m_types);
                    else if (a->value) denoted = type_text(a->value->expr_type.type);
                }

                if (!denoted.empty()) hover(word, "", denoted, "", "", 1);
                else expression_hover(word, n, 1);
                if (t->get_operand() && t->get_operand()->is_type()) type(t->get_operand()->type);
                break;
            }

            case K::DereferenceExpression:
                if (static_cast<N::DereferenceExpression*>(n)->resolved) op_text(n->at, "*");
                break;

            case K::BitwiseNotExpression:
                if (static_cast<N::BitwiseNotExpression*>(n)->resolved) op_text(n->at, "~");
                break;

            case K::SubscriptExpression:
                if (static_cast<N::SubscriptExpression*>(n)->resolved) op_text(n->at, "[");
                break;

            case K::MemberAccessExpression: {
                auto* m = static_cast<N::MemberAccessExpression*>(n);
                use(m->m_member, m->resolved);
                describe(m->m_member, m->resolved, n, 1);
                if (m->m_op == N::MemberAccessExpression::Op::Arrow && !m->arrow_ops.empty()) op_text(n->at, "->");

                if (m->m_op == N::MemberAccessExpression::Op::Scope && m->m_object && m->m_object->kind == K::TemplateInstantiation) {
                    const N::ASTNode* t = static_cast<N::TemplateInstantiation*>(m->m_object)->m_template;
                    std::string_view name;
                    if (t && t->kind == K::Identifier) name = static_cast<const N::Identifier*>(t)->name;
                    else if (t && t->kind == K::QualifiedIdentifier) name = static_cast<const N::QualifiedIdentifier*>(t)->simple_name();
                    if (inside(name.data(), name.size())) m_template_qualifiers.insert(offset(name.data()));
                }
                break;
            }

            case K::CallExpression: {
                auto* c = static_cast<N::CallExpression*>(n);
                if (c->resolved && c->m_callee && c->m_callee->kind == K::Identifier) {
                    auto* id = static_cast<N::Identifier*>(c->m_callee);
                    if (kind_of(c->resolved) != "operator") use(id->name, c->resolved, 2);
                }

                const std::string_view callee = callee_name(c->m_callee);

                if (!callee.empty()) {
                    if (c->resolved) describe(callee, c->resolved, nullptr, 2);
                    hover(callee, "", type_text(n->expr_type.type), "", evaluate(n), 2);
                }

                targs(c->m_template_args);
                break;
            }

            case K::TemplateInstantiation: {
                auto* t = static_cast<N::TemplateInstantiation*>(n);
                targs(t->m_args);
                const std::string value = evaluate(n);
                if (!value.empty()) hover(callee_name(t->m_template), "", type_text(n->expr_type.type), "", value, 2);
                break;
            }

            case K::CastExpression:
                type(static_cast<N::CastExpression*>(n)->target);
                break;

            case K::NewExpression:
                type(static_cast<N::NewExpression*>(n)->type);
                break;

            case K::VariableDeclaration: {
                auto* v = static_cast<N::VariableDeclaration*>(n);
                type(v->m_type_info);
                declare(v->m_name, v->symbol, "variable");

                if (v->symbol) {
                    describe(v->m_name, v->symbol, nullptr, 3);
                } else {
                    std::string t = type_text(v->m_type_info, m_types);
                    if ((t.empty() || t == "auto") && v->m_initializer) t = type_text(v->m_initializer->expr_type.type);
                    hover(v->m_name, t.empty() ? "" : t + ": " + std::string(v->m_name), "", "", "", 3);
                }
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<N::ArrayDeclaration*>(n);
                type(a->m_element_type);
                declare(a->m_name, a->symbol, "variable");
                if (a->symbol) describe(a->m_name, a->symbol, nullptr, 3);
                break;
            }

            case K::FunctionParameter: {
                auto* p = static_cast<N::FunctionParameter*>(n);
                type(p->m_type);
                declare(p->m_name, nullptr, "parameter");
                const std::string t = type_text(p->m_type, m_types);
                if (!t.empty()) hover(p->m_name, t + (p->is_variadic() ? "...: " : ": ") + std::string(p->m_name), "", "", "", 3);
                break;
            }

            case K::FunctionDeclaration: {
                auto* f = static_cast<N::FunctionDeclaration*>(n);
                type(f->m_return_type);

                if (!f->m_name_parts.empty()) {
                    declare(f->m_name_parts.back(), f->symbol, "function");
                    std::string value;

                    if (!m_in_instantiation && m_eval && f->symbol && (!f->m_parameters || f->m_parameters->empty()) && f->m_modifiers.has(modifiers::RawModifiers::Constexpr)) {
                        value = value_text(m_eval->eval_value_function(f->symbol), f->m_return_type.canonical);
                    }

                    hover(f->m_name_parts.back(), signature(f, m_types), "", "", value, 3);
                }
                break;
            }

            case K::OperatorFunctionDeclaration: {
                auto* f = static_cast<N::OperatorFunctionDeclaration*>(n);
                type(f->m_return_type);
                type(f->conversion_type);
                break;
            }

            case K::LambdaExpression: {
                auto* l = static_cast<N::LambdaExpression*>(n);
                type(l->m_return_type);
                for (N::LambdaExpression* s : l->specializations) walk(s);
                break;
            }

            case K::RecordDeclaration: {
                auto* r = static_cast<N::RecordDeclaration*>(n);
                if (!r->m_name_parts.empty()) {
                    declare(r->m_name_parts.back(), r->symbol, r->m_form == N::RecordDeclaration::Form::Class ? "class" : "struct");
                    describe(r->m_name_parts.back(), r->symbol, nullptr, 3);
                }
                type(r->m_inherits);
                for (const auto& b : r->m_extra_bases) type(b);
                targs(r->m_spec_args);
                break;
            }

            case K::EnumDeclaration: {
                auto* e = static_cast<N::EnumDeclaration*>(n);
                declare(e->name, e->symbol, "enum");
                describe(e->name, e->symbol, nullptr, 3);
                type(e->underlying_type);
                break;
            }

            case K::EnumValue:
                declare(static_cast<N::EnumValue*>(n)->name, nullptr, "enumMember");
                break;

            case K::NamespaceDeclaration:
                for (std::string_view part : static_cast<N::NamespaceDeclaration*>(n)->m_name_parts) declare(part, nullptr, "namespace");
                break;

            case K::TemplateParameter: {
                auto* t = static_cast<N::TemplateParameter*>(n);
                declare(t->m_name, nullptr, "typeParameter");
                type(t->m_type);
                type(t->m_default_type);
                break;
            }

            case K::ForEachStatement: {
                auto* f = static_cast<N::ForEachStatement*>(n);
                type(f->m_element_type);
                declare(f->m_var_name, nullptr, "variable");
                if (const std::string t = type_text(f->m_element_type, m_types); !t.empty()) hover(f->m_var_name, t + ": " + std::string(f->m_var_name), "", "", "", 3);
                break;
            }

            case K::TryCatchStatement:
                for (auto* h = static_cast<N::TryCatchStatement*>(n); h; h = h->next_handler) {
                    type(h->catch_type);
                    declare(h->catch_name, nullptr, "variable");
                }
                break;

            case K::UsingDeclaration: {
                auto* u = static_cast<N::UsingDeclaration*>(n);
                if (u->variant == N::UsingDeclaration::Variant::Alias || u->variant == N::UsingDeclaration::Variant::Typedef) declare(u->name, nullptr, "type");
                if (u->variant == N::UsingDeclaration::Variant::NamespaceAlias) declare(u->name, nullptr, "namespace");
                type(u->aliased_type);
                break;
            }

            case K::ConceptDeclaration:
                declare(static_cast<N::ConceptDeclaration*>(n)->name, nullptr, "concept");
                break;

            default:
                break;
        }

        N::for_each_child<true>(n, [this](N::ASTNode* c) { walk(c); });
    }

    std::string json(const ErrorReporter& errors, const WarningReporter& warnings, FileId main, const SourceManager& sources, bool semantic, std::size_t error_limit = static_cast<std::size_t>(-1), std::size_t warning_limit = static_cast<std::size_t>(-1)) const {
        std::ostringstream out;
        out << "{\"semantic\":" << (semantic ? "true" : "false") << ",\"tokens\":[";
        bool first = true;

        for (const auto& [at, e] : m_tokens) {
            std::uint32_t mods = e.mods;
            if (m_template_qualifiers.count(at)) mods |= TemplateQualifier;
            if (!first) out << ",";
            first = false;
            const Position p = position(at, e.len);
            out << "[" << p.line << "," << p.col << "," << p.len << "," << json_string(e.kind) << ",[";
            bool mfirst = true;

            for (std::size_t i = 0; i < sizeof(modifier_names) / sizeof(modifier_names[0]); ++i) {
                if (!(mods & (1u << i))) continue;
                if (!mfirst) out << ",";
                mfirst = false;
                out << json_string(modifier_names[i]);
            }

            out << "]]";
        }

        out << "],\"diagnostics\":[";
        first = true;

        const auto diagnostic = [&](const char* severity, FileId file, std::size_t line, const std::string& message, const char* at, std::size_t width) {
            if (!first) out << ",";
            first = false;
            std::string text = message;
            std::uint32_t l = line > 0 ? static_cast<std::uint32_t>(line - 1) : 0;
            std::uint32_t c = 0;
            std::uint32_t n = 0;

            if (inside(at)) {
                const Position p = position(offset(at), width ? width : 1);
                l = p.line; c = p.col; n = p.len;
            } else if (file != main && file != INVALID_FILE && file < sources.file_count()) {
                text = sources.absolute_path(file) + ":" + std::to_string(line) + ": " + message;
                l = 0;
            }

            out << "{\"severity\":\"" << severity << "\",\"line\":" << l << ",\"col\":" << c << ",\"len\":" << n << ",\"message\":" << json_string(text) << "}";
        };

        const auto& error_list = errors.errors();
        const auto& warning_list = warnings.warnings();
        const std::size_t error_count = std::min<std::size_t>(error_list.size(), error_limit);
        const std::size_t warning_count = std::min<std::size_t>(warning_list.size(), warning_limit);

        for (std::size_t i = 0; i < error_count; ++i) {
            const CompilerError& e = error_list[i];
            diagnostic("error", e.file_id, e.line, e.message, e.at, e.width);
        }

        for (std::size_t i = 0; i < warning_count; ++i) {
            const CompilerWarning& w = warning_list[i];
            diagnostic("warning", w.file_id, w.line, w.message, nullptr, 0);
        }
        out << "],\"hovers\":[";
        first = true;

        const auto list = [&out](const char* key, const std::vector<std::string>& items) {
            if (items.empty()) return;
            out << ",\"" << key << "\":[";

            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out << ",";
                out << json_string(items[i]);
            }

            out << "]";
        };

        for (const auto& [at, h] : m_hovers) {
            if (h.code.empty() && h.types.empty() && h.values.empty() && h.results.empty()) continue;
            if (!first) out << ",";
            first = false;
            const Position p = position(at, h.len);
            out << "{\"line\":" << p.line << ",\"col\":" << p.col << ",\"len\":" << p.len;
            if (!h.code.empty()) out << ",\"code\":" << json_string(h.code);
            list("types", h.types);
            list("values", h.values);
            list("results", h.results);
            out << "}";
        }

        out << "]}\n";
        return out.str();
    }

private:
    struct Entry {
        std::uint32_t    len  = 0;
        std::string_view kind;
        std::uint32_t    mods = 0;
        int              rank = 0;
    };

    struct Position {
        std::uint32_t line = 0;
        std::uint32_t col  = 0;
        std::uint32_t len  = 0;
    };

    const char*                          m_begin;
    const char*                          m_end;
    std::vector<std::size_t>             m_line_starts;
    std::map<std::size_t, Entry>         m_tokens;
    std::set<std::size_t>                m_template_qualifiers;
    std::unordered_set<const N::ASTNode*> m_seen;
    S::ConstEvaluator*                   m_eval = nullptr;
    S::TypeContext*                      m_types = nullptr;
    bool                                 m_in_instantiation = false;

    struct Hover {
        std::uint32_t            len  = 0;
        int                      rank = 0;
        std::string              code;
        std::vector<std::string> types;
        std::vector<std::string> values;
        std::vector<std::string> results;
    };

    std::map<std::size_t, Hover> m_hovers;

    static constexpr std::size_t max_alternatives = 6;

    static void remember(std::vector<std::string>& list, const std::string& text) {
        if (text.empty() || list.size() >= max_alternatives) return;
        if (std::find(list.begin(), list.end(), text) == list.end()) list.push_back(text);
    }

    void hover(std::string_view at, const std::string& code, const std::string& type, const std::string& value, const std::string& result, int rank) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        if (code.empty() && type.empty() && value.empty() && result.empty()) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());

        if (!code.empty() && (h.code.empty() || rank > h.rank)) {
            h.code = code;
            h.rank = rank;
        }

        remember(h.types, type);
        remember(h.values, value);
        remember(h.results, result);
    }

    std::string evaluate(N::ASTNode* e) {
        if (!m_eval || !e) return std::string();
        S::Type* t = e->expr_type.type;
        if (t && t->is_dependent()) return std::string();
        const S::ConstValue v = m_eval->eval(e);
        if (!v.ok()) return std::string();
        return value_text(v, t);
    }

    void expression_hover(std::string_view at, N::ASTNode* e, int rank) {
        if (at.empty() || !e) return;
        hover(at, "", type_text(e->expr_type.type), evaluate(e), "", rank);
    }

    static std::string qualified_name(const S::Symbol* s) {
        if (!s) return std::string();
        std::vector<std::string_view> parts{ s->name };
        const S::Scope* sc = s->owner;

        for (int guard = 0; sc && guard < 32; ++guard, sc = sc->parent) {
            const S::Symbol* o = sc->owner_symbol;
            if (!o || o == s || o->name.empty()) continue;
            if (o->kind != S::SymbolKind::Namespace && o->kind != S::SymbolKind::Module && o->kind != S::SymbolKind::Type && o->kind != S::SymbolKind::Enum) continue;
            parts.insert(parts.begin(), o->name);
        }

        return joined(parts);
    }

    std::string symbol_type(const S::Symbol* s, const N::ASTNode* use) const {
        std::string t;
        if (s->type) t = type_text(*s->type, m_types);

        if (t.empty() || t == "auto" || t.find("typeof(") != std::string::npos || t.find("decltype(") != std::string::npos) {
            if (use && use->expr_type.type) t = type_text(use->expr_type.type);
            else if (s->decl && s->decl->kind == K::VariableDeclaration) {
                const auto* v = static_cast<const N::VariableDeclaration*>(s->decl);
                if (v->m_initializer && v->m_initializer->expr_type.type) t = type_text(v->m_initializer->expr_type.type);
            }
        }

        return t;
    }

    void describe(std::string_view at, const S::Symbol* original, N::ASTNode* use, int rank) {
        const S::Symbol* s = follow(original);
        if (!s || at.empty()) return;
        std::string code;
        std::string type;
        std::string value;

        switch (s->kind) {
            case S::SymbolKind::Variable: {
                std::string mods;
                if (s->decl && s->decl->kind == K::VariableDeclaration) mods = prefix(static_cast<const N::VariableDeclaration*>(s->decl)->get_type_info().modifiers);
                const std::string t = symbol_type(s, use);
                const std::string shown = mods.find("constexpr") != std::string::npos ? without_const(t) : t;
                code = mods + (shown.empty() ? std::string() : shown + ": ") + qualified_name(s);
                if (m_eval) value = value_text(m_eval->value_of(const_cast<S::Symbol*>(s)), s->type ? s->type->canonical : nullptr);
                break;
            }

            case S::SymbolKind::Parameter: {
                const std::string t = symbol_type(s, use);
                code = (t.empty() ? std::string() : t + ": ") + std::string(s->name);
                break;
            }

            case S::SymbolKind::Function:
                if (s->decl && s->decl->kind == K::FunctionDeclaration) code = signature(static_cast<const N::FunctionDeclaration*>(s->decl), m_types);
                break;

            case S::SymbolKind::Type:
                if (s->decl && s->decl->kind == K::RecordDeclaration && static_cast<const N::RecordDeclaration*>(s->decl)->m_form != N::RecordDeclaration::Form::Class) code = "struct " + qualified_name(s);
                else code = "class " + qualified_name(s);
                break;

            case S::SymbolKind::TypeAlias: {
                code = "using " + qualified_name(s);
                const std::string t = s->type ? type_text(*s->type, m_types) : std::string();
                if (!t.empty()) code += " = " + t;
                break;
            }

            case S::SymbolKind::Enum:          code = "enum " + qualified_name(s); break;
            case S::SymbolKind::EnumConstant:
                code = qualified_name(s);
                if (m_eval) value = value_text(m_eval->value_of(const_cast<S::Symbol*>(s)), nullptr);
                break;
            case S::SymbolKind::Namespace:
            case S::SymbolKind::Module:        code = "namespace " + qualified_name(s); break;
            case S::SymbolKind::Concept:       code = "concept " + qualified_name(s); break;
            case S::SymbolKind::TemplateParam: code = "template parameter " + std::string(s->name); break;
            case S::SymbolKind::Import:        break;
        }

        if (use && (s->kind == S::SymbolKind::Variable || s->kind == S::SymbolKind::Parameter || s->kind == S::SymbolKind::TemplateParam)) {
            if (value.empty()) value = evaluate(use);
            if (s->kind == S::SymbolKind::TemplateParam || m_in_instantiation) type = type_text(use->expr_type.type);
        }

        hover(at, code, type, value, "", rank);
    }

    std::string_view word_at(const char* at) const {
        if (!inside(at) || !word_start(*at)) return {};
        const char* e = at;
        while (e < m_end && (word_start(*e) || (*e >= '0' && *e <= '9'))) ++e;
        return std::string_view(at, static_cast<std::size_t>(e - at));
    }

    std::string_view op_text_at(const char* at, std::string_view text) const {
        if (!inside(at, text.size()) || std::string_view(at, text.size()) != text) return {};
        return std::string_view(at, text.size());
    }

    std::string_view callee_name(N::ASTNode* callee) const {
        if (!callee) return {};
        if (callee->kind == K::Identifier) return static_cast<N::Identifier*>(callee)->name;
        if (callee->kind == K::QualifiedIdentifier) {
            auto* q = static_cast<N::QualifiedIdentifier*>(callee);
            return q->parts().empty() ? std::string_view() : q->parts().back();
        }
        if (callee->kind == K::MemberAccessExpression) return static_cast<N::MemberAccessExpression*>(callee)->m_member;
        if (callee->kind == K::TemplateInstantiation) return callee_name(static_cast<N::TemplateInstantiation*>(callee)->m_template);
        return {};
    }

    std::size_t offset(const char* p) const { return static_cast<std::size_t>(p - m_begin); }

    static std::uint32_t utf16_units(const char* p, std::size_t n) {
        std::uint32_t units = 0;

        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(p[i]);
            if ((c & 0xC0) == 0x80) continue;
            units += (c >= 0xF0) ? 2 : 1;
        }

        return units;
    }

    Position position(std::size_t at, std::size_t len) const {
        const auto it = std::upper_bound(m_line_starts.begin(), m_line_starts.end(), at);
        const std::size_t line = static_cast<std::size_t>(it - m_line_starts.begin()) - 1;
        const std::size_t start = m_line_starts[line];
        const std::size_t avail = static_cast<std::size_t>(m_end - (m_begin + at));
        Position p;
        p.line = static_cast<std::uint32_t>(line);
        p.col  = utf16_units(m_begin + start, at - start);
        p.len  = utf16_units(m_begin + at, std::min(len, avail));
        return p;
    }

    void add(std::string_view name, std::string_view kind, std::uint32_t mods, int rank) {
        if (name.empty() || kind.empty() || !inside(name.data(), name.size()) || !word_start(name.front())) return;
        const std::size_t at = offset(name.data());
        auto it = m_tokens.find(at);

        if (it == m_tokens.end()) {
            m_tokens.emplace(at, Entry{ static_cast<std::uint32_t>(name.size()), kind, mods, rank });
            return;
        }

        if (rank > it->second.rank) {
            it->second.kind = kind;
            it->second.mods = mods | (it->second.mods & Overloaded);
            it->second.rank = rank;
            it->second.len  = static_cast<std::uint32_t>(name.size());
        }
    }

    void use(std::string_view name, const S::Symbol* s, int rank = 1) {
        if (!s) return;
        add(name, kind_of(s), 0, rank);
    }

    void declare(std::string_view name, const S::Symbol* s, std::string_view fallback) {
        std::string_view kind = s ? kind_of(s) : std::string_view();
        if (kind.empty()) kind = fallback;
        add(name, kind, Declaration, 3);
    }

    void op_text(const char* at, std::string_view text) {
        if (!inside(at, text.size()) || std::string_view(at, text.size()) != text) return;
        const std::size_t o = offset(at);
        auto it = m_tokens.find(o);
        if (it == m_tokens.end()) m_tokens.emplace(o, Entry{ static_cast<std::uint32_t>(text.size()), "operator", Overloaded, 1 });
        else it->second.mods |= Overloaded;
    }

    std::string_view op_spelling(const char* at, tokenizing::Token::Kind kind) const {
        if (!inside(at)) return {};
        const tokenizing::Token t(kind, 0);
        std::string_view best;

        for (std::size_t i = 0; i < t.spelling_count(); ++i) {
            const std::string_view s = t.spellings()[i];
            if (s.size() > best.size() && inside(at, s.size()) && std::string_view(at, s.size()) == s) best = s;
        }

        if (best.empty()) {
            static constexpr std::string_view symbols[] = {
                "<<=", ">>=", "<=>", "**=", "->", "&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "**", "+", "-", "*", "/", "%", "<", ">", "=", "!", "&", "|", "^", "~"
            };

            for (const std::string_view s : symbols) {
                if (inside(at, s.size()) && std::string_view(at, s.size()) == s) { best = s; break; }
            }
        }

        if (best.empty()) best = word_at(at);
        return best.empty() ? std::string_view() : std::string_view(at, best.size());
    }

    void op(const char* at, tokenizing::Token::Kind kind) {
        const std::string_view best = op_spelling(at, kind);
        if (!best.empty()) op_text(at, best);
    }

    void qualifiers(const std::vector<std::string_view>& parts, std::size_t count, const S::Symbol* last) {
        last = follow(last);
        const S::Scope* sc = last ? last->owner : nullptr;

        for (std::size_t i = count; i-- > 0;) {
            const S::Symbol* o = nullptr;

            for (int guard = 0; sc && guard < 8; ++guard) {
                o = sc->owner_symbol;
                if (o && o->name == parts[i]) break;
                o = nullptr;
                if (sc->kind != S::Scope::Kind::Template) break;
                sc = sc->parent;
            }

            if (!o) return;
            add(parts[i], kind_of(o), 0, 1);
            sc = o->owner;
        }
    }

    void targs(const std::vector<parser_types::TemplateArgument*>& args) {
        for (parser_types::TemplateArgument* a : args) {
            if (!a) continue;
            if (a->is_type()) type(a->type);
            else walk(a->value);
        }
    }

    void type(const parser_types::TypeInfo& t) {
        if (t.type && t.type->is_user_defined()) {
            const auto* u = static_cast<const parser_types::UserDefinedType*>(t.type);
            const std::vector<std::string_view>& parts = u->parts();

            if (!parts.empty() && t.resolved) {
                use(parts.back(), t.resolved);
                describe(parts.back(), t.resolved, nullptr, 1);
                const S::Symbol* r = follow(t.resolved);

                if (t.canonical && r) {
                    const std::string full = type_text(t.canonical);
                    const bool alias_says_it = r->kind == S::SymbolKind::TypeAlias && r->type && type_text(*r->type, m_types) == full;
                    if (full != qualified_name(r) && full != r->name && !alias_says_it) hover(parts.back(), "", full, "", "", 1);
                }
            }
        }

        targs(t.template_args);
    }
};

} // namespace

std::string highlight_json(CompilerPipeline& compiler) {
    const std::optional<FileId> main = compiler.load_input();
    const SourceManager& sources = compiler.get_sources();
    const bool semantic = !compiler.had_front_end_errors();

    if (!main) {
        Collector empty("", 0);
        return empty.json(compiler.get_reporter(), compiler.get_warnings(), INVALID_FILE, sources, false);
    }

    const std::size_t error_limit = compiler.get_reporter().errors().size();
    const std::size_t warning_limit = compiler.get_warnings().warnings().size();
    S::Instantiator& instantiator = compiler.mutable_instantiator();
    const bool was_frozen = instantiator.frozen();
    instantiator.freeze();
    Collector c(sources.data(*main), sources.size(*main), semantic ? &instantiator.evaluator() : nullptr, semantic ? &compiler.mutable_types() : nullptr);

    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (u.file == *main && u.ast) c.walk(u.ast);
    }

    compiler.get_instantiator().for_each([&c](const S::Instantiator::Instantiation& I) {
        if (I.decl) c.walk_instantiation(I.decl);
    });

    instantiator.freeze(was_frozen);
    return c.json(compiler.get_reporter(), compiler.get_warnings(), *main, sources, semantic, error_limit, warning_limit);
}

} // namespace driver
} // namespace walnut
