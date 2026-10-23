#include "Semantics/ownership.hpp"
#include "Semantics/scope.hpp"
#include "Parser/Nodes/node_children.hpp"

#include <algorithm>
#include <utility>

namespace walnut {
namespace semantics {

namespace {

using K = nodes::ASTNode::Kind;

std::string quoted(std::string_view name) { return "'" + std::string(name) + "'"; }

} // namespace

void OwnershipChecker::run(FileId file, nodes::ASTNode* root, bool every_function) {
    m_file = file;
    m_every_function = every_function;
    find(root);
}

bool OwnershipChecker::wanted(const nodes::ASTNode* f) const {
    return m_every_function || nodes::in_safe_code(f);
}

void OwnershipChecker::find(nodes::ASTNode* n) {
    if (!n) return;
    switch (n->kind) {
        case K::FunctionDeclaration: {
            auto* f = static_cast<nodes::FunctionDeclaration*>(n);
            if (f->m_body && wanted(f)) { m_strict = nodes::in_safe_code(f); check_function(f->m_parameters, f->m_body, &f->m_return_type); m_strict = false; }
            break;
        }
        case K::OperatorFunctionDeclaration: {
            auto* f = static_cast<nodes::OperatorFunctionDeclaration*>(n);
            if (f->m_body && wanted(f)) { m_strict = nodes::in_safe_code(f); check_function(f->m_parameters, f->m_body, &f->m_return_type); m_strict = false; }
            break;
        }
        case K::ConstructorDeclaration: {
            auto* f = static_cast<nodes::ConstructorDeclaration*>(n);
            if (f->m_body && wanted(f)) { m_strict = nodes::in_safe_code(f); check_function(f->m_parameters, f->m_body, nullptr); m_strict = false; }
            break;
        }
        case K::DestructorDeclaration: {
            auto* f = static_cast<nodes::DestructorDeclaration*>(n);
            if (f->m_body && wanted(f)) { m_strict = nodes::in_safe_code(f); check_function(nullptr, f->m_body, nullptr); m_strict = false; }
            break;
        }
        case K::LambdaExpression: {
            auto* f = static_cast<nodes::LambdaExpression*>(n);
            if (f->m_body && wanted(f)) { m_strict = nodes::in_safe_code(f); check_function(f->m_parameters, f->m_body, &f->m_return_type); m_strict = false; }
            break;
        }
        default:
            break;
    }
    nodes::for_each_child(n, [this](nodes::ASTNode* c) { find(c); });
}

void OwnershipChecker::check_function(nodes::FunctionParameters* params, nodes::ASTNode* body, const parser_types::TypeInfo* ret) {
    m_flow = Flow{};
    m_blocks.clear();
    m_blocks.emplace_back();
    m_jumps.clear();
    m_gave_up = false;
    m_next_heap = 0;
    m_returns_ref = ret && ret->has_reference();
    m_returns_ptr = ret && !m_returns_ref && ret->has_pointer();

    if (params) {
        for (nodes::FunctionParameter* p : params->m_params) {
            if (!p || !p->symbol) continue;
            declare(p->symbol, !p->m_type.has_reference(), false, p->line);
            set_kind(p->symbol, p->m_type);
        }
    }

    if (body->kind == K::BlockStatement) {
        for (nodes::ASTNode* s : static_cast<nodes::BlockStatement*>(body)->statements) statement(s);
    } else {
        expr(body, Use::Escape);
    }

    if (m_flow.live) leaks_at_exit(nullptr);
    m_blocks.clear();
}

void OwnershipChecker::declare(Symbol* sym, bool stack, bool array, std::uint32_t line) {
    if (!sym) return;
    Var v;
    v.depth = m_blocks.size();
    v.stack = stack;
    v.array = array;
    v.decl_line = line;
    v.at = line;
    m_flow.vars[sym] = v;
    if (m_blocks.empty()) m_blocks.emplace_back();
    m_blocks.back().push_back(sym);
}

void OwnershipChecker::open_block() { m_blocks.emplace_back(); }

void OwnershipChecker::close_block() {
    if (m_blocks.empty()) return;
    std::vector<Symbol*> ending = std::move(m_blocks.back());
    m_blocks.pop_back();

    for (Symbol* sym : ending) {
        auto it = m_flow.vars.find(sym);
        if (it == m_flow.vars.end()) continue;
        const Var v = it->second;
        if (m_flow.live && v.owns_new && v.life == Life::Live) {
            warn(v.new_line, quoted(sym->name) + " holds memory from 'new' that is never deleted before its block ends");
        }
        m_flow.vars.erase(it);
        if (!v.stack) continue;
        for (auto& [other, ov] : m_flow.vars) {
            if (ov.points_to == sym && ov.life != Life::Moved && ov.life != Life::Deleted) {
                ov.life = Life::Dangling;
                ov.reported = false;
                ov.at = v.decl_line;
            }
            for (Held& h : ov.held) {
                if (h.target != sym || h.dead) continue;
                h.dead = true;
                h.declared = v.decl_line;
                h.reported = false;
            }
        }
    }
}

void OwnershipChecker::block(nodes::BlockStatement* b) {
    open_block();
    for (nodes::ASTNode* s : b->statements) statement(s);
    close_block();
}

void OwnershipChecker::statement(nodes::ASTNode* s) {
    if (!s || !m_flow.live) return;
    switch (s->kind) {
        case K::BlockStatement:
            block(static_cast<nodes::BlockStatement*>(s));
            break;
        case K::ExpressionStatement:
            expr(static_cast<nodes::ExpressionStatement*>(s)->expr, Use::Read);
            break;
        case K::VariableDeclaration:
            declaration(static_cast<nodes::VariableDeclaration*>(s));
            break;
        case K::ArrayDeclaration: {
            auto* a = static_cast<nodes::ArrayDeclaration*>(s);
            expr(a->m_dimension_expr, Use::Read);
            expr(a->m_initializer, Use::Escape);
            declare(a->symbol, !a->m_array_modifiers.has(modifiers::RawModifiers::Static), true, a->line);
            break;
        }
        case K::ReturnStatement:
            return_statement(static_cast<nodes::ReturnStatement*>(s));
            break;
        case K::IfStatement:
            if_statement(static_cast<nodes::IfStatement*>(s));
            break;
        case K::WhileStatement: {
            auto* w = static_cast<nodes::WhileStatement*>(s);
            loop(w->condition, w->body, nullptr, false);
            break;
        }
        case K::DoWhileStatement: {
            auto* w = static_cast<nodes::DoWhileStatement*>(s);
            loop(nullptr, w->body, w->condition, true);
            break;
        }
        case K::ForStatement: {
            auto* f = static_cast<nodes::ForStatement*>(s);
            open_block();
            if (f->var_init) statement(f->var_init);
            if (f->initializer) {
                if (f->initializer->kind == K::VariableDeclaration || f->initializer->kind == K::BlockStatement) statement(f->initializer);
                else expr(f->initializer, Use::Read);
            }
            loop(f->condition, f->body, f->increment, false);
            close_block();
            break;
        }
        case K::ForEachStatement: {
            auto* f = static_cast<nodes::ForEachStatement*>(s);
            expr(f->m_container, Use::Read);
            open_block();
            declare(f->symbol, !f->m_element_type.has_reference(), false, f->line);
            set_kind(f->symbol, f->m_element_type);
            {
                std::string path;
                bool local = false;
                Symbol* root = holder_root(f->m_container, path, local);
                Var* holder = local ? var(root) : nullptr;
                Var* element = var(f->symbol);
                if (holder && element && element->pointer) {
                    for (const Held& h : holder->held) {
                        if (!h.dead || h.path != path + "[]") continue;
                        element->life = Life::Dangling;
                        element->points_to = h.target;
                        element->at = h.declared;
                        element->reported = false;
                        break;
                    }
                }
            }
            loop(nullptr, f->m_body, nullptr, false);
            close_block();
            break;
        }
        case K::SwitchStatement:
            switch_statement(static_cast<nodes::SwitchStatement*>(s));
            break;
        case K::TryCatchStatement:
            try_statement(static_cast<nodes::TryCatchStatement*>(s));
            break;
        case K::SingleStatement:
            jump(static_cast<nodes::SingleStatement*>(s));
            break;
        case K::FunctionDeclaration:
        case K::OperatorFunctionDeclaration:
        case K::ConstructorDeclaration:
        case K::DestructorDeclaration:
        case K::RecordDeclaration:
        case K::NamespaceDeclaration:
        case K::TemplateDeclaration:
            break;
        default:
            expr(s, Use::Read);
            break;
    }
}

void OwnershipChecker::declaration(nodes::VariableDeclaration* d) {
    const bool is_static = d->m_type_info.modifiers.has(modifiers::RawModifiers::Static);
    if (!d->symbol) {
        expr(d->m_initializer, Use::Escape);
        for (Symbol* b : d->binding_symbols) declare(b, !is_static && !d->binding_by_ref, false, d->line);
        return;
    }
    declare(d->symbol, !is_static && !d->m_type_info.has_reference(), false, d->line);
    set_kind(d->symbol, d->m_type_info);
    if (d->m_initializer) initialize(d->symbol, d->m_initializer, d->line, false);
}

void OwnershipChecker::if_statement(nodes::IfStatement* s) {
    std::vector<Flow> outs;
    Flow next = m_flow;
    for (nodes::IfBranch* b : s->branches) {
        if (!b) continue;
        m_flow = next;
        expr(b->condition, Use::Read);
        next = m_flow;
        statement(b->body);
        outs.push_back(m_flow);
    }
    m_flow = next;
    if (s->else_branch) statement(s->else_branch);
    outs.push_back(m_flow);
    m_flow = join(outs);
}

void OwnershipChecker::loop(nodes::ASTNode* before, nodes::ASTNode* body, nodes::ASTNode* after, bool body_first) {
    std::vector<Flow> exits;
    const auto pass = [&]() {
        m_jumps.push_back(Jump{});
        if (before) expr(before, Use::Read);
        if (!body_first) exits.push_back(m_flow);
        statement(body);
        std::vector<Flow> ends = m_jumps.back().continues;
        ends.push_back(m_flow);
        m_flow = join(ends);
        if (after) {
            if (after->kind == K::BlockStatement) statement(after);
            else expr(after, Use::Read);
        }
        if (body_first) exits.push_back(m_flow);
        for (Flow& b : m_jumps.back().breaks) exits.push_back(std::move(b));
        m_jumps.pop_back();
    };
    pass();
    if (m_flow.live) pass();
    m_flow = join(exits);
}

void OwnershipChecker::switch_statement(nodes::SwitchStatement* s) {
    expr(s->condition, Use::Read);
    const Flow pre = m_flow;
    std::vector<Flow> outs;
    bool has_default = false;
    bool falling = false;
    Flow carried;
    m_jumps.push_back(Jump{ true, {}, {} });
    for (nodes::SwitchCase* c : s->cases) {
        if (!c) continue;
        if (!c->value) has_default = true;
        if (falling && carried.live) m_flow = join({ pre, carried });
        else m_flow = pre;
        open_block();
        for (nodes::ASTNode* st : c->body) statement(st);
        close_block();
        falling = c->has_fallthrough;
        if (falling) carried = m_flow;
        else outs.push_back(m_flow);
    }
    if (falling) outs.push_back(carried);
    for (Flow& b : m_jumps.back().breaks) outs.push_back(std::move(b));
    std::vector<Flow> continues = std::move(m_jumps.back().continues);
    m_jumps.pop_back();
    if (!continues.empty()) {
        for (auto it = m_jumps.rbegin(); it != m_jumps.rend(); ++it) {
            if (it->is_switch) continue;
            for (Flow& f : continues) it->continues.push_back(std::move(f));
            break;
        }
    }
    if (!has_default) outs.push_back(pre);
    m_flow = join(outs);
}

void OwnershipChecker::try_statement(nodes::TryCatchStatement* t) {
    const Flow pre = m_flow;
    if (t->try_body) block(t->try_body);
    const Flow after_try = m_flow;
    const Flow catch_start = join({ pre, after_try });
    std::vector<Flow> outs{ after_try };
    for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) {
        m_flow = catch_start;
        if (h->catch_body) block(h->catch_body);
        outs.push_back(m_flow);
    }
    m_flow = join(outs);
}

void OwnershipChecker::jump(nodes::SingleStatement* s) {
    using V = nodes::SingleStatement::Variant;
    switch (s->variant) {
        case V::Break:
            if (!m_jumps.empty()) m_jumps.back().breaks.push_back(m_flow);
            m_flow.live = false;
            break;
        case V::Continue:
            for (auto it = m_jumps.rbegin(); it != m_jumps.rend(); ++it) {
                if (it->is_switch) continue;
                it->continues.push_back(m_flow);
                break;
            }
            m_flow.live = false;
            break;
        case V::Goto:
        case V::Label:
        case V::Repeat:
            m_gave_up = true;
            break;
        default:
            break;
    }
}

void OwnershipChecker::return_statement(nodes::ReturnStatement* r) {
    nodes::ASTNode* value = r->m_value;
    Symbol* keep = nullptr;
    if (value) {
        Symbol* direct = symbol_of(value);
        if (m_returns_ref && direct) {
            if (Var* v = var(direct); v && v->stack) {
                error(r->line, "returns a reference to local " + quoted(direct->name) + ", which no longer exists once the function returns");
            }
        } else if (m_returns_ref) {
            if (Symbol* base = local_value_base(value)) {
                error(r->line, "returns a reference into local " + quoted(base->name) + ", which no longer exists once the function returns");
            }
        }
        if (m_returns_ptr) {
            if (value->kind == K::ReferenceExpression) {
                nodes::ASTNode* operand = static_cast<nodes::ReferenceExpression*>(value)->operand;
                Symbol* target = symbol_of(operand);
                if (Var* v = var(target); v && v->stack) {
                    error(r->line, "returns the address of local " + quoted(target->name) + ", which no longer exists once the function returns");
                } else if (Symbol* base = local_value_base(operand)) {
                    error(r->line, "returns an address inside local " + quoted(base->name) + ", which no longer exists once the function returns");
                }
            } else if (direct) {
                if (Var* v = var(direct)) {
                    if (v->array && v->stack) {
                        error(r->line, "returns local array " + quoted(direct->name) + ", which no longer exists once the function returns");
                    } else if (v->points_to && v->life == Life::Live) {
                        if (Var* t = var(v->points_to); t && t->stack) {
                            error(r->line, "returns " + quoted(direct->name) + ", which points to local " + quoted(v->points_to->name) + " that no longer exists once the function returns");
                        }
                    }
                }
            }
        }
        if (Var* v = var(direct); v && v->stack && !v->pointer) {
            for (const Held& h : v->held) {
                Var* t = var(h.target);
                if (h.dead || !t || !t->stack) continue;
                error(r->line, "returns " + quoted(direct->name) + ", but its '" + h.path + "' points to local " + quoted(h.target->name) + ", which no longer exists once the function returns");
                break;
            }
        }
        keep = direct;
        expr(value, Use::Escape);
    }
    if (m_flow.live) leaks_at_exit(keep);
    m_flow.live = false;
}

void OwnershipChecker::leaks_at_exit(Symbol* keep) {
    for (auto& [sym, v] : m_flow.vars) {
        if (sym == keep || !v.owns_new || v.life != Life::Live) continue;
        warn(v.new_line, quoted(sym->name) + " holds memory from 'new' that is never deleted or handed off");
    }
}

void OwnershipChecker::expr(nodes::ASTNode* e, Use use) {
    if (!e) return;
    const Use passed = (use == Use::Escape || use == Use::Deref) ? use : Use::Read;
    switch (e->kind) {
        case K::Identifier: {
            touch(static_cast<nodes::Identifier*>(e)->resolved, use, e->line);
            return;
        }
        case K::DereferenceExpression:
            check_held(static_cast<nodes::DereferenceExpression*>(e)->operand, e->line);
            expr(static_cast<nodes::DereferenceExpression*>(e)->operand, Use::Deref);
            return;
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            if (m->m_op == nodes::MemberAccessExpression::Op::Scope) return;
            if (m->m_op == nodes::MemberAccessExpression::Op::Arrow) check_held(m->m_object, e->line);
            expr(m->m_object, m->m_op == nodes::MemberAccessExpression::Op::Arrow ? Use::Deref : Use::Read);
            return;
        }
        case K::SubscriptExpression: {
            auto* s = static_cast<nodes::SubscriptExpression*>(e);
            check_held(s->m_array, e->line);
            expr(s->m_array, Use::Deref);
            expr(s->m_index, Use::Read);
            return;
        }
        case K::ReferenceExpression:
            expr(static_cast<nodes::ReferenceExpression*>(e)->operand, use == Use::Escape ? Use::Escape : Use::Address);
            return;
        case K::CallExpression: {
            auto* c = static_cast<nodes::CallExpression*>(e);
            if (is_move(c) && c->m_arguments.size() == 1) {
                nodes::ASTNode* arg = c->m_arguments[0];
                expr(arg, Use::Read);
                if (Var* v = var(symbol_of(arg))) {
                    v->life = Life::Moved;
                    v->reported = false;
                    v->at = e->line;
                    v->owns_new = false;
                    v->points_to = nullptr;
                }
                return;
            }
            if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
                auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
                if (m->m_op != nodes::MemberAccessExpression::Op::Scope) {
                    if (stores_argument(m->m_member)) {
                        for (nodes::ASTNode* a : c->m_arguments) {
                            if (stored_target(a)) store(m->m_object, a, true, e->line);
                        }
                    } else if (m->m_member == "clear") {
                        forget_stored(m->m_object, true);
                    }
                }
            }
            expr(c->m_callee, Use::Read);
            for (nodes::ASTNode* a : c->m_arguments) expr(a, Use::Escape);
            return;
        }
        case K::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);
            if (b->op == tokenizing::Token::Kind::Equal) {
                assign(b->left, b->right, e->line);
                return;
            }
            expr(b->left, passed);
            expr(b->right, passed);
            return;
        }
        case K::NewExpression: {
            auto* n = static_cast<nodes::NewExpression*>(e);
            for (nodes::ASTNode* a : n->args) expr(a, Use::Escape);
            expr(n->array_size, Use::Read);
            return;
        }
        case K::DeleteExpression:
            delete_expression(static_cast<nodes::DeleteExpression*>(e));
            return;
        case K::LambdaExpression:
            escape_all(e);
            return;
        default:
            break;
    }
    nodes::for_each_child(e, [this, passed](nodes::ASTNode* c) { expr(c, passed); });
}

