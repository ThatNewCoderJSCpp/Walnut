#include "Driver/perf_hints.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Semantics/scope.hpp"
#include "Semantics/symbol.hpp"
#include "Semantics/type.hpp"

#include <algorithm>
#include <regex>
#include <set>
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
using TK = tokenizing::Token::Kind;
using RM = modifiers::RawModifiers;
using FQ = modifiers::FunctionQualifiers;
using BK = parser_types::PrimitiveType::BaseKind;

S::Type* strip(S::Type* t) {
    while (t && t->is_reference()) t = static_cast<S::ReferenceType*>(t)->referent();
    return t;
}

bool is_builtin_base(S::Type* t, BK base) {
    t = strip(t);
    return t && t->is_builtin() && static_cast<S::BuiltinType*>(t)->base() == base;
}

bool is_stringish(S::Type* t) { return is_builtin_base(t, BK::String) || is_builtin_base(t, BK::Text); }

bool is_dynamic(S::Type* t) {
    t = strip(t);
    return t && t->is_builtin() && static_cast<S::BuiltinType*>(t)->is_dynamic();
}

bool is_wide_int(S::Type* t) {
    t = strip(t);
    if (!t || !t->is_builtin()) return false;
    auto* b = static_cast<S::BuiltinType*>(t);
    return b->base() == BK::Int && !b->is_hardware();
}

bool costly_to_copy(S::Type* t) {
    if (!t || t->is_reference() || t->is_pointer()) return false;
    return t->is_record() || is_stringish(t);
}

const S::Symbol* root_of(const N::ASTNode* e) {
    while (e) {
        switch (e->kind) {
            case K::Identifier: return static_cast<const N::Identifier*>(e)->resolved;
            case K::MemberAccessExpression: {
                auto* m = static_cast<const N::MemberAccessExpression*>(e);
                if (m->m_op != N::MemberAccessExpression::Op::Dot) return nullptr;
                e = m->m_object;
                break;
            }
            case K::SubscriptExpression: e = static_cast<const N::SubscriptExpression*>(e)->m_array; break;
            default: return nullptr;
        }
    }
    return nullptr;
}

std::string line_text(const SourceManager& sources, FileId f, std::uint32_t line) {
    if (f >= sources.file_count() || line == 0) return std::string();
    const char* data = sources.data(f);
    const std::size_t size = sources.size(f);
    std::size_t at = 0;
    for (std::uint32_t l = 1; l < line && at < size; ++at) if (data[at] == '\n') ++l;
    std::size_t end = at;
    while (end < size && data[end] != '\n' && data[end] != '\r') ++end;
    return std::string(data + at, end - at);
}

std::string escape_regex(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (std::string_view("\\^$.|?*+()[]{}").find(c) != std::string_view::npos) out += '\\';
        out += c;
    }
    return out;
}

std::string by_reference_edit(const std::string& line, std::string_view name, bool loop_binding) {
    const std::string tail = loop_binding ? "(\\s+in\\b)" : "(\\s*[,)=])";
    const std::regex re("((?:^|[(,]\\s*))([A-Za-z_][A-Za-z0-9_:<>, ]*?)(\\s*:\\s*)(" + escape_regex(name) + ")" + tail);
    std::smatch m;
    if (!std::regex_search(line, m, re)) return std::string();
    std::string type = m[2].str();
    while (!type.empty() && type.back() == ' ') type.pop_back();
    if (type.find('&') != std::string::npos || type.find('*') != std::string::npos) return std::string();
    if (type.rfind("const ", 0) != 0) type = "const " + type;
    return m.prefix().str() + m[1].str() + type + "&" + m[3].str() + m[4].str() + m[5].str() + m.suffix().str();
}

bool cheap_call_name(std::string_view n) {
    return n == "size" || n == "length" || n == "count" || n == "empty" || n == "countof" || n == "capacity" || n == "is_empty" || n == "front" || n == "back";
}

bool is_push_name(std::string_view n) {
    return n == "push_back" || n == "push" || n == "append" || n == "add" || n == "emplace_back" || n == "emplace" || n == "push_front" || n == "insert";
}

std::uint32_t last_line(const N::ASTNode* n) {
    if (!n) return 0;
    std::uint32_t best = n->line;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { best = std::max(best, last_line(c)); });
    return best;
}

