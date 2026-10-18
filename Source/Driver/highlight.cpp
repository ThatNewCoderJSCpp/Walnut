#include "Driver/highlight.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Semantics/const_evaluator.hpp"
#include "Semantics/instantiator.hpp"
#include "Semantics/scope.hpp"
#include "Semantics/symbol.hpp"

#include <algorithm>
#include <cctype>
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
    TemplateQualifier = 1u << 2,
    VariableTemplate  = 1u << 3
};

constexpr std::string_view modifier_names[] = { "declaration", "overloaded", "templateQualifier", "variableTemplate" };

const S::Symbol* follow(const S::Symbol* s) {
    for (int i = 0; s && i < 16; ++i) {
        if (s->kind == S::SymbolKind::Import && s->import_target) { s = s->import_target; continue; }
        if (s->merged_into && s->merged_into != s) { s = s->merged_into; continue; }
        break;
    }
    return s;
}

bool is_variable_template(const S::Symbol* s) {
    s = follow(s);
    return s && s->kind == S::SymbolKind::Function && s->decl && s->decl->kind == K::FunctionDeclaration && static_cast<const N::FunctionDeclaration*>(s->decl)->variable_template;
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
            if (is_variable_template(s)) return in_record(s) ? "property" : "variable";
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

std::string iterable_text(const N::FunctionParameter* p, S::TypeContext* types);

std::string default_after(std::string_view name, bool angle) {
    if (name.empty() || !name.data()) return std::string();
    const char* p = name.data() + name.size();
    const char* limit = p + 400;
    while (p < limit && *p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= limit || *p != '=' || p[1] == '=') return std::string();
    ++p;
    while (p < limit && *p && (*p == ' ' || *p == '\t')) ++p;
    const char* start = p;
    int depth = 0;
    int angles = 0;
    char quote = 0;
    for (; p < limit && *p; ++p) {
        const char c = *p;
        if (quote) {
            if (c == '\\' && p[1]) ++p;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if ((c == ')' || c == ']' || c == '}') && depth > 0) { --depth; continue; }
        if (angle && c == '<') { ++angles; continue; }
        if (angle && c == '>' && angles > 0) { --angles; continue; }
        if (depth == 0 && angles == 0 && (c == ',' || c == ';' || c == ')' || c == ']' || c == '}' || c == '\n' || (angle && c == '>'))) break;
    }
    std::string out(start, static_cast<std::size_t>(p - start));
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r')) out.pop_back();
    if (out.size() > 80) out = out.substr(0, 77) + "...";
    return out;
}

std::string default_text(std::string_view name, bool angle) {
    const std::string text = default_after(name, angle);
    return text.empty() ? std::string("...") : text;
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
            out += p->is_iterable ? iterable_text(p, types) : type_text(p->m_type, types);

            if (p->is_variadic()) {
                out += "...";
                if (p->m_variadic_length != static_cast<std::size_t>(-1) && p->m_variadic_length > 1) out += "[" + std::to_string(p->m_variadic_length) + "]";
            }

            if (!p->m_name.empty()) out += ": " + std::string(p->m_name);
            if (p->m_initializer) out += " = " + default_text(p->m_name, false);
        }
    }

    out += ")";
    const std::string ret = type_text(f->m_return_type, types);
    if (!ret.empty()) out += " -> " + ret;
    return out;
}

std::string iterable_text(const N::FunctionParameter* p, S::TypeContext* types) {
    const std::string out = "iterable<" + type_text(p->iterable_element, types) + ">";
    const bool mutable_ref = p->m_type.has_reference() && !p->m_type.modifiers.has(modifiers::RawModifiers::Const);
    return mutable_ref ? out + "&" : out;
}

std::string params_text(const N::FunctionParameters* ps, S::TypeContext* types) {
    std::string out;
    if (!ps) return out;
    bool first = true;

    for (const N::FunctionParameter* p : ps->m_params) {
        if (!p) continue;
        if (!first) out += ", ";
        first = false;
        out += p->is_iterable ? iterable_text(p, types) : type_text(p->m_type, types);

        if (p->is_variadic()) {
            out += "...";
            if (p->m_variadic_length != static_cast<std::size_t>(-1) && p->m_variadic_length > 1) out += "[" + std::to_string(p->m_variadic_length) + "]";
        }

        if (!p->m_name.empty()) out += ": " + std::string(p->m_name);
        if (p->m_initializer) out += " = " + default_text(p->m_name, false);
    }

    return out;
}