void OwnershipChecker::touch(Symbol* sym, Use use, std::uint32_t line) {
    Var* v = var(sym);
    if (!v || use == Use::Address) return;
    if (v->reported) {
    } else if (v->life == Life::Moved) {
        error(line, quoted(sym->name) + " is used after it was moved on line " + std::to_string(v->at));
        v->reported = true;
    } else if (v->life == Life::Deleted && (use == Use::Deref || v->reference)) {
        if (v->reference) error(line, quoted(sym->name) + " refers to memory that was deleted on line " + std::to_string(v->at) + (v->deleted_via ? " through " + quoted(v->deleted_via->name) : std::string()));
        else if (v->deleted_via) error(line, quoted(sym->name) + " is used after the memory it points to was deleted through " + quoted(v->deleted_via->name) + " on line " + std::to_string(v->at));
        else error(line, quoted(sym->name) + " is used after it was deleted on line " + std::to_string(v->at));
        v->reported = true;
    } else if (v->life == Life::Dangling && use == Use::Deref) {
        const std::string target = v->points_to ? quoted(v->points_to->name) : std::string("a local");
        error(line, quoted(sym->name) + " points to " + target + " (declared on line " + std::to_string(v->at) + "), which no longer exists after its block ended");
        v->reported = true;
    }
    if (use == Use::Escape) v->owns_new = false;
}