std::size_t statement_count(const N::ASTNode* body) {
    if (!body || body->kind != K::BlockStatement) return 0;
    return static_cast<const N::BlockStatement*>(body)->get_statements().size();
}

bool has_reserve(const N::ASTNode* n, const S::Symbol* target) {
    if (!n) return false;
    if (n->kind == K::CallExpression) {
        auto* c = static_cast<const N::CallExpression*>(n);
        if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
            auto* m = static_cast<const N::MemberAccessExpression*>(c->m_callee);
            if ((m->m_member == "reserve" || m->m_member == "resize") && root_of(m->m_object) == target) return true;
        }
    }
    bool found = false;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (!found && has_reserve(c, target)) found = true; });
    return found;
}

std::size_t self_calls(const N::ASTNode* n, const S::Symbol* fn) {
    if (!n || n->kind == K::LambdaExpression) return 0;
    std::size_t count = 0;
    if (n->kind == K::CallExpression && static_cast<const N::CallExpression*>(n)->resolved == fn) ++count;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { count += self_calls(c, fn); });
    return count;
}

bool writes_param(const N::ASTNode* n, const S::Symbol* p) {
    if (!n) return false;
    switch (n->kind) {
        case K::BinaryExpression: {
            auto* b = static_cast<const N::BinaryExpression*>(n);
            if (is_assignment_op(b->op) && root_of(b->left) == p) return true;
            break;
        }
        case K::UnaryExpression: {
            auto* u = static_cast<const N::UnaryExpression*>(n);
            if ((u->op == TK::DoublePlus || u->op == TK::DoubleMinus) && root_of(u->operand) == p) return true;
            break;
        }
        case K::ReferenceExpression:
            if (root_of(static_cast<const N::ReferenceExpression*>(n)->operand) == p) return true;
            break;
        case K::ReturnStatement: {
            bool direct = false;
            N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (c->kind == K::Identifier && root_of(c) == p) direct = true; });
            if (direct) return true;
            break;
        }
        case K::CallExpression: {
            auto* c = static_cast<const N::CallExpression*>(n);
            const N::FunctionParameters* params = nullptr;
            if (c->resolved && c->resolved->decl && c->resolved->decl->kind == K::FunctionDeclaration) params = static_cast<const N::FunctionDeclaration*>(c->resolved->decl)->m_parameters;
            for (std::size_t i = 0; i < c->m_arguments.size(); ++i) {
                if (root_of(c->m_arguments[i]) != p) continue;
                if (!params || i >= params->m_params.size()) return true;
                const parser_types::TypeInfo& t = params->m_params[i]->get_type();
                if (t.has_reference() && !t.modifiers.has(RM::Const)) return true;
            }
            if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
                auto* m = static_cast<const N::MemberAccessExpression*>(c->m_callee);
                if (root_of(m->m_object) == p) {
                    const S::Symbol* fn = c->resolved;
                    bool is_const = false;
                    if (fn && fn->decl && fn->decl->kind == K::FunctionDeclaration) is_const = static_cast<const N::FunctionDeclaration*>(fn->decl)->m_qualifiers.has(FQ::Const);
                    if (!is_const) return true;
                }
            }
            break;
        }
        case K::LambdaExpression: {
            auto* l = static_cast<const N::LambdaExpression*>(n);
            if (l->m_captures) {
                for (const N::LambdaCaptureItem& c : l->m_captures->captures()) {
                    if (c.mode == N::LambdaCaptureItem::Mode::AllByReference || (c.mode == N::LambdaCaptureItem::Mode::ByReference && c.resolved == p)) return true;
                }
            }
            break;
        }
        case K::VariableDeclaration: {
            auto* v = static_cast<const N::VariableDeclaration*>(n);
            if (v->m_type_info.has_reference() && !v->m_type_info.modifiers.has(RM::Const) && root_of(v->m_initializer) == p) return true;
            break;
        }
        default: break;
    }
    bool found = false;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (!found && writes_param(c, p)) found = true; });
    return found;
}

class Collector {
public:
    Collector(PerfInfo& out, const SourceManager& sources, S::TypeContext& types) : m_out(out), m_sources(sources), m_types(types) {}