std::string template_default(const N::TemplateParameter* p, S::TypeContext* types) {
    if (!p) return std::string();
    if (p->m_has_default_type) {
        const std::string t = type_text(p->m_default_type, types);
        if (!t.empty()) return " = " + t;
    }
    if (p->m_default_value || p->m_has_default_type) return " = " + default_text(p->m_name, true);
    return std::string();
}

std::string template_parameter_text(const N::TemplateParameter* p, S::TypeContext* types) {
    std::string out = p->is_type_param() ? std::string("typename") : type_text(p->m_type, types);
    if (p->m_is_pack) out += "...";
    if (!p->m_name.empty()) out += " " + std::string(p->m_name);
    return out + template_default(p, types);
}

std::string template_prefix(const S::Symbol* s, S::TypeContext* types) {
    if (!s || !s->template_decl) return std::string();
    std::string out = "template <";
    bool first = true;

    for (const N::TemplateParameter* p : s->template_decl->m_params) {
        if (!p || p->m_name.rfind("iterable<", 0) == 0) continue;
        if (!first) out += ", ";
        first = false;
        if (p->is_type_param()) out += "typename";
        else out += type_text(p->m_type, types);
        if (p->m_is_pack) out += "...";
        if (!p->m_name.empty()) out += " " + std::string(p->m_name);
        out += template_default(p, types);
    }

    if (first) return std::string();
    return out + "> ";
}

std::string operator_spelling(const N::OperatorFunctionDeclaration* o, S::TypeContext* types) {
    if (o->is_conversion()) return type_text(o->conversion_type, types);
    std::string out;

    if (o->at && std::string_view(o->at, 8) == "operator") {
        const char* p = o->at + 8;
        while (*p == ' ' || *p == '\t') ++p;

        if (*p == '(') {
            int depth = 1;
            const char* start = ++p;

            for (; *p && *p != '\n'; ++p) {
                if (*p == '(') ++depth;
                if (*p == ')' && --depth == 0) break;
            }

            if (*p == ')') {
                for (const char* q = start; q < p; ++q) if (*q != ' ' && *q != '\t') out += *q;
                if (!out.empty()) return out;
            }
        }
    }

    for (tokenizing::Token::Kind k : o->op_tokens) {
        const tokenizing::Token t(k, 0);
        if (t.spelling_count()) out += std::string(t.spellings()[0]);
    }

    return out;
}

std::string record_name_of(const S::Symbol* s) {
    if (!s || !s->owner || !s->owner->owner_symbol) return std::string();
    return std::string(s->owner->owner_symbol->name);
}

std::string function_signature(const S::Symbol* s, S::TypeContext* types) {
    s = follow(s);
    if (!s || !s->decl) return std::string();

    if (s->decl->kind == K::FunctionDeclaration) return template_prefix(s, types) + signature(static_cast<const N::FunctionDeclaration*>(s->decl), types);

    if (s->decl->kind == K::ConstructorDeclaration) {
        const auto* c = static_cast<const N::ConstructorDeclaration*>(s->decl);
        const std::string owner = record_name_of(s);
        return template_prefix(s, types) + "constructor" + (owner.empty() ? std::string() : " " + owner) + "(" + params_text(c->m_parameters, types) + ")";
    }

    if (s->decl->kind == K::OperatorFunctionDeclaration) {
        const auto* o = static_cast<const N::OperatorFunctionDeclaration*>(s->decl);
        std::string out = template_prefix(s, types) + "operator(" + operator_spelling(o, types) + ") function(" + params_text(o->m_parameters, types) + ")";
        const std::string ret = type_text(o->m_return_type, types);
        if (!ret.empty() && !o->is_conversion()) out += " -> " + ret;
        return out;
    }

    return std::string();
}

std::string_view decl_name(const N::ASTNode* d) {
    if (!d) return {};

    switch (d->kind) {
        case K::FunctionDeclaration: {
            const auto* f = static_cast<const N::FunctionDeclaration*>(d);
            return f->m_name_parts.empty() ? std::string_view() : f->m_name_parts.back();
        }
        case K::VariableDeclaration:  return static_cast<const N::VariableDeclaration*>(d)->m_name;
        case K::ArrayDeclaration:     return static_cast<const N::ArrayDeclaration*>(d)->m_name;
        case K::FunctionParameter:    return static_cast<const N::FunctionParameter*>(d)->m_name;
        case K::RecordDeclaration: {
            const auto* r = static_cast<const N::RecordDeclaration*>(d);
            return r->m_name_parts.empty() ? std::string_view() : r->m_name_parts.back();
        }
        case K::EnumDeclaration:      return static_cast<const N::EnumDeclaration*>(d)->name;
        case K::EnumValue:            return static_cast<const N::EnumValue*>(d)->name;
        case K::NamespaceDeclaration: {
            const auto* n = static_cast<const N::NamespaceDeclaration*>(d);
            return n->m_name_parts.empty() ? std::string_view() : n->m_name_parts.back();
        }
        case K::TemplateParameter:    return static_cast<const N::TemplateParameter*>(d)->m_name;
        case K::UsingDeclaration:     return static_cast<const N::UsingDeclaration*>(d)->name;
        case K::ConceptDeclaration:   return static_cast<const N::ConceptDeclaration*>(d)->name;
        case K::ForEachStatement:     return static_cast<const N::ForEachStatement*>(d)->m_var_name;
        case K::TemplateDeclaration:  return decl_name(static_cast<const N::TemplateDeclaration*>(d)->m_declaration);
        default: break;
    }

    return {};
}

