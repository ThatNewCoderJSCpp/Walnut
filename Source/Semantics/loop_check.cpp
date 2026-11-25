#include "Semantics/type_walker.hpp"
#include "Semantics/semantic_error.hpp"
#include "Semantics/semantic_warning.hpp"
#include "Parser/Nodes/node_children.hpp"
#include <unordered_set>
#include <vector>
#include <string>

namespace walnut {
namespace semantics {

namespace {

using K = nodes::ASTNode::Kind;
using TK = tokenizing::Token::Kind;

struct LoopFacts {
    bool exits = false;
    bool calls = false;
    std::unordered_set<const Symbol*> changed;
};

const Symbol* root_symbol(const nodes::ASTNode* e) {
    while (e) {
        switch (e->kind) {
            case K::Identifier: return static_cast<const nodes::Identifier*>(e)->resolved;
            case K::MemberAccessExpression: {
                auto* m = static_cast<const nodes::MemberAccessExpression*>(e);
                if (m->m_op != nodes::MemberAccessExpression::Op::Dot) return nullptr;
                e = m->m_object;
                break;
            }
            case K::SubscriptExpression: e = static_cast<const nodes::SubscriptExpression*>(e)->m_array; break;
            default: return nullptr;
        }
    }
    return nullptr;
}

bool is_local_variable(const Symbol* s) {
    if (!s || (s->kind != SymbolKind::Variable && s->kind != SymbolKind::Parameter)) return false;
    if (!s->owner) return false;
    const auto k = s->owner->kind;
    if (k != Scope::Kind::Function && k != Scope::Kind::Block && k != Scope::Kind::Lambda) return false;
    if (!s->type) return false;
    if (s->type->has_reference()) return false;
    const auto& m = s->type->modifiers;
    if (m.has(modifiers::RawModifiers::Static) || m.has(modifiers::RawModifiers::ThreadLocal) || m.has(modifiers::RawModifiers::Volatile)) return false;
    if (m.has(modifiers::RawModifiers::Lazy) || m.has(modifiers::RawModifiers::LazyThreadsafe)) return false;
    return true;
}

bool is_loop_kind(K k) {
    return k == K::WhileStatement || k == K::DoWhileStatement || k == K::ForStatement || k == K::ForEachStatement;
}

std::string_view loop_label(const nodes::ASTNode* n) {
    switch (n->kind) {
        case K::WhileStatement: return static_cast<const nodes::WhileStatement*>(n)->label;
        case K::DoWhileStatement: return static_cast<const nodes::DoWhileStatement*>(n)->label;
        case K::ForStatement: return static_cast<const nodes::ForStatement*>(n)->label;
        case K::ForEachStatement: return static_cast<const nodes::ForEachStatement*>(n)->label;
        default: return {};
    }
}

class LoopScanner {
public:
    LoopScanner(LoopFacts& facts, std::string_view own_label) : m_facts(facts), m_own(own_label) {}

