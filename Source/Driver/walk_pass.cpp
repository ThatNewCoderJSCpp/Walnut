#include "Driver/walk_pass.hpp"
#include "Parser/Nodes/node_children.hpp"

#include <functional>
#include <unordered_map>

namespace walnut {
namespace driver {

WalkPass::WalkPass(ErrorReporter& reporter, WarningReporter& warnings, semantics::TypeContext& types, Arena& arena, semantics::Instantiator& inst) noexcept : m_reporter(reporter)
    , m_warnings(warnings)
    , m_types(types)
    , m_ctx(arena, reporter)      
    , m_scopes(m_ctx)
    , m_resolver(m_ctx)
    , m_inst(inst)
{
    m_inst.build_scopes = [this](nodes::ASTNode* n, semantics::Scope* parent) {
        m_scopes.build_into(n, parent);
    };
    m_inst.resolve_names = [this](nodes::ASTNode* n, semantics::Scope* scope) {
        m_resolver.resolve_into(n, scope);
    };
}

WalkPass::~WalkPass() {
    m_inst.build_scopes = nullptr;
    m_inst.resolve_names = nullptr;
}

void WalkPass::run(const FrontPass& front) {
    semantics::ThrowContext throws(m_types);
    semantics::TypeWalker::reset_deferred_bodies();

    for (FileId id : front.order()) {
        const UnitFrontResult* unit = front.result(id);
        if (!unit || !unit->ast || !unit->root) { continue; }
        ScopedFile _f(m_reporter, unit->file);
        ScopedFile _w(m_warnings, unit->file);
        semantics::TypeWalker walker(m_types, m_reporter, m_warnings, unit->root, &m_inst, &throws);
        walker.run(unit->ast);
        walker.type_pending_instances();
    }

    semantics::check_noexcept_contracts(throws, m_types, m_reporter);
    check_inline_cycles(front);
    check_externs(front);
}

std::string WalkPass::native_type_problem(semantics::Type* t, bool is_return, bool cpp) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t) return "has no type";

    if (t->is_reference()) {
        auto* r = static_cast<semantics::ReferenceType*>(t);
        semantics::Type* inner = r->referent();
        const bool const_string = inner && inner->is_builtin() && static_cast<semantics::BuiltinType*>(inner)->base() == BK::String && inner->cv().is_const;
        if (!is_return && const_string && r->ref_qual() == semantics::RefQual::LValue) return {};
        return "uses a reference; only 'const string&' parameters can be references";
    }

    if (!t->is_builtin()) return "uses '" + semantics::type_str_of(t) + "', which has no C/C++ equivalent; use int, float, bool, char or string types";
    auto* b = static_cast<semantics::BuiltinType*>(t);

    switch (b->base()) {
        case BK::Int:
            if (bit_width_of_rank(b->width()) <= 64 || cpp) return {};
            return "uses a " + std::to_string(bit_width_of_rank(b->width())) + "-bit integer; C integers are at most 64 bits (C++ functions receive wider integers as Walnut's own walnut_rt types)";
        case BK::Float: {
            const unsigned bits = bit_width_of_rank(b->width());
            if (bits == 32 || bits == 64 || cpp) return {};
            return "uses a " + std::to_string(bits) + "-bit float; C only has 32-bit and 64-bit floats, so use 'short float' for a C double or 'short short float' for a C float";
        }
        case BK::Bool:
        case BK::Char:
        case BK::String:
            return {};
        case BK::Void:
            if (is_return) return {};
            return "has a void parameter";
        default:
            return "uses '" + semantics::type_str_of(t) + "', which has no C/C++ equivalent";
    }
}