class Collector {
public:
    Collector(const char* begin, std::size_t size, S::ConstEvaluator* eval = nullptr, S::TypeContext* types = nullptr) : m_begin(begin), m_end(begin + size), m_eval(eval), m_types(types) {
        m_line_starts.push_back(0);
        for (std::size_t i = 0; i < size; ++i) if (begin[i] == '\n') m_line_starts.push_back(i + 1);
    }

    Collector(const char* begin, std::size_t size, S::ConstEvaluator* eval, S::TypeContext* types, const SourceManager* sources, FileId main, S::Instantiator* inst) : Collector(begin, size, eval, types) {
        m_sources = sources;
        m_main = main;
        m_inst = inst;
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
                    overload_hover(id->name, id->resolved, nullptr, 1);
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
                if (b->resolved) operator_hover(op_spelling(b->at, b->op), b->resolved, n);
                break;
            }

            case K::UnaryExpression: {
                auto* u = static_cast<N::UnaryExpression*>(n);
                if (u->resolved) op(u->at, u->op);
                expression_hover(op_spelling(u->at, u->op), n, 1);
                if (u->resolved) operator_hover(op_spelling(u->at, u->op), u->resolved, n);
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
                if (auto* r = static_cast<N::DereferenceExpression*>(n)->resolved) { op_text(n->at, "*"); operator_hover(op_text_at(n->at, "*"), r, n); }
                break;

            case K::BitwiseNotExpression:
                if (auto* r = static_cast<N::BitwiseNotExpression*>(n)->resolved) { op_text(n->at, "~"); operator_hover(op_text_at(n->at, "~"), r, n); }
                break;

            case K::SubscriptExpression:
                if (auto* r = static_cast<N::SubscriptExpression*>(n)->resolved) { op_text(n->at, "["); operator_hover(op_text_at(n->at, "["), r, n); }
                break;

            case K::MemberAccessExpression: {
                auto* m = static_cast<N::MemberAccessExpression*>(n);
                const S::Symbol* member = m->resolved ? m->resolved : scope_member(m);
                if (!member) member = dot_member(m);
                const std::string_view member_text = member_name_in_source(m);
                use(member_text, member);
                describe(member_text, member, m->resolved ? n : nullptr, 1);
                if (!member && depends_on_template(m->m_object)) add(member_text, m_called.count(m) ? "method" : "property", 0, 0);
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
                if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) m_called.insert(c->m_callee);
                if (c->resolved && c->m_callee && c->m_callee->kind == K::Identifier) {
                    auto* id = static_cast<N::Identifier*>(c->m_callee);
                    const bool constructor = follow(c->resolved) && follow(c->resolved)->decl && follow(c->resolved)->decl->kind == K::ConstructorDeclaration;
                    if (kind_of(c->resolved) != "operator" && !constructor) use(id->name, c->resolved, 2);
                }

                const std::string_view callee = callee_name(c->m_callee);

                if (!callee.empty()) {
                    const bool via_operator = c->resolved && kind_of(c->resolved) == "operator";
                    if (c->resolved && !via_operator) describe(callee, c->resolved, nullptr, 2);
                    hover(callee, "", type_text(n->expr_type.type), "", evaluate(n), 2);
                    if (c->resolved && !via_operator) overload_hover(callee, c->resolved, n, 2);
                    if (c->resolved && !via_operator) link(callee, c->resolved, 2);
                    if (!c->resolved) candidate_hover(callee, c);
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
                if (p->is_iterable) type(p->iterable_element);
                declare(p->m_name, nullptr, "parameter");
                const std::string t = p->is_iterable ? iterable_text(p, m_types) : type_text(p->m_type, m_types);
                if (!t.empty()) hover(p->m_name, t + (p->is_variadic() ? "...: " : ": ") + std::string(p->m_name) + (p->m_initializer ? " = " + default_text(p->m_name, false) : std::string()), "", "", "", 3);
                break;
            }

            case K::FunctionDeclaration: {
                auto* f = static_cast<N::FunctionDeclaration*>(n);
                type(f->m_return_type);

                if (!f->m_name_parts.empty()) {
                    declare(f->m_name_parts.back(), f->symbol, f->variable_template ? "variable" : "function", f->variable_template ? VariableTemplate : 0);
                    std::string value;

                    if (!m_in_instantiation && m_eval && f->symbol && (!f->m_parameters || f->m_parameters->empty()) && f->m_modifiers.has(modifiers::RawModifiers::Constexpr)) {
                        value = value_text(m_eval->eval_value_function(f->symbol), f->m_return_type.canonical);
                    }

                    hover(f->m_name_parts.back(), template_prefix(f->symbol, m_types) + signature(f, m_types), "", "", value, 3);
                    overload_hover(f->m_name_parts.back(), f->symbol, nullptr, 3);
                }
                break;
            }

            case K::OperatorFunctionDeclaration: {
                auto* f = static_cast<N::OperatorFunctionDeclaration*>(n);
                type(f->m_return_type);
                type(f->conversion_type);

                if (f->symbol) {
                    const std::string_view word = word_at(n->at);
                    hover(word, function_signature(f->symbol, m_types), "", "", "", 3);
                    overload_hover(word, f->symbol, nullptr, 3);
                    link(word, f->symbol);
                }
                break;
            }

            case K::ConstructorDeclaration: {
                auto* f = static_cast<N::ConstructorDeclaration*>(n);

                if (f->symbol) {
                    const std::string_view word = word_at(n->at);
                    hover(word, function_signature(f->symbol, m_types), "", "", "", 3);
                    overload_hover(word, f->symbol, nullptr, 3);
                    link(word, f->symbol);
                }
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
                hover(t->m_name, "(template parameter) " + template_parameter_text(t, m_types), "", "", "", 3);
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

            case K::TemplateDeclaration: {
                auto* t = static_cast<N::TemplateDeclaration*>(n);
                for (N::TemplateParameter* p : t->m_params) walk(p);
                walk(t->m_requires_clause);
                break;
            }

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
            if (h.overloads.size() > 1 || (h.guess && !h.overloads.empty())) list("overloads", h.overloads);
            if (h.guess) out << ",\"guess\":true";
            list("types", h.types);
            list("values", h.values);
            list("results", h.results);
            out << "}";
        }

        out << "],\"locations\":[";
        first = true;

        for (const Location& l : m_locations) {
            if (!first) out << ",";
            first = false;
            out << "[" << json_string(m_paths[l.file]) << "," << l.line << "," << l.col << "," << l.len << "]";
        }

        out << "],\"links\":[";
        first = true;

        for (const auto& [at, k] : m_links) {
            if (!first) out << ",";
            first = false;
            const Position p = position(at, k.len);
            out << "[" << p.line << "," << p.col << "," << p.len << "," << k.definition << ",[";

            for (std::size_t i = 0; i < k.declarations.size(); ++i) {
                if (i) out << ",";
                out << k.declarations[i];
            }

            out << "]]";
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
        int                      overload_rank = 0;
        std::vector<std::string> overloads;
        std::string              code;
        std::vector<std::string> types;
        std::vector<std::string> values;
        std::vector<std::string> results;
        bool                     guess = false;
    };

    std::map<std::size_t, Hover> m_hovers;

    struct Location {
        std::size_t   file = 0;
        std::uint32_t line = 0;
        std::uint32_t col  = 0;
        std::uint32_t len  = 0;
    };

    struct Link {
        std::uint32_t            len = 0;
        int                      rank = 0;
        long                     definition = -1;
        std::vector<long>        declarations;
    };

    const SourceManager*                         m_sources = nullptr;
    FileId                                       m_main = INVALID_FILE;
    S::Instantiator*                             m_inst = nullptr;
    std::vector<std::string>                     m_paths;
    std::vector<Location>                        m_locations;
    std::map<const char*, long>                  m_location_ids;
    std::map<FileId, std::vector<std::size_t>>   m_file_lines;
    std::map<std::size_t, Link>                  m_links;

    const S::Symbol* origin(const S::Symbol* s) const {
        s = follow(s);
        if (!s || !m_inst) return s;
        S::Instantiator::Instantiation* I = m_inst->owning(const_cast<S::Symbol*>(s));
        if (!I || I->sym != s || !I->source || !I->source->m_declaration) return s;
        const N::ASTNode* d = I->source->m_declaration;
        const S::Symbol* primary = nullptr;
        if (d->kind == K::FunctionDeclaration) primary = static_cast<const N::FunctionDeclaration*>(d)->symbol;
        else if (d->kind == K::OperatorFunctionDeclaration) primary = static_cast<const N::OperatorFunctionDeclaration*>(d)->symbol;
        else if (d->kind == K::ConstructorDeclaration) primary = static_cast<const N::ConstructorDeclaration*>(d)->symbol;
        return primary ? follow(primary) : s;
    }

    static bool is_callable(const S::Symbol* s) {
        if (!s || s->kind != S::SymbolKind::Function || !s->decl) return false;
        if (s->decl->kind == K::FunctionDeclaration) return !static_cast<const N::FunctionDeclaration*>(s->decl)->variable_template;
        return s->decl->kind == K::ConstructorDeclaration || s->decl->kind == K::OperatorFunctionDeclaration;
    }

    static bool same_operator(const S::Symbol* a, const S::Symbol* b) {
        if (!a || !b || !a->decl || !b->decl) return false;
        if (a->decl->kind != b->decl->kind) return false;
        if (a->decl->kind != K::OperatorFunctionDeclaration) return true;
        const auto* x = static_cast<const N::OperatorFunctionDeclaration*>(a->decl);
        const auto* y = static_cast<const N::OperatorFunctionDeclaration*>(b->decl);
        if (x->is_conversion() || y->is_conversion()) return x->is_conversion() && y->is_conversion();
        return x->get_overload() == y->get_overload();
    }

    std::vector<const S::Symbol*> overload_set(const S::Symbol* chosen, const N::ASTNode* site) const {
        std::vector<const S::Symbol*> out;
        const S::Symbol* first = origin(chosen);
        if (!is_callable(first)) return out;
        out.push_back(first);

        const auto add_one = [&out](const S::Symbol* s) {
            s = follow(s);
            if (s && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        };

        if (site && m_types) {
            const auto it = m_types->overload_orders.find(site);
            if (it != m_types->overload_orders.end()) for (const S::Symbol* s : it->second) add_one(s);
        }

        if (first->owner) {
            for (const S::Symbol* o = first->owner->find_local(first->name); o; o = o->next_overload) {
                if (o->merged_into || !is_callable(o) || !same_operator(o, first)) continue;
                add_one(o);
            }
        }

        return out;
    }

    void overload_hover(std::string_view at, const S::Symbol* chosen, const N::ASTNode* site, int rank) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        const std::vector<const S::Symbol*> set = overload_set(chosen, site);
        if (set.size() < 2) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());
        if (!h.overloads.empty() && (rank < h.overload_rank || h.guess)) return;
        std::vector<std::string> list;
        const S::Symbol* resolved = follow(chosen);
        std::string head = function_signature(resolved, m_types);
        if (head.empty()) head = function_signature(set.front(), m_types);
        list.push_back(head);

        for (std::size_t i = 1; i < set.size(); ++i) {
            const std::string sig = function_signature(set[i], m_types);
            if (!sig.empty() && std::find(list.begin(), list.end(), sig) == list.end()) list.push_back(sig);
        }

        if (list.size() < 2) return;
        h.overloads = std::move(list);
        h.overload_rank = rank;
        if (h.code.empty() || rank >= h.rank) { h.code = h.overloads.front(); h.rank = rank; }
    }

