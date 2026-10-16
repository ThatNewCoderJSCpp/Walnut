# random

`random` makes random numbers, booleans, characters and strings, and shuffles collections.

```walnut
import module.random
```

Everything in `random` is in the namespace `std::random`. The functions use Fizmo's random number generators: the free functions draw from the operating system's secure random source, and a [`Generator`](#generator) gives a repeatable sequence from a seed.

## Contents

1. [A first example](#a-first-example)
2. [Numbers](#numbers)
3. [Booleans](#booleans)
4. [Characters](#characters)
5. [Strings and text](#strings-and-text)
6. [Shuffling](#shuffling)
7. [Generator](#generator)
8. [Errors](#errors)

## A first example

```walnut
import module.random
import module.io

using std::random::random_int;
using std::random::random_float;
using std::random::random_bool;
using std::random::random_string;

function main() -> int {
    int: die = random_int(1, 6);
    float: angle = random_float(0.0, 360.0);
    bool: heads = random_bool();
    string: password = random_string(12);
    std::io::println("rolled ", die, ", password ", password);
    return 0;
} end function main
```

## Numbers

| Function | Gives |
|---|---|
| `random_int(lo, hi)` | An integer from `lo` to `hi`, both included. `lo` and `hi` have the same integer type, which is also the result's type |
| `random_int<T>()` | Any value of the integer type `T` |
| `random_float(lo, hi)` | A float between `lo` and `hi`, of the same float type as the bounds |
| `random_float<T>()` | A float of type `T` from `0` up to (but not including) `1` |

- Every integer and float width works, from `short short int` to `long long integer` and from `short short float` to `long long double`. Wide types get random bits for their whole width, not just 64 of them.
- The bounds can be given in either order.
- Every value in the range is equally likely.

## Booleans

`random_bool()` is `true` half the time. `random_bool(chance)` is `true` with the given probability, a `short float` from `0.0` (never) to `1.0` (always).

## Characters

| Function | Gives |
|---|---|
| `random_char()` | A letter (`A`-`Z`, `a`-`z`) or a digit (`0`-`9`) |
| `random_char(lo, hi)` | A character from `lo` to `hi`, both included, such as `random_char('a', 'z')`. Any Unicode range works; the surrogate code points `U+D800`-`U+DFFF` are skipped |
| `random_char(alphabet)` | One of the characters of the string `alphabet`, each equally likely |

## Strings and text

| Function | Gives |
|---|---|
| `random_string(length)` | `length` random letters and digits |
| `random_string(length, alphabet)` | `length` characters picked from `alphabet` |
| `random_text(...)` | The same two forms, giving a `text` (the alphabet is a `text` too) |

```walnut
import module.random

function main() -> int {
    string: code = std::random::random_string(6, "0123456789");
    text: greek = std::random::random_text(4, `αβγδ`);
    return 0;
} end function main
```

Here `code` is six digits and `greek` is four Greek letters. Lengths and alphabets count characters, not bytes.

## Shuffling

`shuffle(items)` puts the elements of anything with `size()` and `[]` in a random order, such as a `Vector`, a `DynamicArray` or an array. Every order is equally likely.

```walnut
import module.random
import module.collections

function main() -> int {
    std::collections::Vector<int>: deck{1, 2, 3, 4, 5};
    std::random::shuffle(deck);
    return 0;
} end function main
```

## Generator

A `Generator` has all the functions above as methods, but takes its numbers from a seeded generator (a 64-bit Mersenne Twister) instead of the operating system. Two generators with the same seed give the same sequence, so a game or a simulation can be replayed. Without a seed, a generator starts from a random one.

```walnut
import module.random

using std::random::Generator;

function main() -> int {
    Generator: a(2026ul);
    Generator: b(2026ul);
    bool: same = a.random_int(1, 100) == b.random_int(1, 100);
    a.seed(7ul);
    float: f = a.random_float<float>();
    return 0;
} end function main
```

Here `same` is always `true`.

| Method | Does |
|---|---|
| `Generator()`, `Generator(seed)` | A generator with a random seed, or with the given `unsigned long int` seed |
| `seed(value)` | Starts the sequence over from a new seed |
| `reseed()` | Starts over from a random seed |
| `random_int`, `random_float`, `random_bool`, `random_char`, `random_string`, `random_text`, `shuffle` | The same as the free functions |

A generator is fast, but its numbers are predictable to anyone who knows the seed, so use the free functions for passwords, keys and tokens.

## Errors

These stop the program with a runtime error:

- a `random_int` or `random_float` bound that is `inf`, `nan`, `undefined` or `indeterminate`;
- a negative length for `random_string` or `random_text`;
- an empty alphabet, or one that isn't valid UTF-8;
- a `random_char(lo, hi)` range that goes past `U+10FFFF` or holds only surrogates.