    void scan(nodes::ASTNode* n) {
        if (!n) return;
        switch (n->kind) {
            case K::ReturnStatement:
            case K::CoReturnStatement:
            case K::CoYieldExpression:
            case K::AwaitExpression:
            case K::ThrowExpression:
                m_facts.exits = true;
                break;
            case K::SingleStatement: {
                auto* s = static_cast<nodes::SingleStatement*>(n);
                using V = nodes::SingleStatement::Variant;
                if (s->variant == V::Goto) m_facts.exits = true;
                else if (s->variant == V::Break) {
                    if (s->label.empty()) { if (m_nested == 0) m_facts.exits = true; }
                    else if (!inner_label(s->label)) m_facts.exits = true;
                } else if (s->variant == V::Continue || s->variant == V::Repeat) {
                    if (!s->label.empty() && s->label != m_own && !inner_label(s->label)) m_facts.exits = true;
                }
                return;
            }
            case K::LambdaExpression:
                return;
            case K::CallExpression: {
                auto* c = static_cast<nodes::CallExpression*>(n);
                m_facts.calls = true;
                for (nodes::ASTNode* a : c->m_arguments) {
                    const nodes::ASTNode* arg = a;
                    if (arg && arg->kind == K::ReferenceExpression) arg = static_cast<const nodes::ReferenceExpression*>(arg)->operand;
                    if (const Symbol* r = root_symbol(arg)) m_facts.changed.insert(r);
                }
                if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
                    auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
                    if (m->m_op == nodes::MemberAccessExpression::Op::Dot) {
                        if (const Symbol* r = root_symbol(m->m_object)) m_facts.changed.insert(r);
                    }
                }
                break;
            }
            case K::NewExpression:
            case K::DeleteExpression:
            case K::BraceConstructExpression:
                m_facts.calls = true;
                if (n->kind == K::DeleteExpression) {
                    nodes::for_each_child(n, [&](nodes::ASTNode* c) { if (const Symbol* r = root_symbol(c)) m_facts.changed.insert(r); });
                }
                break;
            case K::BinaryExpression: {
                auto* b = static_cast<nodes::BinaryExpression*>(n);
                if (b->resolved) m_facts.calls = true;
                if (is_assignment_op(b->op)) {
                    if (const Symbol* r = root_symbol(b->left)) m_facts.changed.insert(r);
                }
                break;
            }
            case K::UnaryExpression: {
                auto* u = static_cast<nodes::UnaryExpression*>(n);
                if (u->resolved) m_facts.calls = true;
                if (u->op == TK::DoublePlus || u->op == TK::DoubleMinus) {
                    if (const Symbol* r = root_symbol(u->operand)) m_facts.changed.insert(r);
                }
                break;
            }
            case K::ReferenceExpression: {
                if (const Symbol* r = root_symbol(static_cast<nodes::ReferenceExpression*>(n)->operand)) m_facts.changed.insert(r);
                break;
            }
            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(n);
                if (v->m_type_info.has_reference() || !v->m_bindings.empty()) {
                    if (const Symbol* r = root_symbol(v->m_initializer)) m_facts.changed.insert(r);
                }
                break;
            }
            case K::ForEachStatement: {
                auto* f = static_cast<nodes::ForEachStatement*>(n);
                if (const Symbol* r = root_symbol(f->m_container)) m_facts.changed.insert(r);
                break;
            }
            case K::SwitchStatement:
                ++m_nested;
                nodes::for_each_child(n, [&](nodes::ASTNode* c) { scan(c); });
                --m_nested;
                return;
            default:
                break;
        }

        if (is_loop_kind(n->kind)) {
            ++m_nested;
            m_inner_labels.push_back(loop_label(n));
            nodes::for_each_child(n, [&](nodes::ASTNode* c) { scan(c); });
            m_inner_labels.pop_back();
            --m_nested;
            return;
        }

        nodes::for_each_child(n, [&](nodes::ASTNode* c) { scan(c); });
    }

private:
    bool inner_label(std::string_view l) const {
        for (std::string_view x : m_inner_labels) if (!x.empty() && x == l) return true;
        return false;
    }