void OwnershipChecker::assign(nodes::ASTNode* left, nodes::ASTNode* right, std::uint32_t line) {
    Symbol* target = symbol_of(left);
    if (target && var(target)) {
        initialize(target, right, line, true);
        return;
    }
    if (stored_target(right)) store(left, right, false, line);
    else forget_stored(left, false);
    expr(left, Use::Read);
    expr(right, Use::Escape);
}

void OwnershipChecker::initialize(Symbol* sym, nodes::ASTNode* right, std::uint32_t line, bool reassign) {
    bool from_new = false;
    Symbol* points_to = nullptr;
    if (right && right->kind == K::NewExpression) {
        expr(right, Use::Read);
        from_new = true;
    } else if (right && right->kind == K::ReferenceExpression) {
        nodes::ASTNode* operand = static_cast<nodes::ReferenceExpression*>(right)->operand;
        Symbol* t = symbol_of(operand);
        if (Var* tv = var(t); tv && tv->stack) points_to = t;
        else if (Symbol* base = local_value_base(operand)) points_to = base;
        expr(right, points_to ? Use::Address : Use::Escape);
    } else {
        Symbol* source = symbol_of(right);
        if (Var* sv = var(source); sv && sv->points_to && sv->life == Life::Live) points_to = sv->points_to;
        if (Var* sv = var(source); sv && sv->array && sv->stack) points_to = source;
        expr(right, Use::Escape);
    }

    Symbol* source = nullptr;
    if (Var* tv = var(sym); tv && !from_new) source = tv->reference ? lvalue_source(right) : (tv->pointer ? pointer_source(right) : nullptr);
    const std::uint32_t heap = from_new ? ++m_next_heap : (source && source != sym ? heap_of(source) : 0);

    Var* v = var(sym);
    if (!v) return;
    v->heap = heap;
    v->deleted_via = nullptr;
    if (reassign && m_flow.live && v->owns_new && v->life == Life::Live) {
        warn(line, quoted(sym->name) + " is reassigned while it still holds memory from 'new' on line " + std::to_string(v->new_line) + ", so that memory leaks");
    }
    v->life = Life::Live;
    v->reported = false;
    v->at = line;
    v->owns_new = from_new;
    v->new_line = from_new ? line : 0;
    v->points_to = points_to;
}

