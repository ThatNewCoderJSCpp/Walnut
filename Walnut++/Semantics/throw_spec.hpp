#ifndef WALNUT_SEMANTICS_THROW_SPEC_HPP
#define WALNUT_SEMANTICS_THROW_SPEC_HPP

#include "type_impl.hpp"
#include "conversion.hpp"
#include "symbol.hpp"
#include "../Parser/nodes.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace semantics {

namespace tdetail {
    inline nodes::ASTNode* mn(const nodes::ASTNode* n) {
        return const_cast<nodes::ASTNode*>(n);
    }
}

struct FunctionLike {
    nodes::ASTNode*                      decl   = nullptr;
    Symbol*                              symbol = nullptr;
    nodes::ASTNode*                      body   = nullptr;
    const modifiers::FunctionQualifiers* quals  = nullptr;  
    std::string_view                     name;

    bool valid() const { return decl != nullptr; }
};

inline FunctionLike as_function_like(nodes::ASTNode* n) {
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

enum class ThrowSpec : std::uint8_t { NoThrow = 0, MayThrow, Unknown };

struct ThrowInfo {
    bool               unknown = false;
    std::vector<Type*> thrown;

    ThrowSpec spec() const {
        if (unknown) return ThrowSpec::Unknown;
        return thrown.empty() ? ThrowSpec::NoThrow : ThrowSpec::MayThrow;
    }

    bool nothrow() const { return spec() == ThrowSpec::NoThrow; }

    void add(Type* t) {
        if (!t) { unknown = true; return; }
        for (Type* e : thrown) if (e == t) return;
        thrown.push_back(t);
    }

    void merge(const ThrowInfo& o) {
        unknown = unknown || o.unknown;
        for (Type* t : o.thrown) add(t);
    }
};

class ThrowAnalyzer {
public:
    explicit ThrowAnalyzer(TypeContext& types) : m_types(types) {}

    ThrowSpec declared_or_inferred(Symbol* fn) {
        if (!fn) return ThrowSpec::Unknown;
        if (declared_noexcept(fn)) return ThrowSpec::NoThrow;
        return of_function_body(fn).spec();
    }

    const ThrowInfo& of_function_body(Symbol* fn) {
        static const ThrowInfo unknown_info = [] { ThrowInfo i; i.unknown = true; return i; }();
        if (!fn) return unknown_info;
        if (auto it = m_cache.find(fn); it != m_cache.end()) return it->second;
        if (!m_active.insert(fn).second) return unknown_info;
        const FunctionLike f = as_function_like(fn->decl);
        ThrowInfo info;

        if (!f.valid() || !f.body) {
            info.unknown = true;                                  
        } else {
            if (f.decl->kind == nodes::ASTNode::Kind::ConstructorDeclaration) {
                auto* ct = static_cast<nodes::ConstructorDeclaration*>(f.decl);
                for (nodes::ASTNode* e : ct->get_init_list()) info.merge(of_expr(e));
            }
            info.merge(of_stmt(f.body));
        }

        m_active.erase(fn);
        return m_cache.emplace(fn, std::move(info)).first->second;
    }

    ThrowInfo of_expr(nodes::ASTNode* e) {
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
                if (callee->intrinsic) return info;
                if (declared_noexcept(callee)) return info;
                info.merge(of_function_body(callee));
                return info;
            }

            case K::NewExpression: {
                auto* n = static_cast<nodes::NewExpression*>(e);
                for (nodes::ASTNode* a : n->args) info.merge(of_expr(a));
                info.merge(of_expr(n->array_size));
                info.unknown = true;            // allocation failure — set your policy here
                return info;
            }

            case K::BinaryExpression: {
                auto* b = static_cast<nodes::BinaryExpression*>(e);
                info.merge(of_expr(b->left));
                info.merge(of_expr(b->right));
                if (b->resolved) info.merge(of_function_body(b->resolved));   
                return info;
            }

            case K::UnaryExpression: {
                auto* u = static_cast<nodes::UnaryExpression*>(e);
                info.merge(of_expr(u->operand));
                if (u->resolved) info.merge(of_function_body(u->resolved));
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

            default:
                for_each_child(e, [&](nodes::ASTNode* c) { info.merge(of_expr(c)); });
                return info;
        }
    }

    ThrowInfo of_stmt(nodes::ASTNode* s) {
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

            case K::TryCatchStatement: {
                auto* t = static_cast<nodes::TryCatchStatement*>(s);
                ThrowInfo body = of_stmt(t->get_try_body());

                for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) {
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
                for (nodes::TryCatchStatement* h = t; h; h = h->next_handler) info.merge(of_stmt(h->get_catch_body()));
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
                for_each_child(s, [&](nodes::ASTNode* c) { info.merge(of_stmt(c)); });
                return info;
        }
    }

private:
    bool caught_by(Type* thrown, Type* catch_type) const {
        if (!thrown || !catch_type) return false;
        if (thrown == catch_type) return true;
        return rank_conversion(thrown, catch_type, m_types) != ConversionRank::None;
    }

    static bool declared_noexcept(Symbol* fn) {
        using FQ = modifiers::FunctionQualifiers;
        if (!fn) return false;
        const FunctionLike f = as_function_like(fn->decl);
        if (!f.valid() || !f.quals) return false;
        if (f.quals->has_conditional_noexcept()) return false;  
        return f.quals->has(FQ::Noexcept);
    }

    static nodes::ASTNode* function_body(Symbol* fn) {
        if (!fn || !fn->decl || fn->decl->kind != nodes::ASTNode::Kind::FunctionDeclaration) return nullptr;
        return static_cast<nodes::FunctionDeclaration*>(fn->decl)->get_body();
    }

    Type* type_of(nodes::ASTNode* e) const { return e ? e->expr_type.type : nullptr; }

    TypeContext& m_types;
    std::unordered_map<Symbol*, ThrowInfo> m_cache;
    std::unordered_set<Symbol*>            m_active;
};

struct ThrowContext {
    explicit ThrowContext(TypeContext& types) : analyzer(types) {}

    ThrowAnalyzer                analyzer;
    std::vector<nodes::ASTNode*> pending;  
};

inline void check_noexcept_contracts(ThrowContext& ctx, TypeContext& types, ErrorReporter& reporter) {
    using FQ = modifiers::FunctionQualifiers;
    using K  = nodes::ASTNode::Kind;

    for (nodes::ASTNode* node : ctx.pending) {
        const FunctionLike f = as_function_like(node);
        if (!f.valid() || !f.symbol) continue;
        const bool promised = (f.quals && f.quals->has(FQ::Noexcept) && !f.quals->has_conditional_noexcept()) || f.decl->kind == K::DestructorDeclaration;      
        if (!promised) continue;
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

#endif // WALNUT_SEMANTICS_THROW_SPEC_HPP