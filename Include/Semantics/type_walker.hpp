#ifndef WALNUT_SEMA_TYPE_WALKER_HPP
#define WALNUT_SEMA_TYPE_WALKER_HPP

#include <algorithm>
#include <set>
#include <sstream>
#include <tuple>

#include "type_impl.hpp"
#include "conversion.hpp"
#include "overload.hpp"

#include "scope.hpp"
#include "symbol.hpp"
#include "semantic_error.hpp"
#include "semantic_warning.hpp"
#include "prelude.hpp"
#include "resolve.hpp"

#include "../Parser/nodes.hpp"
#include "../Common/error_reporter.hpp"

#include "instantiator.hpp"

#include "throw_spec.hpp"

namespace walnut {
namespace semantics {

class TypeWalker {
public:
    TypeWalker(
        TypeContext& types, ErrorReporter& reporter, WarningReporter& warnings, Scope* root,
        Instantiator* inst = nullptr, ThrowContext* throws = nullptr
    ) noexcept
;

    ~TypeWalker() {
        if (m_inst) m_inst->check_constraints = nullptr;
        m_types.closure_hook = nullptr;
        m_types.expr_walk_hook = m_saved_walk_hook;
        m_types.type_error_hook = m_saved_error_hook;
        m_types.conversions_hook = m_saved_conversions_hook;
        m_types.range_element_hook = m_saved_range_element_hook;
        m_types.template_conversion_hook = nullptr;
        if (m_saved_member_value_hook) {
            m_types.member_value_hook = m_saved_member_value_hook;
        } else {
            TypeContext* ctx = &m_types;
            m_types.member_value_hook = [ctx](Type* rec, std::string_view name, TemplateArg& out) {
                auto it = ctx->member_value_memo.find({ rec, std::string(name) });
                if (it == ctx->member_value_memo.end()) return false;
                out = it->second;
                return true;
            };
        }
        if (m_inst) {
            m_inst->evaluator().concept_value = m_saved_concept_value;
            m_inst->evaluator().variable_template_value = m_saved_variable_template_value;
            m_inst->evaluator().scoped_member_value = m_saved_scoped_member_value;
        }
    }

    void type_pending_instances();

    TypeWalker(const TypeWalker&) = delete;
    TypeWalker& operator=(const TypeWalker&) = delete;

    void run(nodes::BlockStatement* program);

private:
    using K = nodes::ASTNode::Kind;
    using VC = nodes::ValueCategory;

    struct SiteGuard {
        nodes::ASTNode*& slot;
        nodes::ASTNode*  saved;
        SiteGuard(nodes::ASTNode*& s, nodes::ASTNode* now) : slot(s), saved(s) { slot = now; }
        ~SiteGuard() { slot = saved; }
    };

    struct OpResult {
        Type*        type       = nullptr;
        SelectStatus status     = SelectStatus::NoMatch;
        Symbol*      chosen     = nullptr;
        bool         via_member = false;
    };

    struct ControlContextGuard {
        TypeWalker& w; unsigned long long int loops, switches;
        std::vector<std::string_view> labels;
        std::vector<std::pair<const std::vector<nodes::ASTNode*>*, std::size_t>> path;
        std::vector<std::pair<nodes::SingleStatement*, std::vector<std::pair<const std::vector<nodes::ASTNode*>*, std::size_t>>>> jump_labels, jumps;

        explicit ControlContextGuard(TypeWalker& tw);

        ~ControlContextGuard();
    };

    struct ReturnFrame {
        Type* declared = nullptr;   
        bool  deducing = false;    
        bool  seen     = false;     
        Type* deduced  = nullptr;   

        bool  saw_co           = false; 
        bool  saw_value_return = false;
        nodes::ASTNode* site   = nullptr;
    };

