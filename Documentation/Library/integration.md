# integration

`integration` finds integrals: exact antiderivatives and definite integrals of [symbolic](symbolic.md) expressions, and numeric integrals of expressions or ordinary functions in any float type or `BigFloat`.

```walnut
import module.integration
```

Everything in `integration` is in the namespace `std::integration`. It uses [symbolic](symbolic.md), [math](math.md) and [constants](constants.md), so importing `integration` also makes those available.

## Contents

1. [Exact integrals](#exact-integrals)
2. [Numeric integrals of expressions](#numeric-integrals-of-expressions)
3. [Numeric integrals of functions](#numeric-integrals-of-functions)
4. [Choosing a method](#choosing-a-method)
5. [Errors](#errors)
6. [Differences from C++](#differences-from-c)

## Exact integrals

```walnut
import module.integration
import module.io

using Expression = std::symbolic::Expression;

function main() -> int {
    const Expression: x = std::symbolic::symbol("x");
    std::io::println(std::integration::integrate(std::symbolic::sin(std::symbolic::pi() * x), "x").to_string());
    std::io::println(std::integration::integrate(x ** 2, "x", 0, 3).to_string());
    std::io::println(std::integration::integrate(std::symbolic::sin(x), "x", std::symbolic::number(0), std::symbolic::pi()).to_string());
    return 0;
} end function main
```

This prints `-cos(π * x) / π`, `9` and `2`.

| Function | Gives |
|---|---|
| `integrate(f, name)` | An antiderivative of `f` with respect to the symbol `name` |
| `integrate(f, name, lower, upper)` | The definite integral from `lower` to `upper`, simplified; the bounds can be expressions or numbers |
| `antiderivative(f, name)` | The same as `integrate(f, name)` |
| `try_antiderivative(f, name, result)` | Puts an antiderivative in `result` and gives `true`, or gives `false` if none was found, without throwing |

The answers are exact, so `integrate(x ** 2, "x", 0, 1)` is `1/3`, not `0.3333…`. No constant of integration is added. When no antiderivative is found, `integrate` and `antiderivative` throw an `integration_error`; use `try_antiderivative` or [value](#numeric-integrals-of-expressions) when that can happen.

## Numeric integrals of expressions

These work out the value of a definite integral of an expression in `T`, which can be any float type or `std::math::BigFloat`. `tolerance` is how close to the true value the answer should be.

| Function | |
|---|---|
| `value<T>(f, name, a, b, tolerance)` | The exact integral, evaluated in `T`, when an antiderivative is found; otherwise a numeric one |
| `tanh_sinh<T>(f, name, a, b, tolerance, max_level = 14)` | Tanh-sinh (double exponential) quadrature |
| `adaptive_gauss<T>(f, name, a, b, tolerance, max_depth = 60)` | 16-point Gauss–Legendre, splitting the interval where needed |
| `adaptive_simpson<T>(f, name, a, b, tolerance, max_depth = 50)` | Simpson's rule, splitting the interval where needed |

`value` is the one to use in most programs. When there is no antiderivative it tries tanh-sinh, and then adaptive Gauss–Legendre if tanh-sinh does not settle:

```walnut
const Expression: x = std::symbolic::symbol("x");
std::math::set_digits(50);
const std::math::BigFloat: dream = std::integration::value(x ** x, "x", std::math::BigFloat(0), std::math::BigFloat(1), std::math::BigFloat("1e-45"));
```

`x ** x` has no antiderivative, and `dream` is `0.78343051071213440705926438652697546940768199014693`, right to the last digit.

`value`, `tanh_sinh` and `adaptive_gauss` never evaluate `f` at the ends of the interval, so integrands such as `x ** x` at `0`, or `1 / sqrt(x)`, that blow up or are undefined at an end can still be integrated. `tanh_sinh` also skips values that come out infinite or NaN. `adaptive_simpson` does use the ends.

## Numeric integrals of functions

These take an ordinary function or lambda, `function(T) -> T`, where `T` is any float type:

| Function | |
|---|---|
| `tanh_sinh(f, a, b, tolerance, max_level = 14)` | Tanh-sinh quadrature |
| `adaptive_gauss(f, a, b, tolerance, max_depth = 60)` | Adaptive 16-point Gauss–Legendre |
| `adaptive_simpson(f, a, b, tolerance, max_depth = 50)` | Adaptive Simpson's rule |
| `romberg(f, a, b, tolerance, max_steps = 20)` | Romberg integration |
| `gauss_legendre(f, a, b, points = 16)` | One Gauss–Legendre rule with 1 to 64 points, exact for polynomials of degree below `2 * points` |
| `trapezoid(f, a, b, steps = 1000)` | The trapezoid rule with `steps` equal pieces |
| `midpoint(f, a, b, steps = 1000)` | The midpoint rule with `steps` equal pieces |
| `simpson(f, a, b, steps = 1000)` | Simpson's rule with `steps` equal pieces (one more if `steps` is odd) |

```walnut
auto: bell = [](short float: t) -> short float { return std::math::exp(-t * t); };
std::io::println(std::integration::gauss_legendre(bell, 0.0s, 1.0s, 20));
std::io::println(std::integration::tanh_sinh([](float: t) -> float { return 1.0 / std::math::sqrt(t); }, 0.0, 1.0, 1e-25));
```

This prints `0.7468241328124272` and `2`.

The last three use the number of pieces you give and have no tolerance; the rest keep working until they reach the tolerance, and throw if they cannot.

## Choosing a method

- For an expression, use `value`.
- For a smooth function, or one that blows up only at the ends, use `tanh_sinh`. Its error roughly squares with each level, so it reaches high precision quickly.
- For a function with a kink or jump inside the interval, use `adaptive_gauss`, which splits the interval around the trouble.
- For a polynomial, or a smooth function where a fixed cost matters, use `gauss_legendre`.
- `trapezoid`, `midpoint`, `simpson`, `adaptive_simpson` and `romberg` are there for teaching and for comparing with other code.

## Errors

The class `std::errors::integration_error` is thrown, with a message from `what()`, when:

- `integrate(f, name)`, `integrate(f, name, lower, upper)` or `antiderivative` finds no antiderivative,
- `tanh_sinh`, `adaptive_gauss`, `adaptive_simpson` or `romberg` cannot reach the tolerance,
- a tolerance is not positive, a number of steps is not positive, or `gauss_legendre` is given fewer than 1 or more than 64 points.

`value` throws only if both of its numeric methods fail. A [symbolic_error](symbolic.md#errors) can also come through when an expression has a symbol other than `name` with no value.

## Differences from C++

- C++ has no integration in its standard library.
- Exact answers come from Fizmo's integrator, and the numeric methods work for every Walnut float width and `BigFloat`.
