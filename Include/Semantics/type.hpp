#ifndef WALNUT_SEMA_TYPE_HPP
#define WALNUT_SEMA_TYPE_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <ostream>
#include <unordered_set>
#include <functional>

#include "../Parser/Types/typename.hpp"  
#include "../Common/arena_allocator.hpp"
#include "const_value.hpp"

namespace walnut {
namespace nodes        { struct ASTNode; }
namespace semantics    { struct Symbol; }
namespace parser_types { class  TypeInfo; }

namespace semantics {

class UserConversionTable;
struct Scope;
class RecordType;
class SubstEnv;

enum class TypeKind : std::uint8_t {
    Builtin = 0, // int, float, bool, char, string, text, void, dynamic
    Pointer,     // T*
    Reference,   // T& / T&&
    Array,       // T[N] / T[]
    Record,      // class/struct/union/template instantiations
    Enum,
    Function,    
    Variant,    
    TypeParam,   
    Dependent,
    PackExpansion,
    Null,       
    Error,
    Closure,
    Coroutine
};

struct CV {
    bool is_const     = false;
    bool is_volatile  = false;
    bool is_immutable = false;

    bool operator==(const CV& o) const {
        return is_const == o.is_const && is_volatile == o.is_volatile && is_immutable == o.is_immutable;
    }

    std::uint8_t bits() const;
};

enum class RefQual : std::uint8_t { None = 0, LValue, RValue }; 

class Type {
public:
    explicit Type(TypeKind k, CV cv = {}) noexcept : m_kind(k), m_cv(cv) {}
    virtual ~Type() = default;

    TypeKind kind() const { return m_kind; }
    const CV& cv()  const { return m_cv; }

    bool is_builtin()        const { return m_kind == TypeKind::Builtin; }
    bool is_pointer()        const { return m_kind == TypeKind::Pointer; }
    bool is_reference()      const { return m_kind == TypeKind::Reference; }
    bool is_array()          const { return m_kind == TypeKind::Array; }
    bool is_record()         const { return m_kind == TypeKind::Record; }
    bool is_enum()           const { return m_kind == TypeKind::Enum; }
    bool is_function()       const { return m_kind == TypeKind::Function; }
    bool is_variant()        const { return m_kind == TypeKind::Variant; }
    bool is_null()           const { return m_kind == TypeKind::Null; }
    bool is_error()          const { return m_kind == TypeKind::Error; }
    bool is_pack_expansion() const { return m_kind == TypeKind::PackExpansion; }
    bool is_dependent()      const { return m_dependent; }

    virtual void write_to(std::ostream& os) const = 0;

protected:
    friend class TypeContext;
    TypeKind m_kind;
    CV       m_cv;
    bool     m_dependent = false;

    void write_cv(std::ostream& os) const;
};

int length_rank(parser_types::LengthModifier l);

int builtin_width(
    parser_types::PrimitiveType::BaseKind base,
    parser_types::LengthModifier l,
    bool long_form
);

const char* length_prefix(int rank);

const char* length_prefix(parser_types::LengthModifier length);

std::string canonical_spelling(parser_types::PrimitiveType::BaseKind base, int w);

class BuiltinType : public Type {
public:
    using BaseKind = parser_types::PrimitiveType::BaseKind;

    BuiltinType(BaseKind base, int width, bool is_unsigned, CV cv);

    BaseKind base()        const { return m_base; }
    int      width()       const { return m_width; }
    bool     is_unsigned() const { return m_unsigned; }
    bool     is_void()     const { return m_base == BaseKind::Void; }
    bool     is_dynamic()  const { return m_base == BaseKind::Dynamic; }
    bool     is_bool()     const { return m_base == BaseKind::Bool; }

    void write_to(std::ostream& os) const override {
        write_cv(os);
        if (m_unsigned) os << "unsigned ";
        os << canonical_spelling(m_base, m_width);
    }

private:
    BaseKind    m_base;
    std::int8_t m_width;
    bool        m_unsigned;
};

class PointerType : public Type {
public:
    PointerType(Type* pointee, CV cv) : Type(TypeKind::Pointer, cv), m_pointee(pointee) {}
    Type* pointee() const { return m_pointee; }
    void write_to(std::ostream& os) const override { m_pointee->write_to(os); os << '*'; write_cv(os); }
private:
    Type* m_pointee;
};

class ReferenceType : public Type {
public:
    ReferenceType(Type* referent, RefQual rq, CV cv) : Type(TypeKind::Reference, cv), m_referent(referent), m_ref(rq) {}
    Type*   referent() const { return m_referent; }
    RefQual ref_qual() const { return m_ref; }

