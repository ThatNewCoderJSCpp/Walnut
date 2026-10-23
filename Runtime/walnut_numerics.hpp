#ifndef WALNUT_NUMERICS_HPP
#define WALNUT_NUMERICS_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../numerics.hpp"

#include <cmath>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace numerics {

namespace fn = fizmo::numerics;

template <typename... T>
struct tag {};

template <typename T>
inline double nat(const T& v) { return to_native<double>(v); }

template <typename T>
inline T wal(double v) { return from_native<T>(v); }

template <typename T>
inline std::vector<double> nat(const std::vector<T>& v) {
    std::vector<double> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) out[i] = nat(v[i]);
    return out;
}

template <typename T>
inline std::vector<T> wal(const std::vector<double>& v) {
    std::vector<T> out;
    out.reserve(v.size());
    for (double d : v) out.push_back(wal<T>(d));
    return out;
}

template <typename V, typename = void>
struct has_native : std::false_type {};

template <typename V>
struct has_native<V, std::void_t<decltype(std::declval<const V&>().native)>> : std::true_type {};

template <typename T, typename V>
inline std::vector<T> unwrap(const V& v) {
    if constexpr (has_native<V>::value) return std::vector<T>(v.native.begin(), v.native.end());
    else return std::vector<T>(v.begin(), v.end());
}

template <typename T, typename V>
inline T unwrap_scalar(const V& v) {
    if constexpr (has_native<V>::value) return static_cast<T>(v.native);
    else return static_cast<T>(v);
}

template <typename T, typename F>
inline fn::OdeSystem<double> system(const F& f, std::size_t n) {
    return [f, n](const double& t, const std::vector<double>& y, std::vector<double>& d) {
        const std::vector<T> r = unwrap<T>(f(wal<T>(t), wal<T>(y)));
        if (r.size() != n) fail("the ODE function returned " + std::to_string(r.size()) + " values for a state of " + std::to_string(n));
        d = nat(r);
    };
}

inline fn::OdeMethod method_of(const std::string& name) {
    if (name == "euler") return fn::OdeMethod::Euler;
    if (name == "midpoint") return fn::OdeMethod::Midpoint;
    if (name == "heun") return fn::OdeMethod::Heun;
    if (name == "rk4") return fn::OdeMethod::Rk4;
    if (name == "backward euler") return fn::OdeMethod::BackwardEuler;
    if (name == "trapezoid") return fn::OdeMethod::Trapezoid;
    if (name == "bdf2") return fn::OdeMethod::Bdf2;
    if (name == "implicit midpoint") return fn::OdeMethod::ImplicitMidpoint;
    fail("unknown ODE method '" + name + "' (use euler, midpoint, heun, rk4, backward euler, trapezoid, bdf2 or implicit midpoint)");
}

template <typename T>
class ode_solution {
public:
    ode_solution() = default;
    explicit ode_solution(fn::OdeSolution<double> s) : m_s(std::move(s)) {}

    const fn::OdeSolution<double>& raw() const { return m_s; }

private:
    fn::OdeSolution<double> m_s;
};

template <typename T> inline long long solution_size(const ode_solution<T>& s) { return static_cast<long long>(s.raw().t.size()); }
template <typename T> inline bool solution_ok(const ode_solution<T>& s) { return s.raw().ok; }
template <typename T> inline std::string solution_message(const ode_solution<T>& s) { return s.raw().message; }
template <typename T> inline long long solution_steps(const ode_solution<T>& s) { return static_cast<long long>(s.raw().steps); }
template <typename T> inline long long solution_rejected(const ode_solution<T>& s) { return static_cast<long long>(s.raw().rejected); }
template <typename T> inline long long solution_evaluations(const ode_solution<T>& s) { return static_cast<long long>(s.raw().evaluations); }
template <typename T> inline bool solution_stopped(const ode_solution<T>& s) { return s.raw().stopped_by_event; }

template <typename T>
inline std::size_t index_of(const ode_solution<T>& s, long long i) {
    if (i < 0 || static_cast<std::size_t>(i) >= s.raw().t.size()) fail("step " + std::to_string(i) + " is out of range (the solution has " + std::to_string(s.raw().t.size()) + " points)");
    return static_cast<std::size_t>(i);
}

