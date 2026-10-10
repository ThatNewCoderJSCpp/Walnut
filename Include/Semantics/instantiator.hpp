#ifndef WALNUT_SEMANTICS_INSTANTIATOR_HPP
#define WALNUT_SEMANTICS_INSTANTIATOR_HPP

#include "substitution.hpp"
#include "type.hpp"
#include "scope.hpp"
#include "symbol.hpp"
#include "overload.hpp"
#include "semantic_error.hpp"
#include "semantic_warning.hpp"
#include "const_value.hpp"
#include "const_evaluator.hpp"
#include "../Parser/nodes.hpp"

#include <deque>

#include <functional>
#include <unordered_map>
#include <vector>

namespace walnut {
namespace semantics {

class Instantiator {
public:
    using Arg = TemplateArg;

    struct Instantiation {
        nodes::TemplateDeclaration* source = nullptr;
        nodes::ASTNode*             decl   = nullptr;  // deep clone (starts untyped)
        Symbol*                     sym    = nullptr;  // clone's entity symbol
        Scope*                      scope  = nullptr;  // private scope wrapping the clone
        SubstEnv                    env;               // ORIGINAL param symbols -> args
        std::vector<Arg>            args;              // post-default, canonical, packs flattened
        Type*                       type = nullptr;    // RecordType / FunctionType
        std::vector<ParamShape>     fn_shapes;
        bool                        typed = false;     
    };

    enum class ArgCoerce : std::uint8_t {
        Ok = 0,
        Overflow,        // magnitude exceeds the declared width
        Underflow,       // nonzero value rounds to zero at the declared width
        WrongForm,       // float argument for an integer parameter, or vice versa
        NotConstant      // no usable value at all
    };

    struct CoerceResult {
        ArgCoerce status  = ArgCoerce::Ok;
        bool      inexact = false;      // rounded, but still a usable argument
        bool ok() const { return status == ArgCoerce::Ok; }

        static CoerceResult fail(ArgCoerce s) { CoerceResult r; r.status = s; return r; }
    };

    struct SpecMatch {
        nodes::TemplateDeclaration* tmpl = nullptr;
        SubstEnv                    env;
        bool                        failed = false;  
    };

public:
    Instantiator(Arena& arena, TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings)
;

    Instantiator(const Instantiator&) = delete;
    Instantiator& operator=(const Instantiator&) = delete;

    std::function<void(nodes::ASTNode*, Scope*)> build_scopes;
    Arena& arena() { return m_arena; }
    std::string_view keep_name(std::string name) { m_pack_names.push_back(std::move(name)); return m_pack_names.back(); }
    std::size_t kept_names() const { return m_pack_names.size(); }
    std::function<void(nodes::ASTNode*, Scope*)> resolve_names;
    std::function<bool(nodes::TemplateDeclaration*, const SubstEnv&, nodes::ASTNode*, bool)> check_constraints;
    std::size_t constraint_failures = 0;

    Instantiation* instantiate(Symbol* generic, std::vector<Arg> args, nodes::ASTNode* site) {
        return instantiate_impl(generic, std::move(args), site, false);
    }

    template <typename Fn>
    void for_each(Fn&& fn) const { for (const auto& kv : m_memo) fn(*kv.second); }

    Instantiation* for_record(RecordType* rt, nodes::ASTNode* site);

    Instantiation* instantiate_for_call(
        Symbol* generic,
        const std::vector<parser_types::TemplateArgument*>& explicit_args,
        const std::vector<Type*>& call_args,
        nodes::ASTNode* site
    );

    std::vector<Arg> canon_args(const std::vector<parser_types::TemplateArgument*>& written, bool& ok);

    void refresh_function_type(Instantiation* I);

    Instantiation* owning(Symbol* s) const;

private:
    struct Key {
        Symbol* generic;
        std::vector<Arg> args;
        bool operator==(const Key& o) const { return generic == o.generic && args == o.args; }
    };

    struct KeyHash {
        std::size_t operator()(const Key& k) const;
    };

    Symbol* primary_of_instance(Symbol* s) const;

    Instantiation* instantiate_impl(Symbol* generic, std::vector<Arg> args, nodes::ASTNode* site, bool quiet);

    SpecMatch select_specialization(Symbol* generic, const std::vector<Arg>& args, nodes::ASTNode* site, bool quiet);

    void finish_function_type(Instantiation* I, nodes::FunctionDeclaration* fd);

    bool flatten_packs(std::vector<Arg>& args);

    static Symbol* template_param_of(nodes::ASTNode* e);

    static bool refers_to_template_param(nodes::ASTNode* e);

    bool classify_value_arg(nodes::ASTNode* e, TemplateArg& out);

    Symbol* call_operator_for(Type* rec, Type* sig);

    std::unordered_set<Type*> m_call_probe;

    Type* member_type(Type* rec, std::string_view name);

    std::deque<std::string> m_pack_names;

    static bool is_value_param(Symbol* s);

    static bool is_value_symbol(Symbol* s);

    int classify_value_symbol(Symbol* s, TemplateArg& out);

    bool eval_value_arg(nodes::ASTNode* e, Arg& a) {
        return const_to_arg(m_eval.eval(e), a);
    }

    bool const_to_arg(const ConstValue& v, Arg& a);

    CoerceResult coerce_value_arg(nodes::TemplateParameter* p, Arg& a);

    bool complete_arguments(nodes::TemplateDeclaration* tmpl, std::vector<Arg>& args, nodes::ASTNode* site, bool quiet);

    bool assemble_args(nodes::TemplateDeclaration* tmpl, const SubstEnv& env, std::vector<Arg>& out, nodes::ASTNode* site, bool quiet);

    bool bind_written(nodes::TemplateParameter* p, const parser_types::TemplateArgument* w, SubstEnv& env);

    static nodes::TemplateParameter* trailing_pack(const std::vector<nodes::TemplateParameter*>& ps) {
        return (!ps.empty() && ps.back()->is_pack()) ? ps.back() : nullptr;
    }

    static bool has_nontrailing_pack(const std::vector<nodes::TemplateParameter*>& ps) {
        for (std::size_t i = 0; i + 1 < ps.size(); ++i) if (ps[i]->is_pack()) return true;
        return false;
    }

    static Symbol* entity_symbol(nodes::ASTNode* d);

    static const std::vector<parser_types::TemplateArgument*>* spec_args_of(nodes::TemplateDeclaration* t);

    static const parser_types::TemplateArgument* trailing_targ_pack(
        const std::vector<parser_types::TemplateArgument*>& written, std::size_t& fixed_out
    );

    bool pattern_matches(const std::vector<parser_types::TemplateArgument*>& written, std::size_t fixed, const std::vector<Arg>& args, const SubstEnv& env);

    bool bindings_complete(nodes::TemplateDeclaration* st, const SubstEnv& env);

    void unsupported(nodes::ASTNode* site, const char* what) {
        SemanticError::template_unsupported(m_reporter, file_of(site), line_of(site), what);
    }

    static FileId        file_of(const nodes::ASTNode* n) { return n ? n->file_id : FileId{}; }
    static std::uint32_t line_of(const nodes::ASTNode* n) { return n ? n->line : 0; }

private:
    Arena&         m_arena;
    TypeContext&   m_types;
    ErrorReporter& m_reporter;
    WarningReporter& m_warnings;
    ConstEvaluator m_eval;

    std::unordered_map<Key, Instantiation*, KeyHash> m_memo;
    std::unordered_map<Scope*, Instantiation*>       m_scope_owner;
    int m_depth = 0;
    static constexpr int kMaxDepth = 256;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_INSTANTIATOR_HPP