    void write_to(std::ostream& os) const override {
        m_referent->write_to(os);
        os << (m_ref == RefQual::RValue ? "&&" : "&");
    }
private:
    Type*   m_referent;
    RefQual m_ref;
};

class ArrayType : public Type {
public:
    ArrayType(Type* element, std::optional<std::size_t> extent, CV cv, Symbol* extent_param = nullptr);
    Type* element() const { return m_element; }
    std::optional<std::size_t> extent() const { return m_extent; }
    Symbol* extent_param() const { return m_extent_param; }

    void write_to(std::ostream& os) const override;
private:
    Type*                      m_element;
    std::optional<std::size_t> m_extent;
    Symbol*                    m_extent_param;
};

struct TemplateArg {
    Type*            type    = nullptr;
    const WideInt*   value   = nullptr;
    const WideFloat* fvalue  = nullptr;
    bool             is_type = true;
    Symbol*          param   = nullptr;
    nodes::ASTNode*  expr    = nullptr;

    static TemplateArg of_type(Type* t)              { TemplateArg a; a.type = t; return a; }
    static TemplateArg of_int(const WideInt* v)      { TemplateArg a; a.is_type = false; a.value = v; return a; }
    static TemplateArg of_float(const WideFloat* v)  { TemplateArg a; a.is_type = false; a.fvalue = v; return a; }

    static TemplateArg dependent(Symbol* p, nodes::ASTNode* e) {
        TemplateArg a; a.is_type = false; a.param = p; a.expr = e; return a;
    }

    bool is_value()           const { return !is_type && (value || fvalue); }
    bool is_dependent_value() const { return !is_type && !value && !fvalue && (param || expr); }

    bool operator==(const TemplateArg& o) const;

    bool operator!=(const TemplateArg& o) const { return !(*this == o); }
};

using TemplateArgs = std::vector<TemplateArg>;

class RecordType : public Type {
public:
    RecordType(Symbol* decl, TemplateArgs args, CV cv) : Type(TypeKind::Record, cv), m_decl(decl), m_args(std::move(args)) {}
    Symbol*             decl() const { return m_decl; }
    const TemplateArgs& args() const { return m_args; }
    bool is_instantiation() const { return !m_args.empty(); }
    void write_to(std::ostream& os) const override;  
private:
    Symbol*      m_decl;
    TemplateArgs m_args;
};

class EnumType : public Type {
public:
    EnumType(Symbol* decl, CV cv) : Type(TypeKind::Enum, cv), m_decl(decl) {}
    Symbol* decl() const { return m_decl; }
    void write_to(std::ostream& os) const override;
private:
    Symbol* m_decl;
};

class FunctionType : public Type {
public:
    FunctionType(Type* ret, std::vector<Type*> params, bool is_const, RefQual rq, bool is_noexcept);
    Type*                     ret()    const { return m_ret; }
    const std::vector<Type*>& params() const { return m_params; }
    bool    is_const()    const { return m_const; }
    RefQual ref_qual()    const { return m_ref; }
    bool    is_noexcept() const { return m_noexcept; }

    void write_to(std::ostream& os) const override;
private:
    Type*              m_ret;
    std::vector<Type*> m_params;
    bool               m_const;
    RefQual            m_ref;
    bool               m_noexcept;
};

class VariantType : public Type {
public:
    explicit VariantType(std::vector<Type*> alts) : Type(TypeKind::Variant), m_alts(std::move(alts)) {}
    const std::vector<Type*>& alternatives() const { return m_alts; }
    void write_to(std::ostream& os) const override;
private:
    std::vector<Type*> m_alts;
};

class TypeParamType : public Type {
public:
    explicit TypeParamType(Symbol* param, CV cv = {}) : Type(TypeKind::TypeParam, cv), m_param(param) { m_dependent = true; }
    Symbol* param() const { return m_param; }
    void write_to(std::ostream& os) const override;
private:
    Symbol* m_param;
};

class DependentType : public Type {
public:
    DependentType(Type* base, std::string_view name) : Type(TypeKind::Dependent), m_base(base), m_name(name) { m_dependent = true; }
    Type*            base() const { return m_base; }
    std::string_view name() const { return m_name; }
    void write_to(std::ostream& os) const override { m_base->write_to(os); os << "::" << m_name; }
private:
    Type*            m_base;
    std::string_view m_name;
};

class NullType : public Type {
public:
    NullType() : Type(TypeKind::Null) {}
    void write_to(std::ostream& os) const override { os << "nullptr_t"; }
};

class ErrorType : public Type {
public:
    ErrorType() : Type(TypeKind::Error) {}
    void write_to(std::ostream& os) const override { os << "<error-type>"; }
};

class ClosureType : public Type {
public:
    explicit ClosureType(nodes::ASTNode* lambda) : Type(TypeKind::Closure), m_lambda(lambda) {}
    nodes::ASTNode* lambda() const { return m_lambda; }
    void write_to(std::ostream& os) const override { os << "generic lambda"; }
private:
    nodes::ASTNode* m_lambda;
};

class CoroutineType : public Type {
public:
    CoroutineType(Type* value, bool is_generator, CV cv) : Type(TypeKind::Coroutine, cv), m_value(value), m_is_generator(is_generator) {}
    Type* value() const { return m_value; }
    bool is_generator() const { return m_is_generator; }
    bool is_task() const { return !m_is_generator; }