void OwnershipChecker::delete_expression(nodes::DeleteExpression* d) {
    nodes::ASTNode* operand = d->operand;
    if (operand && operand->kind == K::ReferenceExpression) {
        Symbol* t = symbol_of(static_cast<nodes::ReferenceExpression*>(operand)->operand);
        if (Var* tv = var(t); tv && tv->stack) {
            error(d->line, "deletes the address of local " + quoted(t->name) + ", which was not made with 'new'");
            return;
        }
    }
    Symbol* sym = symbol_of(operand);
    Var* v = var(sym);
    if (!v) {
        expr(operand, Use::Read);
        return;
    }
    if (v->life == Life::Deleted) {
        if (v->deleted_via) error(d->line, "deletes " + quoted(sym->name) + ", but the memory it points to was already deleted through " + quoted(v->deleted_via->name) + " on line " + std::to_string(v->at));
        else error(d->line, quoted(sym->name) + " is deleted twice (it was already deleted on line " + std::to_string(v->at) + ")");
    } else if (v->life == Life::Moved) {
        touch(sym, Use::Read, d->line);
    } else if (v->array && v->stack) {
        error(d->line, "deletes local array " + quoted(sym->name) + ", which was not made with 'new'");
    } else if (v->points_to && v->life == Life::Live && !v->owns_new) {
        error(d->line, "deletes " + quoted(sym->name) + ", which points to local " + quoted(v->points_to->name) + " that was not made with 'new'");
    }
    v->life = Life::Deleted;
    v->reported = false;
    v->at = d->line;
    v->owns_new = false;
    v->points_to = nullptr;
    v->deleted_via = nullptr;
    if (const std::uint32_t id = v->heap) {
        for (auto& [other, ov] : m_flow.vars) {
            if (other == sym || ov.heap != id) continue;
            ov.life = Life::Deleted;
            ov.reported = false;
            ov.at = d->line;
            ov.owns_new = false;
            ov.points_to = nullptr;
            ov.deleted_via = sym;
        }
    }
}

