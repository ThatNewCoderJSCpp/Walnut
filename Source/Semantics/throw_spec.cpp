#include "Semantics/throw_spec.hpp"
#include "Semantics/semantic_error.hpp"
#include "Parser/Nodes/node_children.hpp"
#include <cstdlib>
#include <sstream>
#include "Semantics/scope.hpp"

namespace walnut {
namespace semantics {

FunctionLike as_function_like(nodes::ASTNode* n) {
    using K = nodes::ASTNode::Kind;
    using tdetail::mn;

    FunctionLike f;
    if (!n) return f;
    f.decl = n;

    switch (n->kind) {
        case K::FunctionDeclaration: {
            auto* d  = static_cast<nodes::FunctionDeclaration*>(n);
            f.symbol = d->symbol;
            f.body   = d->has_body() ? mn(d->get_body()) : nullptr;
            f.quals  = &d->qualifiers();
            f.name   = d->get_name();
            break;
        }
        case K::OperatorFunctionDeclaration: {
            auto* d  = static_cast<nodes::OperatorFunctionDeclaration*>(n);
            f.symbol = d->symbol;
            f.body   = d->has_body() ? mn(d->get_body()) : nullptr;
            f.quals  = &d->qualifiers();
            f.name   = "operator";
            break;
        }
        case K::ConstructorDeclaration: {
            auto* d  = static_cast<nodes::ConstructorDeclaration*>(n);
            f.symbol = d->symbol;
            f.body   = d->has_body() ? mn(d->get_body()) : nullptr;
            f.quals  = &d->qualifiers();
            f.name   = "constructor";
            break;
        }
        case K::DestructorDeclaration: {
            auto* d  = static_cast<nodes::DestructorDeclaration*>(n);
            f.symbol = d->symbol;
            f.body   = d->has_body() ? mn(d->get_body()) : nullptr;
            f.name   = "destructor";
            break;
        }
        default:
            f.decl = nullptr;
            break;
    }

    return f;
}

ThrowSpec ThrowInfo::spec() const {
    if (unknown) return ThrowSpec::Unknown;
    return thrown.empty() ? ThrowSpec::NoThrow : ThrowSpec::MayThrow;
}

ThrowSpec ThrowAnalyzer::declared_or_inferred(Symbol* fn) {
    if (!fn) return ThrowSpec::Unknown;
    if (declared_noexcept(fn)) return ThrowSpec::NoThrow;
    return of_function_body(fn).spec();
}

const ThrowInfo& ThrowAnalyzer::of_function_body(Symbol* fn) {
    static const ThrowInfo unknown_info = [] { ThrowInfo i; i.unknown = true; return i; }();
    static const ThrowInfo nothing_more{};
    if (!fn) return unknown_info;
    if (auto it = m_cache.find(fn); it != m_cache.end()) return it->second;

    if (!m_active.insert(fn).second) {
        m_cycle_targets.insert(fn);
        return nothing_more;
    }

    const FunctionLike f = as_function_like(fn->decl);
    ThrowInfo info;

    if (!f.valid() || !f.body) {
        info.unknown = true;                                  
    } else {
        if (f.decl->kind == nodes::ASTNode::Kind::ConstructorDeclaration) {
            auto* ct = static_cast<nodes::ConstructorDeclaration*>(f.decl);
            for (nodes::ASTNode* e : ct->get_init_list()) {
                const auto* call = e && e->kind == nodes::ASTNode::Kind::CallExpression ? static_cast<nodes::CallExpression*>(e) : nullptr;
                const nodes::ASTNode* callee = call ? call->m_callee : nullptr;
                const Symbol* field = callee && callee->kind == nodes::ASTNode::Kind::Identifier ? static_cast<const nodes::Identifier*>(callee)->resolved : nullptr;
                if (!field || field->kind != SymbolKind::Variable) {
                    info.merge(of_expr(e));
                    continue;
                }
                for (nodes::ASTNode* a : call->m_arguments) info.merge(of_expr(a));
                if (call->resolved && !declared_noexcept(call->resolved)) info.merge(of_function_body(call->resolved));
            }
        }
        info.merge(of_stmt(f.body));
    }

    m_active.erase(fn);
    m_cycle_targets.erase(fn);
    if (!m_cycle_targets.empty()) return m_uncached.emplace_back(std::move(info));
    return m_cache.emplace(fn, std::move(info)).first->second;
}

ThrowInfo ThrowAnalyzer::of_expr(nodes::ASTNode* e) {
    using K = nodes::ASTNode::Kind;
    ThrowInfo info;
    if (!e) return info;

    switch (e->kind) {
        case K::ThrowExpression: {
            auto* t = static_cast<nodes::ThrowExpression*>(e);
            if (t->is_rethrow()) { info.unknown = true; return info; }  
            info.merge(of_expr(t->operand));
            info.add(m_types.strip_cv(type_of(t->operand)));
            return info;
        }             

        case K::CallExpression: {
            auto* c = static_cast<nodes::CallExpression*>(e);
            for (nodes::ASTNode* a : c->get_arguments()) info.merge(of_expr(a));
            info.merge(of_expr(c->get_callee()));
            Symbol* callee = c->resolved;
            if (!callee) { info.unknown = true; return info; }
            if (declared_noexcept(callee)) return info;
            info.merge(of_function_body(callee));
            return info;
        }

        case K::NewExpression: {
            auto* n = static_cast<nodes::NewExpression*>(e);
            for (nodes::ASTNode* a : n->args) info.merge(of_expr(a));
            info.merge(of_expr(n->array_size));
            info.unknown = true;            
            return info;
        }

        case K::BinaryExpression: {
            auto* b = static_cast<nodes::BinaryExpression*>(e);
            info.merge(of_expr(b->left));
            info.merge(of_expr(b->right));
            if (b->resolved && !declared_noexcept(b->resolved)) info.merge(of_function_body(b->resolved));
            return info;
        }

        case K::UnaryExpression: {
            auto* u = static_cast<nodes::UnaryExpression*>(e);
            info.merge(of_expr(u->operand));
            if (u->resolved && !declared_noexcept(u->resolved)) info.merge(of_function_body(u->resolved));
            return info;
        }

        case K::SubscriptExpression: {
            auto* x = static_cast<nodes::SubscriptExpression*>(e);
            info.merge(of_expr(x->m_array));
            info.merge(of_expr(x->m_index));
            if (x->resolved && as_function_like(x->resolved->decl).valid() && !declared_noexcept(x->resolved)) info.merge(of_function_body(x->resolved));
            return info;
        }

        case K::DereferenceExpression: {
            auto* d = static_cast<nodes::DereferenceExpression*>(e);
            info.merge(of_expr(d->operand));
            if (d->resolved && as_function_like(d->resolved->decl).valid() && !declared_noexcept(d->resolved)) info.merge(of_function_body(d->resolved));
            return info;
        }

        case K::BitwiseNotExpression: {
            auto* b = static_cast<nodes::BitwiseNotExpression*>(e);
            info.merge(of_expr(b->operand));
            if (b->resolved && as_function_like(b->resolved->decl).valid() && !declared_noexcept(b->resolved)) info.merge(of_function_body(b->resolved));
            return info;
        }

        case K::TernaryExpression: {
            auto* t = static_cast<nodes::TernaryExpression*>(e);
            info.merge(of_expr(t->condition));
            info.merge(of_expr(t->true_branch));
            info.merge(of_expr(t->false_branch));
            return info;
        }

        case K::CastExpression:
            return of_expr(static_cast<nodes::CastExpression*>(e)->operand);

        case K::Literal:
        case K::Identifier:
        case K::QualifiedIdentifier:
        case K::NoexceptExpression:
        case K::LambdaExpression:
            return info;

        case K::BlockStatement:
        case K::ExpressionStatement:
        case K::IfStatement:
        case K::TryCatchStatement:
        case K::FunctionDeclaration:
        case K::OperatorFunctionDeclaration:
        case K::ConstructorDeclaration:
        case K::DestructorDeclaration:
        case K::RecordDeclaration:
        case K::TemplateDeclaration:
        case K::ConceptDeclaration:
        case K::EnumDeclaration:
        case K::UsingDeclaration:
        case K::NamespaceDeclaration:
        case K::ImportExportDeclaration:
        case K::ModuleDeclaration:
        case K::StaticAssertDeclaration:
            return of_stmt(e);

        default:
            for_each_child(e, [&](nodes::ASTNode* c) { info.merge(of_expr(c)); });
            return info;
    }
}

ThrowInfo ThrowAnalyzer::of_stmt(nodes::ASTNode* s) {
    using K = nodes::ASTNode::Kind;
    ThrowInfo info;
    if (!s) return info;

    switch (s->kind) {
        case K::BlockStatement:
            for (nodes::ASTNode* st : static_cast<nodes::BlockStatement*>(s)->get_statements())
                info.merge(of_stmt(st));
            return info;

        case K::ExpressionStatement:
            return of_expr(static_cast<nodes::ExpressionStatement*>(s)->expr);

        case K::IfStatement: {
            auto* is = static_cast<nodes::IfStatement*>(s);
            bool decided = false;

            for (nodes::IfBranch* br : is->get_branches()) {
                if (br->condition && br->condition->kind == K::VariableDeclaration) info.merge(of_stmt(br->condition));
                else info.merge(of_expr(br->condition));
                if (br->constant_value == 0) continue;
                info.merge(of_stmt(br->body));
                if (br->constant_value == 1) { decided = true; break; }
            }

            if (!decided) info.merge(of_stmt(is->else_branch));
            return info;
        }

        case K::TryCatchStatement: {
            auto* t = static_cast<nodes::TryCatchStatement*>(s);
            ThrowInfo body = of_stmt(t->get_try_body());

            for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) {
                if (!h->is_handler()) continue;
                if (!h->is_typed_catch()) {
                    body = ThrowInfo{};
                } else {
                    Type* ct = m_types.strip_cv(m_types.canonicalize(const_cast<parser_types::TypeInfo&>(h->get_catch_type())));
                    if (ct && ct->is_reference()) ct = m_types.strip_cv(static_cast<ReferenceType*>(ct)->referent());
                    ThrowInfo rest;
                    rest.unknown = body.unknown;
                    for (Type* th : body.thrown) if (!caught_by(th, ct)) rest.thrown.push_back(th);
                    body = std::move(rest);
                }
            }

            info.merge(body);
            for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) if (h->is_handler()) info.merge(of_stmt(h->get_catch_body()));
            if (t->finally_body) info.merge(of_stmt(t->finally_body));
            return info;
        }