    LoopFacts& m_facts;
    std::string_view m_own;
    int m_nested = 0;
    std::vector<std::string_view> m_inner_labels;
};

void collect_escapes(nodes::ASTNode* n, std::unordered_set<const Symbol*>& out, bool& all_escape) {
    if (!n) return;
    if (n->kind == K::ReferenceExpression) {
        if (const Symbol* r = root_symbol(static_cast<nodes::ReferenceExpression*>(n)->operand)) out.insert(r);
    } else if (n->kind == K::LambdaExpression) {
        auto* l = static_cast<nodes::LambdaExpression*>(n);
        if (l->m_captures) {
            for (const nodes::LambdaCaptureItem& c : l->m_captures->captures()) {
                using M = nodes::LambdaCaptureItem::Mode;
                if (c.mode == M::AllByReference) all_escape = true;
                else if (c.mode == M::ByReference && c.resolved) out.insert(c.resolved);
            }
        }
    } else if (n->kind == K::VariableDeclaration) {
        auto* v = static_cast<nodes::VariableDeclaration*>(n);
        if (v->m_type_info.has_reference() || v->m_type_info.has_pointer() || !v->m_bindings.empty()) {
            const nodes::ASTNode* init = v->m_initializer;
            if (const Symbol* r = root_symbol(init)) out.insert(r);
        }
    }
    nodes::for_each_child(n, [&](nodes::ASTNode* c) { collect_escapes(c, out, all_escape); });
}

bool pure_condition(const nodes::ASTNode* n, std::vector<const Symbol*>& vars) {
    if (!n) return true;
    switch (n->kind) {
        case K::Literal: return true;
        case K::Identifier: {
            const Symbol* s = static_cast<const nodes::Identifier*>(n)->resolved;
            if (!s) return false;
            if (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter) {
                if (!is_local_variable(s)) return false;
                vars.push_back(s);
                return true;
            }
            return s->kind == SymbolKind::EnumConstant;
        }
        case K::BinaryExpression: {
            auto* b = static_cast<const nodes::BinaryExpression*>(n);
            if (b->resolved || is_assignment_op(b->op)) return false;
            return pure_condition(b->left, vars) && pure_condition(b->right, vars);
        }
        case K::UnaryExpression: {
            auto* u = static_cast<const nodes::UnaryExpression*>(n);
            if (u->resolved || u->op == TK::DoublePlus || u->op == TK::DoubleMinus) return false;
            return pure_condition(u->operand, vars);
        }
        case K::BitwiseNotExpression: {
            bool ok = true;
            nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { ok = ok && pure_condition(c, vars); });
            return ok;
        }
        case K::TernaryExpression: {
            bool ok = true;
            nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { ok = ok && pure_condition(c, vars); });
            return ok;
        }
        case K::CastExpression: {
            bool ok = true;
            nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { ok = ok && pure_condition(c, vars); });
            return ok;
        }
        default: return false;
    }
}

std::string name_list(const std::vector<const Symbol*>& vars) {
    std::vector<std::string_view> names;
    for (const Symbol* s : vars) {
        bool seen = false;
        for (std::string_view n : names) if (n == s->name) seen = true;
        if (!seen) names.push_back(s->name);
    }
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) out += i + 1 == names.size() ? " or " : ", ";
        out += "'" + std::string(names[i]) + "'";
    }
    return out;
}

int step_direction(const nodes::ASTNode* inc, const Symbol* var, ConstEvaluator& eval) {
    if (!inc) return 0;
    if (inc->kind == K::UnaryExpression) {
        auto* u = static_cast<const nodes::UnaryExpression*>(inc);
        if (u->resolved || root_symbol(u->operand) != var || u->operand->kind != K::Identifier) return 0;
        if (u->op == TK::DoublePlus) return 1;
        if (u->op == TK::DoubleMinus) return -1;
        return 0;
    }
    if (inc->kind == K::BinaryExpression) {
        auto* b = static_cast<const nodes::BinaryExpression*>(inc);
        if (b->resolved || !b->left || b->left->kind != K::Identifier || root_symbol(b->left) != var) return 0;
        if (b->op != TK::PlusEqual && b->op != TK::MinusEqual) return 0;
        ConstValue v = eval.eval(b->right);
        int sign = 0;
        if (v.is_int() && v.i) sign = *v.i > WideInt(0) ? 1 : (*v.i < WideInt(0) ? -1 : 0);
        else if (v.is_float() && v.f) return 0;
        if (b->op == TK::MinusEqual) sign = -sign;
        return sign;
    }
    return 0;
}

std::uint32_t first_line(const nodes::ASTNode* n) {
    if (!n) return 0;
    if (n->line) return n->line;
    std::uint32_t found = 0;
    nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { if (!found) found = first_line(c); });
    return found;
}

bool mentions(const nodes::ASTNode* n, const Symbol* s) {
    if (!n) return false;
    if (n->kind == K::Identifier && static_cast<const nodes::Identifier*>(n)->resolved == s) return true;
    bool found = false;
    nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { if (!found && mentions(c, s)) found = true; });
    return found;
}

}