void OwnershipChecker::escape_all(nodes::ASTNode* n) {
    if (!n) return;
    if (n->kind == K::Identifier) {
        if (Var* v = var(static_cast<nodes::Identifier*>(n)->resolved)) v->owns_new = false;
        return;
    }
    nodes::for_each_child(n, [this](nodes::ASTNode* c) { escape_all(c); });
}

OwnershipChecker::Var* OwnershipChecker::var(Symbol* sym) {
    if (!sym) return nullptr;
    auto it = m_flow.vars.find(sym);
    return it == m_flow.vars.end() ? nullptr : &it->second;
}

bool OwnershipChecker::stores_argument(std::string_view method) {
    static const char* const names[] = { "push_back", "push_front", "emplace_back", "emplace_front", "insert", "emplace", "push", "add", "append", "assign", "set", "put", "prepend", "enqueue" };
    for (const char* n : names) if (method == n) return true;
    return false;
}

std::string OwnershipChecker::place_text(nodes::ASTNode* e) {
    if (!e) return "something";
    switch (e->kind) {
        case K::Identifier: {
            const Symbol* s = static_cast<nodes::Identifier*>(e)->resolved;
            return s ? std::string(s->name) : std::string(static_cast<nodes::Identifier*>(e)->name);
        }
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            const char* op = m->m_op == nodes::MemberAccessExpression::Op::Arrow ? "->" : m->m_op == nodes::MemberAccessExpression::Op::Dot ? "." : "::";
            return place_text(m->m_object) + op + std::string(m->m_member);
        }
        case K::SubscriptExpression:
            return place_text(static_cast<nodes::SubscriptExpression*>(e)->m_array) + "[...]";
        case K::DereferenceExpression:
            return "*" + place_text(static_cast<nodes::DereferenceExpression*>(e)->operand);
        default:
            return "something";
    }
}