        case K::FunctionDeclaration:
        case K::OperatorFunctionDeclaration:
        case K::ConstructorDeclaration:
        case K::DestructorDeclaration:
        case K::RecordDeclaration:
        case K::TemplateDeclaration:
        case K::ConceptDeclaration:
        case K::EnumDeclaration:
        case K::UsingDeclaration:
        case K::NamespaceDeclaration:
        case K::ImportExportDeclaration:
        case K::ModuleDeclaration:
        case K::StaticAssertDeclaration:
            return info;

        default:
            return of_expr(s);
    }
}

bool ThrowAnalyzer::caught_by(Type* thrown, Type* catch_type) const {
    if (!thrown || !catch_type) return false;
    if (thrown == catch_type) return true;
    return rank_conversion(thrown, catch_type, m_types) != ConversionRank::None;
}

bool ThrowAnalyzer::declared_noexcept(Symbol* fn) {
    using FQ = modifiers::FunctionQualifiers;
    if (!fn) return false;
    const FunctionLike f = as_function_like(fn->decl);
    if (!f.valid() || !f.quals) return false;
    if (f.quals->has_conditional_noexcept()) return false;  
    return f.quals->has(FQ::Noexcept);
}

nodes::ASTNode* ThrowAnalyzer::function_body(Symbol* fn) {
    if (!fn || !fn->decl || fn->decl->kind != nodes::ASTNode::Kind::FunctionDeclaration) return nullptr;
    return static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_body();
}