    static const N::FunctionParameters* parameters_of(const S::Symbol* s) {
        if (!s || !s->decl) return nullptr;
        if (s->decl->kind == K::FunctionDeclaration) return static_cast<const N::FunctionDeclaration*>(s->decl)->m_parameters;
        if (s->decl->kind == K::ConstructorDeclaration) return static_cast<const N::ConstructorDeclaration*>(s->decl)->m_parameters;
        if (s->decl->kind == K::OperatorFunctionDeclaration) return static_cast<const N::OperatorFunctionDeclaration*>(s->decl)->m_parameters;
        return nullptr;
    }

    static std::string bare_type(std::string t) {
        for (const char* word : { "const ", "volatile " }) {
            for (std::size_t at = t.find(word); at != std::string::npos; at = t.find(word)) t.erase(at, std::string(word).size());
        }
        while (!t.empty() && (t.back() == '&' || t.back() == ' ')) t.pop_back();
        return t;
    }

    static bool numeric_type(const std::string& t) {
        for (const char* n : { "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed", "size_t" }) {
            if (t.find(n) != std::string::npos && t.find('*') == std::string::npos && t.find('<') == std::string::npos) return true;
        }
        return false;
    }

    static int candidate_score(const S::Symbol* s, const N::CallExpression* c) {
        const N::FunctionParameters* ps = parameters_of(s);
        const std::size_t given = c->m_arguments.size();
        std::size_t required = 0;
        std::size_t total = 0;
        bool variadic = false;
        if (ps) {
            for (const N::FunctionParameter* p : ps->m_params) {
                if (!p) continue;
                if (p->is_variadic()) {
                    variadic = true;
                    continue;
                }
                ++total;
                if (!p->m_initializer) ++required;
            }
        }
        int score = 0;
        if (given >= required && (given <= total || variadic)) score += 1000;
        else score -= 50 * static_cast<int>(given < required ? required - given : given - total);
        if (!ps) return score;
        std::size_t i = 0;
        for (const N::FunctionParameter* p : ps->m_params) {
            if (!p || i >= given) break;
            const N::ASTNode* arg = c->m_arguments[i++];
            if (!arg || !arg->expr_type.type || arg->expr_type.type->is_dependent() || p->is_variadic()) continue;
            const std::string want = bare_type(type_text(p->m_type, nullptr));
            const std::string have = bare_type(type_text(arg->expr_type.type));
            if (want.empty() || have.empty()) continue;
            if (want == have) score += 20;
            else if (numeric_type(want) && numeric_type(have)) score += 8;
            else if ((want.find('*') != std::string::npos) == (have.find('*') != std::string::npos) && want.find_first_of("<") == std::string::npos) score += 2;
            else if (want.size() == 1 || (want.size() < 4 && std::isupper(static_cast<unsigned char>(want[0])))) score += 5;
            else score -= 10;
        }
        return score;
    }