    void unit(FileId file, N::ASTNode* ast) {
        m_file = file;
        m_path = m_sources.absolute_path(file);
        walk(ast);
    }

private:
    void hint(std::uint32_t line, const std::string& kind, const std::string& message, const std::string& after = std::string()) {
        if (!line) return;
        const std::string key = std::to_string(line) + ":" + kind;
        if (!m_seen.insert(m_path + ":" + key).second) return;
        PerfHint h;
        h.file = m_path;
        h.line = line;
        h.function = m_function;
        h.kind = kind;
        h.message = message;
        if (!after.empty()) {
            h.before = line_text(m_sources, m_file, line);
            h.after = after;
            if (h.before == h.after) { h.before.clear(); h.after.clear(); }
        }
        m_out.hints.push_back(std::move(h));
    }

    S::Type* type_of(const N::ASTNode* n) const { return n ? n->expr_type.type : nullptr; }

    void function(N::FunctionDeclaration* fn) {
        if (!fn->m_body || !fn->symbol) return;
        const std::string saved_fn = m_function;
        const N::FunctionDeclaration* saved_decl = m_decl;
        m_function = std::string(fn->get_name());
        if (fn->symbol->owner && fn->symbol->owner->owner_symbol && fn->symbol->owner->owner_symbol->kind == S::SymbolKind::Type) m_function = std::string(fn->symbol->owner->owner_symbol->name) + "::" + m_function;
        m_decl = fn;

        FunctionSpan span;
        span.name = m_function;
        span.file = m_path;
        span.first = fn->line;
        span.last = std::max(fn->line, last_line(fn->m_body));
        span.statements = statement_count(fn->m_body);
        span.is_inline = fn->m_modifiers.has(RM::Inline);
        span.is_main = fn->get_name() == "main" && !fn->symbol->owner->owner_symbol;
        m_out.functions.push_back(span);

        const std::string decl_line = line_text(m_sources, m_file, fn->line);
        if (fn->m_parameters) {
            for (N::FunctionParameter* p : fn->m_parameters->m_params) {
                if (!p || !p->symbol || p->is_variadic()) continue;
                S::Type* t = p->get_type().canonical ? p->get_type().canonical : m_types.canonicalize(p->get_type());
                if (!costly_to_copy(t) || writes_param(fn->m_body, p->symbol)) continue;
                const std::string edited = by_reference_edit(decl_line, p->get_name(), false);
                hint(fn->line, "copy-param:" + std::string(p->get_name()), "'" + m_function + "' takes '" + std::string(p->get_name()) + "' by value, which copies it on every call; it only reads it, so taking it as a const reference avoids the copy", edited);
            }
        }

        if (self_calls(fn->m_body, fn->symbol) >= 2) {
            hint(fn->line, "memoize", "'" + m_function + "' calls itself more than once per call, so the same values are often worked out again and again; remembering results it has already worked out (memoizing), or building them up in a loop, usually makes this much faster");
        }

        if (!span.is_inline && !span.is_main && span.statements > 0 && span.statements <= 2 && self_calls(fn->m_body, fn->symbol) == 0 && !fn->m_qualifiers.has(FQ::Virtual) && !fn->m_qualifiers.has(FQ::Override)) {
            std::string edited = decl_line;
            const std::size_t at = edited.find("function ");
            if (at != std::string::npos) edited.insert(at, "inline ");
            else edited.clear();
            hint(fn->line, "inline", "'" + m_function + "' is small and called very often; marking it 'inline' pastes its body in where it's called, which saves the cost of each call", edited);
        }

        walk(fn->m_body);
        m_function = saved_fn;
        m_decl = saved_decl;
    }

    void record(N::RecordDeclaration* r) {
        if (r->is_union() || !r->symbol) return;
        RecordSpan span;
        span.name = std::string(r->symbol->name);
        span.file = m_path;
        span.line = r->line;
        for (const auto& member : r->m_members) {
            N::ASTNode* m = member.node;
            if (!m) continue;
            if (m->kind == K::VariableDeclaration) {
                auto* v = static_cast<N::VariableDeclaration*>(m);
                if (v->m_type_info.modifiers.has(RM::Static)) continue;
                span.fields.push_back(FieldSpan{ std::string(v->m_name), v->line });
            } else if (m->kind == K::ArrayDeclaration) {
                auto* a = static_cast<N::ArrayDeclaration*>(m);
                if (a->get_array_modifiers().has(RM::Static)) continue;
                span.fields.push_back(FieldSpan{ std::string(a->m_name), a->line });
            }
        }
        m_out.records.push_back(std::move(span));
    }

