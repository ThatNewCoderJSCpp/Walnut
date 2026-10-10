#ifndef WALNUT_CODEGEN_CPP_EMITTER_HPP
#define WALNUT_CODEGEN_CPP_EMITTER_HPP

#include "../Semantics/type_impl.hpp"
#include "../Semantics/conversion.hpp"
#include "../Semantics/overload.hpp"
#include "../Semantics/instantiator.hpp"
#include "../Semantics/const_evaluator.hpp"
#include "../Semantics/prelude.hpp"
#include "../Semantics/scope.hpp"
#include "../Semantics/symbol.hpp"
#include "../Parser/nodes.hpp"
#include "../Lexer/escapes.hpp"
#include "../Common/error_reporter.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walnut {
namespace codegen {

struct CodegenUnit {
    FileId                 file = INVALID_FILE;
    nodes::BlockStatement* ast  = nullptr;
    semantics::Scope*      root = nullptr;
};

class CppEmitter {
    using K   = nodes::ASTNode::Kind;
    using TK  = tokenizing::Token::Kind;
    using BK  = parser_types::PrimitiveType::BaseKind;
    using RM  = modifiers::RawModifiers;
    using FQ  = modifiers::FunctionQualifiers;
    using Sym = semantics::Symbol;
    using SK  = semantics::SymbolKind;
    using Type = semantics::Type;
    using Inst = semantics::Instantiator::Instantiation;

public:
    CppEmitter(semantics::TypeContext& types, semantics::Instantiator& inst, ErrorReporter& reporter)
;

    CppEmitter(const CppEmitter&) = delete;
    CppEmitter& operator=(const CppEmitter&) = delete;

    bool uses_coroutines() const { return m_uses_coroutines; }

    bool emit(const std::vector<CodegenUnit>& units, std::ostream& out);

private:
    struct RecordInfo {
        nodes::RecordDeclaration* decl = nullptr;
        Sym*                      sym  = nullptr;
        Inst*                     inst = nullptr;
        Inst*                     env  = nullptr;
        std::string               cname;
    };

    struct FuncInfo {
        nodes::ASTNode*           decl  = nullptr;
        Sym*                      sym   = nullptr;
        Inst*                     inst  = nullptr;
        RecordInfo*               owner = nullptr;
    };

    struct ParamSlot {
        Type*                          type = nullptr;
        nodes::FunctionParameter*      param = nullptr;
        bool                           group = false;
    };

    semantics::TypeContext&   m_types;
    semantics::Instantiator&  m_inst;
    ErrorReporter&            m_reporter;
    semantics::ConstEvaluator m_eval;
    bool                      m_failed = false;

    std::vector<std::unique_ptr<RecordInfo>>      m_records;
    std::vector<RecordInfo*>                      m_record_order;
    std::unordered_map<const Sym*, RecordInfo*>   m_record_by_sym;
    std::vector<FuncInfo>                         m_functions;
    std::vector<nodes::EnumDeclaration*>          m_enum_decls;
    std::vector<nodes::ASTNode*>                  m_script;
    std::vector<nodes::ASTNode*>                  m_global_decls;
    Sym*                                          m_main_fn = nullptr;

    std::unordered_map<const Sym*, std::string>   m_name_cache;
    std::set<std::string>                         m_used_global_names;
    std::ostringstream                            m_enums;
    std::ostringstream                            m_consts;
    std::map<std::string, std::string>            m_const_pool;
    int                                           m_temp = 0;

    bool                                          m_uses_coroutines = false;
    Type*                                         m_yield_type = nullptr;
    RecordInfo*                                   m_current_record = nullptr;
    bool                                          m_current_static = false;
    Type*                                         m_current_return = nullptr;
    int                                           m_indent = 0;

    void fail_at(const nodes::ASTNode* at, const std::string& what);

    void unsupported(const nodes::ASTNode* at, const std::string& what) {
        fail_at(at, what + " is not supported by the code generator yet");
    }

    struct EnvScope {
        semantics::TypeContext& t; bool on;
        EnvScope(semantics::TypeContext& tc, const semantics::SubstEnv* e) : t(tc), on(e != nullptr) { if (on) t.push_subst(e); }
        ~EnvScope() { if (on) t.pop_subst(); }
    };

    static bool is_template_generic(const nodes::ASTNode* n) { return n && n->kind == K::TemplateDeclaration; }

    void collect_record(nodes::RecordDeclaration* rd, Inst* inst, Inst* env = nullptr);

    void collect_decl(nodes::ASTNode* n, bool top_level);

    void collect(const std::vector<CodegenUnit>& units);

    void collect_locals(nodes::ASTNode* n, Inst* env);

    static bool contains_dependent_args(const std::vector<semantics::TemplateArg>& args);

    RecordInfo* owner_record_of(Sym* s);

    void order_records();

