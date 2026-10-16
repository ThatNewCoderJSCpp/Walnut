# combinatorics

`combinatorics` counts arrangements and selections (`ncr`, `npr`, factorials, multinomials, derangements, lock codes) and has small helpers for combining probabilities.

```walnut
import module.combinatorics
```

Everything is in the namespace `std::combinatorics`.

## A first example

```walnut
import module.combinatorics
import module.io

using namespace std::combinatorics;

function main() -> int {
    std::io::println(ncr(10, 3), " ", npr(5, 2), " ", combinations(52l, 5l));
    std::io::println(lock_combinations(3, 10), " ", lock_permutations(3, 10), " ", derangements(4));
    return 0;
} end function main
```

This prints `120 20 2598960` and `220 1000 9`.

## Counting

The counting functions work with any built-in integer type, and the result has the same type as the arguments. Every step is checked: a result too large for the type throws a [`std::errors::combinatorics_error`](#errors) instead of wrapping around. Use a wider type, such as `long long int` or `long long integer`, for large counts. For example, `factorial(30lli)` works, but `factorial(25l)` does not fit in a `long int`.

Each function has two names. The short ones (`ncr`, `npr`, `lock_combinations`, `lock_permutations`) and the spelled-out ones do the same thing.

| Function | Gives |
|---|---|
| `factorial(n)` | n! |
| `ncr(n, r)`, `combinations(n, r)` | The number of ways to choose `r` of `n` things when order doesn't matter. 0 when `r > n` |
| `npr(n, r)`, `permutations(n, r)` | The number of ways to choose `r` of `n` things in order. 0 when `r > n` |
| `combinations_with_repetition(n, r)` | The number of ways to choose `r` of `n` kinds of thing when a kind can be picked more than once and order doesn't matter |
| `permutations_with_repetition(n, r)` | The number of sequences of length `r` from `n` kinds of thing: `n` to the power `r` |
| `lock_combinations(positions, symbols)` | The number of codes for a lock with `positions` places and `symbols` choices at each place, when order doesn't matter. The same as `combinations_with_repetition(symbols, positions)` |
| `lock_permutations(positions, symbols)` | The number of codes when order does matter: `symbols` to the power `positions` |
| `integer_power(base, exponent)` | `base` to the power `exponent` |
| `multinomial(groups)` | The number of ways to split `groups[0] + groups[1] + ...` things into groups of those sizes. `groups` is a `std::collections::Vector` |
| `derangements(n)` | The number of orderings of `n` things that leave none in its place |

Negative arguments throw a `combinatorics_error`.

## Probability

These work with any float type, including `BigFloat`:

| Function | Gives |
|---|---|
| `binomial_coefficient(n, k)` | The binomial coefficient for real `n` and `k`, through the gamma function |
| `probability_of_all(ps)` | The chance that every one of several independent events happens. `ps` is a `std::collections::Vector` |
| `probability_of_any(ps)` | The chance that at least one of them happens |
| `probability_of_union(a, b, both)` | P(A or B), from P(A), P(B) and P(A and B) |
| `conditional_probability(both, given)` | P(A given B), from P(A and B) and P(B) |
| `bayes(likelihood, prior, evidence)` | P(A given B) by Bayes' theorem, from P(B given A), P(A) and P(B) |
| `complement(p)` | 1 − `p` |

`probability_of_all` and `probability_of_any` throw a `combinatorics_error` for a probability outside 0 to 1. `conditional_probability` and `bayes` throw one when they would divide by 0.

## Errors

The functions above throw a `std::errors::combinatorics_error`. Its `what()` (or its `message` field) says what happened:

```walnut
import module.combinatorics
import module.io

function main() -> int {
    try {
        std::io::println(std::combinatorics::factorial(25l));
    } catch (const std::errors::combinatorics_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `problem: factorial() is too large for its integer type; use a wider one such as long long integer`.