    void write_to(std::ostream& os) const override;
private:
    Type* m_value;
    bool  m_is_generator;
};

class PackExpansionType : public Type {
public:
    explicit PackExpansionType(Type* pattern) : Type(TypeKind::PackExpansion), m_pattern(pattern) { m_dependent = true; }
    Type* pattern() const { return m_pattern; }
    void write_to(std::ostream& os) const override { m_pattern->write_to(os); os << "..."; }
private:
    Type* m_pattern;
};

struct UserConversion {
    Type*              dst      = nullptr;  
    Symbol* op       = nullptr;  
    bool               explicit_ = false;    
};

class UserConversionTable {
public:
    void add(Type* src, const UserConversion& c) { m_map[src].push_back(c); }

    const std::vector<UserConversion>* find(Type* src) const {
        auto it = m_map.find(src);
        return it == m_map.end() ? nullptr : &it->second;
    }

    bool mark_if_new(Type* src) { return m_populated.insert(src).second; }

private:
    std::unordered_map<Type*, std::vector<UserConversion>> m_map;
    std::unordered_set<Type*> m_populated;
};

class TypeContext {
public:
    explicit TypeContext(Arena& arena) : m_arena(arena), m_consts(arena), m_floats(arena) {}
    UserConversionTable& user_conversions() { return m_user_conv; }
    ConstTable&          consts()           { return m_consts; }
    FloatTable&          floats()           { return m_floats; }

    Type* builtin(
        parser_types::PrimitiveType::BaseKind base,
        parser_types::LengthModifier length = parser_types::LengthModifier::None,
        bool is_unsigned = false, bool long_form = false, CV cv = CV{}
    );

    Type* builtin_ranked(
        parser_types::PrimitiveType::BaseKind base,
        int width, bool is_unsigned = false, CV cv = CV{}
    );

    Type* void_()   { return builtin(parser_types::PrimitiveType::BaseKind::Void); }
    Type* dynamic_(){ return builtin(parser_types::PrimitiveType::BaseKind::Dynamic); }
    Type* bool_()   { return builtin(parser_types::PrimitiveType::BaseKind::Bool); }
    Type* null_()   { if (!m_null)  m_null  = make_in<NullType>(m_arena);  return m_null;  }
    Type* error_()  { if (!m_error) m_error = make_in<ErrorType>(m_arena); return m_error; }

    Type* pointer(Type* pointee, CV cv = {});
    Type* reference(Type* referent, RefQual rq, CV cv = {});
    Type* array(Type* element, std::optional<std::size_t> extent, CV cv = {});
    Type* dependent_array(Type* element, Symbol* extent_param, CV cv = {});
    Type* closure(nodes::ASTNode* lambda);
    Type* coroutine(Type* value, bool is_generator, CV cv = {});
    Type* record(Symbol* decl, TemplateArgs args, CV cv = {});
    Type* enum_(Symbol* decl, CV cv = {});
    Type* function(Type* ret, std::vector<Type*> params, bool is_const, RefQual rq, bool is_noexcept);
    Type* variant(std::vector<Type*> alternatives);
    Type* type_param(Symbol* param, CV cv = {}); 
    Type* dependent_name(Type* base, std::string_view name);
    Type* with_cv(Type* base, CV cv);
    Type* canonicalize(const parser_types::TypeInfo& info);
    Type* strip_cv(Type* t) { return t ? with_cv(t, CV{}) : t; }
    Type* apply_subst(Type* t);
    Type* pack_expansion(Type* pattern);