    std::vector<RecordInfo*> value_dependencies(Type* t);

    RecordInfo* record_info_of(Type* t);

    static std::string sanitize(std::string_view s);

    std::string unique_global(std::string base);

    static std::string qualified_name(const Sym* s);

    std::string type_mangle(Type* t) {
        std::ostringstream os;
        if (t) t->write_to(os); else os << "void";
        return sanitize(os.str());
    }

    std::string mangle_args(const std::vector<semantics::TemplateArg>& args);

    static std::string local_name(std::string_view n) {
        return "v_" + sanitize(n);
    }

    semantics::Scope::Kind owner_kind(const Sym* s) const {
        return (s && s->owner) ? s->owner->kind : semantics::Scope::Kind::Module;
    }

    bool is_global_scope_owner(const Sym* s) const;

    std::vector<Type*> param_types_of(const Sym* fn);

    static const nodes::FunctionParameters* params_of(const nodes::ASTNode* d);

    std::string signature_mangle(const Sym* fn);

    std::string operator_word(const nodes::OperatorFunctionDeclaration* op);

    std::string operator_word_named(const nodes::OperatorFunctionDeclaration* op);

    const Sym* definition_of(const Sym* fn);

    std::string function_name(const Sym* fn);

    std::string global_var_name(const Sym* s);

    std::string enum_name(const Sym* s);

    static bool is_void(Type* t) {
        return t && t->is_builtin() && static_cast<semantics::BuiltinType*>(t)->is_void();
    }

    static bool is_dynamic(Type* t) {
        return t && t->is_builtin() && static_cast<semantics::BuiltinType*>(t)->is_dynamic();
    }

    static semantics::BuiltinType* as_builtin(Type* t) {
        return (t && t->is_builtin()) ? static_cast<semantics::BuiltinType*>(t) : nullptr;
    }

    Type* value_type(Type* t);

    std::string builtin_cpp(semantics::BuiltinType* b, const nodes::ASTNode* at);

    std::string cpp_type(Type* t, const nodes::ASTNode* at = nullptr);

    static std::string type_text(Type* t) {
        std::ostringstream os;
        if (t) t->write_to(os); else os << "<none>";
        return os.str();
    }

    Type* type_of(const nodes::ASTNode* n) { return n ? n->expr_type.type : nullptr; }

    bool is_record_t(Type* t) { t = value_type(t); return t && t->is_record(); }

    std::string convert(const std::string& code, Type* from, Type* to, const nodes::ASTNode* at, bool explicit_cast = false);

    nodes::LambdaExpression* closure_spec_for(Type* closure, semantics::FunctionType* sig);

    std::string closure_adapter(const std::string& code, nodes::LambdaExpression* spec, semantics::FunctionType* sig, const nodes::ASTNode* at);

    std::string functor_adapter(const std::string& code, Sym* op, semantics::FunctionType* sig, const nodes::ASTNode* at);

    std::string folded_or(nodes::VariableDeclaration* v, Type* t, bool prefer_constant);

    std::string conv(nodes::ASTNode* n, Type* to, const nodes::ASTNode* at);

    std::string pool_constant(const std::string& type, const std::string& text);

    static std::string cpp_string_literal(const std::string& bytes);

    std::string int_literal(const WideInt& v, Type* t, const nodes::ASTNode* at);

    std::string float_literal(const std::string& text, Type* t, const nodes::ASTNode* at) {
        return pool_constant(cpp_type(t, at), text);
    }

    std::string constant_value_code(const semantics::ConstValue& v, Type* t, const nodes::ASTNode* at);

    std::string literal(nodes::Literal* lit);

    std::string symbol_ref(Sym* s, const nodes::ASTNode* at);

    std::string expr(nodes::ASTNode* e);

    static const char* node_kind_name(const nodes::ASTNode* n);

    std::string fold(nodes::FoldExpression* f);

    std::string truthy(nodes::ASTNode* cond);

    Type* common_type(Type* a, Type* b);

    static const char* arith_helper(TK op);

    static TK compound_base(TK op);

    static bool is_string_t(Type* t) {
        auto* b = as_builtin(t);
        return b && (b->base() == BK::String || b->base() == BK::Text);
    }

    static bool is_char_t(Type* t) {
        auto* b = as_builtin(t);
        return b && b->base() == BK::Char;
    }

    static bool has_effects(const nodes::ASTNode* n);

    static int effect_class(const nodes::ASTNode* n) {
        if (!n || n->kind == K::Literal) return 0;
        return has_effects(n) ? 2 : 1;
    }

    std::string invoke(const std::string& target, const std::vector<std::string>& codes, const std::vector<int>& classes);

    std::string arith(TK op, nodes::ASTNode* l, nodes::ASTNode* r, Type* result, const nodes::ASTNode* at);

