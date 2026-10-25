#ifndef WALNUT_MATH_HPP
#define WALNUT_MATH_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../complex.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace walnut_rt {
namespace math {

template <typename T>
struct tag {};

struct api {};

template <int N> struct rank : rank<N - 1> {};
template <> struct rank<0> {};

template <typename T, typename = void>
struct has_native : std::false_type {};

template <typename T>
struct has_native<T, std::void_t<decltype(std::declval<const T&>().native)>> : std::true_type {};

template <typename T>
inline decltype(auto) unwrap(const T& v) {
    if constexpr (has_native<T>::value) return (v.native);
    else return (v);
}

template <typename W, typename R>
inline auto rewrap(R&& r) {
    if constexpr (has_native<W>::value) {
        using N = std::decay_t<decltype(std::declval<W&>().native)>;
        if constexpr (std::is_same_v<std::decay_t<R>, N>) return W(native_tag{}, std::forward<R>(r));
        else return std::decay_t<R>(std::forward<R>(r));
    } else {
        return std::decay_t<R>(std::forward<R>(r));
    }
}

template <typename... A>
inline constexpr bool any_bigfloat = (std::is_same_v<std::decay_t<A>, mp::BigFloat> || ...);

inline const mp::BigFloatContext& context() { return mp::BigFloatContext::current(); }

template <typename A>
inline auto hardware_widen(const A& a) {
    if constexpr (is_hardware_float_v<A>) return f64(static_cast<double>(a));
    else return a;
}

template <typename W, typename R>
inline auto hardware_back(const R& r) {
    if constexpr (is_float_v<R>) return static_cast<W>(float_to_double(r));
    else return r;
}

template <typename W, typename... Ws>
inline constexpr bool any_hardware = is_hardware_float_v<W> || (is_hardware_float_v<Ws> || ...);

#define WALNUT_MATH_HW_FALLBACK(NAME)                                                                                  \
    template <typename W, typename... Ws>                                                                              \
    inline auto hw(rank<0>, const W& w, const Ws&... ws) {                                                             \
        using H = std::conditional_t<is_hardware_float_v<W>, W, double>;                                               \
        return hardware_back<H>(::walnut_rt::math::NAME(hardware_widen(w), hardware_widen(ws)...));                    \
    }

#define WALNUT_MATH_FN_STD(NAME, TARGET, STD)                                                                          \
    namespace fn_##NAME {                                                                                              \
        template <typename W, typename... Ws>                                                                          \
        auto hw(rank<1>, const W& w, const Ws&... ws) -> decltype(static_cast<W>(STD(w, static_cast<W>(ws)...))) {     \
            return static_cast<W>(STD(w, static_cast<W>(ws)...));                                                      \
        }                                                                                                              \
    }                                                                                                                  \
    WALNUT_MATH_FN(NAME, TARGET)