namespace {

bool allocation_free(nodes::ASTNode* n) {
    using K  = nodes::ASTNode::Kind;
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!n) return true;
    if (n->kind == K::NewExpression || n->kind == K::LambdaExpression || n->kind == K::ThrowExpression) return false;
    if (Type* t = n->expr_type.type) {
        if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
        const bool temporary = n->expr_type.vc == nodes::ValueCategory::RValue;
        if (t->is_builtin()) {
            const BK b = static_cast<BuiltinType*>(t)->base();
            if (b == BK::String || b == BK::Text || b == BK::Dynamic) {
                if (temporary) return false;
            }
        } else if (t->kind() == TypeKind::Record || t->kind() == TypeKind::Variant || t->kind() == TypeKind::Closure || t->kind() == TypeKind::Coroutine) {
            if (temporary) return false;
        } else if (t->kind() == TypeKind::TypeParam || t->kind() == TypeKind::Dependent || t->kind() == TypeKind::PackExpansion || t->is_dependent()) {
            if (temporary) return false;
        }
    }
    if (n->kind == K::BinaryExpression || n->kind == K::UnaryExpression || n->kind == K::SubscriptExpression || n->kind == K::DereferenceExpression) {
        bool generic = false;
        for_each_child(n, [&](nodes::ASTNode* c) {
            Type* ct = c ? c->expr_type.type : nullptr;
            if (ct && ct->is_reference()) ct = static_cast<ReferenceType*>(ct)->referent();
            if (ct && (ct->is_dependent() || ct->kind() == TypeKind::TypeParam || ct->kind() == TypeKind::Dependent || ct->kind() == TypeKind::Record)) generic = true;
        });
        if (generic) return false;
    }
    bool ok = true;
    for_each_child(n, [&](nodes::ASTNode* c) { if (ok && !allocation_free(c)) ok = false; });
    return ok;
}