bool WalkPass::same_signature(const nodes::FunctionDeclaration* a, const nodes::FunctionDeclaration* b) {
    const nodes::FunctionParameters* pa = a->get_parameters();
    const nodes::FunctionParameters* pb = b->get_parameters();
    const std::size_t na = pa ? pa->m_params.size() : 0;
    const std::size_t nb = pb ? pb->m_params.size() : 0;
    if (na != nb) return false;

    for (std::size_t i = 0; i < na; ++i) {
        if (pa->m_params[i]->variadic_length() != pb->m_params[i]->variadic_length()) return false;
        if (pa->m_params[i]->is_exact_variadic() != pb->m_params[i]->is_exact_variadic()) return false;
        if (m_types.canonicalize(pa->m_params[i]->get_type()) != m_types.canonicalize(pb->m_params[i]->get_type())) return false;
    }

    return m_types.canonicalize(a->get_return_type()) == m_types.canonicalize(b->get_return_type());
}

void WalkPass::check_externs(const FrontPass& front) {
    using K = nodes::ASTNode::Kind;
    using RM = modifiers::RawModifiers;
    enum class Where : std::uint8_t { Namespace, Record, Body, Template };

    struct WalnutExtern {
        nodes::ASTNode*               decl;
        semantics::Symbol*            sym;
        FileId                        file;
        std::vector<std::string_view> path;
    };

    std::vector<WalnutExtern> pending;
    std::vector<std::string_view> path;

    const auto fail = [&](const nodes::ASTNode* at, const std::string& msg) {
        semantics::SemanticError::emit(m_reporter, at->file_id, at->line, "Extern", msg);
    };

    const auto linkage_name = [](const RM& m) -> std::string {
        if (m.linkage() == RM::Linkage::C) return "extern \"C\"";
        if (m.linkage() == RM::Linkage::Cpp) return "extern \"C++\"";
        return "extern";
    };

    std::function<void(nodes::ASTNode*, FileId, Where)> visit = [&](nodes::ASTNode* n, FileId file, Where where) {
        if (!n) return;

        switch (n->kind) {
            case K::NamespaceDeclaration: {
                auto* ns = static_cast<nodes::NamespaceDeclaration*>(n);
                const std::size_t depth = path.size();
                for (std::string_view p : ns->m_name_parts) path.push_back(p);
                if (ns->m_body) for (nodes::ASTNode* s : ns->m_body->get_statements()) visit(s, file, where == Where::Namespace ? Where::Namespace : where);
                path.resize(depth);
                return;
            }

            case K::RecordDeclaration: {
                auto* r = static_cast<nodes::RecordDeclaration*>(n);
                for (const auto& m : r->get_members()) visit(m.node, file, where == Where::Template ? Where::Template : Where::Record);
                return;
            }

            case K::TemplateDeclaration:
                nodes::for_each_child(n, [&](nodes::ASTNode* c) { visit(c, file, Where::Template); });
                return;

            case K::FunctionDeclaration: {
                auto* f = static_cast<nodes::FunctionDeclaration*>(n);
                const RM& m = f->get_modifiers();

                if (m.has(RM::Extern)) {
                    const std::string what = linkage_name(m) + " function '" + std::string(f->get_name()) + "'";

                    if (where == Where::Record) fail(f, what + ": class methods cannot be extern");
                    else if (where == Where::Template) fail(f, what + ": templates cannot be extern");
                    else if (where == Where::Body) fail(f, what + ": extern declarations must be at file or namespace level, not inside a function");
                    else if (f->has_body()) fail(f, what + " is defined somewhere else, so it cannot have a body here");
                    else if (m.is_native_extern()) {
                        if (const nodes::FunctionParameters* ps = f->get_parameters()) {
                            for (const nodes::FunctionParameter* p : ps->m_params) {
                                if (p->is_variadic()) { fail(f, what + ": variadic parameters cannot cross into C or C++"); continue; }
                                std::string problem = native_type_problem(m_types.canonicalize(p->get_type()), false, m.linkage() == RM::Linkage::Cpp);
                                if (!problem.empty()) fail(f, what + ": parameter '" + std::string(p->get_name()) + "' " + problem);
                            }
                        }

                        std::string problem = native_type_problem(m_types.canonicalize(f->get_return_type()), true, m.linkage() == RM::Linkage::Cpp);
                        if (!problem.empty()) fail(f, what + ": the return type " + problem);
                    } else if (f->symbol) {
                        pending.push_back(WalnutExtern{ f, f->symbol, file, path });
                    }
                }

                visit(f->get_body(), file, Where::Body);
                return;
            }

            case K::VariableDeclaration: {
                auto* v = static_cast<nodes::VariableDeclaration*>(n);
                const RM& m = v->get_type_info().modifiers;

                if (m.has(RM::Extern)) {
                    const std::string what = linkage_name(m) + " variable '" + std::string(v->get_name()) + "'";

                    if (m.is_native_extern()) fail(v, what + ": C and C++ variables cannot be shared directly yet; expose them through an extern \"C\" or \"C++\" function instead");
                    else if (where == Where::Record) fail(v, what + ": fields cannot be extern");
                    else if (where != Where::Namespace) fail(v, what + ": extern declarations must be at file or namespace level, not inside a function");
                    else if (v->has_initializer()) fail(v, what + " is defined in another file, so it cannot have an initializer here");
                    else if (v->is_structured_binding()) fail(v, what + ": structured bindings cannot be extern");
                    else if (v->symbol) pending.push_back(WalnutExtern{ v, v->symbol, file, path });
                }

                nodes::for_each_child(n, [&](nodes::ASTNode* c) { visit(c, file, Where::Body); });
                return;
            }

            case K::ArrayDeclaration: {
                auto* a = static_cast<nodes::ArrayDeclaration*>(n);
                if (a->get_array_modifiers().has(RM::Extern)) fail(a, "arrays cannot be extern; share them through a function instead");
                nodes::for_each_child(n, [&](nodes::ASTNode* c) { visit(c, file, Where::Body); });
                return;
            }

            default:
                nodes::for_each_child(n, [&](nodes::ASTNode* c) { visit(c, file, where == Where::Namespace ? Where::Body : where); });
                return;
        }
    };

    for (FileId id : front.order()) {
        const UnitFrontResult* unit = front.result(id);
        if (!unit || !unit->ast) continue;
        path.clear();
        for (nodes::ASTNode* s : unit->ast->get_statements()) visit(s, unit->file, Where::Namespace);
    }

    for (WalnutExtern& e : pending) {
        const bool is_fn = e.decl->kind == K::FunctionDeclaration;
        std::vector<std::string_view> parts = e.path;
        parts.push_back(e.sym->name);
        std::vector<semantics::Symbol*> found;
        bool skipped_hidden = false;

        for (FileId id : front.order()) {
            const UnitFrontResult* unit = front.result(id);
            if (!unit || !unit->root) continue;

            for (semantics::Symbol* s = semantics::resolve_qualified(unit->root, parts); s; s = s->next_overload) {
                if (s == e.sym || s->is_imported || !s->decl) continue;
                if (s->visibility == semantics::Visibility::Hidden && e.path.empty() && unit->file != e.file) { skipped_hidden = true; continue; }

                if (is_fn) {
                    if (s->kind != semantics::SymbolKind::Function || s->decl->kind != K::FunctionDeclaration) continue;
                    auto* d = static_cast<nodes::FunctionDeclaration*>(s->decl);
                    if (!d->has_body() || d->get_modifiers().has(RM::Extern)) continue;
                    if (!same_signature(static_cast<nodes::FunctionDeclaration*>(e.decl), d)) continue;
                } else {
                    if (s->kind != semantics::SymbolKind::Variable || s->decl->kind != K::VariableDeclaration) continue;
                    auto* d = static_cast<nodes::VariableDeclaration*>(s->decl);
                    if (d->get_type_info().modifiers.has(RM::Extern)) continue;
                    if (m_types.canonicalize(d->get_type_info()) != m_types.canonicalize(static_cast<nodes::VariableDeclaration*>(e.decl)->get_type_info())) continue;
                }

                found.push_back(s);
            }
        }

        std::string full;
        for (std::size_t i = 0; i < parts.size(); ++i) { if (i) full += "::"; full += std::string(parts[i]); }
        const std::string what = std::string(is_fn ? "extern function '" : "extern variable '") + full + "'";

        if (found.empty() && skipped_hidden) {
            fail(e.decl, what + " is defined in another file, but it is hidden there, so only that file can use it");
        } else if (found.empty()) {
            fail(e.decl, what + " has no matching definition in any Walnut file being compiled; pass the file that defines it too (walnut run main.wal other.wal)");
        } else if (found.size() > 1) {
            fail(e.decl, what + " matches " + std::to_string(found.size()) + " definitions; it must match exactly one");
        } else {
            e.sym->extern_target = found.front();
        }
    }
}