Symbol* OwnershipChecker::holder_root(nodes::ASTNode* e, std::string& path, bool& local) {
    local = false;
    if (!e) return nullptr;
    switch (e->kind) {
        case K::Identifier: {
            Symbol* s = symbol_of(e);
            Var* v = var(s);
            local = v && v->stack && !v->pointer;
            return s;
        }
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            if (m->m_op == nodes::MemberAccessExpression::Op::Scope) return nullptr;
            std::string inner;
            Symbol* root = nullptr;
            if (m->m_op == nodes::MemberAccessExpression::Op::Arrow) {
                Var* p = var(symbol_of(m->m_object));
                if (!p || !p->pointer || !p->points_to || p->life != Life::Live) return symbol_of(m->m_object);
                Var* t = var(p->points_to);
                root = p->points_to;
                local = t && t->stack && !t->pointer;
            } else {
                root = holder_root(m->m_object, inner, local);
            }
            path = inner.empty() ? std::string(m->m_member) : inner + "." + std::string(m->m_member);
            return root;
        }
        case K::SubscriptExpression: {
            std::string inner;
            Symbol* root = holder_root(static_cast<nodes::SubscriptExpression*>(e)->m_array, inner, local);
            path = inner + "[]";
            return root;
        }
        default:
            return nullptr;
    }
}

