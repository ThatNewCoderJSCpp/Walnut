# math

`math` has the mathematical functions, for every float type, and three number types to use them with: `BigFloat`, `BigComplex` and `Complex<T>`. They are Fizmo's math library, used from Walnut as [C++ classes](../../documentation.md#using-c-classes).

```walnut
import module.math
```

Everything in `math` is in the namespace `std::math`.

## Contents

1. [A first example](#a-first-example)
2. [Which types the functions take](#which-types-the-functions-take)
3. [Basic functions](#basic-functions)
4. [Powers, roots and logarithms](#powers-roots-and-logarithms)
5. [Trigonometry](#trigonometry)
6. [Rounding and remainders](#rounding-and-remainders)
7. [Special functions](#special-functions)
8. [BigFloat](#bigfloat)
9. [BigComplex](#bigcomplex)
10. [Complex](#complex)
11. [Special results](#special-results)
12. [Errors](#errors)
13. [Differences from C++](#differences-from-c)

## A first example

```walnut
import module.math
import module.constants
import module.io

using std::math::sqrt;
using std::math::sin;
using std::constants::pi;

function main() -> int {
    float: hyp = sqrt(3.0 * 3.0 + 4.0 * 4.0);
    float: s = sin(pi() / 6.0);
    long float: precise = sqrt(2.0lf);
    std::io::println(hyp, " ", std::math::round(s * 100.0), " ", std::math::sqrt(16));
    return 0;
} end function main
```

This prints `5 50 4`. `precise` is the square root of 2 to about 72 digits, because `long float` is a 256-bit float.

## Which types the functions take

Each function works on:

- every float type, from `short short float` to `long long double`, and gives the same type back, worked out to that type's full precision;
- `BigFloat`, giving a `BigFloat` worked out to the current [precision](#precision);
- `Complex<T>` and `BigComplex`, giving a complex number back (except the real-only functions below);
- every integer type, which is turned into a `float` first, so `sqrt(16)` and `sin(1)` give a `float`.

When a function takes several numbers, they all have the same type: `pow(2.0, 3.0)` and `pow(2.0lf, 3.0lf)` work, but `pow(2.0, 3.0lf)` needs a `static_cast` first.

These functions take only real numbers (floats, `BigFloat` or integers), not complex ones: `atan2` and `atan(y, x)`, `expm1`, `log1p`, `fmod`, `remainder`, `mod_floor`, `mod_euclid`, `hypot`, `radians`, `degrees`, `sign`, the inverse beta functions, the inverse regularized gamma functions, and the generalized Gaussian integral and moment.

## Basic functions

| Function | Gives |
|---|---|
| `abs(x)` | The absolute value. For a complex number, its magnitude, as a real number |
| `sign(x)` | `-1`, `0` or `1` |
| `min(a, b)`, `max(a, b)` | The smaller or larger of the two; any type with `<` |
| `clamp(x, lo, hi)` | `x`, moved into the range `lo` to `hi` |
| `lerp(a, b, t)` | `a + (b - a) * t`, the point a fraction `t` of the way from `a` to `b` |
| `hypot(x, y)` | `sqrt(x * x + y * y)` without overflowing on the way |
| `gcd(a, b)`, `lcm(a, b)` | The greatest common divisor and least common multiple of two integers, of the same integer type |
| `arg(z)`, `conj(z)` | A complex number's angle and its conjugate |
| `radians(degrees)`, `degrees(radians)` | Converts an angle |

## Powers, roots and logarithms

| Function | Gives |
|---|---|
| `sqrt(x)`, `cbrt(x)` | The square and cube root |
| `nth_root(x, n)` | The `n`th root; `n` is a `long int` or the same type as `x` |
| `pow(x, y)` | `x` to the power `y`; `y` is a `long int` or the same type as `x` |
| `pow2(x)`, `pow10(x)` | 2 or 10 to the power `x` |
| `exp(x)`, `expm1(x)` | e to the power `x`, and that minus 1 (accurate for `x` near 0) |
| `ln(x)` or `log(x)` | The natural logarithm |
| `log(x, base)` | The logarithm to any base |
| `log2(x)`, `log10(x)` | Base 2 and base 10 logarithms |
| `log1p(x)` | `ln(1 + x)`, accurate for `x` near 0 |

## Trigonometry

Angles are in radians.

| Functions | |
|---|---|
| `sin`, `cos`, `tan`, `csc`, `sec`, `cot` | Trigonometric functions |
| `arcsin`, `arccos`, `arctan`, `arccot`, `arcsec`, `arccsc` | Their inverses; `asin`, `acos` and `atan` are other names for the first three |
| `atan2(y, x)` or `atan(y, x)` | The angle of the point `(x, y)`, from `-pi` to `pi` |
| `sinh`, `cosh`, `tanh`, `csch`, `sech`, `coth` | Hyperbolic functions |
| `arcsinh`, `arccosh`, `arctanh`, `arccoth`, `arcsech`, `arccsch` | Their inverses; `asinh`, `acosh` and `atanh` are other names for the first three |
| `sinc`, `normalized_sinc` | `sin(x) / x` and `sin(pi x) / (pi x)` |
| `gudermannian`, `inv_gudermannian` | The Gudermannian function and its inverse |

## Rounding and remainders

| Function | Gives |
|---|---|
| `floor(x)`, `ceiling(x)` (or `ceil(x)`) | Rounds down or up to a whole number |
| `round(x)` | Rounds to the nearest whole number, halves away from zero: `round(2.5)` is `3` |
| `trunc(x)` | Drops the fractional part: `trunc(-2.7)` is `-2` |
| `fractional_part(x)` | What `trunc` drops: `fractional_part(2.75)` is `0.75` |
| `fmod(x, y)` | The remainder of `x / y`, with the sign of `x` |
| `remainder(x, y)` | `x` minus the nearest multiple of `y` |
| `mod_floor(x, y)` | The remainder with the sign of `y` |
| `mod_euclid(x, y)` | The remainder that is never negative |

The rounding functions give a float of the same type, not an integer; `static_cast` turns the result into an integer.

## Special functions

| Group | Functions |
|---|---|
| Gamma | `gamma`, `log_gamma`, `log_abs_gamma`, `factorial`, `digamma`, `trigamma`, `polygamma(x, n)`, `harmonic`, `beta(a, b)`, `log_beta`, `log_abs_beta` |
| Incomplete gamma | `lower_incomplete_gamma(a, x)`, `upper_incomplete_gamma(a, x)`, `regularized_gamma_p(a, x)`, `regularized_gamma_q(a, x)`, `inverse_regularized_gamma_p(a, p)`, `inverse_regularized_gamma_q(a, q)` |
| Incomplete beta | `incomplete_beta(x, a, b)`, `regularized_beta(x, a, b)`, `regularized_beta_complement(x, a, b)`, `inverse_regularized_beta(p, a, b)`, `inverse_regularized_beta_complement(q, a, b)` |
| Counting | `rising_pochhammer(x, n)`, `falling_pochhammer(x, n)`, `permutation(n, k)`, `combination(n, k)` |
| Error functions | `erf`, `erfc`, `erfi`, `inv_erf`, `inv_erfc`, `inv_erfi`, `generalized_erf(a, b)`, `generalized_erfc(a, b)`, `generalized_gaussian_integral(a, b)`, `generalized_gaussian_moment(a, b, c)` |
| Zeta and related | `riemann_zeta`, `riemann_xi`, `riemann_chi`, `dirichlet_eta`, `dirichlet_lambda`, `dirichlet_beta`, `hurwitz_zeta(s, a)`, `legendre_chi(s, x)`, `lerch_transcendent(z, s, a)` |
| Polylogarithms | `dilogarithm`, `trilogarithm`, `polylog(s, x)`, `rogers_l`, `rogers_l_normalized`, `spence_function`, `spence_integral`, `clausen(x, n)`, `clausen_integral` |
| Exponential and trigonometric integrals | `exp_integral`, `exp_integral_generalized(x, n)`, `log_integral`, `log_integral_generalized(x, n)`, `sin_integral`, `cos_integral`, `sinh_integral`, `cosh_integral`, `fresnel_c`, `fresnel_s` |
| Lambert W | `lambert_w`, `lambert_w0`, `lambert_wm1`, `lambert_w(x, branch)` |
| Airy and Scorer | `airy_ai`, `airy_bi`, `scorer_gi`, `scorer_hi` |
| Elliptic integrals | `elliptic_k(m)`, `elliptic_e(m)`, `elliptic_e(phi, m)`, `elliptic_f(phi, m)`, `elliptic_pi(n, m)`, `elliptic_pi(n, phi, m)` |
| Carlson forms | `carlson_rc(x, y)`, `carlson_rf(x, y, z)`, `carlson_rd(x, y, z)`, `carlson_rg(x, y, z)`, `carlson_rj(x, y, z, p)` |
| Jacobi elliptic functions | `jacobi_am(u, m)`, and `jacobi_sn`, `jacobi_cn`, `jacobi_dn`, `jacobi_ns`, `jacobi_nc`, `jacobi_nd`, `jacobi_sc`, `jacobi_sd`, `jacobi_cs`, `jacobi_cd`, `jacobi_ds`, `jacobi_dc`, all `(u, m)` |
| Polynomials | `chebyshev_t(x, n)`, `chebyshev_u(x, n)`, `chebyshev_v(x, n)`, `chebyshev_w(x, n)`, `legendre_p(x, n)`, `legendre_q(x, n)`, `fibonacci_polynomial(x, n)`, `lucas_polynomial(x, n)` |
| Debye | `debye(x, n)`, `debye_integral(x, n)`, `debye_tail(x, n)` |

A parameter written `n` above is a `long int`; the others have the type of the first argument.

The incomplete beta functions take `x` first. For floats and `BigFloat` they need `x` from `0` to `1` and positive `a` and `b`, and give `nan` otherwise. For complex numbers they take any `z`, `a` and `b`: outside that range the result is the analytic continuation, with `z^a` and `(1 - z)^b` on their principal branches, so a real `z` above `1` gives the value just below the real axis and one below `0` the value just above it.

## BigFloat

A `BigFloat` is a float with as many bits as you ask for. Every `BigFloat` calculation is rounded to the current precision, which starts at 53 bits (about 15 digits) and is changed with `set_precision` or `set_digits`.

```walnut
import module.math
import module.io

using std::math::BigFloat;

function main() -> int {
    std::math::set_digits(50);
    BigFloat: two(2);
    BigFloat: root = std::math::sqrt(two);
    std::io::println(root);
    std::io::println(root.to_string(10), " ", root * root - 2 < BigFloat("1e-45"));
    BigFloat: third = 1 / BigFloat(3);
    std::io::println(static_cast<float>(third + 1));
    return 0;
} end function main
```

This prints the square root of 2 to 50 digits, then `1.414213562 1`, then `1.3333333333333333333333333333333333`.

| | |
|---|---|
| `BigFloat()`, `BigFloat(n)`, `BigFloat(x)` | Zero, an integer (up to `long int`), or a `float`, `long float`, `long long float`, `long double` or `long long double`, exactly |
| `BigFloat(digits)` | Reads a `string` such as `"3.25"` or `"-1e-40"`. It has to be written out, as `BigFloat("...")` |
| `to_string()`, `to_string(n)` | The number in decimal, with all the current digits or rounded to `n` digits |
| `is_zero()`, `is_negative()`, `is_positive()`, `is_integer()`, `is_finite()`, `is_infinite()`, `is_nan()` | Tests |
| `sign()`, `abs()` | `-1`, `0` or `1`, and the absolute value |
| `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `!=`, `<`, `>`, `<=`, `>=` | Arithmetic and comparisons, with another `BigFloat` or with an integer or float on either side |
| `static_cast<T>(x)` | The nearest value of a float type `T` |

`std::io::println` and `<<` write a `BigFloat` with `to_string()`.

### Precision

| Function | Does |
|---|---|
| `set_precision(bits)` | Sets the precision in bits, at least 2 |
| `set_digits(digits)` | Sets the precision to enough bits for that many decimal digits |
| `precision()`, `digits()` | The current precision in bits and in decimal digits |

The precision belongs to the thread that sets it: each thread starts at 53 bits. It applies to `BigComplex` too.

## BigComplex

A `BigComplex` is a complex number with a `BigFloat` real and imaginary part.

```walnut
import module.math
import module.io

using std::math::BigFloat;
using std::math::BigComplex;

function main() -> int {
    BigComplex: z(BigFloat(1), BigFloat(2));
    BigComplex: w = z * z;
    BigComplex: unit = std::math::exp(BigComplex::i());
    std::io::println(w, " ", w.real(), " ", w.magnitude().to_string(5));
    return 0;
} end function main
```

This prints `(-3, 4) -3 5`.

| | |
|---|---|
| `BigComplex()`, `BigComplex(re)`, `BigComplex(re, im)` | From `BigFloat`s, or from a `long int` or `float`s |
| `BigComplex::i()` | The imaginary unit |
| `BigComplex::polar(magnitude, angle)` | From polar coordinates |
| `real()`, `imaginary()`, `magnitude()`, `argument()`, `conjugate()` | The parts |
| `to_string()`, `to_string(n)` | The number as `(re, im)` |
| `is_zero()`, `is_real()`, `is_imaginary()`, `is_finite()` | Tests |
| `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `!=` | Arithmetic, with another `BigComplex` or with an integer or float on either side |

## Complex

`Complex<T>` is a complex number whose parts have the float type `T`.

```walnut
import module.math
import module.io

using std::math::Complex;

function main() -> int {
    Complex<float>: c(3.0, 4.0);
    std::io::println(c, " ", c.magnitude(), " ", c * 2.0, " ", std::math::sqrt(Complex<float>(-4.0)));
    return 0;
} end function main
```

This prints `3 + 4i 5 6 + 8i 2i`.

| | |
|---|---|
| `Complex<T>()`, `Complex<T>(re)`, `Complex<T>(re, im)` | From parts of type `T` |
| `Complex<T>::i()`, `Complex<T>::polar(magnitude, angle)` | The imaginary unit, and from polar coordinates |
| `real()`, `imaginary()`, `magnitude()`, `magnitude_squared()`, `argument()`, `conjugate()`, `reciprocal()` | The parts |
| `to_string()` | The number as `re + imi` |
| `is_zero()`, `is_real()`, `is_imaginary()`, `is_finite()` | Tests |
| `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `!=` | Arithmetic; `+`, `-`, `*` and `/` also take a `T` on the right |

## Special results

Walnut floats have special values instead of errors, and the functions use them: `sqrt(-1.0)` is `nan`, and `ln(0.0)` is `undefined`. `is_finite()` and the other float tests check for them. A complex function gives the complex answer instead: `sqrt(Complex<float>(-1.0))` is `1i`.

## Errors

These stop the program with a runtime error:

- `set_precision` below 2 bits, or `set_digits` below 1 digit;
- `BigFloat("...")` with text that isn't a number;
- `to_string(n)` with `n` below 1;

## Differences from C++

- The functions work on every Walnut float width, `BigFloat` and complex numbers, not only `float`, `double` and `long double`.
- Integers are turned into `float`, Walnut's 128-bit float, instead of `double`.
- `ln` is the natural logarithm, and `log(x)` is another name for it. `log(x, base)` takes a base.
- The inverse trigonometric functions are `arcsin` and so on; `asin`, `acos`, `atan`, `asinh`, `acosh` and `atanh` work too.
- Constants such as `pi` and `e` are in their own module, [constants](constants.md).
- `round` rounds halves away from zero, like C++'s `std::round`; `ceiling` is also called `ceil`.
