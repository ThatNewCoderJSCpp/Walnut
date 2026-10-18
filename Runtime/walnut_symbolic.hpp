#ifndef WALNUT_SYMBOLIC_HPP
#define WALNUT_SYMBOLIC_HPP

#include "walnut_runtime.hpp"
#include "walnut_math.hpp"
#include "fizmo/../calculus.hpp"
#include "fizmo/../mathtext.hpp"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace walnut_rt {
namespace symbolic {

namespace cas = fizmo::math::cas;
namespace sym = fizmo::math::cas::symbols;

template <typename T>
using native_t = std::decay_t<decltype(::walnut_rt::math::unwrap(std::declval<const T&>()))>;

inline std::recursive_mutex& lock() {
    static std::recursive_mutex m;
    return m;
}

inline cas::SymbolicContext& context() { return cas::G(); }

inline sym::MathExpressionManager& manager() { return context().manager(); }

class node {
public:
    node() : m_expr(manager().constant(sym::Rational())) {}

    explicit node(sym::MathExpression e) : m_expr(e) {}

    explicit node(long long v) {
        std::lock_guard<std::recursive_mutex> g(lock());
        m_expr = manager().constant(sym::Rational(v));
    }

    sym::MathExpression raw() const noexcept { return m_expr; }

    cas::Expression wrap() const { return cas::Expression(m_expr, manager()); }

    bool is_valid() const noexcept { return m_expr.get() && m_expr.get()->type != sym::NodeType::Invalid; }

    bool is_number() const noexcept { return m_expr.get() && m_expr.get()->type == sym::NodeType::Constant && m_expr.get()->exact; }

    bool is_integer() const noexcept { return is_number() && m_expr.get()->exact->is_integer(); }

    bool is_variable() const noexcept { return m_expr.get() && m_expr.get()->type == sym::NodeType::Variable; }

    bool is_constant() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return is_valid() && wrap().variable_names().empty();
    }

    bool identical(const node& o) const noexcept { return m_expr.get() == o.m_expr.get(); }

    bool equals(const node& o) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        if (identical(o)) return true;
        if (!is_valid() || !o.is_valid()) return false;
        return wrap().symbolic_equals(o.wrap());
    }

    bool is_zero() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return is_valid() && wrap().is_symbolically_zero();
    }

    bool depends_on(const std::string& var) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return is_valid() && wrap().depends_on(var);
    }

    long long variable_count() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return is_valid() ? static_cast<long long>(wrap().variable_names().size()) : 0;
    }

    std::string variable_name(long long index) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        const std::vector<std::string> names = is_valid() ? wrap().variable_names() : std::vector<std::string>{};
        if (index < 0 || static_cast<std::size_t>(index) >= names.size()) return std::string();
        return names[static_cast<std::size_t>(index)];
    }

    std::string to_string() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        if (!m_expr.get()) return "invalid";
        return wrap().to_string();
    }

    std::string to_mathtext() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return sym::to_mathtext(m_expr, manager(), cas::MathTextSyntax::Brace);
    }

    std::string to_mathtext_infix() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return sym::to_mathtext(m_expr, manager(), cas::MathTextSyntax::Math);
    }

    std::string numerator() const {
        if (!is_number()) return std::string();
        return m_expr.get()->exact->numerator().to_string();
    }

    std::string denominator() const {
        if (!is_number()) return std::string();
        return m_expr.get()->exact->denominator().to_string();
    }

    node simplify() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(context().simplifier().simplify(m_expr));
    }

    node full_simplify() const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(context().full_simplify(wrap()).inner());
    }

    node derivative(const std::string& var) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(context().derivative(wrap(), var).inner());
    }

    node nth_derivative(const std::string& var, long long order) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        if (order <= 0) return *this;
        return node(context().nth_derivative_iterative(wrap(), var, static_cast<unsigned int>(order)).inner());
    }

    node substitute(const std::string& var, const node& replacement) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(context().substitute(wrap(), var, replacement.wrap()).inner());
    }

    node antiderivative(const std::string& var) const {
        std::lock_guard<std::recursive_mutex> g(lock());
        cas::Expression r = context().integrate(wrap(), var);
        if (r.is_invalid()) return node(manager().invalid_expr());
        return node(r.inner());
    }

    node operator-() const { return unary(sym::NodeType::Negate, *this); }
    node operator+(const node& o) const { return binary(sym::NodeType::Add, *this, o); }
    node operator-(const node& o) const { return binary(sym::NodeType::Subtract, *this, o); }
    node operator*(const node& o) const { return binary(sym::NodeType::Multiply, *this, o); }
    node operator/(const node& o) const { return binary(sym::NodeType::Divide, *this, o); }

    static node unary(sym::NodeType t, const node& a) {
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(manager().unary(t, a.m_expr));
    }

    static node binary(sym::NodeType t, const node& a, const node& b) {
        std::lock_guard<std::recursive_mutex> g(lock());
        if (t == sym::NodeType::Power) return node(manager().power(a.m_expr, b.m_expr));
        return node(manager().binary(t, a.m_expr, b.m_expr));
    }