Symbol* OwnershipChecker::stored_target(nodes::ASTNode* e) {
    if (!e) return nullptr;
    if (e->kind == K::ReferenceExpression) {
        nodes::ASTNode* operand = static_cast<nodes::ReferenceExpression*>(e)->operand;
        Symbol* t = symbol_of(operand);
        if (Var* tv = var(t); tv && tv->stack) return t;
        return local_value_base(operand);
    }
    Symbol* s = symbol_of(e);
    Var* v = var(s);
    if (!v) return nullptr;
    if (v->pointer && v->points_to && v->life == Life::Live) {
        Var* t = var(v->points_to);
        return t && t->stack ? v->points_to : nullptr;
    }
    if (v->array && v->stack) return s;
    return nullptr;
}

void OwnershipChecker::store(nodes::ASTNode* place, nodes::ASTNode* value, bool into_container, std::uint32_t line) {
    Symbol* target = stored_target(value);
    if (!target) return;
    std::string path;
    bool local = false;
    Symbol* root = holder_root(place, path, local);
    if (into_container) path += "[]";
    Var* holder = local ? var(root) : nullptr;
    if (!holder) {
        if (m_flow.live) warn(line, "stores the address of local " + quoted(target->name) + " in '" + place_text(place) + "', which can still be used after " + quoted(target->name) + " is gone");
        return;
    }
    if (!into_container) {
        holder->held.erase(std::remove_if(holder->held.begin(), holder->held.end(), [&](const Held& h) { return h.path == path; }), holder->held.end());
    }
    Held h;
    h.path = path;
    h.target = target;
    h.line = line;
    holder->held.push_back(h);
}

void OwnershipChecker::forget_stored(nodes::ASTNode* place, bool container) {
    std::string path;
    bool local = false;
    Symbol* root = holder_root(place, path, local);
    Var* holder = local ? var(root) : nullptr;
    if (!holder) return;
    if (container) path += "[]";
    holder->held.erase(std::remove_if(holder->held.begin(), holder->held.end(), [&](const Held& h) { return h.path == path; }), holder->held.end());
}

void OwnershipChecker::check_held(nodes::ASTNode* place, std::uint32_t line) {
    if (!place || place->kind == K::Identifier) return;
    std::string path;
    bool local = false;
    Symbol* root = holder_root(place, path, local);
    Var* holder = local ? var(root) : nullptr;
    if (!holder) return;
    for (Held& h : holder->held) {
        if (!h.dead || h.reported || h.path != path) continue;
        h.reported = true;
        error(line, "'" + place_text(place) + "' points to " + quoted(h.target->name) + " (declared on line " + std::to_string(h.declared) + "), which no longer exists after its block ended; the pointer was stored on line " + std::to_string(h.line));
        return;
    }
}

void OwnershipChecker::set_kind(Symbol* sym, const parser_types::TypeInfo& type) {
    if (Var* v = var(sym)) {
        v->reference = type.has_reference();
        v->pointer = !v->reference && type.has_pointer();
    }
}

std::uint32_t OwnershipChecker::heap_of(Symbol* sym) {
    Var* v = var(sym);
    if (!v) return 0;
    if (!v->heap) v->heap = ++m_next_heap;
    return v->heap;
}

Symbol* OwnershipChecker::pointer_source(nodes::ASTNode* e) {
    if (!e) return nullptr;
    if (e->kind == K::Identifier) {
        Symbol* s = symbol_of(e);
        Var* v = var(s);
        return v && v->pointer ? s : nullptr;
    }
    if (e->kind == K::ReferenceExpression) return lvalue_source(static_cast<nodes::ReferenceExpression*>(e)->operand);
    return nullptr;
}

