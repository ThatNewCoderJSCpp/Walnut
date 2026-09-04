#ifndef WALNUT_SEMA_DEFINITION_VALIDATOR_HPP
#define WALNUT_SEMA_DEFINITION_VALIDATOR_HPP

#include <vector>
#include <utility>

#include "type_impl.hpp"
#include "scope.hpp"
#include "symbol.hpp"
#include "semantic_error.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/error_reporter.hpp"

namespace walnut {
namespace semantics {

class DefinitionValidator {
public:
    DefinitionValidator(TypeContext& types, ErrorReporter& reporter) noexcept : m_types(types), m_reporter(reporter) {}

    void run(Scope* root) { visit(root); }

private:
    TypeContext&   m_types;
    ErrorReporter& m_reporter;
    using K  = nodes::ASTNode::Kind;
    using FQ = modifiers::FunctionQualifiers;

    struct SigKey {
        nodes::OverloadableOperator op = nodes::OverloadableOperator::None; 
        std::vector<Type*>          params;
        bool                        is_const = false;
        int                         ref_qual = 0;   // 0 none / 1 '&' / 2 '&&'

        bool operator==(const SigKey& o) const {
            return op == o.op && is_const == o.is_const && ref_qual == o.ref_qual && params == o.params;
        }
    };

    static const nodes::FunctionParameters* params_of(const nodes::ASTNode* decl, nodes::OverloadableOperator& op_out) {
        op_out = nodes::OverloadableOperator::None;
        if (!decl) return nullptr;
        if (decl->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(decl)->get_parameters();
        if (decl->kind == K::ConstructorDeclaration) return static_cast<const nodes::ConstructorDeclaration*>(decl)->get_parameters();

        if (decl->kind == K::OperatorFunctionDeclaration) {
            auto* od = static_cast<const nodes::OperatorFunctionDeclaration*>(decl);
            op_out = od->get_overload();
            return od->get_parameters();
        }

        return nullptr;
    }

    static const modifiers::FunctionQualifiers* quals_of(const nodes::ASTNode* decl) {
        if (!decl) return nullptr;
        if (decl->kind == K::FunctionDeclaration) return &static_cast<const nodes::FunctionDeclaration*>(decl)->qualifiers();
        if (decl->kind == K::ConstructorDeclaration) return &static_cast<const nodes::ConstructorDeclaration*>(decl)->qualifiers();
        if (decl->kind == K::OperatorFunctionDeclaration) return &static_cast<const nodes::OperatorFunctionDeclaration*>(decl)->qualifiers();
        return nullptr;
    }

    static bool has_body(Symbol* s) {
        if (!s || !s->decl) return false;
        if (s->decl->kind == K::FunctionDeclaration) return static_cast<const nodes::FunctionDeclaration*>(s->decl)->has_body();
        if (s->decl->kind == K::ConstructorDeclaration) return static_cast<const nodes::ConstructorDeclaration*>(s->decl)->has_body();
        if (s->decl->kind == K::OperatorFunctionDeclaration) return static_cast<const nodes::OperatorFunctionDeclaration*>(s->decl)->has_body();
        return false;
    }

    SigKey key_of(Symbol* s) {
        SigKey k;
        const nodes::FunctionParameters* ps = params_of(s->decl, k.op);

        if (ps) {
            k.params.reserve(ps->size());
            for (const nodes::FunctionParameter* p : ps->m_params) k.params.push_back(m_types.canonicalize(p->get_type()));
        }

        if (const modifiers::FunctionQualifiers* q = quals_of(s->decl)) {
            k.is_const = q->has(FQ::Const);
            if      (q->has(FQ::RValueRef)) k.ref_qual = 2;
            else if (q->has(FQ::LValueRef)) k.ref_qual = 1;
        }

        return k;
    }

    bool same_signature(Symbol* a, Symbol* b) { return key_of(a) == key_of(b); }

    void merge_overload_chain(Symbol* head) {
        std::vector<std::pair<SigKey, Symbol*>> reps;
        Symbol* prev = nullptr;

        for (Symbol* s = head; s; ) {
            SigKey k = key_of(s);
            Symbol* rep = nullptr;
            for (auto& pr : reps) if (pr.first == k) { rep = pr.second; break; }

            if (!rep) {                                  
                reps.emplace_back(std::move(k), s);
                prev = s;
                s = s->next_overload;
                continue;
            }

            const bool rep_def = has_body(rep);
            const bool s_def   = has_body(s);

            if (s_def && rep_def) {                      
                SemanticError::redefinition(m_reporter, s->decl->file_id, s->decl->line, head->name, rep->decl->file_id, rep->decl->line);
            } else if (s_def && !rep_def) {              
                rep->decl        = s->decl;
                rep->inner_scope = s->inner_scope;
            }

            prev->next_overload = s->next_overload;      
            s = s->next_overload;
        }
    }

    Symbol* base_record_of(nodes::RecordDeclaration* rec) {
        const parser_types::TypeInfo& inh = rec->get_inherits();
        if (!inh.type) return nullptr;
        Type* t = m_types.canonicalize(inh);
        t = t ? m_types.strip_cv(t) : nullptr;
        return (t && t->is_record()) ? static_cast<RecordType*>(t)->decl() : nullptr;
    }

    bool overrides_base_virtual(Symbol* base, Symbol* fn) {
        for (Symbol* b = base; b; ) {
            if (b->inner_scope) {
                for (Symbol* cand = b->inner_scope->find_member(fn->name); cand; cand = cand->next_overload) {
                    if (!cand->decl || cand->decl->kind != K::FunctionDeclaration) continue;
                    if (!same_signature(cand, fn)) continue;
                    const modifiers::FunctionQualifiers* q = quals_of(cand->decl);
                    return q && q->has(FQ::Virtual);     
                }
            }

            nodes::ASTNode* bd = b->decl;
            b = (bd && bd->kind == K::RecordDeclaration) ? base_record_of(static_cast<nodes::RecordDeclaration*>(bd)) : nullptr;
        }

        return false;
    }

    void check_overrides(nodes::RecordDeclaration* rec) {
        Symbol* base = base_record_of(rec);

        for (const nodes::RecordDeclaration::Member& m : rec->get_members()) {
            if (!m.node || m.node->kind != K::FunctionDeclaration) continue;
            auto* fn = static_cast<nodes::FunctionDeclaration*>(m.node);
            if (!fn->qualifiers().has(FQ::Override) || !fn->symbol) continue;

            if (!base)
                SemanticError::invalid_override(m_reporter, fn->file_id, fn->line, fn->get_name(), "the class has no base class");
            else if (!overrides_base_virtual(base, fn->symbol))
                SemanticError::invalid_override(m_reporter, fn->file_id, fn->line, fn->get_name(), "no matching virtual function exists in a base class");
        }
    }

    void visit(Scope* s) {
        if (!s) return;

        for (Symbol* head : s->symbols) {
            if (head->kind == SymbolKind::Function && head->next_overload) merge_overload_chain(head);                 
            if (head->kind == SymbolKind::Type && head->decl && head->decl->kind == K::RecordDeclaration) check_overrides(static_cast<nodes::RecordDeclaration*>(head->decl));
        }

        for (Scope* c : s->children) visit(c);
    }
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_DEFINITION_VALIDATOR_HPP