private:
    sym::MathExpression m_expr;
};

struct api {};

template <typename T>
struct ops {};

template <typename T>
class values {
public:
    values() = default;

    values& set(const std::string& name, const T& value) {
        m_map[name] = value;
        return *this;
    }

    bool has(const std::string& name) const { return m_map.find(name) != m_map.end(); }

    void clear() { m_map.clear(); }

    long long size() const { return static_cast<long long>(m_map.size()); }

    const std::unordered_map<std::string, T>& map() const noexcept { return m_map; }

private:
    std::unordered_map<std::string, T> m_map;
};

inline node variable(const std::string& name) {
    if (name.empty()) fail("variable: a variable needs a name");
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().variable(name));
}

inline node number(long long v) { return node(v); }

inline node parse_number(const std::string& text) {
    sym::Rational r;
    if (!sym::Rational::parse(text, r)) fail("Expression: '" + text + "' isn't an exact number (use digits, a decimal point, an exponent such as 1e-3, or a fraction such as 1/3)");
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().constant(r));
}

inline node rational(long long numerator, long long denominator) {
    if (denominator == 0) fail("rational: the denominator is 0");
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().rational(numerator, denominator));
}

inline node pi() {
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().pi());
}

inline node e() {
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().euler_number());
}

inline node infinity() {
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().pos_inf());
}

template <typename W>
inline node from_float(const W& w) {
    using F = native_t<W>;
    const F& v = ::walnut_rt::math::unwrap(w);
    if constexpr (std::is_floating_point_v<F>) {
        if (std::isnan(v)) {
            std::lock_guard<std::recursive_mutex> g(lock());
            return node(manager().nan_expr());
        }
        std::lock_guard<std::recursive_mutex> g(lock());
        return node(manager().constant(static_cast<double>(v)));
    } else {
        const mp::BigFloat b(v);

        if (!b.is_finite()) {
            std::lock_guard<std::recursive_mutex> g(lock());
            if (b.is_infinite()) return node(b.is_negative() ? manager().neg_inf() : manager().pos_inf());
            return node(manager().nan_expr());
        }

        const std::size_t max_digits = static_cast<std::size_t>(static_cast<double>(F::mantissa_bits + 1) * 0.30103) + 3;
        std::string chosen;
        mp::ScopedContext scope(mp::BigFloatContext(F::mantissa_bits + 8));

        for (std::size_t d = 1; d <= max_digits; ++d) {
            const std::string s = b.to_string_scientific(mp::BigFloat::no_digit_limit, d);
            const mp::BigFloat back(s);
            if (back.is_undefined()) break;
            if (back.template to_floatmp<F>() == v) {
                chosen = s;
                break;
            }
        }

        sym::Rational r;

        if (chosen.empty() || !sym::Rational::parse(chosen, r)) {
            const auto fr = b.frexp();
            const sym::Rational mant(fr.first);
            const std::int64_t ex = fr.second;
            const sym::Rational scale = sym::Rational(sym::Rational::int_pow(fizmo::math::BigInt(2), static_cast<std::uint64_t>(ex < 0 ? -ex : ex)));
            r = ex < 0 ? mant / scale : mant * scale;
        }

        std::lock_guard<std::recursive_mutex> g(lock());
        return node(manager().constant(r));
    }
}