bool TypeWalker::unsigned_never_negative(nodes::ASTNode* cond) {
    if (!cond || cond->kind != K::BinaryExpression) return false;
    auto* b = static_cast<nodes::BinaryExpression*>(cond);
    if (b->resolved) return false;
    nodes::ASTNode* value = nullptr;
    nodes::ASTNode* zero = nullptr;
    if (b->op == TK::GreaterEqual) { value = b->left; zero = b->right; }
    else if (b->op == TK::LessEqual) { value = b->right; zero = b->left; }
    else return false;
    if (!value || !zero || zero->kind != K::Literal) return false;
    ConstValue z = m_eval.eval(zero);
    if (!z.is_int() || !z.i || *z.i != WideInt(0)) return false;
    Type* t = type_of(value);
    if (t && t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    if (!t || !t->is_builtin()) return false;
    auto* bt = static_cast<BuiltinType*>(t);
    return bt->is_unsigned() && !bt->is_bool() && !bt->is_void() && !bt->is_dynamic();
}

void TypeWalker::check_loop(nodes::ASTNode* loop) {
    if (!loop) return;

    nodes::ASTNode* cond = nullptr;
    nodes::ASTNode* body = nullptr;
    nodes::ASTNode* step = nullptr;
    bool has_cond = true;

    switch (loop->kind) {
        case K::WhileStatement: {
            auto* w = static_cast<nodes::WhileStatement*>(loop);
            cond = w->condition; body = w->body;
            break;
        }
        case K::DoWhileStatement: {
            auto* d = static_cast<nodes::DoWhileStatement*>(loop);
            cond = d->condition; body = d->body;
            break;
        }
        case K::ForStatement: {
            auto* f = static_cast<nodes::ForStatement*>(loop);
            cond = f->condition; body = f->body; step = f->increment;
            has_cond = cond != nullptr;
            break;
        }
        default: return;
    }

    if (cond && cond->kind == K::VariableDeclaration) return;
    std::uint32_t at = loop->line;
    if (!at) at = first_line(cond);
    if (!at) at = first_line(step);
    if (!at) at = first_line(body);

    LoopFacts body_facts;
    {
        LoopScanner scanner(body_facts, loop_label(loop));
        scanner.scan(body);
    }
    LoopFacts all = body_facts;
    {
        LoopScanner scanner(all, loop_label(loop));
        scanner.scan(step);
        scanner.scan(cond);
    }

    const std::string kind_word = loop->kind == K::ForStatement ? "for loop" : loop->kind == K::DoWhileStatement ? "do-while loop" : "while loop";

    const bool never_negative = has_cond && unsigned_never_negative(cond);
    if (never_negative) {
        SemanticWarning::emit(m_warnings, file_of(cond), cond->line, "Loop", "an unsigned value is never below 0, so this condition is always true");
    }

    const bool always_true = !has_cond || never_negative || constant_true(cond);

    if (always_true) {
        if (all.exits) return;
        if (!all.calls) {
            SemanticError::emit(m_reporter, file_of(loop), at, "Loop", "this " + kind_word + " never ends: its condition is always true, and nothing in it can leave the loop (no break, return, throw or goto, and no calls that could throw)");
        } else {
            SemanticWarning::emit(m_warnings, file_of(loop), at, "Loop", "this " + kind_word + " may never end: its condition is always true and it has no break, return, throw or goto, so it only stops if something it calls throws or ends the program");
        }
        return;
    }

    if (all.exits) return;

    std::vector<const Symbol*> vars;
    if (!pure_condition(cond, vars) || vars.empty()) return;

    std::unordered_set<const Symbol*> escaped;
    bool all_escape = false;
    if (!m_returns.empty() && m_returns.back().site) collect_escapes(m_returns.back().site, escaped, all_escape);
    if (all_escape) return;

    if (loop->kind == K::ForStatement && step && cond->kind == K::BinaryExpression) {
        auto* b = static_cast<nodes::BinaryExpression*>(cond);
        const Symbol* var = nullptr;
        nodes::ASTNode* bound = nullptr;
        int need = 0;
        const Symbol* l = b->left && b->left->kind == K::Identifier ? static_cast<nodes::Identifier*>(b->left)->resolved : nullptr;
        const Symbol* r = b->right && b->right->kind == K::Identifier ? static_cast<nodes::Identifier*>(b->right)->resolved : nullptr;
        if (b->op == TK::LessThan || b->op == TK::LessEqual) {
            if (l && is_local_variable(l)) { var = l; bound = b->right; need = 1; }
            else if (r && is_local_variable(r)) { var = r; bound = b->left; need = -1; }
        } else if (b->op == TK::GreaterThan || b->op == TK::GreaterEqual) {
            if (l && is_local_variable(l)) { var = l; bound = b->right; need = -1; }
            else if (r && is_local_variable(r)) { var = r; bound = b->left; need = 1; }
        }
        if (var && bound && !mentions(bound, var) && !escaped.count(var)) {
            const int dir = step_direction(step, var, m_eval);
            if (dir != 0 && dir != need) {
                std::vector<const Symbol*> bound_vars;
                const bool bound_fixed = pure_condition(bound, bound_vars);
                bool bound_changes = !bound_fixed;
                for (const Symbol* s : bound_vars) if (body_facts.changed.count(s) || escaped.count(s)) bound_changes = true;
                if (!bound_changes && !body_facts.changed.count(var)) {
                    const std::string msg = "'" + std::string(var->name) + "' moves away from the bound each time round, so once this for loop starts it never ends";
                    if (!all.calls) SemanticError::emit(m_reporter, file_of(loop), at, "Loop", msg);
                    else SemanticWarning::emit(m_warnings, file_of(loop), at, "Loop", msg + " (unless something it calls throws or ends the program)");
                    return;
                }
            }
        }
    }

    std::vector<const Symbol*> unchanged;
    for (const Symbol* s : vars) {
        if (all.changed.count(s) || escaped.count(s)) return;
        unchanged.push_back(s);
    }

    const std::string names = name_list(unchanged);
    if (!all.calls) {
        SemanticError::emit(m_reporter, file_of(loop), at, "Loop", "this " + kind_word + " never ends once it starts: nothing in it changes " + names + ", and it has no break, return, throw or goto");
    } else {
        SemanticWarning::emit(m_warnings, file_of(loop), at, "Loop", "this " + kind_word + " may never end: nothing in it changes " + names + " and it has no break, return, throw or goto, so it only stops if something it calls throws or ends the program");
    }
}

namespace {

bool calls_symbol(const nodes::ASTNode* n, const Symbol* fn) {
    if (!n) return false;
    switch (n->kind) {
        case K::LambdaExpression: return false;
        case K::TernaryExpression: {
            bool found = false;
            bool first = true;
            nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) {
                if (first) { first = false; if (calls_symbol(c, fn)) found = true; }
            });
            return found;
        }
        case K::BinaryExpression: {
            auto* b = static_cast<const nodes::BinaryExpression*>(n);
            if (b->op == TK::LogicAnd || b->op == TK::LogicOr || b->op == TK::DoubleQuestion) return calls_symbol(b->left, fn);
            break;
        }
        case K::CallExpression: {
            auto* c = static_cast<const nodes::CallExpression*>(n);
            if (c->resolved == fn) return true;
            break;
        }
        default: break;
    }
    bool found = false;
    nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { if (!found && calls_symbol(c, fn)) found = true; });
    return found;
}