    void loop(N::ASTNode* n, N::ASTNode* cond) {
        if (cond) {
            bool costly = false;
            std::string name;
            scan_calls(cond, costly, name);
            if (costly) hint(cond->line ? cond->line : n->line, "loop-call", "the loop condition calls '" + name + "' every time round; if its result doesn't change inside the loop, work it out once before the loop and compare with that");
        }
        ++m_loop;
        m_loops.push_back(n);
        if (n->kind == K::ForStatement && static_cast<N::ForStatement*>(n)->increment) m_steps.insert(static_cast<N::ForStatement*>(n)->increment);
        N::for_each_child(n, [&](N::ASTNode* c) { walk(c); });
        m_loops.pop_back();
        --m_loop;
    }

    void scan_calls(N::ASTNode* n, bool& costly, std::string& name) {
        if (!n || costly) return;
        if (n->kind == K::CallExpression) {
            auto* c = static_cast<N::CallExpression*>(n);
            std::string_view called;
            if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) called = static_cast<N::MemberAccessExpression*>(c->m_callee)->m_member;
            else if (c->resolved) called = c->resolved->name;
            if (!called.empty() && !cheap_call_name(called)) { costly = true; name = std::string(called); return; }
        }
        N::for_each_child(n, [&](N::ASTNode* c) { scan_calls(c, costly, name); });
    }

    void walk(N::ASTNode* n) {
        if (!n) return;
        switch (n->kind) {
            case K::FunctionDeclaration:
                function(static_cast<N::FunctionDeclaration*>(n));
                return;
            case K::TemplateDeclaration:
                return;
            case K::LambdaExpression:
                return;
            case K::RecordDeclaration:
                record(static_cast<N::RecordDeclaration*>(n));
                break;
            case K::WhileStatement:
                loop(n, static_cast<N::WhileStatement*>(n)->condition);
                return;
            case K::DoWhileStatement:
                loop(n, static_cast<N::DoWhileStatement*>(n)->condition);
                return;
            case K::ForStatement:
                loop(n, static_cast<N::ForStatement*>(n)->condition);
                return;
            case K::ForEachStatement: {
                auto* f = static_cast<N::ForEachStatement*>(n);
                const bool by_value = !f->m_element_type.has_reference() && !f->is_structured_binding();
                if (by_value && f->m_body) {
                    S::Type* seen = f->m_element_type.canonical;
                    if (seen && costly_to_copy(seen)) {
                        const std::string line = line_text(m_sources, m_file, f->line);
                        const std::string edited = by_reference_edit(line, f->m_var_name, true);
                        hint(f->line, "copy-loop", "each element is copied into '" + std::string(f->m_var_name) + "' every time round; looping with a const reference reads them in place", edited);
                    }
                }
                loop(n, nullptr);
                return;
            }
            case K::VariableDeclaration: {
                auto* v = static_cast<N::VariableDeclaration*>(n);
                if (v->m_initializer && v->symbol && v->m_bindings.empty()) {
                    S::Type* declared = v->m_type_info.canonical;
                    S::Type* init = strip(type_of(v->m_initializer));
                    if (is_dynamic(declared) && init && init->is_builtin() && !is_dynamic(init) && !static_cast<S::BuiltinType*>(init)->is_void()) {
                        const std::string spelled = S::type_str_of(init);
                        const std::string line = line_text(m_sources, m_file, v->line);
                        std::string edited;
                        const std::regex re("\\b(var|variable|dynamic|any|let|def|define)(\\s*:\\s*" + escape_regex(v->m_name) + "\\b)");
                        std::smatch m;
                        if (std::regex_search(line, m, re)) edited = m.prefix().str() + spelled + m[2].str() + m.suffix().str();
                        hint(v->line, "dynamic", "'" + std::string(v->m_name) + "' is a 'var', so every use checks what it holds at run time; it always holds a " + spelled + " here, so declaring it as one is faster", edited);
                    }
                }
                break;
            }
            case K::BinaryExpression: {
                auto* b = static_cast<N::BinaryExpression*>(n);
                if (m_loop) {
                    if (b->op == TK::PlusEqual && is_stringish(type_of(b->left))) {
                        hint(b->line, "string-append", "this adds to a string every time round the loop, which can copy it each time it grows; call reserve on it before the loop with roughly the final length, or collect the pieces and join them once");
                    } else if (b->op == TK::Equal && b->right && b->right->kind == K::BinaryExpression && is_stringish(type_of(b->left))) {
                        auto* r = static_cast<N::BinaryExpression*>(b->right);
                        if (r->op == TK::Plus && root_of(r->left) && root_of(r->left) == root_of(b->left)) {
                            hint(b->line, "string-append", "'x = x + ...' on a string builds a whole new string every time round the loop; 'x += ...' adds to it in place");
                        }
                    }
                    const bool arith = b->op == TK::Plus || b->op == TK::Minus || b->op == TK::Asterisk || b->op == TK::Slash || b->op == TK::Percent || b->op == TK::PlusEqual || b->op == TK::MinusEqual || b->op == TK::AsteriskEqual || b->op == TK::SlashEqual || b->op == TK::PercentEqual;
                    if (arith && !b->resolved && is_wide_int(type_of(b->left)) && is_wide_int(type_of(b->right)) && !m_loops.empty()) {
                        const std::uint32_t loop_line = m_loops.back()->line ? m_loops.back()->line : b->line;
                        hint(loop_line, "wide-int", "this loop does arithmetic on Walnut's built-in ints, which never overflow because they're wider than the processor's own; when the values are known to stay small, std::hardware_int32 or std::hardware_int64 does the same arithmetic much faster");
                    }
                }
                break;
            }
            case K::CallExpression: {
                auto* c = static_cast<N::CallExpression*>(n);
                if (m_loop && c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
                    auto* m = static_cast<N::MemberAccessExpression*>(c->m_callee);
                    const S::Symbol* target = root_of(m->m_object);
                    S::Type* ot = strip(type_of(m->m_object));
                    if (is_push_name(m->m_member) && target && ot && ot->is_record() && m_decl && !has_reserve(m_decl->m_body, target)) {
                        hint(c->line, "reserve:" + std::string(target->name), "'" + std::string(target->name) + "' grows one item at a time inside a loop, so it moves its items to bigger storage again and again; call " + std::string(target->name) + ".reserve(count) before the loop when you know about how many there will be");
                    }
                }
                break;
            }
            case K::UnaryExpression: {
                auto* u = static_cast<N::UnaryExpression*>(n);
                if (m_loop && !u->resolved && (u->op == TK::DoublePlus || u->op == TK::DoubleMinus) && is_wide_int(type_of(u->operand)) && !m_loops.empty() && !m_steps.count(n)) {
                    const std::uint32_t loop_line = m_loops.back()->line ? m_loops.back()->line : u->line;
                    hint(loop_line, "wide-int", "this loop does arithmetic on Walnut's built-in ints, which never overflow because they're wider than the processor's own; when the values are known to stay small, std::hardware_int32 or std::hardware_int64 does the same arithmetic much faster");
                }
                break;
            }
            case K::NewExpression:
                if (m_loop) hint(n->line, "new-in-loop", "'new' runs every time round this loop, and each one asks the system for memory; reuse one object made before the loop, keep the values in a container, or use an arena from std::memory");
                break;
            case K::ThrowExpression:
                hint(n->line, "throw", "this throws often; exceptions are slow when thrown many times, so for an expected outcome return a value instead (such as std::Optional or a bool)");
                break;
            default:
                break;
        }
        N::for_each_child(n, [&](N::ASTNode* c) { walk(c); });
    }

    PerfInfo& m_out;
    const SourceManager& m_sources;
    S::TypeContext& m_types;
    FileId m_file = INVALID_FILE;
    std::string m_path;
    std::string m_function;
    const N::FunctionDeclaration* m_decl = nullptr;
    int m_loop = 0;
    std::vector<N::ASTNode*> m_loops;
    std::unordered_set<const N::ASTNode*> m_steps;
    std::set<std::string> m_seen;
};

}

PerfInfo performance_hints(CompilerPipeline& compiler, const std::function<bool(FileId)>& own_file) {
    PerfInfo out;
    Collector c(out, compiler.get_sources(), compiler.mutable_types());
    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (!u.ast || !own_file(u.file)) continue;
        c.unit(u.file, u.ast);
    }
    out.complexity = estimate_complexity(compiler, own_file);
    return out;
}

} // namespace driver
} // namespace walnut