template <typename T> inline T solution_time(const ode_solution<T>& s, long long i) { return wal<T>(s.raw().t[index_of(s, i)]); }
template <typename T> inline std::vector<T> solution_state(const ode_solution<T>& s, long long i) { return wal<T>(s.raw().y[index_of(s, i)]); }
template <typename T> inline std::vector<T> solution_rate(const ode_solution<T>& s, long long i) { return wal<T>(s.raw().dydt[index_of(s, i)]); }
template <typename T> inline T solution_final_time(const ode_solution<T>& s) { return wal<T>(s.raw().t.back()); }
template <typename T> inline std::vector<T> solution_final_state(const ode_solution<T>& s) { return wal<T>(s.raw().y.back()); }
template <typename T> inline std::vector<T> solution_at(const ode_solution<T>& s, const T& t) { return wal<T>(s.raw().at(nat(t))); }
template <typename T> inline std::vector<T> solution_times(const ode_solution<T>& s) { return wal<T>(s.raw().t); }
template <typename T> inline std::vector<T> solution_component(const ode_solution<T>& s, long long k) {
    if (s.raw().y.empty() || k < 0 || static_cast<std::size_t>(k) >= s.raw().y[0].size()) fail("component " + std::to_string(k) + " is out of range");
    return wal<T>(s.raw().component(static_cast<std::size_t>(k)));
}
template <typename T> inline long long solution_event_count(const ode_solution<T>& s) { return static_cast<long long>(s.raw().events.size()); }
template <typename T> inline T solution_event_time(const ode_solution<T>& s, long long i) {
    if (i < 0 || static_cast<std::size_t>(i) >= s.raw().events.size()) fail("event " + std::to_string(i) + " is out of range");
    return wal<T>(s.raw().events[static_cast<std::size_t>(i)].t);
}
template <typename T> inline std::vector<T> solution_event_state(const ode_solution<T>& s, long long i) {
    if (i < 0 || static_cast<std::size_t>(i) >= s.raw().events.size()) fail("event " + std::to_string(i) + " is out of range");
    return wal<T>(s.raw().events[static_cast<std::size_t>(i)].y);
}

template <typename T>
inline fn::OdeOptions<double> options(const T& rtol, const T& atol) {
    fn::OdeOptions<double> o;
    o.relative_tolerance = nat(rtol);
    o.absolute_tolerance = nat(atol);
    if (!(o.relative_tolerance > 0.0) || !(o.absolute_tolerance > 0.0)) fail("the tolerances have to be positive");
    return o;
}

template <typename T, typename F>
inline ode_solution<T> solve_adaptive(const F& f, const T& t0, const std::vector<T>& y0, const T& t1, const T& rtol, const T& atol) {
    return ode_solution<T>(fn::solve_adaptive<double>(system<T>(f, y0.size()), nat(t0), nat(y0), nat(t1), options(rtol, atol)));
}

template <typename T, typename F>
inline ode_solution<T> solve_stiff(const F& f, const T& t0, const std::vector<T>& y0, const T& t1, const T& rtol, const T& atol) {
    return ode_solution<T>(fn::solve_stiff<double>(system<T>(f, y0.size()), nat(t0), nat(y0), nat(t1), options(rtol, atol)));
}

template <typename T, typename F>
inline ode_solution<T> solve_fixed(const F& f, const T& t0, const std::vector<T>& y0, const T& t1, long long steps, const std::string& method) {
    if (steps <= 0) fail("the number of steps has to be positive");
    return ode_solution<T>(fn::solve_fixed<double>(system<T>(f, y0.size()), nat(t0), nat(y0), nat(t1), static_cast<std::size_t>(steps), method_of(method)));
}

template <typename T, typename F, typename E>
inline ode_solution<T> solve_until(const F& f, const T& t0, const std::vector<T>& y0, const T& t1, const E& event, long long direction, const T& rtol, const T& atol) {
    fn::OdeOptions<double> o = options(rtol, atol);
    o.events.push_back({ [event](const double& t, const std::vector<double>& y) { return nat(unwrap_scalar<T>(event(wal<T>(t), wal<T>(y)))); }, direction < 0 ? -1 : direction > 0 ? 1 : 0, true });
    return ode_solution<T>(fn::solve_adaptive<double>(system<T>(f, y0.size()), nat(t0), nat(y0), nat(t1), o));
}

template <typename T, typename F>
inline ode_solution<T> solve_symplectic(const F& acc, const T& t0, const std::vector<T>& q0, const std::vector<T>& v0, const T& t1, long long steps, const std::string& method) {
    if (steps <= 0) fail("the number of steps has to be positive");
    if (q0.size() != v0.size()) fail("positions and velocities need the same size");
    fn::SymplecticMethod m = fn::SymplecticMethod::Verlet;
    if (method == "yoshida" || method == "yoshida4") m = fn::SymplecticMethod::Yoshida4;
    else if (method == "euler") m = fn::SymplecticMethod::Euler;
    else if (method != "verlet") fail("unknown symplectic method '" + method + "' (use verlet, yoshida or euler)");
    const std::size_t n = q0.size();
    fn::AccelerationFunction<double> a = [acc, n](const double& t, const std::vector<double>& q, const std::vector<double>& v, std::vector<double>& out) {
        const std::vector<T> r = unwrap<T>(acc(wal<T>(t), wal<T>(q), wal<T>(v)));
        if (r.size() != n) fail("the acceleration function returned " + std::to_string(r.size()) + " values for " + std::to_string(n) + " positions");
        out = nat(r);
    };
    return ode_solution<T>(fn::solve_symplectic<double>(a, nat(t0), nat(q0), nat(v0), nat(t1), static_cast<std::size_t>(steps), m));
}

