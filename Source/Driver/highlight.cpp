#include "Driver/highlight.hpp"
#include "Parser/Nodes/node_children.hpp"
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

class Collector {
public:
    Collector(const char* begin, std::size_t size) : m_begin(begin), m_end(begin + size) {
        m_line_starts.push_back(0);
        for (std::size_t i = 0; i < size; ++i) if (begin[i] == '\n') m_line_starts.push_back(i + 1);
    }

    bool inside(const char* p, std::size_t n = 1) const { return p && p >= m_begin && p + n <= m_end; }

    void walk(N::ASTNode* n) {
        if (!n || !m_seen.insert(n).second) return;

        switch (n->kind) {
            case K::Identifier: {
                auto* id = static_cast<N::Identifier*>(n);
                if (id->name != "self" && id->name != "this") use(id->name, id->resolved);
                break;
            }

            case K::QualifiedIdentifier: {
                auto* q = static_cast<N::QualifiedIdentifier*>(n);
                if (q->parts().empty()) break;
                use(q->parts().back(), q->resolved);
                qualifiers(q->parts(), q->parts().size() - 1, q->resolved);
                break;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<N::BinaryExpression*>(n);
                if (b->resolved) op(b->at, b->op);
                break;
            }

            case K::UnaryExpression: {
                auto* u = static_cast<N::UnaryExpression*>(n);
                if (u->resolved) op(u->at, u->op);
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
                targs(c->m_template_args);
                break;
            }

            case K::TemplateInstantiation:
                targs(static_cast<N::TemplateInstantiation*>(n)->m_args);
                break;

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
                break;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<N::ArrayDeclaration*>(n);
                type(a->m_element_type);
                declare(a->m_name, a->symbol, "variable");
                break;
            }

            case K::FunctionParameter: {
                auto* p = static_cast<N::FunctionParameter*>(n);
                type(p->m_type);
                declare(p->m_name, nullptr, "parameter");
                break;
            }

            case K::FunctionDeclaration: {
                auto* f = static_cast<N::FunctionDeclaration*>(n);
                type(f->m_return_type);
                if (!f->m_name_parts.empty()) declare(f->m_name_parts.back(), f->symbol, "function");
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
                if (!r->m_name_parts.empty()) declare(r->m_name_parts.back(), r->symbol, r->m_form == N::RecordDeclaration::Form::Class ? "class" : "struct");
                type(r->m_inherits);
                for (const auto& b : r->m_extra_bases) type(b);
                targs(r->m_spec_args);
                break;
            }

            case K::EnumDeclaration: {
                auto* e = static_cast<N::EnumDeclaration*>(n);
                declare(e->name, e->symbol, "enum");
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

    std::string json(const ErrorReporter& errors, const WarningReporter& warnings, FileId main, const SourceManager& sources, bool semantic) const {
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

        for (const CompilerError& e : errors.errors()) diagnostic("error", e.file_id, e.line, e.message, e.at, e.width);
        for (const CompilerWarning& w : warnings.warnings()) diagnostic("warning", w.file_id, w.line, w.message, nullptr, 0);
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

    void op(const char* at, tokenizing::Token::Kind kind) {
        if (!inside(at)) return;
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
                qualifiers(parts, parts.size() - 1, t.resolved);
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

    Collector c(sources.data(*main), sources.size(*main));

    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (u.file == *main && u.ast) c.walk(u.ast);
    }

    compiler.get_instantiator().for_each([&c](const S::Instantiator::Instantiation& I) {
        if (I.decl) c.walk(I.decl);
    });

    return c.json(compiler.get_reporter(), compiler.get_warnings(), *main, sources, semantic);
}

} // namespace driver
} // namespace walnut