    void push_subst(const SubstEnv* env) { m_subst.push_back(env); }
    void pop_subst()                     { m_subst.pop_back(); }
    const SubstEnv* active_subst() const { return m_subst.empty() ? nullptr : m_subst.back(); }
    const std::vector<const SubstEnv*>& subst_stack() const { return m_subst; }
    std::vector<const SubstEnv*> suspend_subst() { std::vector<const SubstEnv*> saved; saved.swap(m_subst); return saved; }
    void resume_subst(std::vector<const SubstEnv*> saved) { m_subst = std::move(saved); }

    std::function<bool(nodes::ASTNode*, TemplateArg&)> value_arg_hook;
    std::function<bool(nodes::ASTNode*, TemplateArg&)> value_eval_hook;
    std::function<int(Symbol*, TemplateArg&)>          value_symbol_hook;
    std::function<Scope*(RecordType*)>                 record_scope_hook;
    std::function<Type*(Symbol*)>                      instance_type_hook;
    std::function<Symbol*(Symbol*)>                    instance_primary_hook;
    std::function<Type*(Symbol*)>                      default_instance_hook;
    std::function<Symbol*(nodes::ASTNode*)>            extent_param_hook;
    std::function<Type*(Type*, std::string_view)>      member_type_hook;
    std::function<Symbol*(Type*, Type*)>               callable_hook;
    std::function<bool(Type*, Type*)>                  closure_hook;

private:
    Arena& m_arena;
    UserConversionTable m_user_conv;
    ConstTable m_consts;
    FloatTable m_floats;
    Type* m_null = nullptr; 
    Type* m_error = nullptr;
    std::vector<const SubstEnv*> m_subst; 
    std::size_t m_dim_evals = 0;

    struct BuiltinKey {
        std::uint8_t base, length, is_unsigned, cv;

        bool operator==(const BuiltinKey& o) const {
            return base == o.base && length == o.length && is_unsigned == o.is_unsigned && cv == o.cv;
        }
    };

    struct UnaryKey {                          
        TypeKind kind; Type* a; std::uint8_t x; std::uint64_t n; bool has_n;
        bool operator==(const UnaryKey& o) const {
            return kind == o.kind && a == o.a && x == o.x && n == o.n && has_n == o.has_n;
        }
    };

    struct RecordKey {
        Symbol* decl; TemplateArgs args; std::uint8_t cv;
        bool operator==(const RecordKey& o) const { return decl == o.decl && cv == o.cv && args == o.args; }
    };

    struct FuncKey {
        Type* ret; std::vector<Type*> params; std::uint8_t flags;  
        bool operator==(const FuncKey& o) const { return ret == o.ret && flags == o.flags && params == o.params; }
    };

    struct SeqKey {                            
        std::vector<Type*> elems;
        bool operator==(const SeqKey& o) const { return elems == o.elems; }
    };

    struct DepKey {
        Type* base; std::string_view name;
        bool operator==(const DepKey& o) const { return base == o.base && name == o.name; }
    };

    struct H {
        static std::size_t mix(std::size_t h, std::size_t v) { return h * 1000003u ^ (v + 0x9e3779b9u + (h << 6) + (h >> 2)); }
        std::size_t operator()(const BuiltinKey& k) const { return mix(mix(mix(k.base, k.length), k.is_unsigned), k.cv); }
        
        std::size_t operator()(const UnaryKey& k) const;

        std::size_t operator()(const RecordKey& k) const;

        std::size_t operator()(const FuncKey& k) const;

        std::size_t operator()(const SeqKey& k) const {
            std::size_t h = 0; for (Type* e : k.elems) h = mix(h, std::hash<void*>{}(e)); return h;
        }

        std::size_t operator()(const DepKey& k) const {
            return mix(std::hash<void*>{}(k.base), std::hash<std::string_view>{}(k.name));
        }

        std::size_t operator()(Symbol* s) const { return std::hash<void*>{}(s); }
    };

    std::unordered_map<BuiltinKey, Type*, H> m_builtins;
    std::unordered_map<UnaryKey,   Type*, H> m_unary;     
    std::unordered_map<RecordKey,  Type*, H> m_records;
    std::unordered_map<Symbol*,    Type*, H> m_enums;
    std::unordered_map<FuncKey,    Type*, H> m_functions;
    std::unordered_map<SeqKey,     Type*, H> m_variants;
    std::unordered_map<DepKey,     Type*, H> m_dependents;

    bool any_dependent(const std::vector<Type*>& ts) const {
        for (Type* t : ts) if (t && t->is_dependent()) return true;
        return false;
    }

    void mark_dependent(Type* t, bool dep) { if (dep) t->m_dependent = true; }
};

inline std::string type_str_of(Type* t) {
    if (!t) return "<error-type>";
    std::ostringstream os;
    t->write_to(os);
    return os.str();
}

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMA_TYPE_HPP