Symbol* OwnershipChecker::lvalue_source(nodes::ASTNode* e) {
    if (!e) return nullptr;
    switch (e->kind) {
        case K::DereferenceExpression:
            return pointer_source(static_cast<nodes::DereferenceExpression*>(e)->operand);
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            if (m->m_op == nodes::MemberAccessExpression::Op::Arrow) return pointer_source(m->m_object);
            if (m->m_op == nodes::MemberAccessExpression::Op::Dot) return lvalue_source(m->m_object);
            return nullptr;
        }
        case K::SubscriptExpression:
            return pointer_source(static_cast<nodes::SubscriptExpression*>(e)->m_array);
        case K::Identifier: {
            Symbol* s = symbol_of(e);
            Var* v = var(s);
            return v && v->reference && v->heap ? s : nullptr;
        }
        default:
            return nullptr;
    }
}

Symbol* OwnershipChecker::local_value_base(nodes::ASTNode* e) {
    if (!e) return nullptr;
    switch (e->kind) {
        case K::Identifier: {
            Symbol* s = symbol_of(e);
            Var* v = var(s);
            return v && v->stack && !v->pointer ? s : nullptr;
        }
        case K::MemberAccessExpression: {
            auto* m = static_cast<nodes::MemberAccessExpression*>(e);
            return m->m_op == nodes::MemberAccessExpression::Op::Dot ? local_value_base(m->m_object) : nullptr;
        }
        case K::SubscriptExpression: {
            Symbol* s = symbol_of(static_cast<nodes::SubscriptExpression*>(e)->m_array);
            Var* v = var(s);
            return v && v->array && v->stack ? s : nullptr;
        }
        default:
            return nullptr;
    }
}

Symbol* OwnershipChecker::symbol_of(nodes::ASTNode* n) {
    if (!n || n->kind != K::Identifier) return nullptr;
    return static_cast<nodes::Identifier*>(n)->resolved;
}

bool OwnershipChecker::is_move(nodes::CallExpression* c) {
    const Symbol* s = c->resolved;
    if (s) {
        if (s->name != "move" && s->name.rfind("move<", 0) != 0) return false;
        for (const Scope* sc = s->owner; sc; sc = sc->parent) {
            const Symbol* o = sc->owner_symbol;
            if (o && o->kind == SymbolKind::Namespace) return o->name == "core";
        }
        return false;
    }
    if (c->m_callee && c->m_callee->kind == K::MemberAccessExpression) {
        auto* m = static_cast<nodes::MemberAccessExpression*>(c->m_callee);
        if (m->m_op != nodes::MemberAccessExpression::Op::Scope || m->m_member != "move") return false;
        for (std::string_view q : m->qualifier) if (q == "core") return true;
    }
    return false;
}

OwnershipChecker::Flow OwnershipChecker::join(const std::vector<Flow>& flows) {
    const Flow* first = nullptr;
    for (const Flow& f : flows) if (f.live) { first = &f; break; }
    if (!first) {
        Flow dead = flows.empty() ? Flow{} : flows.front();
        dead.live = false;
        return dead;
    }
    Flow out = *first;
    for (const Flow& f : flows) {
        if (!f.live || &f == first) continue;
        for (auto& [sym, v] : out.vars) {
            auto it = f.vars.find(sym);
            if (it == f.vars.end()) continue;
            const Var& o = it->second;
            if (o.life != v.life || o.points_to != v.points_to) {
                v.life = Life::Unknown;
                v.points_to = nullptr;
            }
            if (o.heap != v.heap) v.heap = 0;
            std::vector<Held> kept;
            for (const Held& h : v.held) {
                for (const Held& oh : o.held) {
                    if (oh.path != h.path || oh.target != h.target) continue;
                    Held both = h;
                    both.dead = h.dead && oh.dead;
                    kept.push_back(both);
                    break;
                }
            }
            v.held = std::move(kept);
            v.owns_new = v.owns_new && o.owns_new;
        }
    }
    return out;
}

void OwnershipChecker::error(std::uint32_t line, std::string message) {
    if (m_gave_up) return;
    if (!m_seen.emplace(m_file, line, message).second) return;
    ++m_errors;
    m_reporter.report(ErrorPhase::Semantic, m_file, line, std::move(message));
}

void OwnershipChecker::warn(std::uint32_t line, std::string message) {
    if (m_gave_up) return;
    if (m_strict) {
        error(line, std::move(message) + " (this is an error in safe code)");
        return;
    }
    if (!m_seen.emplace(m_file, line, message).second) return;
    ++m_warned;
    m_warnings.report(ErrorPhase::Semantic, m_file, line, std::move(message));
}

} // namespace semantics
} // namespace walnut