    struct EnvGuard {
        TypeContext& t; bool on;
        EnvGuard(TypeContext& tc, const SubstEnv* e) : t(tc), on(e != nullptr) { if (on) t.push_subst(e); }
        ~EnvGuard() { if (on) t.pop_subst(); }
    };

private:
    TypeContext&                 m_types;
    ErrorReporter&               m_reporter;
    WarningReporter&             m_warnings;
    Scope*                       m_root    = nullptr;
    Scope*                       m_current = nullptr;
    std::function<void(nodes::ASTNode*)> m_saved_walk_hook;
    std::function<void(const std::string&)> m_saved_error_hook;
    std::function<void(Type*)>              m_saved_conversions_hook;
    std::function<Type*(Type*, nodes::ASTNode*)> m_saved_range_element_hook;
    std::function<bool(Type*, std::string_view, TemplateArg&)> m_saved_member_value_hook;
    std::function<int(nodes::TemplateInstantiation*)> m_saved_concept_value;
    std::function<ConstValue(nodes::TemplateInstantiation*)> m_saved_variable_template_value;
    std::function<ConstValue(nodes::MemberAccessExpression*)> m_saved_scoped_member_value;
    nodes::ASTNode*              m_site = nullptr;
    bool                         m_quiet_deleted = false;
    std::set<std::tuple<std::uint32_t, std::size_t, std::string>> m_reported_type_errors;
    Instantiator*                m_inst    = nullptr;
    ThrowContext*                m_throws  = nullptr;
    ConstEvaluator               m_eval;
    std::vector<ReturnFrame>     m_returns;
    std::vector<nodes::ASTNode*> m_fn_bodies;
    std::size_t                  m_catch_depth  = 0; 
    unsigned long long int       m_loop_depth   = 0;
    std::vector<std::string_view> m_loop_labels;
    using StmtPath = std::vector<std::pair<const std::vector<nodes::ASTNode*>*, std::size_t>>;
    StmtPath m_stmt_path;
    std::vector<std::pair<nodes::SingleStatement*, StmtPath>> m_jump_labels, m_jumps;
    void resolve_gotos();
    void build_list(const std::vector<nodes::ASTNode*>& stmts);   
    unsigned long long int       m_switch_depth = 0;

private:
    static nodes::ASTNode* mn(const nodes::ASTNode* n) { return const_cast<nodes::ASTNode*>(n); }
    void assign_type(nodes::ASTNode* n, Type* t) { n->expr_type.type = t; ensure_conversions(t); }
    static Type* type_of(const nodes::ASTNode* n) { return n ? n->expr_type.type : nullptr; }
    Scope* inner_of(Symbol* s) const { return s ? s->inner_scope : nullptr; }
    static FileId file_of(const nodes::ASTNode* n) { return n ? n->file_id : FileId{}; }

    Symbol* record_symbol_of(Type* t);

    void collect_conversions(nodes::ASTNode* n);

    void register_conversion(nodes::OperatorFunctionDeclaration* op);

    void ensure_typed(Instantiator::Instantiation* I);

    Symbol* enclosing_record_symbol(nodes::ASTNode* /*op*/) const;

    void assign_typed(nodes::ASTNode* n, Type* t, VC vc) {
        n->expr_type.type = t;
        n->expr_type.vc   = vc;
        ensure_conversions(t);
    }

    void check_condition(nodes::ASTNode* expr);

    void check_signature_defaults(Symbol* fn, const nodes::FunctionParameters* ps, nodes::ASTNode* site);

    void build(nodes::ASTNode* node);

    void walk_children(nodes::ASTNode* node);

    void walk_expr(nodes::ASTNode* e);

    void check_cast(nodes::CastExpression* c, Type* src, Type* dst);

    Scope* member_scope_of(Type* t);
    Type* record_iteration(nodes::ForEachStatement* s, Type* container);

    const SubstEnv* env_of_symbol(Symbol* s) {
        Instantiator::Instantiation* I = m_inst ? m_inst->owning(s) : nullptr;
        return I ? &I->env : nullptr;
    }

    void collect_member_ops(
        Type* recv, nodes::OverloadableOperator want,
        const std::vector<Type*>& explicit_args,
        std::vector<CandidateMatch>& matches,
        std::vector<Symbol*>& cands, std::vector<bool>& member
    );

    void collect_free_ops(
        nodes::OverloadableOperator want, const std::vector<Type*>& operands,
        std::vector<CandidateMatch>& matches,
        std::vector<Symbol*>& cands, std::vector<bool>& member
    );

