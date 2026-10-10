#ifndef WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP
#define WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP

#include "type_impl.hpp"
#include "substitution.hpp"
#include "const_value.hpp"
#include "symbol.hpp"
#include "scope.hpp"

#include "../Parser/nodes.hpp"

#include <unordered_set>
#include <functional>
#include <vector>

namespace walnut {
namespace semantics {

struct ConstValue {
    enum class Kind : std::uint8_t { None = 0, Int, Float, Bool } kind = Kind::None;

    enum class Fail : std::uint8_t {
        NotConstant = 0,   
        
        Overflow,          
        Underflow,         
        Indeterminate,     
        Undefined,         
        Domain,           
        ShiftWidth,  
        Threw,     
        StepLimit,
        NoReturn,
        BadCall,

        Precision,         
        Wrapped          
    } fail = Fail::NotConstant;

    const WideInt*   i = nullptr;
    const WideFloat* f = nullptr;
    bool             b = false;

    bool ok()        const { return kind != Kind::None; }
    bool is_error()  const { return kind == Kind::None && fail != Fail::NotConstant; }
    bool is_int()    const { return kind == Kind::Int; }
    bool is_float()  const { return kind == Kind::Float; }
    bool is_bool()   const { return kind == Kind::Bool; }

    static ConstValue error(Fail f) { ConstValue c; c.fail = f; return c; }
};


const char* fail_message(ConstValue::Fail f);

class ConstEvaluator {
public:
    explicit ConstEvaluator(TypeContext& types) : m_types(types) {}

    std::function<const SubstEnv*(Symbol*)> env_of;
    std::function<int(nodes::TemplateInstantiation*)> concept_value;

    ConstValue eval(nodes::ASTNode* e) {
        if (!e || m_depth > kMaxDepth) return {};
        ++m_depth;
        ConstValue v = eval_impl(e);
        --m_depth;
        return v;
    }

    ConstValue value_of(Symbol* s) { return eval_symbol(s); }

    static bool is_constant_type(Type* t);

    ConstValue coerce(const ConstValue& v, Type* dst);

    static bool truthy(const ConstValue& v, bool& ok);

    static bool add_wrapped(const WideInt& a, const WideInt& b, const WideInt& r) {
        if (a.is_negative() != b.is_negative()) return false;    
        return r.magnitude() < a.magnitude();                    
    }

    static bool mul_wrapped(const WideInt& a, const WideInt& b, const WideInt& r) {
        if (a.is_zero() || b.is_zero()) return false;
        return (r / b) != a;
    }

    static bool checked_mul(WideInt& acc, const WideInt& x);

    long long size_of_type(Type* t) {
        long long size = 0, align = 0;
        return layout_of(t, size, align) ? size : 0;
    }

    long long align_of_type(Type* t) {
        long long size = 0, align = 0;
        return layout_of(t, size, align) ? align : 0;
    }

    bool layout_of(Type* t, long long& size, long long& align, int depth = 0);

    bool record_layout(RecordType* rt, long long& size, long long& align, int depth);

    static bool char_fits(const WideInt& v);

    ConstValue narrow(ConstValue v, nodes::ASTNode* site);

public:
    struct IntShape {
        unsigned bits = 0;
        bool     is_signed = true;
        bool     known() const { return bits != 0; }
    };

    IntShape shape_of(nodes::ASTNode* site) const;

    IntShape shape_for(nodes::BinaryExpression* b) const;

    static WideUInt width_mask(unsigned bits);

    static WideUInt to_twos(const WideInt& v, unsigned bits) {
        return WideUInt(v) & width_mask(bits);          
    }

    static WideInt from_twos(const WideUInt& pattern, unsigned bits, bool is_signed);

private:
    using K  = nodes::ASTNode::Kind;
    using TK = tokenizing::Token::Kind;

    TypeContext& m_types;
    std::unordered_set<const Symbol*> m_visiting;   
    int m_depth = 0;
    static constexpr int kMaxDepth = 512;

    ConstValue make_int(const WideInt& v);

    static ConstValue make_int(const WideInt* v) {
        ConstValue c; if (v) { c.kind = ConstValue::Kind::Int; c.i = v; }
        return c;
    }

    ConstValue make_float(const WideFloat& v);

    static ConstValue make_float(const WideFloat* v) {
        ConstValue c; if (v) { c.kind = ConstValue::Kind::Float; c.f = v; }
        return c;
    }

    static ConstValue make_bool(bool b) {
        ConstValue c; c.kind = ConstValue::Kind::Bool; c.b = b;
        return c;
    }

    ConstValue eval_impl(nodes::ASTNode* e);

    ConstValue eval_literal(nodes::Literal* lit);

    ConstValue eval_symbol(Symbol* s);

    struct Local {
        ConstValue value;
        Type*      type = nullptr;
    };

    struct Frame {
        std::unordered_map<const Symbol*, Local> locals;
        ConstValue                               result;
    };

    enum class Flow : std::uint8_t { Normal, Return, Break, Continue, Fail };

    std::vector<Frame> m_frames;
    std::unordered_map<const Symbol*, ConstValue> m_symbol_cache;
    ConstValue         m_failure;
    std::size_t        m_steps = 0;
    static constexpr std::size_t kMaxSteps = 1000000;
    static constexpr std::size_t kMaxCallDepth = 256;

    static TK compound_base(TK op);

    static bool is_constexpr_function(const nodes::FunctionDeclaration* fd);

    Local* local_of(nodes::ASTNode* target);

    ConstValue store(Local& slot, ConstValue v);

    ConstValue eval_assign(nodes::BinaryExpression* b);

    ConstValue eval_incdec(nodes::UnaryExpression* u);

    ConstValue eval_call(nodes::CallExpression* c);

    Flow fail(ConstValue v) {
        m_failure = v.is_error() ? v : ConstValue{};
        return Flow::Fail;
    }

    bool test(nodes::ASTNode* cond, bool& taken);

    Flow exec_loop_body(nodes::ASTNode* body, bool& stop);

    Flow exec(nodes::ASTNode* s);

    ConstValue eval_enum_constant(Symbol* s);

    ConstValue eval_binary(nodes::BinaryExpression* b);

    ConstValue apply_binary(TK op, ConstValue l, ConstValue r, nodes::BinaryExpression* b);

    ConstValue eval_int_op(TK op, const WideInt& a, const WideInt& b, const IntShape& sh);

    ConstValue eval_int_pow(const WideInt& a, const WideInt& b);

    ConstValue guard_range(const WideFloat& r, const WideFloat& a, const WideFloat& b);

    ConstValue eval_float_op(TK op, const WideFloat& a, const WideFloat& b);

    static bool wf_is_integral(const WideFloat& x) {
        return x.is_finite() && x.get_fractional_part().is_zero();
    }

    ConstValue eval_float_pow(const WideFloat& a, const WideFloat& b);

    ConstValue finite_result(const WideFloat& r, const WideFloat& a, const WideFloat& b);

    ConstValue convert_to(const ConstValue& v, BuiltinType* dst);

    ConstValue bitnot_at_width(const WideInt& v, unsigned width, bool is_signed);
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_CONSTANT_EVALUATOR_HPP