    std::string call_operator_symbol(Sym* op, const std::vector<nodes::ASTNode*>& operands, const nodes::ASTNode* at);

    std::string pass_arg(nodes::ASTNode* arg, Type* param, const nodes::ASTNode* at);

    std::string binary(nodes::BinaryExpression* b);

    std::string unary(nodes::UnaryExpression* u);

    std::string subscript(nodes::SubscriptExpression* s);

    std::string member_access(nodes::MemberAccessExpression* m);

    void call_arg_parts(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes);

    void grouped_arg_parts(const std::vector<ParamSlot>& slots, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at, std::vector<std::string>& codes, std::vector<int>& classes);

    std::string call_args(Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at);

    std::string invoke_with_args(const std::string& target, Sym* fn, const std::vector<nodes::ASTNode*>& args, const nodes::ASTNode* at);

    std::string aggregate_args(Type* t, const std::vector<nodes::ASTNode*>& els, const nodes::ASTNode* at);

    std::string call(nodes::CallExpression* c);

    static bool is_generic_lambda(const nodes::LambdaExpression* l);

    std::string lambda(nodes::LambdaExpression* l);

    struct LoopLabel {
        std::string name;
        bool        used = false;
    };

    std::vector<LoopLabel> m_loops;

    void loop_body(std::ostream& os, nodes::ASTNode* body);

    std::string pad() const { return std::string(static_cast<std::size_t>(m_indent) * 4, ' '); }

    static bool is_hoisted_function(const nodes::ASTNode* s);

    static bool is_local_function(const Sym* s);

    std::string param_type_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at);

    void nested_function(std::ostream& os, nodes::FunctionDeclaration* fd);

    void block(std::ostream& os, nodes::ASTNode* b);

    void field_names(RecordInfo* r, std::vector<std::string>& out, int depth = 0);

    std::vector<std::string> binding_parts(Type* source, std::size_t count, const nodes::ASTNode* at);

    std::string binding_block(Type* source, bool by_ref, bool is_const, const std::string& init_code, const SmallVector<Sym*, 4>& symbols, const nodes::ASTNode* at);

    static bool is_binding_symbol(const Sym* s);

    std::string local_decl(nodes::VariableDeclaration* v, bool with_semicolon = true);

    static bool prim_auto(nodes::VariableDeclaration* v);

    std::string brace_list(nodes::BraceInitializerList* list, Type* target, const nodes::ASTNode* at);

    Type* array_decl_type(nodes::ArrayDeclaration* a);

    std::string array_init(nodes::ArrayDeclaration* a, Type* t);

    int fold_condition(nodes::IfBranch* br);

    void if_statement(std::ostream& os, nodes::IfStatement* s);

    void switch_statement(std::ostream& os, nodes::SwitchStatement* s);

    void statement(std::ostream& os, nodes::ASTNode* s);

    Type* return_type_of(Sym* fn);

    std::string param_list(const nodes::FunctionParameters* ps, const nodes::ASTNode* at, bool with_defaults = false);

    std::vector<ParamSlot> param_slots(const Sym* fn, bool& has_group);

    const semantics::SubstEnv* outer_env(const FuncInfo& f) const {
        return (f.owner && f.owner->env && f.owner->env != f.inst) ? &f.owner->env->env : nullptr;
    }

    std::string function_header(const FuncInfo& f, bool qualified, bool in_class);

    void emit_function_body(std::ostream& os, const FuncInfo& f);

    static bool decl_is_async(const nodes::ASTNode* d);

    semantics::CoroutineType* generator_of(Type* t);

    void callable_body(std::ostream& os, nodes::ASTNode* body, Type* ret, bool async, const std::string& caps, bool is_main);

    void emit_enums();

    semantics::ConstValue enum_value(nodes::EnumValue* v) {
        nodes::Identifier id(v->name, v->line);
        id.resolved = v->symbol;
        return m_eval.eval(&id);
    }

    void emit_record(std::ostream& os, RecordInfo* r);

    void emit_record_definitions(std::ostream& os, RecordInfo* r);

    void emit_records(std::ostream& os) {
        emit_enums();
        for (RecordInfo* r : m_record_order) emit_record(os, r);
    }

    void emit_prototypes(std::ostream& os);

    Type* declared_type(nodes::VariableDeclaration* v);

    bool is_closure_global(const Sym* s);

    bool is_reference_global(const Sym* s);

    std::string direct_init_args(nodes::ASTNode* init, Type* t, const nodes::ASTNode* at);

    std::unordered_map<const nodes::VariableDeclaration*, std::string> m_binding_globals;

    std::string binding_global(const nodes::VariableDeclaration* v);

    void emit_globals(std::ostream& os);

    void emit_definitions(std::ostream& os);

    void emit_init(std::ostream& os);
};

} // namespace codegen
} // namespace walnut

#endif // WALNUT_CODEGEN_CPP_EMITTER_HPP