template <typename T, typename F>
inline std::vector<T> newton(const F& f, const std::vector<T>& x0, const T& tolerance, long long max_iterations) {
    std::vector<double> x = nat(x0);
    fn::VectorFunction<double> g = [f](const std::vector<double>& v, std::vector<double>& out) { out = nat(unwrap<T>(f(wal<T>(v)))); };
    const fn::NewtonResult<double> r = fn::newton_dense<double>(g, x, nullptr, nat(tolerance), static_cast<int>(max_iterations));
    if (!r.converged) fail("Newton's method didn't converge (the residual is still " + std::to_string(r.residual) + ")");
    return wal<T>(x);
}

template <typename A>
inline const auto& unwrap_rows(const A& a) {
    if constexpr (has_native<A>::value) return a.native;
    else return a;
}

template <typename T, typename A>
inline std::vector<T> solve_linear(const A& rows, const std::vector<T>& b) {
    const std::size_t n = b.size();
    std::vector<std::vector<T>> a;
    for (const auto& r : unwrap_rows(rows)) a.push_back(unwrap<T>(r));
    if (a.size() != n) fail("the matrix needs one row for each entry of the right-hand side");
    fn::DenseMatrix<double> m(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].size() != n) fail("the matrix has to be square");
        for (std::size_t j = 0; j < n; ++j) m(i, j) = nat(a[i][j]);
    }
    std::vector<double> x = nat(b);
    if (!fn::solve_dense(m, x)) fail("the matrix is singular");
    return wal<T>(x);
}

template <typename T>
class sparse_matrix {
public:
    sparse_matrix() = default;
    explicit sparse_matrix(std::size_t n) : m_n(n) {}

    std::size_t m_n = 0;
    std::vector<fn::Triplet> m_entries;
};

template <typename T> inline sparse_matrix<T> make_sparse(long long n) {
    if (n <= 0) fail("a sparse matrix needs a positive size");
    return sparse_matrix<T>(static_cast<std::size_t>(n));
}

template <typename T> inline void sparse_add(sparse_matrix<T>& m, long long row, long long col, const T& value) {
    if (row < 0 || col < 0 || static_cast<std::size_t>(row) >= m.m_n || static_cast<std::size_t>(col) >= m.m_n) fail("(" + std::to_string(row) + ", " + std::to_string(col) + ") is outside the matrix");
    m.m_entries.push_back({ static_cast<std::size_t>(row), static_cast<std::size_t>(col), nat(value) });
}

template <typename T> inline long long sparse_size(const sparse_matrix<T>& m) { return static_cast<long long>(m.m_n); }
template <typename T> inline long long sparse_entries(const sparse_matrix<T>& m) { return static_cast<long long>(m.m_entries.size()); }

template <typename T> inline std::vector<T> sparse_multiply(const sparse_matrix<T>& m, const std::vector<T>& x) {
    if (x.size() != m.m_n) fail("the vector's size doesn't match the matrix");
    return wal<T>(fn::CsrMatrix::from_triplets(m.m_n, m.m_entries) * nat(x));
}

template <typename T> inline std::vector<T> sparse_solve(const sparse_matrix<T>& m, const std::vector<T>& b, const std::string& method) {
    if (b.size() != m.m_n) fail("the right-hand side's size doesn't match the matrix");
    fn::SparseSolver s;
    if (method == "auto" || method.empty()) s.method = fn::SparseMethod::Automatic;
    else if (method == "banded") s.method = fn::SparseMethod::BandedLU;
    else if (method == "gmres") s.method = fn::SparseMethod::Gmres;
    else if (method == "cg") s.method = fn::SparseMethod::ConjugateGradient;
    else if (method == "bicgstab") s.method = fn::SparseMethod::BiCgStab;
    else fail("unknown sparse method '" + method + "' (use auto, banded, gmres, cg or bicgstab)");
    if (!s.factor(fn::CsrMatrix::from_triplets(m.m_n, m.m_entries))) fail("the matrix is singular");
    std::vector<double> x = nat(b);
    if (!s.solve(x)) fail("the " + s.name() + " solver didn't converge");
    return wal<T>(x);
}

} // namespace numerics
} // namespace walnut_rt

#endif // WALNUT_NUMERICS_HPP