void WalkPass::check_inline_cycles(const FrontPass& front) {
    using K = nodes::ASTNode::Kind;
    using RM = modifiers::RawModifiers;
    std::vector<nodes::FunctionDeclaration*> inlines;

    const auto as_inline = [](nodes::ASTNode* n) -> nodes::FunctionDeclaration* {
        if (!n || n->kind != K::FunctionDeclaration) return nullptr;
        auto* f = static_cast<nodes::FunctionDeclaration*>(n);
        return f->get_modifiers().has(RM::Inline) && f->has_body() ? f : nullptr;
    };

    std::vector<nodes::ASTNode*> stack;
    for (FileId id : front.order()) {
        if (const UnitFrontResult* unit = front.result(id); unit && unit->ast) stack.push_back(unit->ast);
    }

    while (!stack.empty()) {
        nodes::ASTNode* n = stack.back();
        stack.pop_back();
        if (nodes::FunctionDeclaration* f = as_inline(n)) inlines.push_back(f);
        nodes::for_each_child(n, [&](nodes::ASTNode* c) { stack.push_back(c); });
    }

    if (inlines.empty()) return;
    std::unordered_map<nodes::FunctionDeclaration*, std::vector<nodes::FunctionDeclaration*>> calls;

    for (nodes::FunctionDeclaration* f : inlines) {
        std::vector<nodes::FunctionDeclaration*>& out = calls[f];
        std::vector<nodes::ASTNode*> body{ f->get_body() };

        while (!body.empty()) {
            nodes::ASTNode* n = body.back();
            body.pop_back();
            if (!n || n->kind == K::LambdaExpression) continue;

            if (n->kind == K::CallExpression) {
                const semantics::Symbol* s = static_cast<nodes::CallExpression*>(n)->resolved;
                if (s) if (nodes::FunctionDeclaration* g = as_inline(s->decl)) out.push_back(g);
            }

            nodes::for_each_child(n, [&](nodes::ASTNode* c) { body.push_back(c); });
        }
    }

    enum class Mark : std::uint8_t { None, Active, Done };
    std::unordered_map<nodes::FunctionDeclaration*, Mark> mark;
    std::vector<nodes::FunctionDeclaration*> path;
    std::unordered_map<nodes::FunctionDeclaration*, bool> reported;

    std::function<void(nodes::FunctionDeclaration*)> visit = [&](nodes::FunctionDeclaration* f) {
        mark[f] = Mark::Active;
        path.push_back(f);

        for (nodes::FunctionDeclaration* g : calls[f]) {
            if (mark[g] == Mark::Active) {
                std::string chain;
                bool in_cycle = false;

                for (nodes::FunctionDeclaration* p : path) {
                    if (p == g) in_cycle = true;
                    if (in_cycle) chain += "'" + std::string(p->get_name()) + "' -> ";
                }

                chain += "'" + std::string(g->get_name()) + "'";

                if (!reported[g]) {
                    reported[g] = true;
                    semantics::SemanticError::emit(
                        m_reporter, g->file_id, g->line, "Inline",
                        "inline function '" + std::string(g->get_name()) + "' would have to be pasted into itself (" + chain +
                        "); remove 'inline' from at least one of these functions"
                    );
                }
            } else if (mark[g] == Mark::None) {
                visit(g);
            }
        }

        path.pop_back();
        mark[f] = Mark::Done;
    };

    for (nodes::FunctionDeclaration* f : inlines) if (mark[f] == Mark::None) visit(f);
}

} // namespace driver
} // namespace walnut
