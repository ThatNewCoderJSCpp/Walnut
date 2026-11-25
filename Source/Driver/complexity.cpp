#include "Driver/complexity.hpp"
#include "Parser/Nodes/node_children.hpp"
#include "Semantics/scope.hpp"
#include "Semantics/symbol.hpp"
#include "Semantics/type.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace driver {

Growth Growth::times(const Growth& o) const {
    Growth g;
    g.exponential = exponential || o.exponential;
    g.power = power + o.power;
    g.logs = logs + o.logs;
    return g;
}

bool Growth::below(const Growth& o) const {
    if (exponential != o.exponential) return !exponential;
    if (power != o.power) return power < o.power;
    return logs < o.logs;
}

Growth Growth::larger(const Growth& o) const { return below(o) ? o : *this; }

std::string Growth::text() const {
    if (exponential) return "O(2^n)";
    std::string inner;
    if (power == 1) inner = "n";
    else if (power > 1) inner = "n^" + std::to_string(power);
    if (logs > 0) {
        if (!inner.empty()) inner += " ";
        inner += logs == 1 ? "log n" : "log^" + std::to_string(logs) + " n";
    }
    return "O(" + (inner.empty() ? std::string("1") : inner) + ")";
}

namespace {

namespace N = nodes;
namespace S = semantics;
using K = N::ASTNode::Kind;
using TK = tokenizing::Token::Kind;
using RM = modifiers::RawModifiers;

Growth constant() { return Growth{}; }
Growth linear() { Growth g; g.power = 1; return g; }
Growth logarithmic() { Growth g; g.logs = 1; return g; }
Growth n_log_n() { Growth g; g.power = 1; g.logs = 1; return g; }
Growth exponential() { Growth g; g.exponential = true; return g; }

struct Cost {
    Growth time;
    Growth space;
    std::vector<ComplexityReason> time_reasons;
    std::vector<ComplexityReason> space_reasons;

    void take_time(const Growth& g, const std::vector<ComplexityReason>& why) {
        if (time.below(g)) { time = g; time_reasons = why; }
    }

    void take_space(const Growth& g, const std::vector<ComplexityReason>& why) {
        if (space.below(g)) { space = g; space_reasons = why; }
    }

    void absorb(const Cost& o) {
        take_time(o.time, o.time_reasons);
        take_space(o.space, o.space_reasons);
    }
};

S::Type* strip(S::Type* t) {
    while (t && t->is_reference()) t = static_cast<S::ReferenceType*>(t)->referent();
    return t;
}

bool is_literal_number(const N::ASTNode* n, long long& value) {
    if (!n || n->kind != K::Literal) return false;
    std::string digits;
    for (char c : static_cast<const N::Literal*>(n)->value) {
        if (c == '_' || c == '\'') continue;
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        digits += c;
    }
    if (digits.empty()) return false;
    value = std::strtoll(digits.c_str(), nullptr, 10);
    return true;
}

bool is_small_literal(const N::ASTNode* n, long long want) {
    long long v = 0;
    return is_literal_number(n, v) && v == want;
}

bool only_literals(const N::ASTNode* n, const std::set<const S::Symbol*>& counters) {
    if (!n) return true;
    if (n->kind == K::Literal) return true;
    if (n->kind == K::Identifier) {
        const S::Symbol* s = static_cast<const N::Identifier*>(n)->resolved;
        if (!s) return false;
        if (counters.count(s)) return true;
        return s->kind == S::SymbolKind::EnumConstant;
    }
    if (n->kind == K::CallExpression || n->kind == K::MemberAccessExpression) return false;
    bool ok = true;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (ok && !only_literals(c, counters)) ok = false; });
    return ok;
}

const S::Symbol* root_of(const N::ASTNode* e) {
    while (e) {
        switch (e->kind) {
            case K::Identifier: return static_cast<const N::Identifier*>(e)->resolved;
            case K::MemberAccessExpression: e = static_cast<const N::MemberAccessExpression*>(e)->m_object; break;
            case K::SubscriptExpression: e = static_cast<const N::SubscriptExpression*>(e)->m_array; break;
            default: return nullptr;
        }
    }
    return nullptr;
}