    void push_return(Type* declared, nodes::ASTNode* site) {
        ReturnFrame f;
        f.declared = declared;
        f.site     = site;
        m_returns.push_back(f);
    }

    void push_return(const parser_types::TypeInfo& rt, nodes::ASTNode* site);

    void contribute_return(Type* contrib, nodes::ASTNode* site);

    void pop_return_fixed() { m_returns.pop_back(); }

    Type* pop_return(const parser_types::TypeInfo& rt);

    void note_coroutine(nodes::ASTNode* site, std::string_view kw);

    CoroutineType* generator_type(Type* t);

    static bool is_async_function(const Symbol* fn);

    Type* async_result(const Symbol* fn, Type* ret) {
        if (!is_async_function(fn) || !ret) return ret;
        return m_types.coroutine(ret, false);
    }

    Type* iterated_element(Type* container);

    Type* range_element(Type* container, nodes::ASTNode* site);

    void type_for_each(nodes::ForEachStatement* s);

    Type* array_symbol_type(nodes::ArrayDeclaration* a);

    void check_array_declaration(nodes::ArrayDeclaration* a);

    Type* symbol_type(Symbol* s);

    static nodes::FunctionParameter* variadic_param_of(Symbol* s);

    static Symbol* find_pack_ref(nodes::ASTNode* n);

    static void substitute_symbol(nodes::ASTNode* n, Symbol* from, Symbol* to);

    nodes::ASTNode* clone_resolved(nodes::ASTNode* n);

    Symbol* synthetic_symbol(const std::string& name, Type* t, nodes::ASTNode* decl);

    nodes::ASTNode* ident_for(Symbol* s, std::uint32_t line);

    nodes::ASTNode* binary_node(nodes::ASTNode* l, tokenizing::Token::Kind op, nodes::ASTNode* r, std::uint32_t line);

    void record_fields(Scope* sc, std::vector<Symbol*>& out, int depth = 0);

    void type_bindings(nodes::ASTNode* site, nodes::ASTNode* init_expr, Type* init, parser_types::TypeInfo& ti, bool is_const,
                       SmallVector<Symbol*, 4>& symbols, Type*& source, bool& by_ref, bool from_loop);

    static bool is_generic_lambda(const nodes::LambdaExpression* l);

    nodes::LambdaExpression* specialize_lambda(nodes::LambdaExpression* gl, const std::vector<Type*>& args, nodes::ASTNode* site, bool quiet);

    static bool is_pack_spread(const nodes::ASTNode* a);

    void expand_pack_arguments(nodes::CallExpression* c);

    void type_fold(nodes::FoldExpression* f);

    Type* type_of_literal(nodes::Literal* lit);

    void rebase_on_instance(nodes::RecordDeclaration* r);

    void refresh_member(nodes::Identifier* id);

    Type* record_type_of(Symbol* sym);

    Type* this_type();

    Type* enclosing_record_type();

    Type* type_binary(nodes::BinaryExpression* b, Symbol*& resolved_op);

    Type* type_unary(nodes::UnaryExpression* u, Symbol*& resolved_op);

    Type* common_type(Type* a, Type* b, nodes::ASTNode* site);

    void walk_param_defaults(const nodes::FunctionParameters* ps);

    Symbol* generic_of_instance(Symbol* s);
    bool is_template_instance(Symbol* s);

    Type* type_call(nodes::CallExpression* c, const std::vector<Type*>& args, VC& vc);

    static bool derives_from(Scope* s, Scope* base, int depth = 0);

    bool is_friend_of(Scope* record_scope);

    void check_member_access(Symbol* found, nodes::ASTNode* site);

    Type* type_member_access(nodes::MemberAccessExpression* m);
    void  finish_member_access(nodes::MemberAccessExpression* m, bool as_callee = false);
    static std::size_t fixed_parameter_count(const Symbol* fn);
    int call_object_constness(nodes::CallExpression* c);

    struct DeferredBody {
        nodes::ASTNode*              node  = nullptr;
        Scope*                       scope = nullptr;
        Instantiator::Instantiation* inst  = nullptr;
    };