    std::vector<const S::Symbol*> candidates_for(const N::CallExpression* c) const {
        std::vector<const S::Symbol*> out;
        const auto add_chain = [&out](const S::Symbol* first) {
            for (const S::Symbol* o = first; o; o = o->next_overload) {
                const S::Symbol* f = follow(o);
                if (!f || o->merged_into || !is_callable(f)) continue;
                if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
            }
        };
        const auto add_constructors = [&](const S::Symbol* type) {
            type = follow(type);
            if (!type || type->kind != S::SymbolKind::Type || !type->inner_scope) return;
            add_chain(type->inner_scope->find_local("constructor"));
        };
        const N::ASTNode* callee = c->m_callee;
        if (callee && callee->kind == K::TemplateInstantiation) callee = static_cast<const N::TemplateInstantiation*>(callee)->m_template;
        const S::Symbol* named = nullptr;
        if (callee && callee->kind == K::Identifier) named = static_cast<const N::Identifier*>(callee)->resolved;
        else if (callee && callee->kind == K::QualifiedIdentifier) named = static_cast<const N::QualifiedIdentifier*>(callee)->resolved;
        else if (callee && callee->kind == K::MemberAccessExpression) {
            const auto* m = static_cast<const N::MemberAccessExpression*>(callee);
            named = m->resolved ? m->resolved : scope_member(m);
            if (!named) named = dot_member(m);
        }
        named = follow(named);
        if (!named) return out;
        if (named->kind == S::SymbolKind::Type) {
            add_constructors(named);
            return out;
        }
        if (!is_callable(named)) return out;
        if (named->owner) add_chain(named->owner->find_local(named->name));
        if (out.empty()) out.push_back(named);
        return out;
    }