bool halves(const N::ASTNode* n) {
    if (!n) return false;
    if (n->kind == K::BinaryExpression) {
        auto* b = static_cast<const N::BinaryExpression*>(n);
        if (!b->resolved) {
            long long v = 0;
            if ((b->op == TK::Slash || b->op == TK::SlashEqual) && is_literal_number(b->right, v) && v >= 2) return true;
            if ((b->op == TK::DoubleGreaterThan || b->op == TK::ShiftRightEqual) && is_literal_number(b->right, v) && v >= 1) return true;
            if ((b->op == TK::Asterisk || b->op == TK::AsteriskEqual) && is_literal_number(b->right, v) && v >= 2) return true;
            if ((b->op == TK::DoubleLessThan || b->op == TK::ShiftLeftEqual) && is_literal_number(b->right, v) && v >= 1) return true;
        }
    }
    if (n->kind == K::LambdaExpression) return false;
    bool found = false;
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (!found && halves(c)) found = true; });
    return found;
}

void declared_counters(const N::ASTNode* n, std::set<const S::Symbol*>& out) {
    if (!n) return;
    if (n->kind == K::VariableDeclaration) {
        auto* v = static_cast<const N::VariableDeclaration*>(n);
        if (v->symbol) out.insert(v->symbol);
    }
    N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { declared_counters(c, out); });
}

bool is_push_name(std::string_view n) {
    return n == "push_back" || n == "push" || n == "append" || n == "add" || n == "emplace_back" || n == "emplace" || n == "push_front" || n == "insert" || n == "set" || n == "put";
}

bool contains_any(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* w : words) if (text.find(w) != std::string::npos) return true;
    return false;
}

class Analyzer {
public:
    Analyzer(const SourceManager& sources) : m_sources(sources) {}

    void collect(FileId file, N::ASTNode* n) {
        if (!n) return;
        if (n->kind == K::FunctionDeclaration) {
            auto* fn = static_cast<N::FunctionDeclaration*>(n);
            if (fn->m_body && fn->symbol) {
                m_decls[fn->symbol] = fn;
                m_order.push_back({ file, fn });
            }
        }
        if (n->kind == K::LambdaExpression) return;
        N::for_each_child(n, [&](N::ASTNode* c) { collect(file, c); });
    }