    static inline std::unordered_map<Symbol*, DeferredBody> m_deferred;
    static inline std::unordered_set<Symbol*> m_requested;

public:
    static void reset_deferred_bodies() {
        m_deferred.clear();
        m_requested.clear();
    }

private:
    nodes::ASTNode* m_force_node = nullptr;
    bool defer_member_body(nodes::ASTNode* node, Symbol* sym, bool has_body, bool eager);
    void ensure_member_body(Symbol* sym);
    bool in_friend_body() const;
    Scope* qualified_base_scope(Scope* rs, Symbol* q, int depth);
    void check_brace_init(nodes::BraceInitializerList* list, Type* target, nodes::ASTNode* site);
    static bool is_static_member_function(const Symbol* fn);
    bool  is_builtin_method_call(nodes::CallExpression* c);
    bool  rewrite_extension_call(nodes::CallExpression* c);
    void  refine_by_value_category(std::vector<CandidateMatch>& matches, const std::vector<std::vector<ParamShape>>& shapes_list, const std::vector<nodes::ASTNode*>& args, std::size_t offset);
    void  substitute_pack_types(nodes::ASTNode* n, const std::vector<Symbol*>& pks, std::size_t k);
    std::string candidate_notes(Symbol* head, const std::vector<Type*>& args, const std::vector<nodes::ASTNode*>* arg_nodes, std::string_view shown);
    Type* type_builtin_method(nodes::CallExpression* c, const std::vector<Type*>& args);

    static bool is_instance_field(const Symbol* s);
    static bool is_lazy_field(const Symbol* s);
    std::string lazy_write_problem(nodes::ASTNode* target);
    bool lazy_blocked(const Symbol* s, bool object_const);

    Type* add_const(Type* t);

    static bool is_const_method(const Symbol* fn);

    bool is_member_function(const Symbol* fn) const;
    nodes::VariableDeclaration* pack_field_group(nodes::ASTNode* e);
    void expand_pack_list(std::vector<nodes::ASTNode*>& items);
    nodes::VariableDeclaration* pack_field_group_of(Symbol* s);

    void check_const_call(Symbol* chosen, nodes::CallExpression* c);

    Type* arrow_target(Type* obj, std::vector<Symbol*>* chain = nullptr);

    Type* deduce_auto_declared(const parser_types::TypeInfo& ti, Type* init);

    Type* deduce_auto(Type* init);

    Type* decay_ref(Type* t, VC& vc);

    std::vector<ParamShape> shapes_of(Symbol* fn);

    Symbol* overload_origin(Symbol* s);

    void remember_overloads(nodes::ASTNode* site, const std::vector<CandidateMatch>& matches, const std::vector<Symbol*>& origins, std::size_t chosen);

    void keep_overloads(nodes::ASTNode* site, Symbol* chosen);

    Symbol* callee_symbol(nodes::ASTNode* n);

    void check_convertible(Type* from, Type* to, nodes::ASTNode* site);

    bool binds_nonconst_lvalue_ref_to_rvalue(nodes::ASTNode* expr, Type* from, Type* to);

    static bool is_plain_literal(const nodes::ASTNode* n);

    Type* adapt_literal(nodes::ASTNode* lit, Type* lit_type, Type* other);

    bool constant_fits(nodes::ASTNode* expr, Type* from, Type* to);

    void check_binding(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site, bool check_rvalue_refs = true);

    static std::string_view template_name_of(nodes::TemplateDeclaration* tmpl);

    int         m_constraint_depth = 0;
    std::string m_last_constraint_failure;

    bool concept_satisfied(Symbol* cept, const TemplateArgs& args);

    bool concept_args(nodes::TemplateInstantiation* t, Symbol*& cept, TemplateArgs& args);

    int concept_id_value(nodes::TemplateInstantiation* t);
    bool is_variable_template(Symbol* g);
    ConstValue scoped_member_value(nodes::MemberAccessExpression* m);
    bool member_value(Type* rec, std::string_view name, TemplateArg& out);
    void rebind_inherited(nodes::QualifiedIdentifier* q);
    ConstValue variable_template_value(nodes::TemplateInstantiation* t);