bool may_leave(const nodes::ASTNode* n) {
    if (!n) return false;
    switch (n->kind) {
        case K::ReturnStatement:
        case K::CoReturnStatement:
        case K::CoYieldExpression:
        case K::AwaitExpression:
        case K::ThrowExpression:
            return true;
        case K::SingleStatement:
            return static_cast<const nodes::SingleStatement*>(n)->variant == nodes::SingleStatement::Variant::Goto;
        case K::LambdaExpression:
            return false;
        default: break;
    }
    bool found = false;
    nodes::for_each_child(const_cast<nodes::ASTNode*>(n), [&](nodes::ASTNode* c) { if (!found && may_leave(c)) found = true; });
    return found;
}

}

void TypeWalker::check_self_recursion(const Symbol* fn, nodes::ASTNode* body, nodes::ASTNode* site) {
    if (!fn || !body || body->kind != K::BlockStatement || !site) return;
    for (nodes::ASTNode* s : static_cast<nodes::BlockStatement*>(body)->get_statements()) {
        if (!s) continue;
        const bool plain = s->kind == K::ExpressionStatement || s->kind == K::VariableDeclaration || s->kind == K::ReturnStatement;
        if (plain && calls_symbol(s, fn)) {
            SemanticError::emit(m_reporter, file_of(s), s->line, "Recursion", "'" + std::string(fn->name) + "' always calls itself here before it can return, so every call recurses forever; add a case that returns without calling '" + std::string(fn->name) + "' again");
            return;
        }
        if (may_leave(s)) return;
    }
}

} // namespace semantics
} // namespace walnut
