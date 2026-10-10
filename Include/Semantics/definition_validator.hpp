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
        Type*                       conversion = nullptr;

        bool operator==(const SigKey& o) const;
    };

    static const nodes::FunctionParameters* params_of(const nodes::ASTNode* decl, nodes::OverloadableOperator& op_out);

    static const modifiers::FunctionQualifiers* quals_of(const nodes::ASTNode* decl);

    static bool has_body(Symbol* s);

    SigKey key_of(Symbol* s);

    bool same_signature(Symbol* a, Symbol* b) { return key_of(a) == key_of(b); }

    void merge_overload_chain(Symbol* head);

    Symbol* base_record_of(nodes::RecordDeclaration* rec);

    bool overrides_base_virtual(Symbol* base, Symbol* fn);

    void check_overrides(nodes::RecordDeclaration* rec);

    void visit(Scope* s);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_DEFINITION_VALIDATOR_HPP