    bool requires_satisfied(nodes::RequiresExpression* r);

    bool constraint_holds(nodes::ASTNode* e, std::string& failed);

    bool constraints_satisfied(nodes::TemplateDeclaration* tmpl, const SubstEnv& env, std::string& failed);

    std::unordered_set<const Symbol*> m_unassigned;
    nodes::ASTNode* m_statement_yield = nullptr;
    std::unordered_map<const Symbol*, Type*> m_array_types;
    bool m_in_const_method = false;
    int  m_typing_depth = 0;
    bool m_typing_depth_reported = false;
    bool m_allow_abstract = false;

    bool is_trivial_scalar(Type* t);

    void note_read(Symbol* s, nodes::ASTNode* site);

    void touch_record(Type* t, nodes::ASTNode* site);



    CandidateMatch match_with_constants(nodes::CallExpression* c, const std::vector<ParamShape>& shapes, const std::vector<Type*>& args);

    void check_arg_bindings(nodes::CallExpression* c, const std::vector<ParamShape>& shapes);

    void check_constant_init(nodes::VariableDeclaration* v, Type* target);

    enum class OperandClass : std::uint8_t { Unknown, Int, Char, Float, Bool, String, Pointer, Null, Enum, Other, Special };

    OperandClass operand_class(Type* t);

    static tokenizing::Token::Kind compound_base(tokenizing::Token::Kind op);

    static bool builtin_operands_ok(tokenizing::Token::Kind op, OperandClass a, OperandClass b);

    bool check_builtin_operands(nodes::BinaryExpression* b, Type* l, Type* r);

    void ensure_conversions(Type* t);

    void check_operator_signature(nodes::OperatorFunctionDeclaration* od, Symbol* sym);
    void check_type_query_overload(nodes::TypeQueryOverload* q);

    Type* resolve_member_operator(Type* recv, nodes::OverloadableOperator wanted, const std::vector<Type*>& extra, Symbol** chosen = nullptr);

    OpResult resolve_operator(nodes::OverloadableOperator want, const std::vector<Type*>& operands);
    Type* init_list_element(Type* t);
    Symbol* init_list_constructor(Type* rec, const std::vector<nodes::ASTNode*>& els, Type*& il_type, nodes::ASTNode* site);
    bool element_converts(nodes::ASTNode* el, Type* to);
    ConversionRank rank_brace_arg(nodes::ASTNode* arg, Type* param);
    void bind_brace_args(const std::vector<nodes::ASTNode*>* nodes, const std::vector<ParamShape>& shapes, nodes::ASTNode* site);
    bool prefer_rvalue_ref_for_temporaries(std::vector<CandidateMatch>& matches, const std::vector<Symbol*>& cands, const std::vector<bool>& member, const std::vector<Type*>& operands);
    Type* synthesize_comparison(nodes::BinaryExpression* b, Type* l, Type* r, Symbol*& resolved_op);
    bool memberwise_equality(Type* t, int depth);
    std::string default_construct_failure(Type* t, int depth = 0);
    void check_alignas(parser_types::TemplateArgument* a, Type* natural, nodes::ASTNode* site);
    void report_not_dynamic(Type* from, nodes::ASTNode* site);
    std::string plain_value_failure(Type* t, int depth = 0);
    void check_union(nodes::RecordDeclaration* r);
    void type_type_test(nodes::CastExpression* c);
    void type_special_test(nodes::CastExpression* c);
    bool text_displayable(Type* t);
    void check_member_ambiguity(Scope* members, std::string_view name, nodes::ASTNode* site);
    nodes::ASTNode* m_operator_site = nullptr;
    void check_member_initialization(nodes::ConstructorDeclaration* ct, const std::vector<const Symbol*>& initialized);
    bool report_if_deleted(Symbol* fn);