std::string owner_text(const Symbol* s) {
    const Scope* sc = s ? s->owner : nullptr;
    while (sc && !sc->owner_symbol) sc = sc->parent;
    if (!sc) return std::string();
    const Symbol* o = sc->owner_symbol;
    if (o->bound_type) {
        std::ostringstream os;
        o->bound_type->write_to(os);
        return os.str();
    }
    return std::string(o->name);
}

bool heavy_type(Type* t) {
    using BK = parser_types::PrimitiveType::BaseKind;
    if (!t) return true;
    if (t->is_reference()) t = static_cast<ReferenceType*>(t)->referent();
    if (t->is_builtin()) {
        const BK b = static_cast<BuiltinType*>(t)->base();
        return b == BK::String || b == BK::Text || b == BK::Dynamic;
    }
    return t->kind() == TypeKind::Record || t->kind() == TypeKind::Variant || t->kind() == TypeKind::Closure || t->kind() == TypeKind::Coroutine || t->kind() == TypeKind::TypeParam || t->kind() == TypeKind::Dependent;
}

bool cheap_inits(const FunctionLike& f) {
    if (f.decl->kind != nodes::ASTNode::Kind::ConstructorDeclaration) return true;
    for (nodes::ASTNode* e : static_cast<nodes::ConstructorDeclaration*>(f.decl)->get_init_list()) {
        if (!e) continue;
        if (!allocation_free(e)) return false;
        bool heavy = false;
        for_each_child(e, [&](nodes::ASTNode* c) { if (c && heavy_type(c->expr_type.type)) heavy = true; });
        if (heavy) return false;
    }
    return true;
}

bool returns_cheaply(const FunctionLike& f, TypeContext& types) {
    using K  = nodes::ASTNode::Kind;
    using BK = parser_types::PrimitiveType::BaseKind;
    const parser_types::TypeInfo* info = nullptr;
    if (f.decl->kind == K::FunctionDeclaration) info = &static_cast<nodes::FunctionDeclaration*>(f.decl)->get_return_type();
    else if (f.decl->kind == K::OperatorFunctionDeclaration) info = &static_cast<nodes::OperatorFunctionDeclaration*>(f.decl)->get_return_type();
    else return true;
    Type* r = types.canonicalize(*info);
    if (!r || r->is_error()) return false;
    if (r->is_reference() || r->is_pointer() || r->is_enum()) return true;
    if (r->is_builtin()) {
        const BK b = static_cast<BuiltinType*>(r)->base();
        return b != BK::String && b != BK::Text && b != BK::Dynamic;
    }
    return false;
}

} // namespace

void check_noexcept_contracts(ThrowContext& ctx, TypeContext& types, ErrorReporter& reporter) {
    using FQ = modifiers::FunctionQualifiers;
    using K  = nodes::ASTNode::Kind;
    static const bool report_candidates = std::getenv("WALNUT_NOEXCEPT_REPORT") != nullptr;

    for (nodes::ASTNode* node : ctx.pending) {
        const FunctionLike f = as_function_like(node);
        if (!f.valid() || !f.symbol) continue;
        const bool promised = (f.quals && f.quals->has(FQ::Noexcept) && !f.quals->has_conditional_noexcept()) || f.decl->kind == K::DestructorDeclaration;      
        if (!promised) {
            if (report_candidates && f.quals && !f.quals->has_conditional_noexcept() && f.body && returns_cheaply(f, types) && allocation_free(f.body) && cheap_inits(f) && ctx.analyzer.of_function_body(f.symbol).nothrow()) {
                reporter.report(ErrorPhase::Semantic, f.decl->file_id, f.decl->line, "NoexceptCandidate " + std::string(f.name) + " in " + owner_text(f.symbol));
            } else if (report_candidates && f.quals && f.body) {
                reporter.report(ErrorPhase::Semantic, f.decl->file_id, f.decl->line, "NoexceptNot " + std::string(f.name) + " in " + owner_text(f.symbol));
            }
            continue;
        }
        const ThrowInfo& body = ctx.analyzer.of_function_body(f.symbol);
        if (body.nothrow()) continue;

        SemanticError::noexcept_violation(
            reporter, f.decl->file_id, f.decl->line, f.name,
            body.thrown.empty() ? "an unanalyzable call" : type_str_of(body.thrown.front())
        );
    }

    ctx.pending.clear();
}

} // namespace semantics
} // namespace walnut