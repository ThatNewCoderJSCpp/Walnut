# numerics

`numerics` solves ordinary differential equations, systems of nonlinear equations and linear systems, dense or sparse. It is Walnut's front end to Fizmo's numerics component, so the same solvers that run Arbor's and Sapling's simulations are available from any Walnut program.

```walnut
import module.numerics
```

Everything in `numerics` is in the namespace `std::numerics`. It uses [collections](collections.md), so importing `numerics` also makes that available. In every function, `T` can be any built-in float type. The solvers work in `double` internally, whatever `T` is.

## Contents

1. [Ordinary differential equations](#ordinary-differential-equations)
2. [Stopping at an event](#stopping-at-an-event)
3. [Mechanics](#mechanics)
4. [The solution](#the-solution)
5. [Nonlinear equations](#nonlinear-equations)
6. [Linear systems](#linear-systems)
7. [Errors](#errors)

## Ordinary differential equations

A system of equations y′ = f(t, y) is a function that takes the time and the current state and gives the rate of change of each component.

```walnut
import module.numerics
import module.io

using Vec = std::collections::Vector<double>;

function main() -> int {
    const auto: oscillator = [](double: t, Vec: y) -> Vec { return Vec(y[1], -y[0]); };
    const auto: s = std::numerics::solve<double>(oscillator, 0.0, Vec(1.0, 0.0), 10.0);
    std::io::println(`x(10) = ${s.final_state()[0]}`);
    std::io::println(`x(2.5) = ${s.at(2.5)[0]}`);
    return 0;
} end function main
```

| Function | |
|---|---|
| `solve<T>(f, t0, y0, t1, relative_tolerance = 1e-8, absolute_tolerance = 1e-10)` | Adaptive Dormand–Prince 5(4). The one to use for most problems |
| `solve_stiff<T>(f, t0, y0, t1, relative_tolerance = 1e-6, absolute_tolerance = 1e-9)` | Adaptive implicit solver for stiff problems, where `solve` would need tiny steps |
| `solve_fixed<T>(f, t0, y0, t1, steps, method = "rk4")` | `steps` equal steps of the named method |

The methods for `solve_fixed` are `"euler"`, `"midpoint"`, `"heun"`, `"rk4"`, `"backward euler"`, `"trapezoid"`, `"bdf2"` and `"implicit midpoint"`. The last four are implicit and solve a Newton system at every step.

A problem is stiff when it has parts that change on very different time scales, such as a chemical reaction with a fast and a slow step. If `solve` takes many thousands of steps for a smooth answer, try `solve_stiff`.

## Stopping at an event

`solve_until` stops the first time `event(t, y)` crosses zero. `direction` is `-1` to catch only falling crossings, `1` for rising ones and `0` (the default) for both. The stopping point is found to the solver's tolerance, not just to the nearest step.

```walnut
const auto: fall = [](double: t, Vec: y) -> Vec { return Vec(y[1], -9.81); };
const auto: ground = [](double: t, Vec: y) -> double { return y[0]; };
const auto: s = std::numerics::solve_until<double>(fall, 0.0, Vec(10.0, 0.0), 10.0, ground, -1);
std::io::println(`lands after ${s.final_time()} s`);
```

`stopped_by_event()` says whether the event happened before `t1`.

## Mechanics

`solve_symplectic` is for systems where the acceleration depends only on time, position and velocity, as in orbits and molecular dynamics. Its methods keep the energy of a conservative system from drifting, even over very long runs.

| Function | |
|---|---|
| `solve_symplectic<T>(acceleration, t0, q0, v0, t1, steps, method = "verlet")` | `steps` equal steps; `acceleration(t, q, v)` gives the acceleration of each coordinate |

The methods are `"verlet"` (2nd order), `"yoshida"` (4th order) and `"euler"` (symplectic Euler, 1st order). The states in the solution are the positions followed by the velocities, so with n coordinates, `state(i)[k]` is position `k` and `state(i)[n + k]` is its velocity.

```walnut
const auto: spring = [](double: t, Vec: q, Vec: v) -> Vec { return Vec(-q[0]); };
const auto: s = std::numerics::solve_symplectic<double>(spring, 0.0, Vec(1.0), Vec(0.0), 100.0, 10000, "yoshida");
```

## The solution

All the solvers give an `OdeSolution<T>`.

| Method | Gives |
|---|---|
| `ok()` | Whether the solver reached the end (or the event) |
| `message()` | Why it stopped early, when `ok()` is false |
| `size()` | The number of stored points, including the start |
| `time(i)`, `state(i)`, `rate(i)` | The time, state and rate of change at point `i` |
| `final_time()`, `final_state()` | The last point |
| `at(t)` | The state at any time between the start and the end, interpolated with cubic Hermite curves through the stored points and rates |
| `times()` | Every stored time |
| `component(k)` | Component `k` of the state at every stored time |
| `steps()`, `rejected()`, `evaluations()` | Accepted steps, rejected steps and calls to `f` |
| `stopped_by_event()` | Whether an event stopped the solver |
| `event_count()`, `event_time(i)`, `event_state(i)` | The events found |

`times()` and `component(k)` together are what to plot.

## Nonlinear equations

`newton` solves f(x) = 0 for a vector `x`, with the Jacobian worked out numerically.

```walnut
const auto: f = [](Vec: x) -> Vec { return Vec(x[0] * x[0] + x[1] * x[1] - 4.0, x[0] - x[1]); };
const Vec: root = std::numerics::newton<double>(f, Vec(1.0, 1.0));
```

| Function | |
|---|---|
| `newton<T>(f, x0, tolerance = 1e-12, max_iterations = 50)` | A root near `x0`. `f` must give as many values as `x` has |

## Linear systems

| Function | |
|---|---|
| `solve_linear<T>(a, b)` | x with a·x = b, by LU with partial pivoting; `a` is a `Vector` of rows |
| `SparseMatrix<T>(n)` | An empty n × n sparse matrix |
| `m.add(row, column, value)` | Adds `value` to an entry; adding to the same entry twice sums them |
| `m.size()`, `m.entries()` | n, and the number of entries added |
| `m.multiply(x)` | m·x |
| `m.solve(b, method)` | x with m·x = b |

The methods for `solve` are `"auto"` (a direct banded solve when the matrix is narrow, GMRES otherwise), `"banded"`, `"gmres"`, `"cg"` (conjugate gradients, for symmetric positive definite matrices) and `"bicgstab"`.

```walnut
std::numerics::SparseMatrix<double>: m = std::numerics::SparseMatrix<double>(100);
for (long int: i = 0; i < 100; i += 1) {
    m.add(i, i, 2.0);
    if (i > 0) then { m.add(i, i - 1, -1.0); }
    if (i < 99) then { m.add(i, i + 1, -1.0); }
}
Vec: b;
for (long int: i = 0; i < 100; i += 1) {
    b.push_back(1.0);
}
const Vec: x = m.solve(b, "cg");
```

## Errors

These throw a runtime error with a message saying what went wrong:

- a system function that gives a different number of values than the state has
- an unknown method name
- `newton` not converging within `max_iterations`
- a singular matrix in `solve_linear`, or a sparse solve that does not converge
- an index out of range in `time`, `state`, `rate`, `component` or `event_time`, or in `SparseMatrix::add`

A solver that cannot keep its step size above the smallest allowed does not throw: it gives a solution with `ok()` false and the points up to where it stopped.