template <typename W>
inline node from_big(const W& w) {
    const mp::BigFloat& b = ::walnut_rt::math::unwrap(w);
    if (!b.is_finite()) {
        std::lock_guard<std::recursive_mutex> g(lock());
        if (b.is_infinite()) return node(b.is_negative() ? manager().neg_inf() : manager().pos_inf());
        return node(manager().nan_expr());
    }

    const auto fr = b.frexp();
    const sym::Rational mant(fr.first);
    const std::int64_t ex = fr.second;
    const sym::Rational scale = sym::Rational(sym::Rational::int_pow(fizmo::math::BigInt(2), static_cast<std::uint64_t>(ex < 0 ? -ex : ex)));
    std::lock_guard<std::recursive_mutex> g(lock());
    return node(manager().constant(ex < 0 ? mant / scale : mant * scale));
}

inline node power(const node& a, const node& b) { return node::binary(sym::NodeType::Power, a, b); }
inline node modulo(const node& a, const node& b) { return node::binary(sym::NodeType::Modulo, a, b); }
inline node log_base(const node& x, const node& base) { return node::binary(sym::NodeType::Log, x, base); }
inline node root(const node& x, const node& n) { return node::binary(sym::NodeType::Root, x, n); }
inline node beta(const node& a, const node& b) { return node::binary(sym::NodeType::BetaFunction, a, b); }
inline node polygamma(const node& n, const node& x) { return node::binary(sym::NodeType::Polygamma, x, n); }
inline node lambert_w(const node& x, const node& k) { return node::binary(sym::NodeType::LambertW, x, k); }
inline node polylog(const node& s, const node& x) { return node::binary(sym::NodeType::Polylog, x, s); }
inline node combination(const node& n, const node& k) { return node::binary(sym::NodeType::Combination, n, k); }
inline node permutation(const node& n, const node& k) { return node::binary(sym::NodeType::Permutation, n, k); }

#define WALNUT_SYMBOLIC_UNARY(NAME, TYPE) \
    inline node NAME(const node& x) { return node::unary(sym::NodeType::TYPE, x); }

WALNUT_SYMBOLIC_UNARY(abs, AbsoluteValue)
WALNUT_SYMBOLIC_UNARY(floor, Floor)
WALNUT_SYMBOLIC_UNARY(ceiling, Ceil)
WALNUT_SYMBOLIC_UNARY(round, Round)
WALNUT_SYMBOLIC_UNARY(trunc, Truncate)
WALNUT_SYMBOLIC_UNARY(fractional_part, FractionalPart)
WALNUT_SYMBOLIC_UNARY(sign, Sign)
WALNUT_SYMBOLIC_UNARY(unit_step, UnitStep)
WALNUT_SYMBOLIC_UNARY(sqrt, Sqrt)
WALNUT_SYMBOLIC_UNARY(cbrt, Cbrt)
WALNUT_SYMBOLIC_UNARY(exp, NaturalExp)
WALNUT_SYMBOLIC_UNARY(ln, NaturalLog)
WALNUT_SYMBOLIC_UNARY(sin, Sin)
WALNUT_SYMBOLIC_UNARY(cos, Cos)
WALNUT_SYMBOLIC_UNARY(tan, Tan)
WALNUT_SYMBOLIC_UNARY(csc, Csc)
WALNUT_SYMBOLIC_UNARY(sec, Sec)
WALNUT_SYMBOLIC_UNARY(cot, Cot)
WALNUT_SYMBOLIC_UNARY(arcsin, Arcsin)
WALNUT_SYMBOLIC_UNARY(arccos, Arccos)
WALNUT_SYMBOLIC_UNARY(arctan, Arctan)
WALNUT_SYMBOLIC_UNARY(arccsc, Arccsc)
WALNUT_SYMBOLIC_UNARY(arcsec, Arcsec)
WALNUT_SYMBOLIC_UNARY(arccot, Arccot)
WALNUT_SYMBOLIC_UNARY(sinh, Sinh)
WALNUT_SYMBOLIC_UNARY(cosh, Cosh)
WALNUT_SYMBOLIC_UNARY(tanh, Tanh)
WALNUT_SYMBOLIC_UNARY(csch, Csch)
WALNUT_SYMBOLIC_UNARY(sech, Sech)
WALNUT_SYMBOLIC_UNARY(coth, Coth)
WALNUT_SYMBOLIC_UNARY(arcsinh, Arcsinh)
WALNUT_SYMBOLIC_UNARY(arccosh, Arccosh)
WALNUT_SYMBOLIC_UNARY(arctanh, Arctanh)
WALNUT_SYMBOLIC_UNARY(arccsch, Arccsch)
WALNUT_SYMBOLIC_UNARY(arcsech, Arcsech)
WALNUT_SYMBOLIC_UNARY(arccoth, Arccoth)
WALNUT_SYMBOLIC_UNARY(sinc, Sinc)
WALNUT_SYMBOLIC_UNARY(normalized_sinc, NormalSinc)
WALNUT_SYMBOLIC_UNARY(erf, Erf)
WALNUT_SYMBOLIC_UNARY(erfc, Erfc)
WALNUT_SYMBOLIC_UNARY(erfi, Erfi)
WALNUT_SYMBOLIC_UNARY(inv_erf, InverseErf)
WALNUT_SYMBOLIC_UNARY(inv_erfc, InverseErfc)
WALNUT_SYMBOLIC_UNARY(inv_erfi, InverseErfi)
WALNUT_SYMBOLIC_UNARY(gamma, Gamma)
WALNUT_SYMBOLIC_UNARY(factorial, Factorial)
WALNUT_SYMBOLIC_UNARY(digamma, Digamma)
WALNUT_SYMBOLIC_UNARY(trigamma, Trigamma)
WALNUT_SYMBOLIC_UNARY(riemann_zeta, RiemannZeta)
WALNUT_SYMBOLIC_UNARY(dilogarithm, Dilogarithm)
WALNUT_SYMBOLIC_UNARY(trilogarithm, Trilogarithm)
WALNUT_SYMBOLIC_UNARY(exp_integral, ExponentialIntegral)
WALNUT_SYMBOLIC_UNARY(log_integral, LogarithmicIntegral)
WALNUT_SYMBOLIC_UNARY(sin_integral, SinIntegral)
WALNUT_SYMBOLIC_UNARY(cos_integral, CosIntegral)
WALNUT_SYMBOLIC_UNARY(sinh_integral, SinhIntegral)
WALNUT_SYMBOLIC_UNARY(cosh_integral, CoshIntegral)
WALNUT_SYMBOLIC_UNARY(fresnel_s, FresnelS)
WALNUT_SYMBOLIC_UNARY(fresnel_c, FresnelC)
WALNUT_SYMBOLIC_UNARY(gudermannian, Gudermannian)
WALNUT_SYMBOLIC_UNARY(inv_gudermannian, InverseGudermannian)