#define WALNUT_MATH_FN(NAME, TARGET)                                                                                   \
    template <typename W, typename... Ws>                                                                              \
    inline auto NAME(const W& w, const Ws&... ws);                                                                     \
    namespace fn_##NAME {                                                                                              \
        WALNUT_MATH_HW_FALLBACK(NAME)                                                                                  \
        template <typename... A>                                                                                       \
        auto go(rank<1>, const A&... a) -> decltype(::fizmo::math::TARGET(a..., std::declval<const mp::BigFloatContext&>())) { \
            return ::fizmo::math::TARGET(a..., context());                                                             \
        }                                                                                                              \
        template <typename... A>                                                                                       \
        auto go(rank<0>, const A&... a) -> decltype(::fizmo::math::TARGET(a...)) {                                    \
            return ::fizmo::math::TARGET(a...);                                                                        \
        }                                                                                                              \
    }                                                                                                                  \
    template <typename W, typename... Ws>                                                                              \
    inline auto NAME(const W& w, const Ws&... ws) {                                                                    \
        if constexpr (any_hardware<W, Ws...>) {                                                                        \
            return fn_##NAME::hw(rank<1>{}, w, ws...);                                                                 \
        } else if constexpr (any_bigfloat<decltype(unwrap(w)), decltype(unwrap(ws))...>) {                             \
            return rewrap<W>(fn_##NAME::go(rank<1>{}, unwrap(w), unwrap(ws)...));                                      \
        } else {                                                                                                       \
            return rewrap<W>(fn_##NAME::go(rank<0>{}, unwrap(w), unwrap(ws)...));                                      \
        }                                                                                                              \
    }

#define WALNUT_MATH_CONST(NAME)                                                                                        \
    namespace const_##NAME {                                                                                           \
        template <typename C = mp::BigFloatContext>                                                                    \
        auto big(rank<1>) -> decltype(mp::constants::NAME(std::declval<const C&>())) { return mp::constants::NAME(context()); } \
    }                                                                                                                  \
    template <typename W>                                                                                              \
    inline W NAME() {                                                                                                  \
        if constexpr (has_native<W>::value) return W(native_tag{}, const_##NAME::big(rank<1>{}));                     \
        else return mp::constants::NAME<W>();                                                                          \
    }

#define WALNUT_MATH_INDEXED_CONST(NAME, BIG)                                                                           \
    template <typename W>                                                                                              \
    inline W NAME(long long n) {                                                                                       \
        if (n < 0) fail(std::string(#NAME) + ": the index " + std::to_string(n) + " is negative");                    \
        const std::size_t k = static_cast<std::size_t>(n);                                                             \
        if constexpr (has_native<W>::value) return W(native_tag{}, BIG);                                               \
        else return mp::constants::NAME<W>(k);                                                                         \
    }

WALNUT_MATH_FN_STD(sin, sin, std::sin)
WALNUT_MATH_FN_STD(cos, cos, std::cos)
WALNUT_MATH_FN_STD(tan, tan, std::tan)
WALNUT_MATH_FN(csc, csc)
WALNUT_MATH_FN(sec, sec)
WALNUT_MATH_FN(cot, cot)
WALNUT_MATH_FN_STD(sinh, sinh, std::sinh)
WALNUT_MATH_FN_STD(cosh, cosh, std::cosh)
WALNUT_MATH_FN_STD(tanh, tanh, std::tanh)
WALNUT_MATH_FN(csch, csch)
WALNUT_MATH_FN(sech, sech)
WALNUT_MATH_FN(coth, coth)
WALNUT_MATH_FN_STD(arcsin, arcsin, std::asin)
WALNUT_MATH_FN_STD(arccos, arccos, std::acos)
WALNUT_MATH_FN_STD(arctan, arctan, std::atan)
WALNUT_MATH_FN(arccot, arccot)
WALNUT_MATH_FN(arcsec, arcsec)
WALNUT_MATH_FN(arccsc, arccsc)
WALNUT_MATH_FN_STD(arcsinh, arcsinh, std::asinh)
WALNUT_MATH_FN_STD(arccosh, arccosh, std::acosh)
WALNUT_MATH_FN_STD(arctanh, arctanh, std::atanh)
WALNUT_MATH_FN(arccoth, arccoth)
WALNUT_MATH_FN(arcsech, arcsech)
WALNUT_MATH_FN(arccsch, arccsch)
WALNUT_MATH_FN_STD(exp, exp, std::exp)
WALNUT_MATH_FN_STD(ln, ln, std::log)
WALNUT_MATH_FN_STD(log2, log2, std::log2)
WALNUT_MATH_FN_STD(log10, log10, std::log10)
WALNUT_MATH_FN_STD(sqrt, sqrt, std::sqrt)
WALNUT_MATH_FN_STD(cbrt, cbrt, std::cbrt)
WALNUT_MATH_FN(pow2, pow2)
WALNUT_MATH_FN(pow10, pow10)
WALNUT_MATH_FN_STD(log1p, log1p, std::log1p)
WALNUT_MATH_FN_STD(expm1, expm1, std::expm1)
WALNUT_MATH_FN(sinc, sinc)
WALNUT_MATH_FN(normalized_sinc, normalized_sinc)
WALNUT_MATH_FN(gudermannian, gudermannian)
WALNUT_MATH_FN(inv_gudermannian, inv_gudermannian)
WALNUT_MATH_FN_STD(erf, erf, std::erf)
WALNUT_MATH_FN_STD(erfc, erfc, std::erfc)
WALNUT_MATH_FN(erfi, erfi)
WALNUT_MATH_FN(inv_erf, inv_erf)
WALNUT_MATH_FN(inv_erfc, inv_erfc)
WALNUT_MATH_FN(inv_erfi, inv_erfi)
WALNUT_MATH_FN_STD(gamma, gamma, std::tgamma)
WALNUT_MATH_FN(log_gamma, log_gamma)
WALNUT_MATH_FN_STD(log_abs_gamma, log_abs_gamma, std::lgamma)
WALNUT_MATH_FN(factorial, factorial)
WALNUT_MATH_FN(digamma, digamma)
WALNUT_MATH_FN(trigamma, trigamma)
WALNUT_MATH_FN(harmonic, harmonic)
WALNUT_MATH_FN(riemann_zeta, riemann_zeta)
WALNUT_MATH_FN(riemann_xi, riemann_xi)
WALNUT_MATH_FN(riemann_chi, riemann_chi)
WALNUT_MATH_FN(dirichlet_eta, dirichlet_eta)
WALNUT_MATH_FN(dirichlet_lambda, dirichlet_lambda)
WALNUT_MATH_FN(dirichlet_beta, dirichlet_beta)
WALNUT_MATH_FN(dilogarithm, dilogarithm)
WALNUT_MATH_FN(trilogarithm, trilogarithm)
WALNUT_MATH_FN(rogers_l, rogers_l)
WALNUT_MATH_FN(rogers_l_normalized, rogers_l_normalized)
WALNUT_MATH_FN(spence_function, spence_function)
WALNUT_MATH_FN(spence_integral, spence_integral)
WALNUT_MATH_FN(airy_ai, airy_ai)
WALNUT_MATH_FN(airy_bi, airy_bi)
WALNUT_MATH_FN(scorer_gi, scorer_gi)
WALNUT_MATH_FN(scorer_hi, scorer_hi)
WALNUT_MATH_FN(elliptic_k, elliptic_k)
WALNUT_MATH_FN(elliptic_e, elliptic_e)
WALNUT_MATH_FN(lambert_w, lambert_w)
WALNUT_MATH_FN(lambert_w0, lambert_w0)
WALNUT_MATH_FN(lambert_wm1, lambert_wm1)
WALNUT_MATH_FN(fresnel_c, fresnel_c)
WALNUT_MATH_FN(fresnel_s, fresnel_s)
WALNUT_MATH_FN(sin_integral, sin_integral)
WALNUT_MATH_FN(cos_integral, cos_integral)
WALNUT_MATH_FN(sinh_integral, sinh_integral)
WALNUT_MATH_FN(cosh_integral, cosh_integral)
WALNUT_MATH_FN(exp_integral, exp_integral)
WALNUT_MATH_FN(log_integral, log_integral)
WALNUT_MATH_FN(clausen_integral, clausen_integral)
WALNUT_MATH_FN_STD(floor, floor, std::floor)
WALNUT_MATH_FN_STD(ceiling, ceiling, std::ceil)
WALNUT_MATH_FN_STD(round, round, std::round)
WALNUT_MATH_FN_STD(trunc, trunc, std::trunc)
WALNUT_MATH_FN(fractional_part, fractional_part)
WALNUT_MATH_FN_STD(atan2, atan2, std::atan2)
WALNUT_MATH_FN_STD(pow, pow, std::pow)
WALNUT_MATH_FN(log, log)
WALNUT_MATH_FN(nth_root, nth_root)
WALNUT_MATH_FN_STD(fmod, fmod, std::fmod)
WALNUT_MATH_FN_STD(remainder, remainder, std::remainder)
WALNUT_MATH_FN(mod_floor, mod_floor)
WALNUT_MATH_FN(mod_euclid, mod_euclid)
WALNUT_MATH_FN(generalized_erf, generalized_erf)
WALNUT_MATH_FN(generalized_erfc, generalized_erfc)
WALNUT_MATH_FN(beta, beta)
WALNUT_MATH_FN(log_beta, log_beta)
WALNUT_MATH_FN(log_abs_beta, log_abs_beta)
WALNUT_MATH_FN(legendre_chi, legendre_chi)
WALNUT_MATH_FN(hurwitz_zeta, hurwitz_zeta)
WALNUT_MATH_FN(polylog, polylog)
WALNUT_MATH_FN(carlson_rc, carlson_rc)
WALNUT_MATH_FN(elliptic_f, elliptic_f)
WALNUT_MATH_FN(elliptic_pi, elliptic_pi)
WALNUT_MATH_FN(jacobi_am, jacobi_am)
WALNUT_MATH_FN(jacobi_sn, jacobi_sn)
WALNUT_MATH_FN(jacobi_cn, jacobi_cn)
WALNUT_MATH_FN(jacobi_dn, jacobi_dn)
WALNUT_MATH_FN(jacobi_ns, jacobi_ns)
WALNUT_MATH_FN(jacobi_nc, jacobi_nc)
WALNUT_MATH_FN(jacobi_nd, jacobi_nd)
WALNUT_MATH_FN(jacobi_sc, jacobi_sc)
WALNUT_MATH_FN(jacobi_sd, jacobi_sd)
WALNUT_MATH_FN(jacobi_cs, jacobi_cs)
WALNUT_MATH_FN(jacobi_cd, jacobi_cd)
WALNUT_MATH_FN(jacobi_ds, jacobi_ds)
WALNUT_MATH_FN(jacobi_dc, jacobi_dc)
WALNUT_MATH_FN(generalized_gaussian_integral, generalized_gaussian_integral)
WALNUT_MATH_FN(regularized_gamma_p, regularized_gamma_p)
WALNUT_MATH_FN(regularized_gamma_q, regularized_gamma_q)
WALNUT_MATH_FN(lower_incomplete_gamma, lower_incomplete_gamma)
WALNUT_MATH_FN(upper_incomplete_gamma, upper_incomplete_gamma)
WALNUT_MATH_FN(inverse_regularized_gamma_p, inverse_regularized_gamma_p)
WALNUT_MATH_FN(inverse_regularized_gamma_q, inverse_regularized_gamma_q)
WALNUT_MATH_FN(rising_pochhammer, rising_pochhammer)
WALNUT_MATH_FN(falling_pochhammer, falling_pochhammer)
WALNUT_MATH_FN(permutation, permutation)
WALNUT_MATH_FN(combination, combination)
WALNUT_MATH_FN(polygamma, polygamma)
WALNUT_MATH_FN(chebyshev_t, chebyshev_t)
WALNUT_MATH_FN(chebyshev_u, chebyshev_u)
WALNUT_MATH_FN(chebyshev_v, chebyshev_v)
WALNUT_MATH_FN(chebyshev_w, chebyshev_w)
WALNUT_MATH_FN(fibonacci, fibonacci)
WALNUT_MATH_FN(lucas, lucas)
WALNUT_MATH_FN(fibonacci_polynomial, fibonacci_polynomial)
WALNUT_MATH_FN(lucas_polynomial, lucas_polynomial)
WALNUT_MATH_FN(legendre_p, legendre_p)
WALNUT_MATH_FN(legendre_q, legendre_q)
WALNUT_MATH_FN(exp_integral_generalized, exp_integral_generalized)
WALNUT_MATH_FN(log_integral_generalized, log_integral_generalized)
WALNUT_MATH_FN(clausen, clausen)
WALNUT_MATH_FN(debye, debye)
WALNUT_MATH_FN(debye_integral, debye_integral)
WALNUT_MATH_FN(debye_tail, debye_tail)
WALNUT_MATH_FN(lerch_transcendent, lerch_transcendent)
WALNUT_MATH_FN(carlson_rf, carlson_rf)
WALNUT_MATH_FN(carlson_rd, carlson_rd)
WALNUT_MATH_FN(carlson_rg, carlson_rg)
WALNUT_MATH_FN(generalized_gaussian_moment, generalized_gaussian_moment)
WALNUT_MATH_FN(regularized_beta, regularized_beta)
WALNUT_MATH_FN(regularized_beta_complement, regularized_beta_complement)
WALNUT_MATH_FN(incomplete_beta, incomplete_beta)
WALNUT_MATH_FN(inverse_regularized_beta, inverse_regularized_beta)
WALNUT_MATH_FN(inverse_regularized_beta_complement, inverse_regularized_beta_complement)
WALNUT_MATH_FN(carlson_rj, carlson_rj)
WALNUT_MATH_FN(log_base, log)
WALNUT_MATH_FN_STD(abs, abs, std::fabs)
WALNUT_MATH_CONST(sqrt2)
WALNUT_MATH_CONST(inv_sqrt2)
WALNUT_MATH_CONST(sqrt3)
WALNUT_MATH_CONST(inv_sqrt3)
WALNUT_MATH_CONST(sqrt5)
WALNUT_MATH_CONST(inv_sqrt5)
WALNUT_MATH_CONST(ln2)
WALNUT_MATH_CONST(inv_ln2)
WALNUT_MATH_CONST(ln10)
WALNUT_MATH_CONST(inv_ln10)
WALNUT_MATH_CONST(e)
WALNUT_MATH_CONST(inv_e)
WALNUT_MATH_CONST(phi)
WALNUT_MATH_CONST(inv_phi)
WALNUT_MATH_CONST(psi)
WALNUT_MATH_CONST(pi)
WALNUT_MATH_CONST(two_pi)
WALNUT_MATH_CONST(half_pi)
WALNUT_MATH_CONST(third_pi)
WALNUT_MATH_CONST(quarter_pi)
WALNUT_MATH_CONST(sixth_pi)
WALNUT_MATH_CONST(pi_180)
WALNUT_MATH_CONST(inv_pi)
WALNUT_MATH_CONST(two_inv_pi)
WALNUT_MATH_CONST(inv_two_pi)
WALNUT_MATH_CONST(inv_pi_180)
WALNUT_MATH_CONST(zeta2)
WALNUT_MATH_CONST(apery)
WALNUT_MATH_CONST(sqrt_pi)
WALNUT_MATH_CONST(inv_sqrt_pi)
WALNUT_MATH_CONST(two_inv_sqrt_pi)
WALNUT_MATH_CONST(euler_mascheroni)
WALNUT_MATH_CONST(silver_ratio)
WALNUT_MATH_CONST(bronze_ratio)
WALNUT_MATH_CONST(silver_constant)
template <typename W>
inline W catalan() {
    if constexpr (has_native<W>::value) return W(native_tag{}, ::fizmo::math::dirichlet_beta(mp::BigFloat(2), context()));
    else return mp::constants::catalan<W>();
}
WALNUT_MATH_INDEXED_CONST(stieltjes, mp::constants::stieltjes(k, context()))
WALNUT_MATH_INDEXED_CONST(bernoulli, mp::constants::bernoulli_float(k, context()))
WALNUT_MATH_INDEXED_CONST(metallic_ratio, mp::constants::metallic_ratio(k, context()))
WALNUT_MATH_INDEXED_CONST(inv_metallic_ratio, mp::constants::inv_metallic_ratio(k, context()))
WALNUT_MATH_INDEXED_CONST(metallic_mean, mp::constants::metallic_mean(k, context()))
WALNUT_MATH_INDEXED_CONST(inv_metallic_mean, mp::constants::inv_metallic_mean(k, context()))
WALNUT_MATH_INDEXED_CONST(beraha, mp::constants::beraha(k, context()))
WALNUT_MATH_INDEXED_CONST(tangent_number, mp::BigFloat(mp::constants::tangent_number(k)))
WALNUT_MATH_INDEXED_CONST(secant_number, mp::BigFloat(mp::constants::secant_number(k)))
WALNUT_MATH_INDEXED_CONST(euler_number, mp::BigFloat(mp::constants::euler_number(k)))

template <typename W>
inline W eulerian_number(long long n, long long m) {
    if (n < 0) fail("eulerian_number: the index " + std::to_string(n) + " is negative");
    if (m < 0) fail("eulerian_number: the index " + std::to_string(m) + " is negative");
    const std::size_t a = static_cast<std::size_t>(n);
    const std::size_t b = static_cast<std::size_t>(m);
    if constexpr (has_native<W>::value) return W(native_tag{}, mp::BigFloat(mp::constants::eulerian_number(a, b)));
    else return mp::constants::eulerian_number<W>(a, b);
}

#undef WALNUT_MATH_FN
#undef WALNUT_MATH_CONST
#undef WALNUT_MATH_INDEXED_CONST

inline void set_precision(long long bits) {
    if (bits < 2) fail("set_precision: a BigFloat needs at least 2 bits, not " + std::to_string(bits));
    mp::BigFloatContext::current().precision = mp::BigFloatContext::clamp_precision(static_cast<std::size_t>(bits));
}

inline long long precision() { return static_cast<long long>(mp::BigFloatContext::current().precision); }

inline void set_digits(long long digits) {
    if (digits < 1) fail("set_digits: a BigFloat needs at least 1 digit, not " + std::to_string(digits));
    mp::BigFloatContext::current().precision = mp::BigFloatContext::precision_from_dps(static_cast<std::size_t>(digits));
}

inline long long digits() { return static_cast<long long>(mp::BigFloatContext::current().dps()); }

inline mp::BigFloat parse_big(const std::string& text) {
    mp::BigFloat v(text);
    if (v.is_undefined()) fail("BigFloat: '" + text + "' isn't a number");
    return v;
}

inline std::string big_to_string(const mp::BigFloat& v, long long digits) {
    if (digits < 1) fail("to_string: the digit count " + std::to_string(digits) + " must be at least 1");
    return v.to_string(mp::BigFloat::no_digit_limit, -5, static_cast<std::size_t>(digits));
}

inline std::string big_complex_to_string(const mp::BigComplex& v, long long digits) {
    if (digits < 1) fail("to_string: the digit count " + std::to_string(digits) + " must be at least 1");
    return v.to_string(mp::BigFloat::no_digit_limit, -5, static_cast<std::size_t>(digits));
}

template <typename F>
inline mp::BigFloat big_from(const F& v) { return mp::BigFloat(v); }

template <typename F>
inline mp::BigComplex big_complex_of(const F& re) { return mp::BigComplex(mp::BigFloat(re)); }

template <typename F>
inline mp::BigComplex big_complex_of(const F& re, const F& im) { return mp::BigComplex(mp::BigFloat(re), mp::BigFloat(im)); }

inline mp::BigComplex big_polar(const mp::BigFloat& r, const mp::BigFloat& theta) {
    return mp::BigComplex(::fizmo::math::cos(theta, context()) * r, ::fizmo::math::sin(theta, context()) * r);
}

inline mp::BigFloat big_magnitude(const mp::BigComplex& z) { return z.magnitude(context()); }
inline mp::BigFloat big_argument(const mp::BigComplex& z) { return z.argument(context()); }

template <typename F>
inline F to_float(const mp::BigFloat& v) { return static_cast<F>(v); }

} // namespace math
} // namespace walnut_rt

#endif // WALNUT_MATH_HPP