    std::vector<ComplexityEstimate> run() {
        std::vector<ComplexityEstimate> out;
        for (const auto& [file, fn] : m_order) {
            const Cost& c = of(fn);
            ComplexityEstimate e;
            e.name = display_name(fn);
            e.file = m_sources.absolute_path(file);
            e.first = fn->line;
            e.last = std::max<std::uint32_t>(fn->line, last_line(fn->m_body));
            e.time = c.time;
            e.space = c.space;
            e.time_reasons = c.time_reasons;
            e.space_reasons = c.space_reasons;
            e.recursive = m_recursive.count(fn) > 0;
            out.push_back(std::move(e));
        }
        return out;
    }

private:
    static std::uint32_t last_line(const N::ASTNode* n) {
        if (!n) return 0;
        std::uint32_t best = n->line;
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { best = std::max(best, last_line(c)); });
        return best;
    }

    static std::string display_name(const N::FunctionDeclaration* fn) {
        std::string name(fn->get_name());
        if (fn->symbol && fn->symbol->owner && fn->symbol->owner->owner_symbol && fn->symbol->owner->owner_symbol->kind == S::SymbolKind::Type) name = std::string(fn->symbol->owner->owner_symbol->name) + "::" + name;
        return name;
    }

    const Cost& of(N::FunctionDeclaration* fn) {
        auto it = m_done.find(fn);
        if (it != m_done.end()) return it->second;
        if (m_busy.count(fn)) {
            static const Cost none;
            return none;
        }
        m_busy.insert(fn);
        N::FunctionDeclaration* saved = m_current;
        m_current = fn;
        Cost body = cost(fn->m_body);
        m_current = saved;
        m_busy.erase(fn);

        const std::size_t self = self_calls(fn->m_body, fn->symbol);
        if (self > 0) {
            m_recursive.insert(fn);
            const bool shrinks_by_half = halving_recursion(fn);
            const std::string name = display_name(fn);
            Cost c;
            Growth depth = shrinks_by_half ? logarithmic() : linear();
            if (self == 1) {
                if (shrinks_by_half && body.time.power >= 1) c.take_time(body.time, body.time_reasons);
                else c.take_time(depth.times(body.time), { { fn->line, "'" + name + "' calls itself once, each time with a " + std::string(shrinks_by_half ? "halved" : "smaller") + " value, so it runs about " + std::string(shrinks_by_half ? "log n" : "n") + " times" } });
            } else if (shrinks_by_half) {
                if (body.time.power >= 1) c.take_time(body.time.times(logarithmic()), { { fn->line, "'" + name + "' splits its input in half and calls itself on each part, doing " + body.time.text() + " work at each level (like merge sort)" } });
                else c.take_time(linear(), { { fn->line, "'" + name + "' splits its input in half and calls itself on each part" } });
            } else {
                c.take_time(exponential(), { { fn->line, "'" + name + "' calls itself " + std::to_string(self) + " times with only slightly smaller values, so the calls multiply; remembering results it has already worked out brings this down to about O(n)" } });
            }
            c.take_space(depth, { { fn->line, "each call of '" + name + "' waits on the stack for the one it makes, " + std::string(shrinks_by_half ? "about log n" : "up to about n") + " deep" } });
            c.take_space(body.space, body.space_reasons);
            auto [slot, added] = m_done.emplace(fn, std::move(c));
            return slot->second;
        }
        auto [slot, added] = m_done.emplace(fn, std::move(body));
        return slot->second;
    }

    static std::size_t self_calls(const N::ASTNode* n, const S::Symbol* fn) {
        if (!n || n->kind == K::LambdaExpression) return 0;
        std::size_t count = 0;
        if (n->kind == K::CallExpression && static_cast<const N::CallExpression*>(n)->resolved == fn) ++count;
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { count += self_calls(c, fn); });
        return count;
    }

    bool halving_recursion(const N::FunctionDeclaration* fn) {
        std::unordered_map<const S::Symbol*, const N::ASTNode*> inits;
        gather_inits(fn->m_body, inits);
        bool halving = false;
        visit_self_calls(fn->m_body, fn->symbol, [&](const N::CallExpression* c) {
            for (const N::ASTNode* a : c->m_arguments) {
                if (halves(a)) halving = true;
                const S::Symbol* s = a && a->kind == K::Identifier ? static_cast<const N::Identifier*>(a)->resolved : nullptr;
                if (s) {
                    auto it = inits.find(s);
                    if (it != inits.end() && halves(it->second)) halving = true;
                }
                if (a && a->kind == K::BinaryExpression) {
                    auto* b = static_cast<const N::BinaryExpression*>(a);
                    for (const N::ASTNode* side : { b->left, b->right }) {
                        const S::Symbol* r = side && side->kind == K::Identifier ? static_cast<const N::Identifier*>(side)->resolved : nullptr;
                        auto it = r ? inits.find(r) : inits.end();
                        if (it != inits.end() && halves(it->second)) halving = true;
                    }
                }
            }
        });
        return halving;
    }

    static void gather_inits(const N::ASTNode* n, std::unordered_map<const S::Symbol*, const N::ASTNode*>& out) {
        if (!n || n->kind == K::LambdaExpression) return;
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<const N::VariableDeclaration*>(n);
            if (v->symbol && v->m_initializer) out[v->symbol] = v->m_initializer;
        }
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { gather_inits(c, out); });
    }

    template <typename F>
    static void visit_self_calls(const N::ASTNode* n, const S::Symbol* fn, F&& f) {
        if (!n || n->kind == K::LambdaExpression) return;
        if (n->kind == K::CallExpression && static_cast<const N::CallExpression*>(n)->resolved == fn) f(static_cast<const N::CallExpression*>(n));
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { visit_self_calls(c, fn, f); });
    }

    Growth loop_runs(N::ASTNode* loop, std::string& why) {
        switch (loop->kind) {
            case K::ForEachStatement: {
                auto* f = static_cast<N::ForEachStatement*>(loop);
                if (f->m_container && f->m_container->kind == K::BraceInitializerList) { why = "loops over a fixed list"; return constant(); }
                if (f->m_container && f->m_container->kind == K::CallExpression) {
                    auto* c = static_cast<N::CallExpression*>(f->m_container);
                    bool fixed = !c->m_arguments.empty();
                    for (N::ASTNode* a : c->m_arguments) if (!only_literals(a, {})) fixed = false;
                    if (fixed) { why = "loops a fixed number of times"; return constant(); }
                }
                why = "loops over every item";
                return linear();
            }
            case K::ForStatement: {
                auto* f = static_cast<N::ForStatement*>(loop);
                std::set<const S::Symbol*> counters;
                declared_counters(f->initializer, counters);
                declared_counters(f->var_init, counters);
                if (f->increment && halves(f->increment)) { why = "multiplies or divides its counter each time, so it runs about log n times"; return logarithmic(); }
                if (f->condition && only_literals(f->condition, counters) && (!f->initializer || only_literals_init(f->initializer)) && (!f->var_init || only_literals_init(f->var_init))) {
                    why = "runs a fixed number of times";
                    return constant();
                }
                why = "runs about n times";
                return linear();
            }
            case K::WhileStatement:
            case K::DoWhileStatement: {
                N::ASTNode* body = loop->kind == K::WhileStatement ? static_cast<N::WhileStatement*>(loop)->body : static_cast<N::DoWhileStatement*>(loop)->body;
                if (halves(body)) { why = "halves (or doubles) a value each time round, so it runs about log n times"; return logarithmic(); }
                why = "runs about n times";
                return linear();
            }
            default:
                why = "runs about n times";
                return linear();
        }
    }

    static bool only_literals_init(const N::ASTNode* n) {
        if (!n) return true;
        if (n->kind == K::VariableDeclaration) {
            auto* v = static_cast<const N::VariableDeclaration*>(n);
            return !v->m_initializer || only_literals(v->m_initializer, {});
        }
        bool ok = true;
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (ok && !only_literals_init(c)) ok = false; });
        return ok;
    }

    Cost library_call(const N::CallExpression* c) {
        Cost out;
        if (!c->m_callee || c->m_callee->kind != K::MemberAccessExpression) {
            std::string_view name = c->resolved ? c->resolved->name : std::string_view();
            if (name == "sort" || name == "stable_sort") out.take_time(n_log_n(), { { c->line, "sorting takes about n log n steps" } });
            else if (name == "find" || name == "find_if" || name == "count" || name == "count_if" || name == "reverse" || name == "copy" || name == "fill" || name == "accumulate" || name == "sum" || name == "min_element" || name == "max_element" || name == "contains" || name == "all_of" || name == "any_of" || name == "none_of" || name == "remove_if")
                out.take_time(linear(), { { c->line, "'" + std::string(name) + "' looks at every item" } });
            return out;
        }
        auto* m = static_cast<const N::MemberAccessExpression*>(c->m_callee);
        const std::string_view name = m->m_member;
        S::Type* t = strip(m->m_object ? m->m_object->expr_type.type : nullptr);
        const std::string type = t ? S::type_str_of(t) : std::string();
        const bool keyed = contains_any(type, { "Map", "Set", "Dictionary", "map", "set" });
        const bool hashed = contains_any(type, { "Hash", "Unordered", "unordered", "hash" });
        if (name == "sort" || name == "stable_sort") out.take_time(n_log_n(), { { c->line, "sorting takes about n log n steps" } });
        else if (keyed && (name == "find" || name == "contains" || name == "get" || name == "insert" || name == "erase" || name == "remove" || name == "count" || name == "at" || name == "set" || name == "put")) {
            if (!hashed) out.take_time(logarithmic(), { { c->line, "looking up a key in a sorted map or set takes about log n steps" } });
        } else if (!keyed && (name == "find" || name == "contains" || name == "index_of" || name == "count" || name == "remove" || name == "erase" || name == "reverse" || name == "replace" || name == "split" || name == "join" || name == "copy" || name == "fill" || name == "substr" || name == "to_string" || name == "includes" || name == "starts_with" || name == "ends_with"))
            out.take_time(linear(), { { c->line, "'" + std::string(name) + "' goes through the items one by one" } });
        else if (!keyed && (name == "insert" || name == "push_front" || name == "pop_front") && !contains_any(type, { "Deque", "List", "deque", "list" }))
            out.take_time(linear(), { { c->line, "'" + std::string(name) + "' in the middle or at the front moves the items after it" } });
        if (name == "resize" || name == "reserve" || name == "assign") {
            bool fixed = true;
            for (N::ASTNode* a : c->m_arguments) if (!only_literals(a, {})) fixed = false;
            if (!fixed) out.take_space(linear(), { { c->line, "'" + std::string(name) + "' makes room for about n items" } });
        }
        return out;
    }

    Cost cost(N::ASTNode* n) {
        Cost out;
        if (!n || n->kind == K::LambdaExpression || n->kind == K::FunctionDeclaration) return out;
        switch (n->kind) {
            case K::WhileStatement:
            case K::DoWhileStatement:
            case K::ForStatement:
            case K::ForEachStatement: {
                std::string why;
                const Growth runs = loop_runs(n, why);
                Cost inside;
                N::for_each_child(n, [&](N::ASTNode* c) { inside.absorb(cost(c)); });
                std::vector<ComplexityReason> reasons = { { n->line, "the loop on line " + std::to_string(n->line) + " " + why } };
                for (const ComplexityReason& r : inside.time_reasons) reasons.push_back(r);
                out.take_time(runs.times(inside.time), reasons);
                out.take_space(inside.space, inside.space_reasons);
                std::set<const S::Symbol*> grown;
                pushes_outside(n, n, grown);
                if (!grown.empty() && !runs.below(linear())) {
                    std::string names;
                    for (const S::Symbol* s : grown) { if (!names.empty()) names += ", "; names += "'" + std::string(s->name) + "'"; }
                    std::vector<ComplexityReason> space_why = { { n->line, names + " gets an item each time round the loop on line " + std::to_string(n->line) } };
                    out.take_space(runs, space_why);
                }
                return out;
            }
            case K::CallExpression: {
                auto* c = static_cast<N::CallExpression*>(n);
                N::for_each_child(n, [&](N::ASTNode* ch) { out.absorb(cost(ch)); });
                if (c->resolved && m_current && c->resolved == m_current->symbol) return out;
                auto it = c->resolved ? m_decls.find(c->resolved) : m_decls.end();
                bool fixed = !c->m_arguments.empty();
                for (N::ASTNode* a : c->m_arguments) if (!only_literals(a, {})) fixed = false;
                if (it != m_decls.end() && fixed) return out;
                if (it != m_decls.end()) {
                    const Cost& callee = of(it->second);
                    std::vector<ComplexityReason> why = { { c->line, "calls '" + display_name(it->second) + "', which is " + callee.time.text() } };
                    for (const ComplexityReason& r : callee.time_reasons) why.push_back(r);
                    out.take_time(callee.time, why);
                    std::vector<ComplexityReason> space_why = { { c->line, "calls '" + display_name(it->second) + "', which uses " + callee.space.text() + " memory" } };
                    for (const ComplexityReason& r : callee.space_reasons) space_why.push_back(r);
                    out.take_space(callee.space, space_why);
                } else {
                    out.absorb(library_call(c));
                }
                return out;
            }
            case K::NewExpression:
                break;
            default:
                break;
        }
        N::for_each_child(n, [&](N::ASTNode* c) { out.absorb(cost(c)); });
        return out;
    }

    void pushes_outside(const N::ASTNode* loop, const N::ASTNode* n, std::set<const S::Symbol*>& out) {
        if (!n || n->kind == K::LambdaExpression) return;
        if (n->kind == K::CallExpression) {
            auto* c = static_cast<const N::CallExpression*>(n);
            if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
                auto* m = static_cast<const N::MemberAccessExpression*>(c->m_callee);
                const S::Symbol* target = root_of(m->m_object);
                if (is_push_name(m->m_member) && target && !declared_inside(loop, target)) out.insert(target);
            }
        }
        if (n->kind == K::BinaryExpression) {
            auto* b = static_cast<const N::BinaryExpression*>(n);
            S::Type* t = strip(b->left ? b->left->expr_type.type : nullptr);
            const S::Symbol* target = root_of(b->left);
            if (b->op == TK::PlusEqual && t && t->is_builtin() && target && !declared_inside(loop, target)) {
                const std::string spelled = S::type_str_of(t);
                if (spelled == "string" || spelled == "text") out.insert(target);
            }
        }
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { pushes_outside(loop, c, out); });
    }

    static bool declared_inside(const N::ASTNode* n, const S::Symbol* s) {
        if (!n) return false;
        if (n->kind == K::VariableDeclaration && static_cast<const N::VariableDeclaration*>(n)->symbol == s) return true;
        bool found = false;
        N::for_each_child(const_cast<N::ASTNode*>(n), [&](N::ASTNode* c) { if (!found && declared_inside(c, s)) found = true; });
        return found;
    }

    const SourceManager& m_sources;
    std::unordered_map<const S::Symbol*, N::FunctionDeclaration*> m_decls;
    std::vector<std::pair<FileId, N::FunctionDeclaration*>> m_order;
    std::map<const N::FunctionDeclaration*, Cost> m_done;
    std::set<const N::FunctionDeclaration*> m_busy;
    std::set<const N::FunctionDeclaration*> m_recursive;
    N::FunctionDeclaration* m_current = nullptr;
};

}

std::vector<ComplexityEstimate> estimate_complexity(CompilerPipeline& compiler, const std::function<bool(FileId)>& own_file) {
    Analyzer a(compiler.get_sources());
    for (const CompilerPipeline::AnalyzedUnit& u : compiler.analyzed_units()) {
        if (!u.ast || !own_file(u.file)) continue;
        a.collect(u.file, u.ast);
    }
    return a.run();
}

} // namespace driver
} // namespace walnut