    enum class SpecialOp : std::uint8_t { CopyCtor, CopyAssign, MoveCtor, MoveAssign };
    nodes::RecordDeclaration* record_decl_of(Type* t, Symbol*& owner);
    int self_ref_kind(const nodes::FunctionParameters* ps, Type* self);
    void special_decls(nodes::RecordDeclaration* rd, Type* self, int& copy, int& move);
    std::string special_failure(Type* t, SpecialOp op, int depth);
    bool is_xvalue(nodes::ASTNode* e);
    bool implicitly_moved(nodes::ASTNode* e, nodes::ASTNode* site);
    void check_copy_or_move(nodes::ASTNode* expr, Type* from, Type* to, nodes::ASTNode* site);
    std::string equality_failure_of(Type* l, Type* r);

    OpResult resolve_incdec(Type* recv, nodes::OverloadableOperator want, bool postfix);

    Type* template_param_type(Symbol* s);

    Type* constructed_type(nodes::ASTNode* callee, Symbol* callee_sym, nodes::CallExpression* call = nullptr);

    Symbol* resolve_construction(Type* constructed, const std::vector<Type*>& args, nodes::ASTNode* site);

    void type_member_init(nodes::ASTNode* e);

    std::vector<Type*> param_types_for(Symbol* fn);

    std::string pure_virtual_left(Type* t);

    static bool lambda_copies(nodes::LambdaExpression* l, Symbol* s);

    bool captured_const(Symbol* s);

    bool is_polymorphic(Type* t);

    void check_not_abstract(Type* t, nodes::ASTNode* site);

    bool is_copy_source(Symbol* rec, Type* arg);

    Symbol* resolve_constructor(Symbol* rec, const std::vector<Type*>& args, nodes::ASTNode* site);

    Symbol* find_alloc_operator(Type* in_record, nodes::OverloadableOperator want);

    FunctionType* as_function_type(Type* t);

    const std::vector<nodes::ASTNode*>* m_ctor_arg_nodes = nullptr;

    static bool is_lvalue(const nodes::ASTNode* n) {
        return n && n->expr_type.vc == VC::LValue;
    }

    static bool symbol_is_object(Symbol* s) {
        return s && (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter);
    }

    bool is_addressable(nodes::ASTNode* n);

    bool type_is_const(Type* t) const { return t && t->cv().is_const; }   

    static bool known_t(Type* t) { return t && !t->is_error(); }

    static bool is_dynamic_t(Type* t) {
        return t && t->is_builtin() && static_cast<BuiltinType*>(t)->is_dynamic();
    }

    static bool is_void_t(Type* t) {
        return t && t->is_builtin() && static_cast<BuiltinType*>(t)->is_void();
    }

    static bool is_int_type(Type* t);

    bool is_switchable(Type* t) {
        return t && (is_integral_t(t) || t->is_enum() || operand_class(t) == OperandClass::String);
    }

    static bool is_numeric_t(Type* t);

    static bool is_integral_t(Type* t);

    bool is_arithmetic_or_pointer(Type* t) { return is_numeric_t(t) || (t && t->is_pointer()); }

    bool is_bool_testable(Type* t);

    static bool prim_is(const parser_types::TypeInfo& ti, parser_types::PrimitiveType::BaseKind bk);

    static bool is_single(const nodes::ASTNode* n, nodes::SingleStatement::Variant v);

    static bool terminates(const nodes::ASTNode* s);

    static bool breaks_out(const nodes::ASTNode* s);

    bool constant_true(nodes::ASTNode* cond);

    bool always_exits(nodes::ASTNode* s);

    void check_discarded_result(nodes::ASTNode* e);

    void check_chosen_overload_visibility(nodes::CallExpression* c);

    void check_falls_off(Type* ret, nodes::ASTNode* body, std::string_view name, nodes::ASTNode* site);

    static bool case_terminates(const nodes::SwitchCase* c) {
        const auto& body = c->body;
        return !body.empty() && terminates(body.back());
    }

    static bool marked_fallthrough(const nodes::SwitchCase* c);

    bool operator_arity_ok(nodes::OverloadableOperator op, bool is_member, std::size_t n, const char*& why);

    ConstValue eval_checked(nodes::ASTNode* e, bool required = false);

    static std::string type_str(Type* t) {
        return type_str_of(t);
    }

    static std::string_view callee_name(nodes::ASTNode* n);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_WALKER_HPP