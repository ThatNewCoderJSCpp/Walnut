# constants

`constants` has mathematical constants such as `pi` and `e`, worked out to the full precision of whichever float type you ask for.

```walnut
import module.constants
```

Everything in `constants` is in the namespace `std::constants`. It uses [math](math.md), so importing `constants` also makes `std::math` available.

## Contents

1. [Using a constant](#using-a-constant)
2. [The constants](#the-constants)
3. [Errors](#errors)
4. [Differences from C++](#differences-from-c)

## Using a constant

Each constant is a function. Without a type it gives a `float`; with one it gives that type:

```walnut
import module.constants
import module.io

using std::constants::pi;

function main() -> int {
    float: p = pi();
    long double: q = pi<long double>();
    std::math::BigFloat: r = pi<std::math::BigFloat>();
    float: b = std::constants::bernoulli(4);
    std::io::println(pi<short float>(), " ", b);
    return 0;
} end function main
```

This prints `3.1415926535897933 -0.03333333333333333333333333333333333`. `q` has every digit a `long double` can hold, and `r` is worked out to the current `BigFloat` [precision](math.md#precision).

The type can be any float type, from `short short float` to `long long double`, or `std::math::BigFloat`.

## The constants

| Constants | |
|---|---|
| `pi`, `two_pi`, `half_pi`, `third_pi`, `quarter_pi`, `sixth_pi`, `pi_180` (`pi / 180`) | Multiples of pi |
| `inv_pi`, `two_inv_pi`, `inv_two_pi`, `inv_pi_180`, `sqrt_pi`, `inv_sqrt_pi`, `two_inv_sqrt_pi` | Reciprocals and roots of them |
| `e`, `inv_e`, `ln2`, `inv_ln2`, `ln10`, `inv_ln10` | e and logarithms |
| `sqrt2`, `inv_sqrt2`, `sqrt3`, `inv_sqrt3`, `sqrt5`, `inv_sqrt5` | Square roots |
| `phi`, `inv_phi`, `psi`, `silver_ratio`, `bronze_ratio`, `silver_constant` | The golden ratio, its conjugate `psi`, and their relatives |
| `euler_mascheroni`, `catalan`, `apery` (`zeta(3)`), `zeta2` (`zeta(2)`) | Other named constants |
| `bernoulli(n)`, `euler_number(n)`, `tangent_number(n)`, `secant_number(n)`, `stieltjes(n)` | Numbered sequences |
| `metallic_ratio(n)`, `inv_metallic_ratio(n)`, `metallic_mean(n)`, `inv_metallic_mean(n)`, `beraha(n)` | Numbered families of constants |

The numbered ones take a `long int` index: `bernoulli<long float>(10)`.

## Errors

A negative index for a numbered constant, such as `bernoulli(-1)`, stops the program with a runtime error.

## Differences from C++

- Constants are functions: `pi()` and `pi<long double>()` instead of C++'s `std::numbers::pi` and `std::numbers::pi_v<long double>`.
- There are many more of them, and they work for every Walnut float width and `BigFloat`.
