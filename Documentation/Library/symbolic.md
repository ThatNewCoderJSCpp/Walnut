# symbolic

`symbolic` does exact algebra on expressions such as `x ** 2 * sin(x) + 1/3`: building them, simplifying, differentiating, substituting, turning them into MathText, and evaluating them in any float type or `BigFloat`.

```walnut
import module.symbolic
```

Everything in `symbolic` is in the namespace `std::symbolic`. It uses [math](math.md), so importing `symbolic` also makes `std::math` available. It is built on Fizmo's computer algebra system.

## Contents

1. [Expressions](#expressions)
2. [Making expressions](#making-expressions)
3. [Exact numbers](#exact-numbers)
4. [Operators](#operators)
5. [Functions](#functions)
6. [Simplifying, differentiating and substituting](#simplifying-differentiating-and-substituting)
7. [Questions about an expression](#questions-about-an-expression)
8. [Printing and MathText](#printing-and-mathtext)
9. [Evaluating](#evaluating)
10. [Errors](#errors)
11. [Differences from C++](#differences-from-c)

## Expressions

An `Expression` is a formula made of numbers, symbols, `pi`, `e`, operators and functions:

```walnut
import module.symbolic
import module.io

using Expression = std::symbolic::Expression;

function main() -> int {
    const Expression: x = std::symbolic::symbol("x");
    const Expression: f = x ** 2 * std::symbolic::sin(x) + 3 * x - std::symbolic::rational(5, 2);
    std::io::println(f.derivative("x").simplify().to_string());
    std::io::println(f.to_mathtext_infix());
    std::io::println(f.evaluate<float>("x", 0.5));
    return 0;
} end function main
```

This prints the derivative `2 * x * sin(x) + x^2 * cos(x) + 3`, then `x^2 * sin(x) + 3 * x - 5/2`, then `-0.8801436153489492499316780161961071`.

Expressions are values: copying one is cheap, and methods such as `simplify` give a new expression instead of changing the old one. Equal expressions share their storage, so building the same formula twice costs almost nothing.

## Making expressions

| Function | Gives |
|---|---|
| `symbol(name)` | The symbol `name`, such as `symbol("x")` |
| `number(value)` | A number from any integer or float type, or from a `std::math::BigFloat` |
| `number(digits)` | An exact number from a string: `"42"`, `"0.1"`, `"-2.5e-3"` or `"1/3"` |
| `rational(numerator, denominator)` | The exact fraction `numerator / denominator` |
| `pi()`, `e()` | The constants π and e, kept as symbols, not rounded |
| `positive_infinity()` | ∞ |
| `Expression()` | An empty, invalid expression (see [is_valid](#questions-about-an-expression)) |

`symbol` is not called `variable` because `variable` is a keyword in Walnut, and `positive_infinity` is not called `infinity` for the same reason.

## Exact numbers

Numbers in an expression are exact fractions with no size limit, so `1/3` stays `1/3` and `0.1 + 0.2 - 0.3` simplifies to exactly `0`. Nothing is rounded until you [evaluate](#evaluating).

A float passed to `number`, or used in a mixed operator such as `x * 0.1`, becomes the shortest fraction that rounds back to it, so `number(0.1)` is `1/10`, not the binary value nearest to 0.1. A `BigFloat` is converted exactly. To be sure of the value, pass a string: `number("0.1")`.

`pi()` and `e()` are symbols, so `sin(pi() / 6)` simplifies to `1/2` and `ln(e())` to `1`.

## Operators

`+`, `-`, `*`, `/`, `**` (power) and `%` (remainder) work between expressions, and `+`, `-`, `*`, `/` and `**` also between an expression and any integer or float, on either side: `3 * x`, `x / 2`, `2 ** x`. `+=`, `-=`, `*=` and `/=` work with another expression. Unary `-` negates.

The operators build the formula without simplifying it. Call `simplify` or `full_simplify` when you want it tidied.

`==` and `!=` compare symbolically: `a == b` is true when `a - b` simplifies to `0`, so `(x + 1) ** 2 == x ** 2 + 2 * x + 1` is true. `identical` checks whether two expressions are written the same way.

## Functions

Every function takes and gives an `Expression`.

| Functions | |
|---|---|
| `sqrt`, `cbrt`, `root(x, n)`, `pow(base, exponent)`, `exp` | Powers and roots |
| `ln`, `log(x, base)`, `log2`, `log10` | Logarithms |
| `sin`, `cos`, `tan`, `sec`, `csc`, `cot` | Trigonometry |
| `arcsin`, `arccos`, `arctan`, `arcsec`, `arccsc`, `arccot` | Inverse trigonometry |
| `sinh`, `cosh`, `tanh`, `sech`, `csch`, `coth`, `arcsinh`, `arccosh`, `arctanh`, `arcsech`, `arccsch`, `arccoth` | Hyperbolic functions |
| `abs`, `sign`, `floor`, `ceiling`, `round`, `trunc`, `fractional_part`, `unit_step` | Pieces of numbers |
| `factorial`, `gamma`, `beta(a, b)`, `digamma`, `trigamma`, `polygamma(n, x)`, `combination(n, k)`, `permutation(n, k)` | Factorials and gamma |
| `erf`, `erfc`, `erfi`, `inv_erf`, `inv_erfc`, `inv_erfi` | Error functions |
| `exp_integral`, `log_integral`, `sin_integral`, `cos_integral`, `sinh_integral`, `cosh_integral`, `fresnel_s`, `fresnel_c` | Integral functions |
| `riemann_zeta`, `dilogarithm`, `trilogarithm`, `polylog(s, x)`, `lambert_w(x)`, `lambert_w(x, branch)` | Zeta, polylogarithms and Lambert W |
| `sinc`, `normalized_sinc`, `gudermannian`, `inv_gudermannian` | Others |

## Simplifying, differentiating and substituting

| Method | Gives |
|---|---|
| `simplify()` | The expression with numbers folded and like terms combined |
| `full_simplify()` | A stronger, slower simplification that also uses identities |
| `derivative(name)` | The derivative with respect to the symbol `name` |
| `derivative(name, order)` | The `order`th derivative; order `0` gives the expression back |
| `substitute(name, replacement)` | The expression with the symbol `name` replaced by another expression or by a number |

Antiderivatives and definite integrals are in [integration](integration.md).

## Questions about an expression

| Method | |
|---|---|
| `is_valid()` | False only for an empty `Expression()` |
| `is_number()`, `is_integer()` | Whether it is an exact number, or an exact whole number |
| `is_variable()` | Whether it is a single symbol |
| `is_constant()` | Whether it has no symbols, such as `pi() / 2` |
| `is_zero()` | Whether it simplifies to `0` |
| `depends_on(name)` | Whether the symbol `name` appears in it |
| `variable_count()`, `variable_name(index)` | The symbols it uses |
| `numerator()`, `denominator()` | The parts of an exact number, as strings of digits, since they can be any size |

## Printing and MathText

`to_string()` gives a readable form such as `x^2 * sin(x) + 3 * x - 5/2`, and `to_text()` gives the same as a `text`.

`to_mathtext()` gives the expression in MathText's brace form, which Fizmo's MathText renderer draws as typeset math:

```
subtract{add{multiply{power{"x", 2}, sin{"x"}}, multiply{3, "x"}}, fraction{5, 2}}
```

`to_mathtext_infix()` gives the same thing in MathText's ordinary infix form, `x^2 * sin(x) + 3 * x - 5/2`. Both parse with no errors in MathText, so they can be stored, edited or drawn.

## Evaluating

`evaluate<T>` works out the value of an expression in the type `T`, which can be any float type from `short short float` to `long long double`, or `std::math::BigFloat`:

| Method | |
|---|---|
| `evaluate<T>()` | The value of an expression with no symbols |
| `evaluate<T>(name, value)` | The value with the one symbol `name` set to `value` |
| `evaluate<T>(values)` | The value with symbols set from a `Values<T>` |

`Values<T>` holds a value for each symbol:

```walnut
std::symbolic::Values<float>: values;
values.set("x", 2.0);
values.set("y", 3.0);
const float: v = (x * y + y ** 2).evaluate<float>(values);
```

`set(name, value)`, `has(name)`, `clear()` and `size()` work as their names say.

The exact parts are rounded only at the end, in `T`, so a `BigFloat` evaluation gives every digit of the current [precision](math.md#precision) and a `long double` one every digit a `long double` holds.

## Errors

The class `std::errors::symbolic_error` is thrown, with a message from `what()`, when:

- an expression is evaluated with a symbol that has no value, or an empty expression is evaluated,
- `numerator` or `denominator` is used on something that is not an exact number,
- `derivative` is given a negative order,
- `variable_name` is given an index out of range,
- `symbol` is given an empty name, or `rational` a denominator of `0`.

A string passed to `number` that is not an exact number stops the program with a runtime error.

## Differences from C++

- C++ has no computer algebra in its standard library.
- Numbers are exact fractions, and `pi` and `e` are symbols, instead of being rounded to `double`.
- `**` is the power operator, and `==` compares mathematically rather than by how the expression is written.