#undef WALNUT_SYMBOLIC_UNARY

template <typename T>
inline std::string missing_variable(const node& e, const values<T>& vals) {
    std::lock_guard<std::recursive_mutex> g(lock());
    if (!e.is_valid()) return std::string();
    for (const std::string& name : e.wrap().variable_names()) {
        if (!vals.has(name)) return name;
    }
    return std::string();
}

template <typename N, typename V>
inline void bind_values(sym::BasicEvalContext<N>& ctx, const values<V>& vals) {
    std::lock_guard<std::recursive_mutex> g(lock());
    const sym::VariableTable& table = manager().variables();
    ctx.resize(table.size());
    ctx.policy.missing_in_table = cas::MissingVariablePolicy::ReturnNaN;
    ctx.policy.missing_in_input = cas::MissingVariablePolicy::ReturnNaN;

    for (const auto& kv : vals.map()) {
        const std::uint64_t id = table.get(kv.first);
        if (id == sym::VariableTable::invalid_id || id >= ctx.values.size()) continue;
        ctx.values[id]   = static_cast<N>(::walnut_rt::math::unwrap(kv.second));
        ctx.assigned[id] = true;
    }
}

template <typename T>
inline T evaluate_with(const node& e, const values<T>& vals) {
    if (!e.is_valid()) fail("evaluate: the expression is invalid");
    using N = native_t<T>;

    if constexpr (std::is_floating_point_v<N>) {
        sym::BasicEvalContext<double> ctx;
        bind_values(ctx, vals);
        return T(static_cast<N>(sym::evaluate_as<double>(e.raw(), ctx)));
    } else {
        sym::BasicEvalContext<N> ctx;
        bind_values(ctx, vals);
        return ::walnut_rt::math::rewrap<T>(sym::evaluate_as<N>(e.raw(), ctx));
    }
}

template <typename T>
inline T evaluate_constant(const node& e) { return evaluate_with<T>(e, values<T>()); }

template <typename T>
inline T evaluate_at(const node& e, const std::string& var, const T& value) {
    values<T> v;
    v.set(var, value);
    return evaluate_with<T>(e, v);
}

} // namespace symbolic
} // namespace walnut_rt

#endif // WALNUT_SYMBOLIC_HPP