    void candidate_hover(std::string_view at, const N::CallExpression* c) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        std::vector<const S::Symbol*> set = candidates_for(c);
        if (set.empty()) return;
        std::vector<std::pair<int, const S::Symbol*>> ranked;
        for (const S::Symbol* s : set) ranked.emplace_back(candidate_score(s, c), s);
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::string> list;
        for (const auto& r : ranked) {
            const std::string sig = function_signature(r.second, m_types);
            if (!sig.empty() && std::find(list.begin(), list.end(), sig) == list.end()) list.push_back(sig);
        }
        if (list.empty()) return;
        Hover& h = m_hovers[offset(at.data())];
        if (h.len == 0) h.len = static_cast<std::uint32_t>(at.size());
        h.overloads = list;
        h.overload_rank = 3;
        h.code = list.front();
        h.rank = 3;
        h.guess = true;
    }

    void operator_hover(std::string_view at, const S::Symbol* chosen, const N::ASTNode* site) {
        if (at.empty() || !chosen) return;
        const std::string sig = function_signature(chosen, m_types);
        if (!sig.empty()) hover(at, sig, "", "", "", 2);
        overload_hover(at, chosen, site, 2);
        link(at, chosen, 2);
    }

    FileId file_holding(const char* p) const {
        if (!m_sources || !p) return INVALID_FILE;
        if (m_main != INVALID_FILE && inside(p)) return m_main;

        for (std::size_t i = 0; i < m_sources->file_count(); ++i) {
            const FileId id = static_cast<FileId>(i);
            if (id == m_main) continue;
            const char* b = m_sources->data(id);
            if (b && p >= b && p < b + m_sources->size(id)) return id;
        }

        return INVALID_FILE;
    }

    long location_of(std::string_view name) {
        if (name.empty()) return -1;
        const auto known = m_location_ids.find(name.data());
        if (known != m_location_ids.end()) return known->second;
        const FileId file = file_holding(name.data());
        if (file == INVALID_FILE) return -1;
        Location l;

        if (file == m_main) {
            const Position p = position(offset(name.data()), name.size());
            l.line = p.line; l.col = p.col; l.len = p.len;
        } else {
            const char* b = m_sources->data(file);
            auto& lines = m_file_lines[file];

            if (lines.empty()) {
                lines.push_back(0);
                for (std::size_t i = 0; i < m_sources->size(file); ++i) if (b[i] == '\n') lines.push_back(i + 1);
            }

            const std::size_t at = static_cast<std::size_t>(name.data() - b);
            const auto it = std::upper_bound(lines.begin(), lines.end(), at);
            const std::size_t line = static_cast<std::size_t>(it - lines.begin()) - 1;
            l.line = static_cast<std::uint32_t>(line);
            l.col  = utf16_units(b + lines[line], at - lines[line]);
            l.len  = utf16_units(name.data(), name.size());
        }

        const std::string path = file == m_main ? std::string() : m_sources->absolute_path(file);
        auto p = std::find(m_paths.begin(), m_paths.end(), path);
        if (p == m_paths.end()) { m_paths.push_back(path); p = m_paths.end() - 1; }
        l.file = static_cast<std::size_t>(p - m_paths.begin());
        const long id = static_cast<long>(m_locations.size());
        m_locations.push_back(l);
        m_location_ids.emplace(name.data(), id);
        return id;
    }

    std::string_view name_in(const N::ASTNode* d) const {
        std::string_view name = decl_name(d);
        if (name.empty() && d && (d->kind == K::ConstructorDeclaration || d->kind == K::OperatorFunctionDeclaration)) {
            const char* p = d->at;
            if (p) {
                const char* e = p;
                while (word_start(*e) || (*e >= '0' && *e <= '9')) ++e;
                name = std::string_view(p, static_cast<std::size_t>(e - p));
            }
        }
        return name;
    }

    void link(std::string_view at, const S::Symbol* original, int rank = 1) {
        if (at.empty() || !inside(at.data(), at.size())) return;
        const auto existing = m_links.find(offset(at.data()));
        if (existing != m_links.end() && existing->second.rank > rank) return;
        const S::Symbol* s = follow(original);
        if (!s) return;
        const S::Symbol* target = s->extern_target ? follow(s->extern_target) : s;
        if (!target) target = s;
        const long def = location_of(name_in(target->decl));
        if (def < 0) return;
        Link k;
        k.len = static_cast<std::uint32_t>(at.size());
        k.rank = rank;
        k.definition = def;

        const auto add_decl = [&k, this](const N::ASTNode* d) {
            const long id = location_of(name_in(d));
            if (id >= 0 && id != k.definition && std::find(k.declarations.begin(), k.declarations.end(), id) == k.declarations.end()) k.declarations.push_back(id);
        };

        for (const N::ASTNode* d : target->declarations) add_decl(d);
        if (target != s) { add_decl(s->decl); for (const N::ASTNode* d : s->declarations) add_decl(d); }
        m_links[offset(at.data())] = std::move(k);
    }

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
                if (s->decl && s->decl->kind == K::VariableDeclaration) {
                    const auto* v = static_cast<const N::VariableDeclaration*>(s->decl);
                    if (v->m_initializer) {
                        const std::string init = default_after(v->m_name, false);
                        if (!init.empty()) code += " = " + init;
                    }
                }
                if (m_eval) value = value_text(m_eval->value_of(const_cast<S::Symbol*>(s)), s->type ? s->type->canonical : nullptr);
                break;
            }

            case S::SymbolKind::Parameter: {
                const std::string t = symbol_type(s, use);
                code = (t.empty() ? std::string() : t + ": ") + std::string(s->name);
                if (s->decl && s->decl->kind == K::FunctionParameter) {
                    const auto* p = static_cast<const N::FunctionParameter*>(s->decl);
                    if (p->m_initializer) code += " = " + default_text(p->m_name, false);
                }
                break;
            }

            case S::SymbolKind::Function:
                code = function_signature(s, m_types);
                break;

            case S::SymbolKind::Type: {
                std::string head = template_prefix(s, m_types);
                if (!head.empty()) head.back() = '\n';
                if (s->decl && s->decl->kind == K::RecordDeclaration && static_cast<const N::RecordDeclaration*>(s->decl)->m_form != N::RecordDeclaration::Form::Class) code = head + "struct " + qualified_name(s);
                else code = head + "class " + qualified_name(s);
                break;
            }

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
            case S::SymbolKind::TemplateParam:
                if (s->decl && s->decl->kind == K::TemplateParameter) code = "(template parameter) " + template_parameter_text(static_cast<const N::TemplateParameter*>(s->decl), m_types);
                else code = "template parameter " + std::string(s->name);
                break;
            case S::SymbolKind::Import:        break;
        }

        if (use && (s->kind == S::SymbolKind::Variable || s->kind == S::SymbolKind::Parameter || s->kind == S::SymbolKind::TemplateParam)) {
            if (value.empty()) value = evaluate(use);
            if (s->kind == S::SymbolKind::TemplateParam || m_in_instantiation) type = type_text(use->expr_type.type);
        }

        hover(at, code, type, value, "", rank);
    }

    std::unordered_set<const N::ASTNode*> m_called;

    static bool depends_on_template(const N::ASTNode* o) {
        if (!o) return false;
        const S::Type* t = o->expr_type.type;
        return !t || t->is_dependent();
    }

    static const S::Symbol* dot_member(const N::MemberAccessExpression* m) {
        if (m->m_op == N::MemberAccessExpression::Op::Scope || !m->m_object || m->m_member.empty()) return nullptr;
        const N::ASTNode* o = m->m_object;
        const S::Symbol* object = nullptr;
        if (o->kind == K::Identifier) object = static_cast<const N::Identifier*>(o)->resolved;
        else if (o->kind == K::MemberAccessExpression) object = static_cast<const N::MemberAccessExpression*>(o)->resolved;
        object = follow(object);
        if (!object || (object->kind != S::SymbolKind::Variable && object->kind != S::SymbolKind::Parameter) || !object->type) return nullptr;
        const S::Symbol* record = follow(object->type->resolved);
        if (!record || record->kind != S::SymbolKind::Type || !record->inner_scope) return nullptr;
        return record->inner_scope->find_member(m->m_member);
    }

    std::string_view member_name_in_source(const N::MemberAccessExpression* m) const {
        if (m->m_member.empty() || inside(m->m_member.data(), m->m_member.size())) return m->m_member;
        const char* p = m->at;
        if (!inside(p)) return m->m_member;
        if (*p == '.') ++p;
        else if (inside(p, 2) && (std::string_view(p, 2) == "->" || std::string_view(p, 2) == "::")) p += 2;
        else return m->m_member;
        while (inside(p) && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        if (!inside(p, m->m_member.size()) || std::string_view(p, m->m_member.size()) != m->m_member) return m->m_member;
        return std::string_view(p, m->m_member.size());
    }

    static const S::Symbol* scope_member(const N::MemberAccessExpression* m) {
        if (m->m_op != N::MemberAccessExpression::Op::Scope || !m->m_object || m->m_member.empty()) return nullptr;
        const N::ASTNode* o = m->m_object;
        if (o->kind == K::TemplateInstantiation) o = static_cast<const N::TemplateInstantiation*>(o)->m_template;
        const S::Symbol* owner = nullptr;
        if (o && o->kind == K::Identifier) owner = static_cast<const N::Identifier*>(o)->resolved;
        else if (o && o->kind == K::QualifiedIdentifier) owner = static_cast<const N::QualifiedIdentifier*>(o)->resolved;
        else if (o && o->kind == K::MemberAccessExpression) owner = static_cast<const N::MemberAccessExpression*>(o)->resolved;
        owner = follow(owner);
        if (!owner || (owner->kind != S::SymbolKind::Type && owner->kind != S::SymbolKind::Enum) || !owner->inner_scope) return nullptr;
        return owner->inner_scope->find_member(m->m_member);
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
        add(name, kind_of(s), is_variable_template(s) ? VariableTemplate : 0, rank);
        if (kind_of(s) != "operator") link(name, s);
    }

    void declare(std::string_view name, const S::Symbol* s, std::string_view fallback, std::uint32_t mods = 0) {
        std::string_view kind = s ? kind_of(s) : std::string_view();
        if (kind.empty()) kind = fallback;
        if (is_variable_template(s)) mods |= VariableTemplate;
        add(name, kind, Declaration | mods, 3);
        if (s) link(name, s);
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
            link(parts[i], o);
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
    Collector c(sources.data(*main), sources.size(*main), semantic ? &instantiator.evaluator() : nullptr, semantic ? &compiler.mutable_types() : nullptr, &sources, *main, semantic ? &instantiator : nullptr);

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
