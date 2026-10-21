# The Walnut Language

This is the reference for everything Walnut can do. For installing Walnut and running the compiler, see [README.md](README.md).

Walnut is statically typed and compiles to C++. Its numbers are [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) multiprecision numbers, so every integer and float has the same exact width on every platform.

This file describes the language itself. Walnut's library is documented separately, one page per module, in [Documentation/Library](Documentation/Library/README.md); `core` comes with every program without an `import`, and other modules such as `io` and `collections` are imported by name. Output, files and other system access come from C or C++ code you call directly; see [Calling C and C++](#calling-c-and-c).

## Contents

1. [A first program](#a-first-program)
2. [Source files](#source-files)
3. [Types](#types)
4. [Variables](#variables)
5. [References and pointers](#references-and-pointers)
6. [Arrays](#arrays)
7. [Strings, text and characters](#strings-text-and-characters)
8. [Enums, aliases, variants and unions](#enums-aliases-variants-and-unions)
9. [Operators](#operators)
10. [Conversions and casts](#conversions-and-casts)
11. [Checking types with is and as](#checking-types-with-is-and-as)
12. [Control flow](#control-flow)
13. [Functions](#functions)
14. [Exceptions](#exceptions)
15. [Templates and concepts](#templates-and-concepts)
16. [Lambdas](#lambdas)
17. [Coroutines](#coroutines)
18. [Classes and structs](#classes-and-structs)
19. [Copying and moving](#copying-and-moving)
20. [Inheritance](#inheritance)
21. [Operator overloading](#operator-overloading)
22. [Compile-time code](#compile-time-code)
23. [Type operations and traits](#type-operations-and-traits)
24. [Namespaces](#namespaces)
25. [Files, imports and modules](#files-imports-and-modules)
26. [Calling C and C++](#calling-c-and-c)
27. [The preprocessor](#the-preprocessor)
28. [The command line](#the-command-line)
29. [Left out on purpose](#left-out-on-purpose)

## A first program

```walnut
extern "C" function puts(string: line) -> int;

function main() -> int {
    puts("Hello, Walnut!");
    return 0;
} end function main
```

`walnut run hello.wal` prints `Hello, Walnut!`. `puts` is the C library's function; the `extern "C"` line declares it so Walnut can call it.

`main` returns the program's exit code. It is optional: a file made only of top-level statements runs from top to bottom like a script.

Most examples below don't print anything. They state the values their variables end up with.

## Source files

Walnut files end in `.wal`. Walnut header files, which are meant to be included or imported rather than compiled on their own, end in `.walh`; see [Header files](#header-files).

**Statements** end with `;`. Extra semicolons (`;;`) are allowed.

**Comments**
- `//` runs to the end of the line.
- `/* ... */` can span lines, and block comments nest.

**Blocks** are written with `{ }`. A block can be empty, on one line (`{ }`) or across several.

**Named declarations end with `end`.** Functions, classes, structs, unions and namespaces close with `end <kind> <name>`, and the name must match the opening name:

```walnut
namespace shapes {
    function area(int: w, int: h) -> int {
        return w * h;
    } end function area
} end namespace shapes
```

`if`, loops, `switch`, `enum` and lambdas don't use `end`.

**Keyword aliases.** Many keywords have alternative spellings that mean exactly the same thing:

| Keyword | Also written |
|---|---|
| `array` | `arr` |
| `auto` | `automatic` |
| `bool` | `boolean` |
| `char` | `character` |
| `const` | `constant` |
| `string` | `str` |
| `text` | `txt` |
| `float` | `dec`, `floater` |
| `var` (dynamic) | `variable`, `let`, `def`, `define`, `any` |
| `true` / `false` | `TRUE` / `FALSE`, `True` / `False` |
| `infinity` | `inf` |
| `inherits` | `inherit`, `extends`, `from` (in a class heading) |
| `sizeof`, `alignof`, `countof`, `typeof`, `typeid` | `size_of`, `align_of`, `count_of`, `type_of`, `type_id` |
| `alignas`, `bit_cast`, `thread_local`, `hoist` | `align_as`, `bitcast`, `threadlocal`, `hoisted` |
| `nodiscard`, `discard_nodiscard` | `no_discard`, `discard_no_discard` |

`integer`, `double` and `decimal` look like aliases but are wider types; see [Numbers](#numbers).

**Reserved words.** Keywords can't be used as variable, parameter, field or type names. The ones most likely to clash with a name you'd want are `in`, `is`, `as`, `local`, `text`, `arr`, `inf`, `nan`, `undefined`, `special`, `from`, `hidden` and `self`.

**Keywords as function and method names.** Where a keyword can't mean anything else, it can still name a function or method:
- Any keyword works as a member name after `.` and `->`: `list.end()`, `range.in(x)`, `p->from()`.
- These keywords can also name functions and methods, and be called on their own (`end()`, `in(3)`) or after `::` (`Shape::from(...)`): `end`, `in`, `is`, `as`, `from`, `then`, `down`, `inherits`, `extends`, `implicit`, `explicit`, `override`, `abstract`, `fallthrough`, `repeat`, `hoist`, `hidden`, `local`, `global`, `export`, `include`, `module`, `mutable`, `lazy`, `friend`, `virtual`, `concept`, `async`, `infinity` and `nan`. A name like that is only taken as a function when it is followed by `(`, so `repeat;` and `x is int` keep their usual meaning.

```walnut
class Range {
public:
    int: lo = 1;
    int: hi = 5;
    function in(int: x) const -> bool {
        return x >= lo && x < hi;
    } end function in
    function end() const -> int {
        return hi;
    } end function end
    function last() const -> int {
        return end() - 1;
    } end function last
} end class Range

function main() -> int {
    Range: r;
    bool: inside = r.in(3);
    int: top = r.last();
    return 0;
} end function main
```

Here `inside` is `true` and `top` is `4`.

## Types

### Numbers

Every number type has a fixed width. You pick the width with `short` and `long`: each one moves the type one step narrower or wider, and you can use up to two of them.

| Integer type | Bits | Float type | Bits |
|---|---|---|---|
| `short short int` | 8 | `short short float` | 32 |
| `short int` (or `short`) | 16 | `short float` | 64 |
| `int` | 32 | `float` | 128 |
| `long int` (or `long`) | 64 | `long float` | 256 |
| `long long int` (or `long long`) | 128 | `long long float` | 512 |
| `short integer` | 256 | `double` | 512 |
| `integer` | 512 | `long double` | 1024 |
| `long integer` | 1024 | `long long double` | 2048 |
| `long long integer` | 2048 | | |

The wide spellings start higher:
- `integer` is 4 steps above `int` (`short short integer` is 128 bits).
- `double` and `decimal` are 2 steps above `float` (`short double` is 256 bits).

The 64-bit `short float` and the 32-bit `short short float` have the same total size as C's `double` and `float`, but they aren't the same format. Fizmo floats don't use the IEEE layout, so each one has one more bit of precision than its C counterpart.

**Unsigned.** Any number type can be `unsigned`. The word goes first: `unsigned int`, `unsigned long int`, `unsigned float`. A bare `unsigned` means `unsigned int`.

**Unsigned floats** can't hold a negative value. A result that would be negative becomes 0.

```walnut
function main() -> int {
    unsigned float: price = 2.5;
    unsigned float: change = 1.0;
    change = change - price;
    return 0;
} end function main
```

Here `change` ends up as `0`.

**Infinity and other special values.** Every integer and float type can also hold `inf`, `-inf`, `nan`, `undefined` and `indeterminate`; see [Special values](#special-values).

### Number literals

| Form | Example |
|---|---|
| Decimal | `42`, `1_000_000`, `1'000'000` |
| Hexadecimal | `0xFF`, `0xFF_FF` |
| Octal | `0o17`, `0o7_55` |
| Binary | `0b1010`, `0b1010_1010` |
| Float | `2.5`, `5.`, `1e3`, `2.5E-2` |

**Digit separators.** `_` and `'` can sit between two digits anywhere in a number, and are ignored.

**Leading zeros.** A leading `0` doesn't make a number octal as it does in C: `017` is seventeen, and the compiler warns that it may have been meant as `0o17`.

**Types.** A plain integer literal gets the narrowest of `int`, `long int`, `long long int` and the wider types that can hold it. A plain float literal is a `float`.

**Suffixes** choose a literal's type directly:

| Suffix | Meaning |
|---|---|
| `u` | Unsigned |
| `s`, `ss` | One or two steps narrower |
| `l`, `ll` | One or two steps wider |
| `i` | The `integer` family |
| `f` | The `float` family |
| `d` | The `double` family |

A suffix is built from up to three parts, in any order and in upper or lower case:
- `u`, at most once.
- Up to two `s` or up to two `l`, which can't be mixed.
- At most one family letter: `i`, `f` or `d`.

Without a family letter, an integer literal is in the `int` family and a floating-point literal is in the `float` family. The `s` and `l` steps then count from that family's type:

| Literal | Type |
|---|---|
| `5`, `5s`, `5ll` | `int`, `short int`, `long long int` |
| `5i`, `5si`, `5lli` | `integer`, `short integer`, `long long integer` |
| `2.5`, `2.5s`, `2.5l` | `float`, `short float`, `long float` |
| `2.5d`, `2.5sd`, `2.5lld` | `double`, `short double`, `long long double` |
| `7ULL` | `unsigned long long int` |
| `1.25u` | `unsigned float` |

- `f` and `d` on an integer literal make it a floating-point value: `3f` is the `float` `3.0` and `2ld` is a `long double`.
- `i` only goes on integer literals; `2.5i` is an error.
- `f` and `d` can't go on hexadecimal or binary literals, because there they are digits: `0x1Fd` is the `int` `0x1FD`.

```walnut
function main() -> int {
    unsigned long long int: big = 7ULL;
    short short int: tiny = 100ss;
    short float: half = 0.5s;
    long int: mask = 0xFF_FFl;
    int: million = 1'000'000;
    return 0;
} end function main
```

**Checks**
- An integer that doesn't fit its suffix's type is a warning, and it wraps around: `300ss` becomes 44.
- Negating an unsigned literal (`-5u`) is a warning, because it wraps to a large positive value.
- A float too small for its suffix's type is a warning, because it becomes 0.
- A float too large for its type is an error, because the type can't represent it (`1e400s`). So is an integer too large for every integer type.
- Suffixes are part of the language. They can't be overloaded, and you can't define new ones.

### Special values

Every integer and float type, signed or unsigned, can hold these values besides ordinary numbers:

| Value | Meaning | Examples |
|---|---|---|
| `inf` and `-inf` (also `infinity`) | Positive or negative infinity | `inf`, `inf * 2`, a float too large for its type |
| `nan` | A result that exists but isn't a real number (it would be complex) | `(-8.0) ** 0.5` |
| `undefined` | A result that doesn't exist | `1 / 0`, `7 % 0`, `0 ** -1` |
| `indeterminate` | A form whose value can't be decided | `0 / 0`, `inf - inf`, `0 * inf`, `0 ** 0`, `1 ** inf` |

```walnut
function average(int: total, int: count) -> int {
    if (count == 0) then {
        return undefined;
    }
    return total / count;
} end function average

function main() -> int {
    int: a = average(10, 0);
    bool: empty = a is undefined;
    float: f = a;
    long int: n = 5;
    n = n * inf;
    unsigned int: cap = inf;
    auto: x = nan;
    return 0;
} end function main
```

Here `a` is `undefined`, `empty` is `true`, `f` is `undefined`, `n` is `inf`, `cap` is `inf` and `x` is a `float` holding `nan`.

**Where they come from**
- The keywords `inf`, `nan`, `undefined` and `indeterminate`. On their own they take the type they go into, so `int: x = undefined;` needs no cast. `auto` and `var` make them a `float`.
- Arithmetic: dividing a non-zero number by zero gives `undefined`, for floats as well as integers (not `inf` as in C). `0 / 0` and the other indeterminate forms give `indeterminate`, and a result that would be complex gives `nan`.
- A float that overflows becomes `inf`. Integer overflow still wraps around; it never becomes `inf`.

**Arithmetic with special values**
- They pass through: `undefined + 1` is `undefined`, `inf + 1` is `inf` and `-inf * 2` is `-inf`.
- When two different ones meet, `undefined` wins over `indeterminate`, which wins over `nan`.
- `x / inf` is `0` and `x % inf` is `x` for an ordinary `x`. `inf % x` is `undefined`.
- `&`, `|`, `^`, `~`, `<<` and `>>` on an integer holding a special value give `undefined`.
- Mixing a special value with a number gives the number's type: if `n` is a `long int`, `n + inf` is a `long int`.

**Comparisons**
- `nan`, `undefined` and `indeterminate` are unordered: `==`, `<`, `<=`, `>` and `>=` are `false` and `!=` is `true`, even when a value is compared with itself. Check for them with `is`.
- `inf` is greater than every number and `-inf` is less than every number. `inf == inf` is `true`.

**Checking with `is`.** `x is nan`, `x is undefined`, `x is indeterminate` and `x is inf` check which special value `x` holds. `x is inf` is true for both `inf` and `-inf`; use `x == -inf` to check the sign. These work on numbers, [special(T)](#specialt) values and `var` values.

**Unsigned types** have no `-inf`. Putting `-inf` into an unsigned type is a compile-time error when Walnut can see it; while the program runs it becomes `0`, the same way a negative unsigned float becomes `0`.

**Conversions** between number types keep the special value: a `float` holding `inf` cast to `int` is `inf`, and an `int` holding `nan` converted to `float` is `nan`.

**Constants.** Writing one of the keywords never warns. A constant expression that produces a special value by itself, such as `int: q = 10 / 0;`, is a warning, and `q` is `undefined`.

**Printing.** `inf` and `-inf` print as `∞` and `-∞`; the others print their names.

**Calling C and C++.** Passing an integer that holds a special value to a C or C++ integer parameter stops the program with a runtime error, because C integers can't hold them. A float passes `inf` as infinity and the other three as NaN.

### special(T)

Strings, characters, classes and other types that aren't numbers can't hold special values. `special(T)` holds either a `T` or one of the special values, so a function can return one when you allow it:

```walnut
function lookup(int: id) -> special(string) {
    if (id < 0) then {
        return undefined;
    }
    return "walnut";
} end function lookup

function main() -> int {
    special(string): name = lookup(-1);
    bool: missing = name is undefined;
    name = lookup(3);
    bool: found = name is string;
    string: s = name as string;
    return 0;
} end function main
```

Here `missing` is `true`, `found` is `true` and `s` is `"walnut"`.

- A `T` or a special value converts to `special(T)` implicitly.
- `is T` and `as T` work as they do on a [variant](#variants). `as T` stops the program with a runtime error when it holds a special value, so check with `is` first when you aren't sure.
- `is nan`, `is undefined`, `is indeterminate` and `is inf` check which special value it holds.
- `T` can't be a number type or `var`, because those already hold special values. It also can't be a reference, `void` or another `special(...)`.
- With no initializer, a `special(T)` holds a default `T`.

### Other types

| Type | What it holds |
|---|---|
| `bool` | `true` or `false` |
| `char` | One Unicode character (code point) |
| `string` | UTF-8 text |
| `text` | UTF-8 text. It works like `string`, but it is a separate type, so the two don't convert into each other implicitly |
| `auto` | Takes its type from the initializer |
| `var` | A dynamic value whose type can change at run time |
| `variant(T1, T2, ...)` | One value of one of the listed types; see [Variants](#variants) |
| `special(T)` | A `T` or one of the special values (`inf`, `nan`, `undefined`, `indeterminate`); see [special(T)](#specialt) |
| `void` | No value. Only used as a return type |

### Dynamic values

A `var` holds a number, `bool`, `char`, string or function, and checks operations when the program runs. With `var`, the colon is optional.

```walnut
function main() -> int {
    var x = 5;
    var y = x + 2.5;
    x = "now a string";
    var twice = [](int: n) -> int { return n * 2; };
    var r = twice(21);
    return 0;
} end function main
```

Here `y` is `7.5`, `x` ends as `"now a string"`, and `r` is `42`.

**What a `var` can hold**
- A function or lambda can be stored in a `var` when its parameters and result are types a `var` can hold. Calling it converts the arguments while the program runs.
- Assigning a `var` to a typed variable also converts while the program runs. It fails with a runtime error if the value doesn't fit (for example, an `int` into a `string`).
- Classes, pointers and arrays can't be stored in a `var`; that's a compile-time error.

`x is int` and `x as int` check and read what a `var` currently holds; see [Checking types with is and as](#checking-types-with-is-and-as).

## Variables

A declaration is the type, a colon, the name and an optional initializer:

```walnut
function main() -> int {
    int: count = 3;
    float: ratio = 0.5;
    string: name = "walnut";
    auto: total = count * 2;
    int: braced = {6};
    return 0;
} end function main
```

**Several at once.** One declaration can declare several variables of the same type, separated by commas. Each one gets its own initializer, or none:

```walnut
function main() -> int {
    int: a = 1, b = 2, c;
    string: first = "x", second;
    return 0;
} end function main
```

Here `c` is `0` and `second` is `""`. This works anywhere a declaration does: in a function, at file or namespace level, and for fields of a class. In the first part of a `for` loop, `,` still separates the loop's parts, so declare one variable there.

**Initializing with `(...)` and `{...}`.** Instead of `= value`, the values can follow the name in parentheses or braces:

```walnut
class Box {
public:
    int: v;
    string: s;
    constructor(int: x, string: y) : v = x, s = y { }
} end class Box

struct Point {
    int: x;
    int: y;
} end struct Point

function main() -> int {
    Box: a(1, "one");
    Box: b{2, "two"};
    Point: p{3, 4};
    int: n(5), m{6};
    array[] int: nums{1, 2, 3};
    return 0;
} end function main
```

Here `a.v` is `1`, `b.s` is `"two"`, `p.y` is `4`, `n` is `5`, `m` is `6` and `nums` has three elements.

- For a class with constructors, both forms call the constructor that matches the values, the same as `T: x = T(values)`.
- For a struct without constructors, the values fill its fields in order, as in `Point: p = Point{3, 4}`.
- For any other type there can be at most one value, and it is converted as with `=`. `int: n{}` and `int: n()` are zero.
- With `auto` or `var`, there must be exactly one value: `auto: x(5)` is an `int`.

**Rules**
- A variable with no initializer is zero: `0`, `0.0`, `""`, `false`.
- `auto` needs an initializer, and so does a reference.
- Declaring the same name twice in one scope is an error. An inner block may shadow an outer name.

**Global variables.** `::name` refers to the global `name` even when a local shadows it.

**Structured bindings** unpack an array or a struct:

```walnut
struct Point {
    int: x;
    int: y;
} end struct Point

function main() -> int {
    array[3] int: values = {7, 8, 9};
    auto: [a, b, c] = values;
    Point: p = Point{1, 2};
    auto: [px, py] = p;
    return 0;
} end function main
```

Here `a`, `b` and `c` are `7 8 9`, and `px` and `py` are `1 2`.

### Modifiers

Modifiers go before the type.

| Modifier | Meaning |
|---|---|
| `const` | Can't be assigned after initialization |
| `constexpr` | A compile-time constant; the initializer must be constant |
| `constinit` | Initialized at compile time; needs `static` or `thread_local` |
| `static` | One copy that lives for the whole program (a local keeps its value between calls) |
| `thread_local` | One copy per thread |
| `volatile` | Every read and write really happens; the compiler doesn't optimize them away |
| `inline` | Allowed to be defined in several files |
| `extern` | Declared here, defined in another file (see [Linking between files](#linking-between-files)) |
| `mutable` | A class field that can change inside `const` methods |
| `lazy` | A class field computed from its initializer the first time it is read; see [Lazy fields](#lazy-fields) |
| `hoist` | A local that can be used before its declaration line |
| `alignas(N)` | Places the variable at an address that is a multiple of `N` bytes; see [Alignment](#alignment) |
| `global`, `local`, `hidden` | Visibility; see [Visibility](#visibility) |

Walnut rejects combinations that contradict each other:
- `const` with `mutable`
- `static` with `extern`
- `constexpr` with `constinit`, `volatile`, `mutable`, `thread_local` or `extern`
- `inline` with `extern`, `mutable`, `volatile`, `thread_local` or `constinit`
- `mutable` with `static`, `thread_local` or `extern`
- `lazy` with `const`, `constexpr`, `constinit`, `static`, `extern`, `inline`, `thread_local`, `mutable` or `volatile`
- more than one of `global`, `local` and `hidden`

```walnut
function next_id() -> int {
    static int: counter = 0;
    counter++;
    return counter;
} end function next_id

function main() -> int {
    int: first = next_id();
    int: second = next_id();
    constexpr int: size = 4;
    hoisted int: later = 5;
    return 0;
} end function main
```

Here `first` is `1` and `second` is `2`: the `static` counter keeps its value between calls.

### Visibility

Visibility controls whether other files can see a top-level or namespace-level declaration.

| Visibility | Same file | Imported into another file | `extern` from another file |
|---|---|---|---|
| `global` (the default) | yes | yes | yes |
| `local` | yes | no | yes |
| `hidden` | yes | no | no |

A `hidden` declaration inside a namespace can only be used inside that namespace (and the namespaces nested in it).

Inside a class, use `public`, `private` and `protected` instead. Visibility words can't be used inside functions.

### Alignment

`alignas(N)` places a variable, field, array or struct at an address that is a multiple of `N` bytes. `N` must be a power of two, written as a number, a constant, or a type (`alignas(double)` means "aligned like a `double`"). It can't be weaker than the alignment the type already needs.

```walnut
struct alignas(16) Vec4 {
    float: x;
} end struct Vec4

constexpr int: CACHE_LINE = 64;
alignas(CACHE_LINE) int: shared = 0;

function main() -> int {
    alignas(8) int: local_value = 5;
    array alignas(16) [4] int: lanes = {1, 2, 3, 4};
    return 0;
} end function main
```

Here `alignof(Vec4)` is `16`. On an array, `alignas` goes right after `array`, because it aligns the whole array.

## References and pointers

```walnut
struct Pair {
    int: x;
    int: y;
} end struct Pair

function main() -> int {
    int: a = 10;
    int&: r = a;
    r = 20;
    int*: p = &a;
    *p = *p + 1;
    int**: pp = &p;
    **pp = **pp * 2;

    Pair: s = Pair{1, 2};
    Pair*: ps = &s;
    ps->x = 5;
    (*ps).y = 6;

    int*: one = new int(7);
    int*: many = new int[3];
    many[0] = 1;
    delete one;
    delete[] many;
    return 0;
} end function main
```

At the end, `a` is `42` and `s` is `{5, 6}`.

**References**
- `T&` is a reference and `T&&` is an rvalue reference.
- A reference must be initialized.
- A non-const `int&` can't bind to a temporary. `const int&: r = 5;` is fine.

**Pointers**
- `T*` is a pointer and `T**` a pointer to a pointer. `&x` takes an address and `*p` dereferences.
- `->` reaches a member through a pointer; `.` does not dereference pointers.
- `p + n`, `p - q`, `p[i]`, `++` and `--` work like in C++.
- A pointer can be tested as a `bool`.
- `nullptr` is the null pointer.

**Const pointers**
- `const int*` points to a const `int`.
- `int const*` is a pointer that itself is const.

**`new` and `delete`.** `new T(args)` creates one object and `new T[n]` creates an array. Free them with `delete p` and `delete[] p`. A class can provide its own allocator; see [Custom allocation](#custom-allocation).

## Arrays

An array's size is part of its type: `array[N] T`. The size can be any constant expression.

```walnut
function main() -> int {
    array[3] int: zeros;
    arr[3] int: b = {4, 5, 6};
    array int: c[3] = {7, 8, 9};
    array[] int: d = {1, 2, 3, 4};
    array[...] string: words = {"x", "y"};
    array[2] array[3] int: grid = {{1, 2, 3}, {4, 5, 6}};
    int: total = 0;
    for (int: v in d) {
        total += v;
    }
    return 0;
} end function main
```

Here `b[1]` is `5`, `grid[1][2]` is `6`, `countof(d)` is `4`, `countof(grid)` is `2`, and `total` is `10`.

**Size**
- `[]` and `[...]` take their size from the initializer.
- A size that is only known at run time (a variable) is allowed too.

**Multiple dimensions** nest the keyword: `array[2] array[3] int`.

**Using arrays**
- Arrays copy with `=` and loop with `for (... in ...)`.
- `countof` gives the number of elements, and `sizeof` gives the size in bytes.

## Strings, text and characters

```walnut
function main() -> int {
    string: s = "abc";
    char: c = 'd';
    string: joined = s + "def" + c;
    s += "!";
    text: t = `back\`tick`;
    int: letters = countof("héllo");
    string: seen = "";
    for (char: ch in "hé!") {
        seen = seen + ch + "|";
    }
    return 0;
} end function main
```

Here `joined` is `"abcdefd"`, `s` is `"abc!"`, `t` is ``"back`tick"``, `letters` is `5`, and `seen` is `"h|é|!|"`.

**Literals**
- `"..."` is a `string`, `` `...` `` is a `text` and `'x'` is a `char`. String and text literals may span lines.
- Escapes: `\n \t \r \a \b \f \v \e \0 \\ \' \" \? \` \$`, `\xHH`, `\uXXXX`, `\UXXXXXXXX` `\u{...}` and `\s{name}`.
- `\s{name}` is a character by name, so you don't need to look up its code: `'\s{alpha}'` is `'α'`, `"\s{Beta}"` is `"Β"`, and `` `x \s{le} \s{infin}` `` is `` `x ≤ ∞` ``. It works in `char`, `string` and `text` literals. The names are the HTML ones (the same as `&alpha;` in HTML, so `\s{rarr}` is `→`, `\s{copy}` is `©` and `\s{nbsp}` is a no-break space), plus some LaTeX ones HTML doesn't have, such as `\s{infty}`, `\s{neq}`, `\s{to}`, `\s{partial}`, `\s{exists}` and `\s{cdots}`. Names are case-sensitive: `\s{delta}` is `δ` and `\s{Delta}` is `Δ`. A misspelled name is an error that suggests the closest one.
- You can add your own names in a `walnut.bark` file next to your main `.wal` file or in any folder above it (the project's top folder is the usual place). Walnut uses the nearest one, and your names work alongside the built-in ones:

  ```
  names {
      a: alpha;        // another built-in name
      dg: "\u{B0}";    // a character in quotes, written directly or with an escape
      hrt: U+2665;     // a code (0x2665 works too)
      a2: a;           // another of your own names
  }
  ```

  A name uses letters, digits and `_`, can't be one of the built-in names, and stands for exactly one character. Mistakes in the file stop `check`, `build` and `run` with the line they're on. Canopy edits this file from Settings > Special Characters. `std::io::parse_string` and `parse_text` know only the built-in names, since the program running them has no project file.
- Source files are UTF-8, so literals can contain any character directly.

**Operations**
- `+` joins strings, text and characters in any order, and `+=` appends.
- `== != < > <= >=` compare strings, and compare `char`s by code point.
- A `char` plus or minus a whole number is another `char`: `'a' + 2` is `'c'`, and `c += 1` moves `c` to the next character. Two `char`s can also be subtracted; the result is a `char` holding the distance between them, so use `static_cast<int>(c) - static_cast<int>('0')` when you want the number.
- `countof(s)` gives the number of characters (code points), not bytes.
- `for (char: c in s)` walks through the characters.
- Any value can be turned into text with `${}` in a text literal (see [Text interpolation](#text-interpolation)) or with `.to_string()` and `.to_text()` (see [Methods on built-in values](#methods-on-built-in-values)). Reading a value out of a string is done with `std::io::parse_int`, `parse_float`, `parse_bool` and the other parse functions in the [io library](Documentation/Library/io.md#parsing-values), or with a stream such as `std::io::istringstream`.

### Text interpolation

A `text` literal (written with backticks) can hold `${expression}`. The expression is evaluated, turned into text, and put in its place, the same way JavaScript template literals work. This is what makes `text` different from `string`: a `"..."` string literal never interpolates.

```walnut
struct Temp {
    int: degrees = 21;
    operator(string) function() const {
        return "warm";
    }
} end struct Temp

function main() -> int {
    int: n = 5;
    float: half = 2.5;
    string: who = "walnut";
    Temp: t;
    text: a = `n=${n}, half=${half}, sum=${n + half}, who=${who}`;
    text: b = `today is ${t}, and ${`nested ${n * 2}`}`;
    text: c = `cost: \${n} and \$5`;
    return 0;
} end function main
```

Here `a` is `` `n=5, half=2.5, sum=7.5, who=walnut` ``, `b` is `` `today is warm, and nested 10` `` and `c` is `` `cost: ${n} and $5` ``.

- `${...}` takes any expression, including calls, operators, string literals with braces in them and other text literals with their own `${}`.
- Numbers, `bool`, `char`, `string`, `text`, enums (as their number), `var` values, [special values](#special-values) and `special(T)` values can all be interpolated. A float is written in its shortest form, switching to scientific notation for very large or very small values (`1e+30`).
- A class or struct can be interpolated when it has a conversion operator to `string` or `text`. Any other type is an error.
- `\$` writes a `$` without starting an interpolation, so `` `\${x}` `` is the five characters `${x}`. A `$` that isn't followed by `{` doesn't need the backslash.
- The result is always `text`. Use `static_cast<string>(...)` to get a `string`.

### Methods on built-in values

Built-in types have methods, called with `.` like the methods of a class. They work on variables, on any other expression (`"abc".length()`), and through pointers with `->`.

```walnut
function main() -> int {
    string: x = "fvfbkvh";
    text: padded = `  Héllo  `;
    array[4] int: nums = {3, 1, 4, 1};
    int: n = 42;
    int: chars = x.length();
    int: bytes = padded.sizeof();
    string: middle = x.substr(1, 3);
    int: at = x.find("bk");
    bool: has = x.contains('h');
    text: clean = padded.trim().to_upper();
    bool: found = nums.contains(4);
    int: where = nums.find(1);
    string: shown = n.to_string() + "!";
    int: width = n.sizeof();
    return 0;
} end function main
```

Here `chars` is `7`, `bytes` is `10` (`é` takes two bytes), `middle` is `"vfb"`, `at` is `3`, `has` is `true`, `clean` is `` `HÉLLO` ``, `found` is `true`, `where` is `1`, `shown` is `"42!"` and `width` is `4`.

Strings and characters get many more methods from the [strings library](Documentation/Library/strings.md): with `import module.strings`, every function in `std::strings` whose first parameter is a string (or a `char`) can also be called as a method, so `line.split(",")` is `std::strings::split(line, ",")` and `c.is_digit()` is `std::strings::is_digit(c)`. The methods in the tables below always come first.

**Every value**

| Method | Result |
|---|---|
| `sizeof()` | The size in bytes, as an `int`. For a `string` or `text` it is the number of bytes the characters take, and for an array whose size is only known at run time it is the element size times the count |
| `alignof()` | The alignment in bytes |
| `to_string()`, `to_text()` | The value as a `string` or `text`, written the same way as [text interpolation](#text-interpolation) writes it. Not available on classes and structs |
| `countof()` | The same as `countof(x)`: elements of an array, characters of a string, or enumerators of an enum |

**`string` and `text`.** Positions and counts are in characters (code points), not bytes. Methods that give back a string give back the same type they were called on.

| Method | Result |
|---|---|
| `length()` | The number of characters |
| `size()` | The number of bytes |
| `empty()` | `true` if there are no characters |
| `substr(start)`, `substr(start, count)` | The characters from `start` to the end, or `count` characters from `start`. A `start` past the end is an error at run time; a `count` past the end stops at the end |
| `find(needle)` | The position of the first match, or `undefined` if there isn't one (see [Special values](#special-values)) |
| `contains(needle)`, `starts_with(needle)`, `ends_with(needle)` | `bool` |
| `to_upper()`, `to_lower()` | A copy with letters changed. Latin, Greek and Cyrillic letters are converted |
| `trim()` | A copy without spaces, tabs and line breaks at either end |

A needle can be a `string`, a `text` or a `char`.

**Arrays**

| Method | Result |
|---|---|
| `length()`, `size()` | The number of elements |
| `empty()` | `true` if there are no elements |
| `find(value)` | The position of the first element equal to `value`, or `undefined` |
| `contains(value)` | `bool` |

- These methods are part of the language, so they don't need an `import`.
- `sizeof`, `alignof` and `countof` are keywords, but they are allowed as method names after `.` and `->`.
- A class or struct can use `.sizeof()` and `.alignof()` too. Its other methods are its own.
- A method that doesn't exist for a type is an error, such as `n.length()` on an `int`.

## Enums, aliases, variants and unions

### Enums

```walnut
enum Color { Red, Green = 5, Blue }
enum Flags : short int { Read = 1, Write = 2, Exec = 4 }

function main() -> int {
    Color: c = Color::Green;
    int: blue = static_cast<int>(Color::Blue);
    Flags: f = Flags::Read | Flags::Exec;
    f |= Flags::Write;
    bool: can_write = (f & Flags::Write) == Flags::Write;
    return 0;
} end function main
```

Here `blue` is `6`, `countof(Color)` is `3`, `f` holds `7`, and `can_write` is `true`.

- Enumerators are always reached through the enum's name, as in `Color::Blue`.
- `: type` sets the underlying integer type; the default is `int`.
- `countof(E)` is the number of enumerators.
- An enum and an integer don't convert into each other implicitly; use `static_cast`.
- Enums compare with `==` and `<`.
- `|`, `&`, `^` and `~` work on two values of the same enum and give that enum, so enums work as flag sets. Mixing an enum with an integer needs a cast.

### Type aliases

```walnut
typedef int: Meters;
using Name = string;
using Row = array[3] int;

function main() -> int {
    Meters: m = 5;
    Name: n = "walnut";
    Row: r = {1, 2, 3};
    return 0;
} end function main
```

### Variants

`variant(T1, T2, ...)` holds one value of one of the listed types at a time.
- Assigning a value picks the alternative of exactly that type, or the only alternative the value converts to.
- `v is T` tells which type it holds right now, and `v as T` gives that value.
- `as` on the wrong type stops the program with a runtime error, so check with `is` first when you aren't sure.

```walnut
function main() -> int {
    variant(int, string): v = 5;
    bool: holds_int = v is int;
    v as int = 9;
    int: n = v as int;
    v = "hello";
    bool: holds_string = v is string;
    string: s = v as string;
    return 0;
} end function main
```

Here `holds_int` is `true`, `n` is `9`, `holds_string` is `true` and `s` is `"hello"`.

A variant with no initializer holds the default value of its first type.

### Unions

A `union` is like a struct whose members all share the same storage, so only one of them holds a value at a time. Writing one member replaces the others, and its size is the size of its largest member.

```walnut
union Number {
    int: whole;
    float: real;
} end union Number

struct Tagged {
    bool: is_whole;
    Number: value;
} end struct Tagged

function main() -> int {
    Number: n;
    n.whole = 42;
    n.real = 1.5;
    Tagged: t;
    t.is_whole = true;
    t.value.whole = 9;
    return 0;
} end function main
```

At the end `n.real` is `1.5` (the `whole` it held before is gone), and `t.value.whole` is `9`.

**Union rules**
- Members must be plain values: numbers, `bool`, `char`, pointers, enums, fixed-size arrays of those, and structs made only of those. A `string`, a `var`, or a class with its own destructor or copy operations can't be a union member.
- At most one member can have a default value. That member is the one that holds a value when the union is created. Without one, the first member does.
- A union constructor can initialize only one member.
- Unions can't inherit or be inherited from.
- Unions have no generated `==`, because Walnut can't know which member is current. Keep track of that yourself, as `Tagged` does above, or use a [variant](#variants).

## Operators

From lowest to highest precedence:

| Operators | Notes |
|---|---|
| `= += -= *= /= %= **= &= \|= ^= <<= >>=` | Assignment, right to left; `a = b = 5` works |
| `? :` | Conditional |
| `\|\|` | Logical or |
| `&&` | Logical and |
| `\|` | Bitwise or |
| `^` | Bitwise xor |
| `&` | Bitwise and |
| `== !=` | Also written `===` and `!==` |
| `< <= > >=`, `is`, `as` | Comparison and [type checks](#checking-types-with-is-and-as) |
| `<< >>` | Shifts |
| `+ -` | |
| `* / %` | |
| `**` | Power, right to left |
| `- + ! ~ * & ++ --` | Prefix |
| `++ -- [] () . -> ::` | Postfix, member access, scope |

```walnut
function main() -> int {
    int: a = 17;
    int: b = 5;
    int: quotient = a / b;
    int: remainder = a % b;
    int: power = a ** 2;
    int: mixed = 2 + 3 * 4 ** 2;
    int: tower = 2 ** 3 ** 2;
    int: bits = (a & b) | (a ^ b);
    int: i = 0;
    int: before = i++;
    int: after = ++i;
    return 0;
} end function main
```

Here `quotient` is `3`, `remainder` is `2`, `power` is `289`, `mixed` is `50`, `tower` is `512`, `bits` is `21`, `before` is `0` and `after` is `2`.

**Integer arithmetic**
- Overflow wraps around, as in two's complement: `int` 2147483647 + 1 is -2147483648, and `unsigned int` 0 - 1 is 4294967295.
- Division truncates toward zero (`-7 / 2` is -3), and `>>` keeps the sign (`-7 >> 1` is -4).

Integer division or `%` by zero doesn't stop the program: it gives `undefined` (or `indeterminate` for `0 / 0`). See [Special values](#special-values).

**Runtime errors.** These stop the program with a `walnut runtime error` message:
- a shift by a negative amount, or by at least the type's width
- an integer other than 0, 1 and -1 raised to a negative power
- an integer holding a special value used as an array index, a count or a size, or passed to C or C++

**Which types each operator takes**
- `%` and the bitwise operators need integers. Bitwise operators also take two `bool`s, or two values of the same enum.
- `char` does arithmetic with another `char`, and `+` and `-` also take a `char` and a whole number (`'a' + 1`, `c - 3`, `2 + c`), giving a `char`.
- `&&`, `||` and `!` accept anything that can be tested as true or false: numbers, pointers, `bool`.

**Mixing number types** widens to the larger type: `int + float` is a `float`, and `int + long int` is a `long int`.

## Conversions and casts

### Implicit conversions

Walnut converts implicitly only when nothing can be lost:
- a narrower integer to a wider one, or a narrower float to a wider one
- an integer to a float that can hold all of its values
- an unsigned float to a signed float
- a constant that fits, such as `short int: s = 3;`
- a value into a `variant` that has a matching alternative
- a value into a `var`

Everything else needs a cast. That includes:
- narrowing (wider to narrower)
- float to integer
- signed to unsigned, for integers or floats: a negative value would be lost
- `char` ↔ `int`, `bool` ↔ `int`, `string` ↔ `text`

The error message names the cast to use, for example `static_cast<unsigned int>(...)`.

A class converts to another type only through a [conversion operator](#conversion-operators). Its constructors are never used as implicit conversions.

### Casts

All casts are written `name<Type>(value)`.

| Cast | Use it for |
|---|---|
| `static_cast<T>(v)` | Number to number (narrowing, signed ↔ unsigned, float ↔ int), enum ↔ integer, `char` to integer, `string` ↔ `text`, base class to derived class, explicit conversion operators |

**Numbers never become `char` or `bool`.** No cast turns an integer (or any number) into a `char` or a `bool`, not even `static_cast` or `bit_cast`. A `char` only comes from a character literal or another `char` (including one moved by a whole number, as in `'a' + n`), and a `bool` from a comparison such as `x != 0`. Going the other way is allowed with a cast: `static_cast<int>(c)` gives a character's code point.
| `const_cast<T>(v)` | Removing (or adding) `const` or `volatile` through a pointer or reference |
| `dynamic_cast<T>(v)` | Converting a pointer or reference to a class with virtual functions into a derived class, checked while the program runs. A failed pointer cast gives `nullptr`. `as` does the same with an error instead |
| `reinterpret_cast<T>(v)` | Pointer ↔ pointer and pointer ↔ integer, reinterpreting the bits |
| `bit_cast<T>(v)` | Copying the exact bits of a value into another type of the same size |

```walnut
function bump(const int&: r) -> void {
    int&: w = const_cast<int&>(r);
    w = w + 1;
} end function bump

function main() -> int {
    int: n = 4;
    const int*: read_only = &n;
    int*: writable = const_cast<int*>(read_only);
    *writable = 9;
    bump(n);
    int: truncated = static_cast<int>(3.99);
    unsigned int: wrapped = static_cast<unsigned int>(-5);
    return 0;
} end function main
```

Here `n` ends as `10`, `truncated` is `3`, and `wrapped` is `4294967291`.

**`const_cast` rules**
- The target must be a pointer or reference type, and only `const` and `volatile` may differ between the two types.
- Writing through the result is only safe when the original object wasn't declared `const`, as with `n` above.
- To get a non-const copy of a value, just assign it to a non-const variable; no cast is needed.

### Casting to an rvalue reference

`static_cast<T&&>(x)` lets `x` be moved from, so a move constructor or move assignment is chosen instead of a copy. The core library wraps this as `std::core::move(x)`. See [Copying and moving](#copying-and-moving).

## Checking types with is and as

`value is T` is `true` when the value's type, while the program runs, is `T`. `value as T` gives the value as a `T`, and stops the program with a runtime error when it isn't one. They work on three kinds of values:

| Value | `is` checks | `as` gives |
|---|---|---|
| A [variant](#variants) | Which alternative it holds now | That alternative, which can be assigned to |
| A `var` | Whether it holds a number of that kind, a `bool`, a `char` or a string | The value, converted to `T` |
| An object of a class with virtual functions, or a pointer to one | Whether the object's real type is `T` or derives from it | A reference (or pointer) to it as a `T` |

```walnut
class Shape {
public:
    function sides() const virtual -> int { return 0; } end function sides
} end class Shape

class Square inherits Shape {
public:
    int: side = 3;
    function sides() const override -> int { return 4; } end function sides
} end class Square

class Circle inherits Shape { } end class Circle

function main() -> int {
    Square: sq;
    Circle: ci;
    Shape&: s = sq;
    Shape*: p = &ci;
    bool: s_is_square = s is Square;
    bool: p_is_square = p is Square*;
    int: side = (s as Square).side;
    var d = 2.5;
    bool: d_is_float = d is float;
    float: f = d as float;
    return 0;
} end function main
```

Here `s_is_square` is `true`, `p_is_square` is `false`, `side` is `3`, `d_is_float` is `true` and `f` is `2.5`.

`is` also checks for special values: `x is nan`, `x is undefined`, `x is indeterminate` and `x is inf`; see [Special values](#special-values).

Walnut rejects a check that can never be true, such as `v is float` for a `variant(int, string)`, or `is` on a class that doesn't derive from the one tested. A pointer `as` on a null pointer gives `nullptr`.

`is` and `as` sit with the comparison operators, so `v is int && ok` means `(v is int) && ok`.

## Control flow

### if

`if` needs `then` before its block, and so does every `else` branch. Blocks always need braces.

```walnut
function size_name(int: x) -> string {
    if (x > 10) then {
        return "big";
    } else if (x > 3) then {
        return "medium";
    } else then {
        return "small";
    }
} end function size_name

function main() -> int {
    string: a = size_name(20);
    string: b = size_name(5);
    string: c = size_name(1);
    return 0;
} end function main
```

Here `a`, `b` and `c` are `"big"`, `"medium"` and `"small"`. `if constexpr` chooses a branch at compile time; see [Compile-time code](#compile-time-code).

**Declaring in a condition.** A branch's condition can declare a variable. The branch runs when the value tests as true, and the variable exists only inside that branch.

```walnut
function lookup(int: key) -> int {
    return key * 2;
} end function lookup

function main() -> int {
    int: result = 0;
    if (int: found = lookup(0)) then {
        result = found;
    } else if (int: second = lookup(5)) then {
        result = second + 1;
    }
    return 0;
} end function main
```

Here `result` is `11`: `lookup(0)` is 0, which tests as false.

### Loops

```walnut
function main() -> int {
    int: sum = 0;
    for (int: i = 0; i < 3; i++) {
        sum += i;
    }
    for (int: j = 0, j < 2, j++) sum += 10;

    int: k = 0;
    while (k < 3) {
        k++;
    }
    do {
        k--;
    } while (k > 0);

    int: count = 0;
    for (char: c in "ab") {
        count++;
    }
    return 0;
} end function main
```

Here `sum` is `23`, `k` ends at `0`, and `count` is `2`.

**`for`**
- The three parts can be separated by `;` or `,`, and any part can be left out: `for (;;)` loops forever.
- `for (T: x in items)` loops over arrays, strings, generators, and classes that provide `begin()` and `end()` (see [Looping over your own classes](#looping-over-your-own-classes)).
- `for (var: c in v)` loops over a `var` holding a string, giving each character as a `var`. If the `var` holds anything else, the program stops with a runtime error.
- `for (T: x in {1, 2, 3})` loops over a brace list. With `auto`, the elements must share a common type.
- `for (auto: [a, b] in pairs)` unpacks each element as it goes.

**Loop bodies.** `while`, `do ... while` and `for` can take a single statement instead of a block.

**Jumps**
- `break;` leaves the loop and `continue;` goes to the next iteration.
- `repeat;` runs the current iteration again from the top of the body, without checking the condition or running the `for` increment.
- A loop can be given a label, written as a name and a colon before it. `break label;`, `continue label;` and `repeat label;` then act on that loop instead of the innermost one, so an inner loop can leave or continue an outer one.

```walnut
function main() -> int {
    int: tries = 0;
    int: last = 0;
    for (int: i = 0; i < 2; i++) {
        tries++;
        last = i;
        if (tries < 3) then {
            repeat;
        }
    }
    return 0;
} end function main
```

Here the body runs 4 times (three times with `i` at 0, once with `i` at 1), so `tries` is `4` and `last` is `1`.

```walnut
function main() -> int {
    int: found = -1;
    rows: for (int: r = 0; r < 5; r++) {
        for (int: c = 0; c < 5; c++) {
            if (c > r) then {
                continue rows;
            }
            if (r * c == 6) then {
                found = r * 10 + c;
                break rows;
            }
        }
    }
    return 0;
} end function main
```

Here `found` is `32`: the search stops at `r` = 3, `c` = 2, and `continue rows` skips the rest of each row once `c` passes `r`.

- Any `for`, `for ... in`, `while` or `do ... while` loop can have a label.
- The label must belong to a loop around the jump; anything else is an error. A label is only visible inside its own loop, so two separate loops can use the same label.
- Labelled jumps work in `constexpr` functions too.

### goto

`goto name;` jumps to `label name;` in the same function. The label is a statement of its own, so no `:` is involved.

```walnut
function find(int: target) -> int {
    int: found = -1;
    for (int: a = 0; a < 10; a++) {
        for (int: b = 0; b < 10; b++) {
            if (a * b == target) then {
                found = a * 10 + b;
                goto done;
            }
        }
    }
    found = -2;
    label done;
    return found;
} end function find

function main() -> int {
    int: hit = find(12);
    int: miss = find(97);
    return 0;
} end function main
```

Here `hit` is `26` and `miss` is `-2`.

- A goto can jump backwards or forwards, out of any number of blocks, to a label in its own block or in a block around it. It can't jump into a block it isn't inside, such as the body of an `if` or a loop it isn't in.
- A forward jump can't skip a variable declaration in the label's block, because the variable would exist without being initialized. Declare the variable before the `goto`, or put it in its own `{ }` block.
- Each label name can be used once per function. Labels and loop labels are separate, and `label` and `goto` are still usable as ordinary names elsewhere.
- Labelled `break` and `continue` cover most uses of `goto` more clearly; `goto` is there for state machines, shared clean-up code at the end of a function, and code translated from C.

### Looping over your own classes

`for (T: x in obj)` works on any class with a `begin()` method and an `end()` method that take no arguments. They return an iterator, which needs three operators:

| Operator | Used for |
|---|---|
| `operator(!=)` | Checking whether the loop has reached `end()` |
| `operator(++)` (prefix, no parameter) | Moving to the next element |
| `operator(*)` (unary) | Getting the current element; return `T&` to let the loop change elements through `T&: x` |

A plain pointer is already an iterator, so a class that stores its elements in an array can return `T*` from both methods.

```walnut
class Countdown {
    int: start;
public:
    constructor(int: s) : start = s { }

    class Step {
    public:
        int: n;
        constructor(int: x) : n = x { }
        operator(!=) function(const Step&: other) const -> bool {
            return n != other.n;
        }
        operator(++) function() -> Step& {
            n -= 1;
            return *this;
        }
        operator(*) function() const -> int {
            return n;
        }
    } end class Step

    function begin() const -> Step {
        return Step(start);
    } end function begin

    function end() const -> Step {
        return Step(0);
    } end function end
} end class Countdown

function main() -> int {
    int: total = 0;
    for (int: n in Countdown(3)) {
        total = total * 10 + n;
    }
    return 0;
} end function main
```

Here `total` is `321`.

- `end` is a keyword, but it can be used as the name of a method, as above, and called as `list.end()`, or as just `end()` inside the class (see [Keywords as function and method names](#source-files)).
- `begin()` and `end()` are called once, before the loop starts. A `const` object uses the `const` versions.
- Labelled `break` and `continue`, structured bindings (`for (auto: [k, v] in map)`) and `T&` loop variables all work as they do for arrays.
- The [collections library](Documentation/Library/collections.md) uses this for all of its containers.
- Instead of writing an iterator class like `Step`, a class can use one from the [iterators library](Documentation/Library/iterators.md), such as `std::iterators::Pointer<T>` for elements stored one after another, which also has a backwards form.

### switch

`switch` works on integers, characters, enums, `string` and `text`. A case that runs into the next case gets a warning, unless you end it with `fallthrough;` to show the fall-through is intended. `break;` ends a case.

```walnut
function describe(int: n) -> int {
    int: score = 0;
    switch (n) {
        case 1: score = 1; break;
        case 2: score = 20; fallthrough;
        case 3: score += 3; break;
        default: score = 100;
    }
    return score;
} end function describe

function main() -> int {
    int: a = describe(1);
    int: b = describe(2);
    int: c = describe(9);
    return 0;
} end function main
```

Here `a` is `1`, `b` is `23` and `c` is `100`.

A switch on a string compares the whole string with each case, the same as a chain of `==` tests. The cases can be `string` or `text` literals:

```walnut
function command(string: word) -> int {
    switch (word) {
        case "start": return 1;
        case "stop": return 2;
        case `pause`: return 3;
        default: return 0;
    }
} end function command

function main() -> int {
    int: a = command("stop");
    int: b = command("jump");
    return 0;
} end function main
```

Here `a` is `2` and `b` is `0`.

## Functions

```walnut
function add(int: a, int: b = 10) -> int {
    return a + b;
} end function add

function add(string: a, string: b) -> string {
    return a + b;
} end function add

function increment(int&: value) -> void {
    value++;
} end function increment

function twice(int: x) -> {
    return x * 2;
} end function twice

function sum(int...: values) -> int {
    int: total = 0;
    for (int: v in values) {
        total += v;
    }
    return total;
} end function sum

function apply(function(int) -> int: fn, int: v) -> int {
    return fn(v);
} end function apply

function main() -> int {
    int: n = 1;
    increment(n);
    int: a = add(1);
    string: s = add("a", "b");
    int: t = sum(1, 2, 3);
    int: applied = apply(twice, 5);
    return 0;
} end function main
```

Here `n` is `2`, `a` is `11`, `s` is `"ab"`, `t` is `6` and `applied` is `10`.

**Parameters** are written like variables: `type: name`.
- `T&` passes by reference, and `const T&` by read-only reference.
- A parameter can have a default value (`int: b = 10`). It doesn't have to be the last parameter: when a call has no argument that fits it, it takes its default and the arguments go on to the next parameters. `function label(int: n = 1, string: s)` can be called as `label("x")` or `label(2, "x")`.
- A parameter with a default can't be followed by a parameter of the same type unless a parameter that always needs an argument comes between them, because a call couldn't tell which one an argument is for. This also applies to [variadic parameters](#variadic-parameters) with a default, and to a variadic parameter coming after a defaulted one: `function f(int: a = 1, int...: rest)` is an error, while `function g(int: a = 1, string: s, int: b)` is fine.
- `T...: name`, `T...[3]: name` and `T...<3>: name` take several arguments at once. See [Variadic parameters](#variadic-parameters).
- `function(int) -> int` is the type of a function (or lambda) that takes an `int` and returns an `int`. See [Function types](#function-types).

**Return types**
- `-> T` gives the return type.
- `->` with no type works the type out from the `return` statements.
- No arrow at all returns a dynamic `var`.
- A `void` function needs no `return`.

**Overloading.** Functions can be overloaded on their parameter types. When a template and an ordinary function match a call equally well, the ordinary one is used.

**Order doesn't matter.** A function can be called before the line that defines it. A prototype (`function f(int: x) -> int;`) is only needed for `extern` functions and for [methods defined outside their class](#defining-methods-outside-the-class).

**Nesting.** Functions can be declared inside other functions.

### Variadic parameters

A variadic parameter takes several arguments of one type and holds them as an array. `countof(x)` gives how many it got. Functions, methods, constructors, operators and lambdas can all have them. There are three kinds:

| Parameter | Takes | Default value |
|---|---|---|
| `T...: x` | Any number, including none | Not allowed |
| `T...[N]: x` | Up to `N` | Allowed: missing arguments are filled with it, so `x` always holds `N` |
| `T...<N>: x` | Exactly `N` | Allowed: missing arguments are filled with it, so the call may pass fewer than `N` |

```walnut
function sum(int...: xs) -> int {
    int: total = 0;
    for (int: v in xs) {
        total += v;
    }
    return total;
} end function sum

function three(int...[3]: xs = 1) -> int {
    return xs[0] + xs[1] + xs[2];
} end function three

function pair(int...<2>: xs = 7, string: s) -> int {
    return xs[0] + xs[1] + countof(s);
} end function pair

function main() -> int {
    int: a = sum(1, 2, 3);
    int: b = three(5);
    int: c = pair(1, "ab");
    int: d = pair("ab");
    return 0;
} end function main
```

Here `a` is `6`, `b` is `7` (`5 + 1 + 1`), `c` is `10` (`1 + 7 + 2`) and `d` is `16` (`7 + 7 + 2`).

**Mixing variadic parameters with others.** Arguments are handed out from left to right. A variadic parameter keeps taking arguments while they fit its type, and the next parameter starts at the first argument that doesn't:

```walnut
function a(int...: xs, double: y) -> int {
    return countof(xs) * 10 + static_cast<int>(y);
} end function a

function b(int...[3]: xs, double...: ys) -> int {
    return countof(xs) * 10 + countof(ys);
} end function b

function main() -> int {
    int: a1 = a(1, 2, 3, 4.5);
    int: a2 = a(1, 2, 3);
    int: b1 = b(1, 2, 3.5, 4);
    int: b2 = b(1, 2, 3, 4, 5.5);
    int: b3 = b(2.5);
    return 0;
} end function main
```

- `a1` is `34`: `xs` gets `1, 2, 3` and `y` gets `4.5`.
- `a2` is `23`: there is no `double`, so the last argument, `3`, is converted for `y`, and `xs` gets `1, 2`.
- `b1` is `22`: `xs` stops at `3.5`, so `ys` gets `3.5, 4`. Once a call has moved on to `ys`, it doesn't go back to `xs`, so `4` is converted to `double` too.
- `b2` is `32`: `xs` is full after three, so `4` and `5.5` go to `ys`.
- `b3` is `1`: `xs` gets nothing.

The rules behind this:
- An argument goes to the earliest parameter that can still take it, as long as every later parameter that needs an argument can still get one. When two ways of splitting the arguments are equally good, the one where earlier parameters take more wins.
- Exact type matches are preferred over conversions, earlier arguments first, and this matters more than the left-to-right rule. With `function h(double...[2]: xs, int...: ys)`, `h(1, 2)` gives both values to `ys`, because they are `int`s; `h(1.5, 2)` gives `1.5` to `xs` and `2` to `ys`.
- A parameter that gets no exact match can take a converted argument, using the normal [implicit conversions](#implicit-conversions). In `a2` the `int` `3` becomes a `double`. A `double` can't become an `int` this way, so `function p(double...: xs, int: y)` can't be called as `p(1.5, 2.5)`.
- Once the arguments have moved on to a later parameter, they never go back to an earlier one. With `function k(int...: xs, double: y)`, the call `k(1, 2.5, 3)` is an error: `3` comes after `y` and there is nowhere left to put it.
- An unbounded `T...` placed directly before another variadic parameter of the same type `T` is an error, because the first one would always take everything. A bounded one is fine: in `function c(int...[2]: xs, int...: ys)`, `c(1, 2, 3, 4, 5)` gives `xs` two values and `ys` three. An ordinary parameter after an unbounded one is also fine, because it always gets its argument first: in `function d(double...: xs, double: y)`, `d(1.5, 2.5, 3.5)` gives `y` the `3.5`.

### Function types

`function(parameters) -> return_type` is the type of a function or lambda value. The parameters can be variadic and have default values, written the same way as in a declaration but without names:

```walnut
function apply(function(int...[2] = 3, int) -> int: f, int: k) -> int {
    return f(k) + f(1, k) * 100;
} end function apply

function main() -> int {
    auto: add = [](int...[2]: xs = 3, int: k) -> int {
        return xs[0] + xs[1] + k;
    };
    function(int = 5) -> int: bump = [](int: n) -> int { return n + 1; };
    int: a = apply(add, 5);
    int: b = bump();
    return 0;
} end function main
```

Here `a` is `911` (`f(5)` is `3 + 3 + 5` and `f(1, 5)` is `1 + 3 + 5`) and `b` is `6`.

- A call through a function value follows the parameters of its type, with the same rules as a call to a function.
- Default values in a function type have to be constants.
- A value can be stored in a function type that differs from its own only in default values and variadic markers. The type it is called through decides: `bump` above has no default of its own, but its type gives it one, and storing a lambda with defaults in a `function(int, string) -> int` means every argument has to be passed.
- A function also fits a function type whose parameters it can be called with: a parameter taken by value or by `const` reference accepts the same type passed either way, so `[](int: x) -> bool { ... }` can be stored in a `function(const int&) -> bool` and the other way round. The result has to convert without loss (`int` to `long int` does, `long int` to `int` doesn't), and a `void` result accepts any. A non-`const` reference parameter still needs a non-`const` reference.
- A template function can be passed as a value once its template arguments are written: `std::less_than<int>`.
- When a function-type parameter is only called (or passed on to another function), the generated C++ takes the lambda as it is instead of wrapping it, so the call can be inlined as it would be for a template parameter.
- The library names the common shapes: `std::Predicate<T>`, `std::Compare<T>`, `std::Action<T>`, `std::Transform<T, U>`, `std::Combine<U, T>` and `std::Supplier<T>`, with ready-made comparators such as `std::less_than<T>`; see [core](Documentation/Library/core.md#function-types-and-comparators).

### iterable parameters

`iterable<T>` as a parameter type takes anything a `for` loop can walk through whose elements are `T`: an array, a `string` (for `iterable<char>`), a generator, or any class with `begin()` and `end()`. Each call is compiled for the argument's real type, as a template would be, but the function is written without one:

```walnut
function total(iterable<int>: values) -> int {
    int: sum = 0;
    for (int: v in values) {
        sum += v;
    }
    return sum;
} end function total

template <typename T>
function largest(iterable<T>: values) -> T {
    T: best = T();
    for (const T&: v in values) {
        if (best < v) then {
            best = v;
        }
    }
    return best;
} end function largest

function main() -> int {
    array[] int: a = {4, 9, 2};
    int: s = total(a);
    int: m = largest(a);
    char: c = largest("walnut");
    return 0;
} end function main
```

Here `s` is `15`, `m` is `9` and `c` is `'w'`.

- `iterable<T>: values` is read-only and copies nothing, like a `const` reference. `iterable<T>&: values` can change the elements, and only takes something that can be changed.
- `T` can be a fixed type, as in `total`, or a template parameter of the function, which is then worked out from the elements, as in `largest`. With a fixed type, elements that convert to it without loss also work.
- An argument that can't be walked through, or whose elements don't fit `T`, makes the function not a candidate for that call, so another overload can be chosen.
- `iterable` works for parameters of functions and methods. It isn't a type of its own, so it can't be used for variables or results.
- The [iterators library](Documentation/Library/iterators.md) has functions written this way, such as `std::iterators::count_if` and `std::iterators::max_of`.

### main

`main` can return `int` (the program's exit code) or `void`.

To receive command-line arguments, declare `function main(array[] string: args) -> int`. `args` holds only the arguments, not the program's name.

### Function qualifiers

These go after the parameter list:

| Qualifier | Meaning |
|---|---|
| `nodiscard` or `nodiscard("why")` | Ignoring the result is a warning; silence it with `discard_nodiscard(f())` |
| `noexcept`, `noexcept(condition)` | The function doesn't throw |
| `consteval` | Must be evaluated at compile time |
| `async` | A coroutine that returns `task<T>` (see [Coroutines](#coroutines)) |
| `const`, `virtual`, `override` | For methods (see [Classes and structs](#classes-and-structs)) |

These go before `function`: `constexpr`, `inline`, `static`, `hoist`, and the visibility words.

```walnut
function checked(int: x) nodiscard("the result is the answer") -> int {
    return x * 3;
} end function checked

function quiet() noexcept -> int {
    return 1;
} end function quiet

function main() -> int {
    discard_nodiscard(checked(2));
    int: result = checked(2);
    bool: safe = noexcept(quiet());
    return 0;
} end function main
```

Here `result` is `6` and `safe` is `true`.

**Overloading on `const`.** A method can have a `const` and a non-`const` version with the same parameters, such as `function at(long int: i) const -> const T&` and `function at(long int: i) -> T&`. A `const` object calls the `const` one; any other object calls the non-`const` one.

## Exceptions

Any value can be thrown: a number, a string or an object.
- `catch (T: name)` catches values of type `T`. `catch (const T&: e)` avoids a copy.
- `catch (...)` catches anything, and must come last.
- `throw;` on its own rethrows the exception currently being handled.
- The library's exceptions are in the namespace `std::errors`: [`collection_error`](Documentation/Library/collections.md#errors) from `collections`, [`format_error`](Documentation/Library/io.md) and [`input_error`](Documentation/Library/io.md#reading-input) from `io`, [`pointer_error`](Documentation/Library/memory.md#errors) and [`arena_error`](Documentation/Library/memory.md#errors) from `memory`, [`optional_error`](Documentation/Library/optional.md#errors) from `optional`, [`linalg_error`](Documentation/Library/linalg.md) from `linalg`, [`statistics_error`](Documentation/Library/statistics.md) from `statistics`, [`graph_error`](Documentation/Library/graphs.md) from `graphs`, [`time_error`](Documentation/Library/time.md) from `time`, [`network_error`](Documentation/Library/networking.md) from `networking` and [`crypto_error`](Documentation/Library/crypto.md) from `crypto`. Each has `what()`, which gives the message.
- `noexcept(expression)` is `true` when the expression can't throw.

```walnut
class Problem {
public:
    int: code;
    constructor(int: c) : code = c { }
} end class Problem

function risky(int: kind) -> int {
    if (kind == 1) then {
        throw 42;
    }
    if (kind == 2) then {
        throw "boom";
    }
    if (kind == 3) then {
        throw Problem(7);
    }
    return 0;
} end function risky

function main() -> int {
    int: total = 0;
    for (int: k = 1; k <= 3; k++) {
        try {
            risky(k);
        } catch (int: e) {
            total += e;
        } catch (string: s) {
            total += countof(s);
        } catch (const Problem&: p) {
            total += p.code;
        } catch (...) {
            total = -1;
        }
    }
    try {
        try {
            throw 5;
        } catch (int: e) {
            throw;
        }
    } catch (int: e) {
        total += e;
    }
    return 0;
} end function main
```

Here `total` ends as `58`: 42, plus 4 for `"boom"`, plus 7, plus the rethrown 5.

A `throw` written directly inside a `noexcept` function is a compile-time error. Destructors are `noexcept`, so the same applies to them.

## Templates and concepts

### Function templates

```walnut
template <typename T>
function biggest(T: a, T: b) -> T {
    if (a > b) then {
        return a;
    }
    return b;
} end function biggest

template <int N>
function times(int: x) -> int {
    return x * N;
} end function times

template <typename A, typename B>
function add(A: a, B: b) -> std::traits::common_type<A, B> {
    return a + b;
} end function add

function main() -> int {
    int: big = biggest(3, 9);
    string: later = biggest<string>("a", "b");
    int: fifteen = times<3>(5);
    float: mixed = add(1, 2.5);
    return 0;
} end function main
```

Here `big` is `9`, `later` is `"b"`, `fifteen` is `15` and `mixed` is `3.5`.

**Template parameters**
- A parameter can be a type (`typename T`) or a constant (`int N`). A constant can have any integer, float, `bool`, `char`, `string` or `text` type: `template <string Name>` takes `Greeter<"hi">`.
- A parameter can have a default (`typename T = int`).
- At a call, the template arguments are deduced from the function's arguments, or given explicitly as in `biggest<string>(...)`.
- `T::name` reaches a static member, method or member type of the type that `T` stands for, such as `T::value`, `T::make()` or `T::item_type` (usable as a type, as in `Box<T::item_type>`). It works the same through a type alias: with `using Small = Box<1>;`, `Small::name()` calls `Box<1>`'s static `name`.
- A template argument left out because the parameter has a default is filled in, so `Box<1>` and `Box<1, Default>` are the same type and deduce the same way.

### Variadic templates

`typename... Ts` takes any number of types.
- `Ts...: args` declares the matching parameters.
- `args...` expands them, for example into another call.
- `countof(args)` is how many arguments were passed.
- Fold expressions combine all of them with one operator: `(args + ...)`, `(args + ... + 0)`, `(args && ...)`.

```walnut
template <typename... Ts>
function count(Ts...: args) -> int {
    return countof(args);
} end function count

template <typename... Ts>
function total(Ts...: args) -> int {
    return (args + ... + 0);
} end function total

template <typename... Ts>
function forward_total(Ts...: args) -> int {
    return total(args...);
} end function forward_total

function main() -> int {
    int: n = count(1, "a", 2.0);
    int: t = total(1, 2, 3);
    int: f = forward_total(4, 5);
    return 0;
} end function main
```

Here `n` is `3`, `t` is `6` and `f` is `9`.

**Pack fields.** A class template can hold a pack as a field: `Ts...: items;` declares one field for each type in `Ts`. `items[0]`, `items[1]` and so on are the fields, and the index has to be a constant, because each field can have a different type. `countof(items)` is how many there are. A brace list fills them in order, and `{values...}` expands a function's parameter pack into it. A pack field can't have an initializer. [`std::collections::Tuple`](Documentation/Library/collections.md#tuple) is a struct with one pack field.

```walnut
template <typename... Ts>
struct Row {
    Ts...: cells;
} end struct Row

template <typename... Ts>
function make_row(Ts...: values) -> Row<Ts...> {
    return Row<Ts...>{values...};
} end function make_row

function main() -> int {
    Row<int, string>: r{1, "one"};
    auto: s = make_row(2, "two", 2.5);
    r.cells[0] += 10;
    long int: width = countof(s.cells);
    auto: [number, word] = r;
    return 0;
} end function main
```

Here `r.cells[0]` is `11`, `width` is `3`, `number` is `11` and `word` is `"one"`. A function can take any `Row` with `template <typename... Ts> function f(const Row<Ts...>&: row)`, and `Row<>` holds nothing.

**Overloads with packs.** When two overloads match a call equally well, the one with more ordinary (non-pack) parameters wins, because it is the more specific one. If they have the same number, the one with no variadic parameter wins, so `constructor()` is chosen over `constructor(T...: items)` for `Box: b;`:

```walnut
template <typename... Ts>
function describe(Ts...: rest) -> string {
    return "anything";
} end function describe

template <typename... Ts>
function describe(int: first, Ts...: rest) -> string {
    return "starts with an int";
} end function describe

function main() -> int {
    string: a = describe(1, "x");
    string: b = describe("x", 1);
    return 0;
} end function main
```

Here `a` is `"starts with an int"` and `b` is `"anything"`.

### Class templates

```walnut
template <typename T>
class Box {
public:
    T: item;
    constructor(T: i) : item = i { }
    function get() const -> T {
        return item;
    } end function get
} end class Box

template <typename T, int N>
class Buffer {
public:
    array[N] T: data;
    function size() const -> int {
        return N;
    } end function size
} end class Buffer

function main() -> int {
    Box<string>: b = Box<string>("hi");
    Buffer<int, 4>: buf;
    string: got = b.get();
    int: size = buf.size();
    return 0;
} end function main
```

Here `got` is `"hi"` and `size` is `4`.

**Methods are checked when they're used.** A method of a class template is only type-checked, and only turned into C++, for a `T` it is actually called with. So a `Box<T>` can have a `sort()` that needs `<`, and `Box<Point>` still works as long as nobody calls `sort()` on it. A call that does need `<` is an error at that call, as usual. Virtual methods, constructors, destructors, `operator(=)` and conversion operators are always checked, because they can be used without being called by name.

### Variable templates

A variable template is a constant with template parameters. `name<args>` gives its value for those arguments.

```walnut
template <typename T>
constexpr long int: bits = sizeof(T) * 8;

template <typename T>
constexpr bool: is_small = bits<T> <= 32;

template <int N>
constexpr long int: factorial = N * factorial<N - 1>;

template <>
constexpr long int: factorial<0> = 1;

template <typename T>
const string: label = "a " + typeid(T);

template <typename T>
requires is_small<T>
function widen(T: x) -> long int {
    return x;
} end function widen

function main() -> int {
    static_assert(bits<int> == 32, "int is 32 bits");
    long int: f = factorial<10>;
    string: s = label<bool>;
    long int: w = widen(7);
    return 0;
} end function main
```

Here `f` is `3628800`, `s` is `"a bool"` and `w` is `7`.

- A variable template is `constexpr` or `const`, and has an initializer. A `constexpr` one is worked out while compiling, so it can go in `static_assert`, `if constexpr`, `requires` clauses, array sizes and template arguments.
- It can be [specialized](#specialization) like a function: `template <> constexpr long int: factorial<0> = 1;`. A recursive one needs a specialization (or another way to stop), because both sides of `?:` are instantiated; one that never stops is reported as going past the recursion depth limit.
- Inside a class it is `static`: `template <typename T> static constexpr long int: bits = sizeof(T) * 8;`, used as `Limits::bits<int>`.
- The [type traits](#traits) are variable templates in `std::traits`.

### Alias templates

An alias template is a name for a family of types: `template <...> using Name = type;`. `Name<args>` is that type with the arguments filled in.

```walnut
import module.collections

template <typename T>
using Grid = std::collections::Vector<std::collections::Vector<T>>;

template <typename V>
using ByName = std::collections::Map<string, V>;

template <typename T>
using Plain = std::traits::discard_cv<std::traits::discard_ref<T>>;

template <typename... Ts>
using Row = std::collections::Tuple<Ts...>;

function main() -> int {
    Grid<int>: board;
    board.push_back(std::collections::Vector<int>{1, 2, 3});
    ByName<long int>: ages;
    ages.insert("Ada", 36);
    static_assert(std::traits::is_same<Plain<const int&>, int>, "Plain drops const and &");
    Row<int, string>: r{1, "one"};
    return 0;
} end function main
```

- An alias template is the same type as what it stands for, so `Grid<int>` and `Vector<Vector<int>>` can be used interchangeably.
- Its parameters can be types, values (`template <int N> using Buf = Buffer<char, N>;`) and packs, and can have defaults.
- It can't be specialized; specialize the class it names instead.
- The [type operations](#type-operations) are alias templates in `std::traits`.

### Specialization

`template <>` gives a version of a template for specific arguments:

```walnut
template <typename T>
function describe(T: x) -> string {
    return "something";
} end function describe

template <>
function describe<int>(int: x) -> string {
    return "an int";
} end function describe

function main() -> int {
    string: a = describe(2.5);
    string: b = describe(3);
    return 0;
} end function main
```

Here `a` is `"something"` and `b` is `"an int"`. Classes are specialized the same way: `template <> class Box<bool> { ... } end class Box`.

### Type flags and SFINAE

A class template that inherits from [`std::true_type` or `std::false_type`](Documentation/Library/core.md#constants) answers a yes-or-no question about a type, and specializations change the answer for particular types. Its `value` is a `constexpr bool`.

When filling in a function template's parameter types or return type fails, that template is quietly left out of the call instead of being an error ("substitution failure is not an error", SFINAE). [`std::enable_if_t<condition, T>`](Documentation/Library/core.md#enable_if) is `T` when the condition is true and fails otherwise, so it switches overloads on and off:

```walnut
struct Point {
    int: x;
} end struct Point

template <typename T>
struct is_point inherits std::false_type {
} end struct is_point

template <>
struct is_point<Point> inherits std::true_type {
} end struct is_point

template <typename T>
function describe(T: value) -> std::enable_if_t<is_point<T>::value, string> {
    return "a point";
} end function describe

template <typename T>
function describe(T: value) -> std::enable_if_t<!is_point<T>::value, string> {
    return "something else";
} end function describe

function main() -> int {
    Point: p;
    static_assert(is_point<Point>::value && !is_point<int>::value, "flags");
    string: a = describe(p);
    string: b = describe(3);
    return 0;
} end function main
```

Here `a` is `"a point"` and `b` is `"something else"`. A [`requires` clause](#concepts) usually says the same thing more directly (`requires is_point<T>::value`), but `enable_if` works where a constraint can't go, such as inside a type.

### Concepts

A concept is a named requirement on a type. Its body can be:
- a `requires (params) { expressions; }` block, which checks that the expressions compile
- a type trait such as `std::traits::is_arithmetic<T>`, or another `constexpr bool` [variable template](#variable-templates)
- another concept
- any combination of these with `&&`, `||` and `!`

Use a concept in place of `typename`, or in a `requires` clause.

```walnut
template <typename T>
concept Addable = requires (T: a, T: b) { a + b; }

template <typename T>
concept Number = std::traits::is_arithmetic<T> && !std::traits::is_same<T, bool>

template <Addable T>
function twice(T: x) -> T {
    return x + x;
} end function twice

template <typename T>
requires Number<T> && std::traits::is_integer<T>
function half(T: x) -> T {
    return x / 2;
} end function half

function main() -> int {
    static_assert(Addable<string>, "strings can be added");
    static_assert(!Number<bool>, "bool isn't a number here");
    int: a = twice(21);
    string: b = twice<string>("ab");
    int: c = half(9);
    return 0;
} end function main
```

Here `a` is `42`, `b` is `"abab"` and `c` is `4`.

- A requirement can also check the result type: `{ x.size() } -> int;`.
- A concept definition has no `;` after it.
- Calling a constrained template with a type that doesn't qualify names what failed, such as `Addable<P> is not satisfied` or `std::traits::is_integer<float> is not satisfied`.
- Ready-made concepts are in `std::concepts`, which every program has: `integer_type`, `floating_point`, `same_as`, `totally_ordered` and [others](Documentation/Library/core.md#stdconcepts). `template <std::concepts::integer_type T>` and `requires std::concepts::integer_type<T>` both work.

## Lambdas

A lambda is `[captures](parameters) -> return_type { body }`. The `-> return_type` is optional.

```walnut
function make_adder(int: n) -> function(int) -> int {
    return [n](int: x) -> int { return x + n; };
} end function make_adder

function main() -> int {
    int: base = 10;
    auto: add = [base](int: x) -> int { return x + base; };
    auto: grow = [&base]() -> void { base++; };
    auto: counter = [base]() mutable -> int { base++; return base; };
    auto: scaled = [factor = base * 3](int: x) -> int { return x * factor; };
    auto: same = [](auto: x) { return x; };
    grow();
    function(int) -> int: add5 = make_adder(5);
    int: a = add(1);
    int: c1 = counter();
    int: c2 = counter();
    int: s = scaled(2);
    string: word = same("str");
    int: seven = add5(2);
    int: negated = [](int: x) -> int { return -x; }(3);
    return 0;
} end function main
```

Here `base` ends as `11`, `a` is `11`, `c1` and `c2` are `11` and `12`, `s` is `60`, `word` is `"str"`, `seven` is `7` and `negated` is `-3`. `add`, `counter` and `scaled` copied `base` (or computed from it) when they were created, before `grow()` changed it.

**Captures**
- `[x]` copies `x` and `[&x]` refers to it.
- `[name = expression]` creates a new copy initialized from the expression.
- `[=]` copies everything used and `[&]` refers to everything used.
- `[this]` captures the current object.

**Other forms**
- `mutable` lets the lambda change its own copies; without it, changing a copied value is an error.
- An `auto:` parameter makes a generic lambda.
- Parameters can have default values and be [variadic](#variadic-parameters), as for functions. They become part of the lambda's [function type](#function-types), so they keep working when the lambda is copied, stored or passed to another function. Because of that, a lambda's default values have to be constants.

**Storing a lambda.** Keep it in an `auto:` variable, a `function(...) -> ...` variable, or a `var`. A `var` works only when the parameters and the result are types a `var` can hold.

## Coroutines

A function that returns `generator<T>` produces values one at a time with `co_yield`. Loop over it with `for ... in`. A bare `co_return;` (or `return;`) ends it early.

```walnut
function count_to(int: n) -> generator<int> {
    for (int: i = 1; i <= n; i++) {
        co_yield i;
    }
} end function count_to

function compute(int: x) async -> int {
    return x * 10;
} end function compute

function chain(int: x) async -> int {
    int: a = await compute(x);
    return a + await compute(a);
} end function chain

function main() -> int {
    int: sum = 0;
    for (int: v in count_to(3)) {
        sum += v;
    }
    task<int>: t = chain(2);
    int: result = await t;
    return 0;
} end function main
```

Here `sum` is `6` and `result` is `220`.

**`async` functions**
- An `async` function returns `task<T>`; `async -> void` is allowed too.
- `await` (or `co_await`) waits for a task and gives its result. It can be used in any function, including `main`.
- A task starts running when it is awaited, not when it is created.
- `main` itself can't be `async`.

## Classes and structs

```walnut
class Counter {
    int: secret = 5;
public:
    int: value = 10;
    static int: instances = 0;
    mutable int: reads;
    constructor() {
        instances = instances + 1;
    }
    function get() const -> int {
        reads++;
        return this->value + secret;
    } end function get
    function bump() -> Counter& {
        value += 1;
        return *this;
    } end function bump
    static function count() -> int {
        return instances;
    } end function count
} end class Counter

struct Point {
    int: x = 1;
    int: y = 2;
} end struct Point

function main() -> int {
    Counter: c;
    c.bump().bump();
    int: got = c.get();
    int: made = Counter::count();
    Point: p = Point{7, 8};
    return 0;
} end function main
```

Here `got` is `17`, `c.reads` is `1`, `made` is `1`, and `p` is `{7, 8}`.

**Access**
- `class` members are `private` until an access section says otherwise. `struct` members are `public`.
- Sections are written `public:`, `private:` and `protected:`.
- A derived class can use its bases' `public` and `protected` members, but not their `private` ones.

**Members**
- A field can have a default value (`int: value = 10;`).
- `static` fields and methods belong to the class and are reached as `Counter::count()`.
- `const` after a method's parameters means it doesn't change the object. Calling a non-const method on a const object is an error.
- `mutable` fields can still change inside `const` methods.
- `lazy` fields are computed the first time they are read; see [Lazy fields](#lazy-fields).
- `this` is a pointer to the current object: use `this->x` and `*this`.
- `self` is the current object itself, the same as `*this`: `self.x`, `return self;`, and `self(x)` to call the object's own `operator(())`. In a `const` method it is `const`. A lambda that captures `[this]` can use `self` too.

**Construction**
- `Point{7, 8}` sets the fields in order (aggregate initialization).
- `Counter: c;` uses the default constructor.
- `Box: b = Box(5);` calls a constructor.
- `Box: b = {5};` and `Box: b{5};` call a constructor with the values in the braces. A brace list can also be passed straight to a function or constructor (`draw({3, 4})`, `push_back({1, 2})`) or returned (`return {3, 4};`); it builds the parameter's (or return) type the same way. A non-`explicit` constructor that takes the values is used, or the fields are filled in order if the class has no constructors.
- A constructor whose only parameter is a [`std::initializer_list<T>`](Documentation/Library/core.md#stdinitializer_list) is chosen first for a brace list whose values all convert to `T`, as in C++: `Bag: b = {1, 2, 3};` passes all three values to it. Parentheses skip that rule, so `Bag: b(1, 2);` still calls a two-parameter constructor.

**Forward declaration.** `class Name;` declares a class before its definition.

### Lazy fields

A `lazy` field's initializer is a recipe instead of a starting value. It runs the first time the field is read, and the result is kept for later reads. `invalidate` throws the kept value away, so the next read runs the initializer again. This is for values that are expensive to work out and depend on other fields, such as a bounding box or a total:

```walnut
import module.io

class Mesh {
    float: width = 2.0;
    float: height = 3.0;
    int: computed = 0;

    function compute_area() -> float {
        computed++;
        return width * height;
    } end function compute_area

public:
    lazy float: area = compute_area();

    function resize(float: w, float: h) -> void {
        width = w;
        height = h;
        invalidate area;
    } end function resize

    function times_computed() const -> int {
        return computed;
    } end function times_computed
} end class Mesh

function main() -> int {
    Mesh: m;
    std::io::println(m.area, " ", m.area, " ", m.times_computed());
    m.resize(4.0, 5.0);
    std::io::println(m.area, " ", m.times_computed());
    return 0;
} end function main
```

This prints `6 6 1` and then `20 2`: the area is worked out once, and again only after `resize` invalidates it.

- The field is read like any other (`m.area`), and `const` methods can read it too, because computing it doesn't count as changing the object.
- The initializer runs inside the object, so it can use the other fields and call the class's methods, `const` or not. It must be `= <expression>`, not a brace list.
- `invalidate a, b;` invalidates several fields. It can also name a field of another object, as in `invalidate other.area;`.
- Only the class itself (and its friends) can assign a lazy field or invalidate it, and from outside the class it reads as `const`, so `m.items.push_back(x)` is an error too. Assigning sets the value and marks it as computed, so the initializer won't run until the next `invalidate`.
- `public lazy` lets code outside the class assign and invalidate the field as well, as long as the object isn't `const`: `public lazy string: label = default_label();`.
- If the initializer throws, the field stays uncomputed and the next read tries again. If the initializer reads, assigns or invalidates its own field, the program stops with an error instead of looping forever.
- `lazy threadsafe` makes the field safe to read from several threads at once: one thread computes the value while the others wait for it, and assigning and invalidating wait their turn too. Plain `lazy` fields are for one thread at a time. A reference taken from a read (`float&: r = m.area;` inside the class) isn't protected from a later `invalidate`.
- Copying an object copies the computed value along with it, if there is one.
- Lazy fields are skipped by aggregate initialization (`Point{1.0, 2.0}` sets the other fields in order), by structured bindings and by the automatic `==`.
- A lazy field takes one byte more than the value it holds (rounded up to the value's alignment), to remember whether it has been computed.
- Only class and struct fields can be `lazy`; locals, globals, `static` fields, arrays, references and union fields can't.

### Constructors and destructors

```walnut
class Base {
public:
    int: tag;
    constructor(int: t) : tag = t { }
} end class Base

class Rect inherits Base {
public:
    int: w;
    int: h;
    array[2] int: corner;
    constructor(int: a, int: b) : Base(100), w = a, h(b), corner{a, b} { }
    constructor(int: side) explicit : Rect(side, side) { }
    constructor() : Rect{1} { }
    destructor() {
        w = 0;
    }
} end class Rect

function main() -> int {
    Rect: r = Rect(2, 3);
    Rect: s = Rect(4);
    Rect: unit;
    int: area = r.w * r.h;
    return 0;
} end function main
```

Here `area` is `6`. `s` is a 4×4 rectangle and `unit` a 1×1 one, both with `tag` 100.

**The initializer list** comes after `:`. Each field (or base class) can be set three ways: `x = v`, `x(v)` or `x{a, b}`.

**Delegating constructors.** A constructor can hand all the work to another constructor of the same class by naming the class in its initializer list, as `Rect(side, side)` does above. That must be the only initializer, and a constructor can't hand off to itself.

**Template constructors.** A constructor can have its own template parameters, written with `template <...>` in front of it inside the class. Each call works out the parameters from its arguments, the same way a [function template](#function-templates) does, and the class can be a template too:

```walnut
template <typename T>
class Holder {
public:
    T: value;
    string: origin;

    template <typename U>
    constructor(U&&: input) : value(std::core::forward<U>(input)), origin("one value") { }

    template <typename A, typename B>
    constructor(const A&: a, const B&: b) : value(a + b), origin("two values") { }
} end class Holder

function main() -> int {
    string: name = "walnut";
    Holder<string>: copied(name);
    Holder<string>: moved(std::core::move(name));
    Holder<float>: sum(1, 2.5);
    return 0;
} end function main
```

Here `copied.value` is `"walnut"`, `moved.value` is `"walnut"` (and `name` was moved from), and `sum.value` is `3.5` with `sum.origin` set to `"two values"`. `U&&` is a forwarding reference (see [Copying and moving](#copying-and-moving)), so `copied` receives `name` as a `string&` and copies it, while `moved` receives a temporary and moves it.

- A template constructor has to be written inside its class with its body, and can't be `= default` or `= delete`.
- Like in C++, a template constructor is never used to copy or move: copying a `Holder<string>` from another one, or moving it, uses the class's own copy or move constructor even when the template would also fit, unless the template fits better (for example, a non-`const` lvalue passed to a `U&&` parameter).
- Declaring a template constructor takes away the automatic constructor with no arguments, the same as declaring any other constructor. Add `constructor() { }` if `Holder<int>: h;` should still work.

**Special forms**
- `constructor() = default;` asks for the compiler-generated version.
- `= delete;` forbids that constructor.
- `destructor() = default;` and `destructor() = delete;` work the same way.

**Destructors**
- The destructor runs when the object goes out of scope or is deleted. Objects are destroyed in the reverse order they were created.
- In a class with virtual methods, the destructor is virtual automatically.

**Every field must end up initialized.** For each constructor, Walnut checks fields that have no default value and aren't in the initializer list. It is an error if such a field:
- is a reference
- is `const`
- has a class type with no default constructor
- has a class type that can't be default-constructed because of its own fields

The base classes follow the same rule. A class with no default constructor also can't be created without arguments (`Holder: h;`).

```
member 'where' is never initialized, and 'Point' has no default constructor; initialize it in this constructor's initializer list
'Holder' cannot be default-constructed, because its member 'p' has no initializer and 'Point' has no default constructor
```

### Defining methods outside the class

A method can be declared in the class and defined later, outside it. The definition names the class with `::` and must match a declaration's parameters and `const`. `static`, `virtual` and the other qualifiers stay on the declaration.

```walnut
class Account {
    int: balance = 0;
public:
    function deposit(int: amount) -> Account&;
    function get() const -> int;
    static function opened_with(int: amount) -> Account;
} end class Account

function Account::deposit(int: amount) -> Account& {
    balance += amount;
    return *this;
} end function Account::deposit

function Account::get() const -> int {
    return balance;
} end function Account::get

function Account::opened_with(int: amount) -> Account {
    Account: a;
    a.balance = amount;
    return a;
} end function Account::opened_with

function main() -> int {
    Account: acc = Account::opened_with(10);
    acc.deposit(5).deposit(3);
    int: total = acc.get();
    return 0;
} end function main
```

Here `total` is `18`.

- A class inside a namespace is named in full: `function shapes::Circle::area() const -> int`.
- Defining a method that the class doesn't declare is an error, and so is defining one twice.
- Templates' methods are defined inside the class.

**Constructors, destructors and operators** can be defined outside the class the same way. Declare them in the class ending with `;`, then write the definition with the class's name right after the keyword:

```walnut
class Account {
    int: balance;
public:
    constructor(int: start);
    destructor();
    operator(+=) function(int: amount) -> Account&;
    operator(int) function() const;
} end class Account

constructor Account(int: start) : balance = start { }

destructor Account() {
    balance = 0;
}

operator(+=) function Account(int: amount) -> Account& {
    balance += amount;
    return *this;
}

operator(int) function Account() const {
    return balance;
}

function main() -> int {
    Account: acc(10);
    acc += 5;
    int: total = acc;
    return 0;
} end function main
```

Here `total` is `15`.

- The definition's parameters (and `const`, for operators) must match a declaration in the class. The initializer list (`: balance = start`) goes on the definition.
- A class in a namespace is named in full, as in `constructor bank::Account(int: start)`, or the definition goes inside `namespace bank { ... }` with just `constructor Account(...)`. Inside the parameters, the class can be named with or without its namespace.
- A constructor or destructor that is declared but never defined is an error.
- Friend operators aren't members, so they are defined like any free operator, without the class name.

## Copying and moving

The copy and move members are declared like this:

| Member | Declaration |
|---|---|
| Copy constructor | `constructor(const T&: other)` |
| Move constructor | `constructor(T&&: other)` |
| Copy assignment | `operator(=) function(const T&: other) -> T&` |
| Move assignment | `operator(=) function(T&&: other) -> T&` |

**What is generated**
- A class that declares none of them gets all four automatically, each copying or moving field by field.
- If you declare a move constructor, the class gets no copy constructor and no copy assignment. To move-assign, add an `operator(=)` that takes `T&&`.
- A deleted copy constructor also removes copy assignment.
- If the move constructor is deleted, moving falls back to copying.
- A class can't be copied or moved if one of its fields or its bases can't be.

```walnut
class Resource {
public:
    int: id;
    int: copies = 0;
    constructor(int: i) : id = i { }
    constructor(const Resource&: other) : id = other.id, copies = other.copies + 1 { }
    constructor(Resource&&: other) : id = other.id {
        other.id = -1;
    }
    operator(=) function(Resource&&: other) -> Resource& {
        id = other.id;
        other.id = -1;
        return *this;
    }
} end class Resource

function main() -> int {
    Resource: a = Resource(1);
    Resource: b = a;
    Resource: c = std::core::move(a);
    return 0;
} end function main
```

Here `b` is a copy (`b.copies` is `1`), `c` took `a`'s value (`c.id` is `1`), and `a.id` is now `-1`.

`std::core::move(x)` lets `x` be moved from; afterwards `x` is valid but its value is up to the move constructor. It comes from the [core library](Documentation/Library/core.md), and does the same as `static_cast<T&&>(x)`. Returning a local variable from a function moves it automatically.

When a function has both a `const T&` and a `T&&` overload, a temporary or a `std::core::move`d value goes to the `T&&` one and anything else to the `const T&` one, so `v.push_back(Thread(...))` moves a value that can't be copied.

**Forwarding references.** In a function template, a parameter written `T&&`, where `T` is that template's own type parameter, takes anything. Given a variable, `T` becomes a reference (`int&`, or `const int&` for a `const` variable) and the parameter is an lvalue reference to it; given a temporary, `T` is the plain type and the parameter is `T&&`. `static_cast<T&&>(value)` passes the argument on the way it came in:

```walnut
template <typename T>
function kind(T&&: value) -> string {
    if constexpr (std::traits::is_lvalue_ref<T>) then {
        return "variable";
    } else then {
        return "temporary";
    }
} end function kind

template <typename T>
function pass_on(T&&: value) -> string {
    return kind(static_cast<T&&>(value));
} end function pass_on

function main() -> int {
    int: n = 1;
    const int: c = 2;
    string: a = kind(n);
    string: b = kind(3);
    string: d = kind(c);
    string: e = pass_on(n);
    string: f = pass_on(4);
    return 0;
} end function main
```

Here `a`, `d` and `e` are `"variable"`, and `b` and `f` are `"temporary"`. When a `T&` overload and a forwarding `T&&` overload both fit a variable, the `T&` one is chosen.

`std::core::forward<T>(value)` from the [core library](Documentation/Library/core.md) does the same as `static_cast<T&&>(value)` and reads better: `pass_on` above could `return kind(std::core::forward<T>(value));`. A pack works too: `template <typename... Ts> function make(Ts&&...: args)` takes each argument as a forwarding reference.

A copy that isn't allowed is reported with the reason:

```
cannot copy 'NoCopy': its copy constructor is deleted
cannot copy 'Wrap': member 'n' of type 'NoCopy' cannot be copied, because its copy constructor is deleted
cannot copy 'MO': it has its own move constructor, so it gets no copy constructor
```

## Inheritance

```walnut
class Shape {
public:
    string: name;
    constructor(string: n) : name = n { }
    function area() const abstract -> int;
    function describe() const virtual -> string {
        return "shape " + name;
    } end function describe
} end class Shape

class Square inherits Shape {
public:
    int: side;
    constructor(int: s) : Shape("square"), side = s { }
    function area() const override -> int {
        return side * side;
    } end function area
    function describe() const override -> string {
        return Shape::describe() + "!";
    } end function describe
} end class Square

function main() -> int {
    Square: sq = Square(4);
    Shape*: p = &sq;
    int: area = p->area();
    string: text_of = p->describe();
    Square*: back = dynamic_cast<Square*>(p);
    return 0;
} end function main
```

Here `area` is `16`, `text_of` is `"shape square!"`, and `back` points to `sq`.

**The heading.** `class Square inherits Shape` names the base class; `extends` (or `inherit`) means the same thing. All bases are public.

**Virtual methods**
- `virtual` and `override` go after the parameter list.
- `abstract` marks a method that has no body here and must be implemented by subclasses. It implies `virtual`, ends with `;`, and can't be given a definition anywhere, inside or outside the class. A class with an abstract method can't be created, and neither can a subclass that doesn't implement it.
- A `virtual` method with no body that is never defined is abstract too, but the compiler warns about it, because it may be a forgotten definition. Write `abstract` to say it's on purpose.
- Only methods can be abstract, not constructors, destructors or operators.
- `override` on a method that doesn't override anything is an error.

**Calling the base.** `Base::method()` calls a base class's version.

**Casting down.** `dynamic_cast` and `as` need a class with at least one virtual method.

### Multiple inheritance

A class can have several bases, separated by commas. Each base gets its own entry in the initializer list.

```walnut
class Named {
public:
    string: name;
    constructor(string: n) : name = n { }
} end class Named

class Sized {
public:
    int: size;
    constructor(int: s) : size = s { }
    function grow() -> void { size++; } end function grow
} end class Sized

class Crate inherits Named, Sized {
public:
    constructor(string: n, int: s) : Named(n), Sized(s) { }
} end class Crate

function main() -> int {
    Crate: c = Crate("crate", 3);
    c.grow();
    Sized*: as_sized = &c;
    int: size = as_sized->size;
    return 0;
} end function main
```

Here `size` is `4`.

**Ambiguous names.** If two bases both provide a member with the same name, using that name is an error until the class declares its own member of that name to decide which one it means. The same happens when one base is reached twice through different parents (a "diamond"): the class then holds two separate copies of that base.

```
'x' is ambiguous in 'C': it is inherited from 'A' and 'B'; declare 'x' in this class to decide which one it means
```

**Naming a base's member.** `obj.Base::member` (or `ptr->Base::member`) uses the member as `Base` declares it. For a method, this calls `Base`'s version directly, even if it is `virtual` and the object's class overrides it. It also picks one of two inherited members with the same name.

```walnut
class Animal {
public:
    int: legs = 4;
    function speak() const virtual -> string {
        return "...";
    } end function speak
} end class Animal

class Dog inherits Animal {
public:
    int: legs = 3;
    function speak() const override -> string {
        return "woof";
    } end function speak
} end class Dog

function main() -> int {
    Dog: d;
    string: own = d.speak();
    string: base = d.Animal::speak();
    int: dog_legs = d.legs;
    int: animal_legs = d.Animal::legs;
    return 0;
} end function main
```

Here `own` is `"woof"`, `base` is `"..."`, `dog_legs` is `3` and `animal_legs` is `4`. Naming a class that isn't the object's class or one of its bases is an error. Inside a method, `Base::member` without an object does the same for `this`.

### Friends and nested types

```walnut
class Vault {
    int: secret = 42;
public:
    friend function peek(const Vault&: v) -> int;
    enum Mode { Open, Closed }
    class Key {
    public:
        int: code = 7;
    } end class Key
    Mode: mode = Mode::Closed;
    template <typename T>
    function scaled(T: k) const -> T {
        return k * 2;
    } end function scaled
} end class Vault

function peek(const Vault&: v) -> int {
    return v.secret;
} end function peek

function main() -> int {
    Vault: v;
    Vault::Key: k;
    int: secret = peek(v);
    bool: closed = v.mode == Vault::Mode::Closed;
    float: doubled = v.scaled(1.25);
    return 0;
} end function main
```

Here `secret` is `42`, `k.code` is `7`, `closed` is `true` and `doubled` is `2.5`.

**Friends.** `friend function f(...)`, `friend operator(...) function(...)` and `friend class C;` give access to private members. A friend function or operator can be defined right where it is declared; see [Stream operators](#stream-operators) for a friend `<<`.

**Defining a friend inside the class.** A friend defined in the class body is not a member. It belongs to the scope around the class: the namespace (call it as `shapes::f(...)` from outside), or the function body for a class declared inside a function. This works in class templates too:

```walnut
template <typename T>
class Box {
    T: v;
public:
    constructor(T: x) : v = x { }
    friend function get(const Box&: b) -> T {
        return b.v;
    } end function get
    friend operator(+) function(const Box&: a, const Box&: b) -> Box {
        return Box(a.v + b.v);
    }
} end class Box

function main() -> int {
    Box<int>: sum = Box<int>(2) + Box<int>(5);
    int: seven = get(sum);
    string: hi = get(Box<string>("hi"));

    struct Counter {
        int: n = 0;
        friend function bump(Counter&: c) -> int {
            c.n += 1;
            return c.n;
        } end function bump
    } end struct Counter

    Counter: c;
    bump(c);
    int: two = bump(c);
    return 0;
} end function main
```

Here `seven` is `7`, `hi` is `"hi"` and `two` is `2`.

- In a class template, the class's own name without arguments (`Box`) means the current type (`Box<T>`), as it does in the class's methods. The friend becomes a function template over the class's template parameters, so `get` works for every `Box<T>`, and its arguments must let `T` be worked out from the call: `get(sum)` finds `T` from `sum`.
- A friend that is itself a template (`template <typename U> friend function ...`) can be defined in the class as well.
- Inside the friend's body, names are looked up in the class first, as in a method: nested types such as `Mode`, static members and the class's own name work without `Box::`. Members that need an object (fields and non-static methods) must be reached through a parameter, as in `b.v`, because a friend has no `this`.
- A friend can be defined inside a nested class (a class inside another class) too, and is then called as if it were declared after the outer class. The exception is a class nested inside a class template: there, declare the friend in the class and define it after the outer class.

**Nested types.** Classes and enums can be declared inside a class and are named as `Outer::Inner`.

**Member templates.** Methods can be templates.

## Operator overloading

An operator is written `operator(symbol) function(parameters) -> result { ... }`. Write it inside a class to make it a member, or at file or namespace level to make it a free function.

**Overloadable operators**
- Arithmetic: `+ - * / % ** ++ --`
- Comparison and logic: `== != < > <= >= && || !`
- Bitwise: `& | ^ ~ << >>`
- Assignment: `= += -= *= /= %= **= &= |= ^= <<= >>=`
- Access and calls: `[] () ->`
- Allocation: `new delete new[] delete[]`

**How many parameters**
- A member takes one parameter fewer than a free function. The left operand is the object itself.
- Postfix `++` and `--` take an extra `int` parameter that isn't used.

**Member-only operators.** `=`, `[]`, `()`, `->` and conversions must be members.

**Deleting.** `= delete;` forbids an operator for that type.

**At least one user type.** A free operator needs at least one parameter that is a class, struct, union or enum. An overload that only uses built-in types, such as `operator(+) function(int: a, int: b)`, would change how the language itself behaves, so it is an error. `new` and `delete` are the exception.

```walnut
class Vec {
public:
    int: x;
    int: y;
    constructor(int: a, int: b) : x = a, y = b { }
    operator(+) function(const Vec&: o) const -> Vec {
        return Vec(x + o.x, y + o.y);
    }
    operator(+=) function(const Vec&: o) -> Vec& {
        x += o.x;
        y += o.y;
        return *this;
    }
    operator([]) function(int: i) -> int& {
        if (i == 0) then {
            return x;
        }
        return y;
    }
    operator(()) function(int: k) const -> int {
        return (x + y) * k;
    }
} end class Vec

operator(-) function(const Vec&: v) -> Vec {
    return Vec(-v.x, -v.y);
}

function main() -> int {
    Vec: a = Vec(1, 2);
    Vec: b = a + Vec(3, 4);
    b += a;
    b[0] = 10;
    Vec: n = -b;
    int: called = b(2);
    bool: same = a == Vec(1, 2);
    return 0;
} end function main
```

Here `b` is `{10, 8}`, `n` is `{-10, -8}`, `called` is `36` and `same` is `true`.

**Templates.** A free operator can be a template. Its template arguments are worked out from the operands, as for a function call:

```walnut
struct Vec {
    int: x;
    int: y;
} end struct Vec

template <typename T>
operator(*) function(T: k, const Vec&: v) -> Vec {
    return Vec{v.x * k, v.y * k};
}

function main() -> int {
    Vec: v = Vec{1, 2};
    Vec: a = 3 * v;
    return 0;
} end function main
```

Here `a` is `{3, 6}`. Operators declared inside a class can be templates the same way, and every operator except conversions can be one:

```walnut
class Grid {
public:
    int: base = 10;
    template <typename T>
    operator([]) function(T: i) const -> int {
        return base + static_cast<int>(i);
    }
} end class Grid

function main() -> int {
    Grid: g;
    int: a = g[2];
    int: b = g[2.5];
    return 0;
} end function main
```

Here `a` is `12` and `b` is `12`. When a template and an ordinary function match equally well, the ordinary one is used.

A template `++`, `--` or `->` declared inside a class has no operand to work its template arguments out from, so its template parameters need default values (`template <typename T = int>`). A free template `++` or `--` gets them from its operand, as in `template <typename T> operator(++) function(Box<T>&: b) -> Box<T>&`.

### Smart pointers with operator(->)

`operator(->)` makes a class usable like a pointer. It returns a pointer, or another object that has its own `operator(->)`, and `obj->member` follows it.

```walnut
class Engine {
public:
    int: power = 300;
} end class Engine

class Handle {
public:
    Engine*: target;
    operator(->) function() -> Engine* {
        return target;
    }
} end class Handle

function main() -> int {
    Engine: e;
    Handle: h;
    h.target = &e;
    h->power = 350;
    return 0;
} end function main
```

At the end `e.power` is `350`.

### Custom allocation

A class can replace how `new` and `delete` get memory by defining `static operator(new)` and `static operator(delete)`. `operator(new)` receives the number of bytes and returns the memory as a `void*`. `operator(delete)` receives that memory back. The object is constructed in the memory, and destroyed before the memory is handed back.

```walnut
class Pooled {
public:
    int: value;
    static array[64] long int: storage;
    static int: live = 0;
    constructor(int: v) : value = v { }
    static operator(new) function(long int: bytes) -> void* {
        live++;
        return reinterpret_cast<void*>(&storage[0]);
    }
    static operator(delete) function(void*: memory) -> void {
        live--;
    }
} end class Pooled

function main() -> int {
    Pooled*: p = new Pooled(3);
    int: during = Pooled::live;
    delete p;
    int: after = Pooled::live;
    return 0;
} end function main
```

Here `during` is `1` and `after` is `0`.

- `operator(new[])` and `operator(delete[])` do the same for `new T[n]` and `delete[] p`. Every element is constructed and later destroyed, in reverse order.
- A class that defines `operator(new)` must also define `operator(delete)`, and the same for the array forms, because memory from one has to go back to the other.

### Stream operators

The [io library](Documentation/Library/io.md) has C++-style streams: `std::io::cout`, `std::io::cin`, files, string streams and the rest. `<<` writes to an `ostream` and `>>` reads from an `istream`, and both are already defined for the built-in types.

A type of your own adds `<<` and `>>` with an operator that takes the stream as its first parameter and returns it, so calls chain. The operator can be a free function, or a `friend` declared inside the class when it needs private members. Write it for `std::io::ostream&` and `std::io::istream&`, and it works on every stream: `cout`, files, string streams and so on.

```walnut
import module.io

struct Point {
    int: x;
    int: y;
} end struct Point

operator(<<) function(std::io::ostream&: out, const Point&: p) -> std::io::ostream& {
    return out << "(" << p.x << ", " << p.y << ")";
}

operator(>>) function(std::io::istream&: input, Point&: p) -> std::io::istream& {
    return input >> p.x >> p.y;
}

class Money {
    long int: cents;
public:
    constructor(long int: c) : cents = c { }
    friend operator(<<) function(std::io::ostream&: out, const Money&: m) -> std::io::ostream& {
        return out << "$" << m.cents / 100 << "." << std::io::setw(2) << std::io::setfill('0') << m.cents % 100;
    }
} end class Money

function main() -> int {
    std::io::istringstream: source = std::io::istringstream("3 4");
    Point: p;
    source >> p;
    std::io::cout << "point " << p << ", price " << Money(1205) << std::io::endl;
    return 0;
} end function main
```

This prints `point (3, 4), price $12.05`.

- A friend operator has the same parameters as a free one. It is not a member, so it doesn't take the class as `this`.
- `>>` takes the target by non-const reference. If reading fails, the stream's `failbit` is set and `if (stream)` is `false`.

### Conversion operators

`operator(T) function() const { ... }` lets a class convert to `T`. It has no `->`, because the type is in its name. Add `explicit` after `const` to allow the conversion only through `static_cast`.

```walnut
class Meters {
public:
    int: v;
    constructor(int: x) : v = x { }
    operator(int) function() const explicit {
        return v;
    }
    operator(string) function() const {
        return "M";
    }
} end class Meters

function main() -> int {
    Meters: m = Meters(8);
    string: label = m;
    int: raw = static_cast<int>(m);
    return 0;
} end function main
```

Here `label` is `"M"` and `raw` is `8`.

**Conversion templates.** A conversion operator can be a template, which lets one operator cover many target types. The template parameter is worked out from the type being converted to:

```walnut
class Num {
public:
    long int: v = 42;
    template <typename T>
    requires std::traits::is_arithmetic<T>
    operator(T) function() const {
        return static_cast<T>(v);
    }
} end class Num

function main() -> int {
    Num: n;
    int: i = static_cast<int>(n);
    double: d = n;
    short int: s = static_cast<short int>(n);
    return 0;
} end function main
```

Here `i`, `d` and `s` are all `42`.

- The pattern can be more than a bare `T`, as in `operator(T*)`, which only matches pointer types.
- A conversion operator that isn't a template wins over a template that converts to the same type.
- `requires` limits which types the template accepts, as on any template.

**Moving conversions.** `&&` after the empty parameter list, as in `operator(Box<U>) function() && { ... }`, marks a conversion that takes the object over, the way [`UniquePtr`](Documentation/Library/memory.md#uniqueptr) moves its pointer into a `UniquePtr` of a base class. It is only used on a value that is being moved or thrown away anyway: a temporary such as a function's result, or `std::core::move(x)`. Converting a named variable without `std::core::move` is an error that says to add it.

### Generated comparisons

You rarely need to write every comparison operator:
- `!=` comes from `==`.
- `<`, `>`, `<=` and `>=` come from each other, so writing only `<` gives all four.
- `==` is tried in both operand orders.

If a class has no `==`, Walnut generates one that compares the base classes and every non-static field in order. That needs every field to be comparable itself. If one isn't, the error says which field and why:

```
'Outer' has no '==' operator, and one cannot be generated: member 'h' of type 'HasU' cannot be compared with '==', because member 'u' of type 'U' cannot be compared with '==', because unions cannot be compared member by member
```

`<` and the other ordering operators are never generated automatically.

### Trait overloads

A class can tell the [type traits](#type-operations-and-traits) how to treat it, so that your own number types behave like the built-in ones in templates.

**Traits.** A trait overload's body must be a single `return` of a constant. It changes the built-in answer the trait starts from; the traits built on top (such as `is_number` from `is_arithmetic`) follow it. To change a trait from outside the class, or for a type you didn't write, specialize it instead; see [Extending a trait](Documentation/Library/core.md#extending-a-trait). It returns `bool`, or `int` for the rank queries.

**Type operations.** A type operation overload has no body; the type after `->` is the answer. Only `common_type` takes a parameter: the other type.

```walnut
class Fixed {
public:
    int: raw;
    operator(is_arithmetic) function() -> bool { return true; }
    operator(is_integer) function() -> bool { return false; }
    operator(float_rank) function() -> int { return -1; }
    operator(promote) function() -> double;
    operator(common_type) function(int) -> Fixed;
} end class Fixed

function main() -> int {
    static_assert(std::traits::is_arithmetic<Fixed>, "Fixed counts as arithmetic");
    static_assert(!std::traits::is_integer<Fixed>, "but not as an integer");
    static_assert(std::traits::float_rank<Fixed> == -1, "ranked like short float");
    static_assert(std::traits::is_same<std::traits::promote<Fixed>, double>, "promotes to double");
    return 0;
} end function main
```

| | Names |
|---|---|
| Can be overloaded | `is_integer`, `is_float` (or `is_floating`), `is_arithmetic`, `is_signed`, `is_unsigned`, `is_string`, `is_text`, `is_str`, `integer_rank`, `float_rank`, `integer_bits`, `float_bits`, `float_mantissa_bits`, `float_exponent_bits`, `make_signed`, `make_unsigned`, `element_type`, `underlying_type`, `return_type`, `common_type`, `promote`, `demote`, `int_from_float`, `float_from_int` |
| Can be overloaded, with a warning | `is_pointer`, `is_array`, `is_bool`, `is_char`, `is_class`, `is_function` |
| Can't be overloaded | All other traits and type operations, and `sizeof`. Their answer depends only on how the type is written |

## Compile-time code

**`constexpr` functions** can run while compiling, and `constexpr` variables must be computed then. **`consteval` functions** must always run at compile time.

**`static_assert(condition, "message")`** stops compilation when the condition is false. The message is required.

**`if constexpr`** keeps only the branch whose condition is true, so the other branches may contain code that wouldn't compile for this type.

**Random values while compiling.** `std::random`'s functions work here too: `constexpr int: roll = std::random::random_int(1, 6);` picks a number with a cryptographically secure generator while compiling and builds it into the program. See [random](Documentation/Library/random.md#while-compiling).

```walnut
constexpr function fib(int: n) -> int {
    if (n < 2) then {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
} end function fib

static_assert(fib(10) == 55, "fib(10) should be 55");

template <typename T>
function kind(T: value) -> string {
    if constexpr (std::traits::is_integer<T>) then {
        return "integer";
    } else if constexpr (std::traits::is_string<T>) then {
        return "string";
    } else then {
        return "other";
    }
} end function kind

function main() -> int {
    constexpr int: f = fib(12);
    string: a = kind(1);
    string: b = kind("s");
    string: c = kind(1.5);
    return 0;
} end function main
```

Here `f` is `144`, and `a`, `b` and `c` are `"integer"`, `"string"` and `"other"`.

**What can be computed.** Integers, floats, `bool`, `char`, `string` and `text` can all be constants. Strings can be joined with `+` and compared with `==`, `!=`, `<`, `>`, `<=` and `>=` while compiling:

```walnut
constexpr string: first = "wal";
constexpr string: whole = first + "nut";
static_assert(whole == "walnut", "joined at compile time");
static_assert("apple" < "pear", "compared at compile time");
```

## Type operations and traits

Type operations and traits are evaluated at compile time. Type operations produce a type and are [alias templates](#alias-templates) in `std::traits`, such as `std::traits::discard_ref<T>`. Traits produce a value and are [variable templates](#variable-templates) in `std::traits`, such as `std::traits::is_integer<T>`. `typeof`, `decltype`, `discard_const`, `sizeof`, `alignof`, `countof` and `typeid` are operators of the language, and `typeof(value)` turns a value into its type.

### Size, count and names

| Query | Result |
|---|---|
| `sizeof(T or value)` | Size in bytes. Types without a fixed size, such as `string` and `var`, are an error |
| `alignof(T)` | Alignment in bytes |
| `countof(x)` | The number of elements of an array, characters of a string, enumerators of an enum, or arguments in a variadic pack |
| `typeid(T or value)` | The type's name as a `string`, such as `"long int"` or `"Point"`. For a `var`, it is the type of the value it holds right now |

```walnut
struct Point {
    int: x;
} end struct Point

function main() -> int {
    const int: c = 1;
    Point: p;
    var d = 2.5;
    string: a = typeid(c);
    string: b = typeid(long int);
    string: e = typeid(p);
    string: f = typeid(d);
    bool: same = typeid(c) == typeid(int);
    return 0;
} end function main
```

Here `a` is `"int"`, `b` is `"long int"`, `e` is `"Point"`, `f` is `"float"` and `same` is `true`.

`typeof(x)` and `decltype(x)` name types, so they go wherever a type is written (`typeof(x): copy = x;`). Using them as a value is an error that points you to `typeid`.

### Type operations

Apart from `typeof`, `decltype` and `discard_const`, which are keywords, these are alias templates in `std::traits` that come with every program. `using std::traits::decay;` brings one in under its short name.

| Operation | Result |
|---|---|
| `typeof(expr)` | The type of a value, without the reference |
| `decltype(expr)` | The declared type of a name, or `T&` for other lvalues |
| `discard_const(T)`, `std::traits::discard_volatile<T>`, `std::traits::discard_cv<T>` | `T` without `const`/`volatile` |
| `std::traits::discard_ref<T>`, `std::traits::discard_pointer<T>`, `std::traits::decay<T>` | `T` without a reference or pointer; `decay` also turns arrays into pointers |
| `std::traits::add_const<T>`, `std::traits::add_volatile<T>`, `std::traits::add_ref<T>`, `std::traits::add_rvalue_ref<T>`, `std::traits::add_pointer<T>` | `T` with that added |
| `std::traits::make_signed<T>`, `std::traits::make_unsigned<T>` | The signed or unsigned version of a number type, floats included |
| `std::traits::promote<T>`, `std::traits::demote<T>` | The next wider or narrower type of the same kind |
| `std::traits::int_from_float<T>`, `std::traits::float_from_int<T>` | The integer or float type of the same width |
| `std::traits::element_type<T>` | The element type of an array, or `char` for strings |
| `std::traits::underlying_type<E>` | The integer type behind an enum |
| `std::traits::return_type<F>` | The return type of a function type |
| `std::traits::common_type<A, B, ...>` | The type all the arguments convert to |
| `std::traits::conditional<condition, A, B>` | `A` when the condition is true, otherwise `B` |

### Traits

Traits are [variable templates](#variable-templates) in the namespace `std::traits`, which comes with every program like the rest of [core](Documentation/Library/core.md#stdtraits). `std::traits::is_integer<T>` is a `constexpr bool`, so it works in `static_assert`, `if constexpr`, `requires` clauses and concepts, and anywhere else a constant does.

| Trait | Result |
|---|---|
| `is_same<A, B>`, `is_convertible<From, To>`, `is_base_of<Base, Derived>` | `bool` |
| `is_const<T>`, `is_volatile<T>`, `is_ref<T>`, `is_lvalue_ref<T>`, `is_rvalue_ref<T>`, `is_pointer<T>`, `is_array<T>`, `is_function<T>`, `is_void<T>` | `bool` |
| `is_integer<T>`, `is_float<T>`, `is_number<T>`, `is_signed<T>`, `is_unsigned<T>`, `is_bool<T>`, `is_char<T>`, `is_string<T>`, `is_enum<T>`, `is_class<T>`, `is_dynamic<T>` and [about 60 more](Documentation/Library/core.md#stdtraits) | `bool` |
| `is_constructible<T, Args...>`, `is_nothrow_constructible<T, Args...>`, `is_constexpr_constructible<T, Args...>`, `is_assignable<T, U>`, `is_nothrow_assignable<T, U>`, `is_constexpr_assignable<T, U>`, `is_destructible<T>`, `is_nothrow_destructible<T>`, and `nothrow`/`constexpr` versions of the default, copy and move ones | `bool`: whether `T(args...)` or `t = u` works, can't throw, or can run while compiling; see [What a type can do](Documentation/Library/core.md#stdtraits) |
| `integer_rank<T>` | An `int`: the width step of an integer type. `int` is 0, `long int` 1, `short int` -1, and so on up to 6 for `long long integer` |
| `float_rank<T>` | The same for floats: `float` is 0, `short float` -1, `double` 2 |
| `integer_bits<T>`, `float_bits<T>`, `float_mantissa_bits<T>`, `float_exponent_bits<T>`, `array_extent<T>` | `int` sizes |

`is_integer` is only for integer types: `bool` and `char` are their own kinds, with `is_bool` and `is_char`. `using std::traits::is_integer;` brings one in under its short name.

Every `bool` trait reads a struct in `std::traits::extend`, which you can specialize with `std::true_type` or `std::false_type` to give your own types an answer; see [Extending a trait](Documentation/Library/core.md#extending-a-trait). `std::traits::integer_traits<T>` and `std::traits::float_traits<T>` give the limits and sizes of the built-in number types, like C++'s `std::numeric_limits`; see [integer_traits and float_traits](Documentation/Library/core.md#integer_traits-and-float_traits).

```walnut
function main() -> int {
    int: x = 3;
    const int: cx = 4;
    int&: rx = x;
    decltype(rx): alias = x;
    typeof(rx): copy = x;
    std::traits::make_unsigned<int>: u = 7;
    std::traits::conditional<sizeof(int) == 4, int, string>: pick = 9;
    discard_const(typeof(cx)): changeable = 1;
    changeable = 2;
    static_assert(std::traits::is_ref<decltype(rx)>, "decltype keeps the reference");
    static_assert(!std::traits::is_ref<typeof(rx)>, "typeof drops it");
    static_assert(std::traits::is_const<typeof(cx)>, "typeof keeps const");
    static_assert(std::traits::is_same<typeof(x + 2.5), float>, "int + float is float");
    static_assert(std::traits::is_unsigned<typeof(u)>, "make_unsigned made it unsigned");
    static_assert(std::traits::integer_rank<long int> == 1 && std::traits::float_rank<double> == 2, "ranks");
    static_assert(std::traits::is_nothrow_constructible<int, float> && std::traits::is_constexpr_copy_constructible<string>, "numbers and strings");
    static_assert(std::traits::is_assignable<int, short int> && !std::traits::is_assignable<const int, int>, "a const can't be assigned");
    return 0;
} end function main
```

Classes can overload some of these; see [Trait overloads](#trait-overloads).

## Namespaces

```walnut
namespace math {
    function square(int: x) -> int {
        return x * x;
    } end function square

    namespace deep {
        function cube(int: x) -> int {
            return x * x * x;
        } end function cube
    } end namespace deep
} end namespace math

namespace geo::flat {
    function area(int: w, int: h) -> int {
        return w * h;
    } end function area
} end namespace geo::flat

namespace md = math::deep;

function main() -> int {
    int: a = math::square(3);
    int: b = md::cube(2);
    int: c = geo::flat::area(2, 5);
    using namespace math;
    int: d = square(4);
    return 0;
} end function main
```

Here `a` is `9`, `b` is `8`, `c` is `10` and `d` is `16`.

- `namespace a::b { }` declares nested namespaces in one step.
- `namespace short = long::name;` makes an alias.
- `using namespace n;` makes the names in `n` usable without the prefix, until the end of the current block.
- `using a::b;` makes just the one name `b` usable without its prefix, in the current block or namespace. It works for functions (with all their overloads), types, variables, constants and nested namespaces, as in `using geo::Point;` or `using std::io::println;`.

## Files, imports and modules

A program can be several `.wal` files. List them all on the command line, starting with the one that has `main`, or have files import each other.

`#include`, `import` and the command line only take Walnut files: `.wal` and `.walh`. Naming any other kind of file there, such as a Rust or Zig file, is an error. C and C++ files are the one exception, and they go through [extern blocks](#using-c-and-c-files) instead.

### Header files

A `.walh` file is a Walnut header. It holds declarations and their definitions together, like a C++ header that also contains the code, and it is never compiled or linked on its own:
- `#include "shapes.walh"` pastes it into the file, like any `#include`.
- A header is pasted at most once into each file, even if it is included several times, directly or through other headers. No include guards or `#pragma once` are needed.
- Several files of one program can each include the same header. Each gets its own copy, and nothing has to be linked.
- A header can also be imported, like a `.wal` file (`import "shapes.walh" as shapes`).
- Module files found by name are headers (see [Modules by name](#modules-by-name)).
- A header can't be passed to `walnut` on the command line; include or import it from a `.wal` file.

### Exporting

A file chooses what other files can import:

```walnut
function hello(int: n) -> int {
    return n + 1;
} end function hello

namespace shapes {
    function area(int: w, int: h) -> int {
        return w * h;
    } end function area
} end namespace shapes

export hello
export namespace shapes
```

- `export name` exports a single name.
- `export name as other` exports it under a different name.
- `export namespace n` exports a namespace.
- `export this` exports every global declaration in the file.
- `export { ... }` lists several exports at once.

### Importing

| Form | Gives you |
|---|---|
| `import hello from "lib.wal"` | `hello` |
| `import hello from "lib.wal" as greet` | `hello`, named `greet` |
| `import namespace shapes from "lib.wal"` | `shapes::...` |
| `import "lib.wal" as lib` | Everything `lib.wal` exports, as `lib::...` |
| `import module "greet" from "lib.wal"` | A named module block from that file, as `greet::...` |
| `import module.name` | A module found by name, as `name::...`. A module from Walnut's library is `std::name::...` instead |
| `import module.a.b as x` | The module `a/b`, as `x::...` |
| `import module.io as x` | The library module `io`, as `std::io::...` and `x::...` |

`import { ... }` lists several imports at once.

Paths are relative to the importing file. Importing a name that the other file doesn't export is an error.

### Module blocks

A file can group declarations into a named module, which other files import by name:

```walnut
function hello(int: n) -> int {
    return n + 1;
} end function hello

module greet {
    hello
    function triple(int: n) -> int {
        return n * 3;
    } end function triple
}
```

A module block lists names declared elsewhere in the file, and can also define new functions inside it. A module block isn't exported to plain `import "file" as x` imports; import it with `import module "greet" from "file.wal"`.

### Modules by name

`import module.name` looks for a module file called `name.walh` (or `name.wal`).

**Library modules.** When a module comes from Walnut's own library, it exists in `std::name`: `import module.io` gives `std::io::cout`, and the always-present `core` module gives `std::core::move`. `import module.io as io` makes `io` another name for `std::io`. The library's modules are listed in [Documentation/Library](Documentation/Library/README.md).

 In `import module.a.b`, the dots become folders, so it looks for `a/b.walh`. The module is reached through its last name part (`b::`), or through the name given with `as`. A module whose name is a keyword, such as `module.string`, must be imported with `as`.

Walnut searches these folders in order:
1. every `-M <dir>` given on the command line
2. the folders listed in the `WALNUT_MODULE_PATH` environment variable (separated by `:`, or `;` on Windows)
3. the `runtime` folder that comes with Walnut
4. Walnut's own library, which is built into the `walnut` program

A module file usually `#include`s its implementation files and lists its exports:

```
#include "greet_impl/hello.walh"
#include "greet_impl/shout.walh"
export hello
export shout
```

### Linking between files

`extern` declares a function or variable that is defined in another `.wal` file of the same program. That file can be listed on the command line or imported. Its signature must match the definition exactly.

```
extern function helper(int: x) -> int;
extern int: shared_counter;
```

Several declarations can share one `extern` by putting them in an extern block, which closes with `end extern`:

```
extern {
    function helper(int: x) -> int;
    int: shared_counter;
} end extern
```

`extern` also reaches `local` declarations in other files, but not `hidden` ones.

## Calling C and C++

Walnut programs reach the rest of the system (the screen, files, the network) through C and C++ code.

```walnut
extern "C" function puts(string: line) -> int;
extern "C" function abs(int: x) -> int;

function main() -> int {
    puts("from C");
    return abs(-3) - 3;
} end function main
```

**Declaring native functions**
- Declare a C function with `extern "C"`, or a C++ function with `extern "C++"`.
- Several functions can share one `extern "C"` or `extern "C++"` in an extern block. It can only hold function declarations (and, for `extern "C++"`, [C++ classes](#using-c-classes)), can only appear at file or namespace level, and closes with `end extern`:

```walnut
extern "C" {
    function abs(int: x) -> int;
    function labs(long int: x) -> long int;
} end extern

function main() -> int {
    return abs(-3) - 3;
} end function main
```

- An `extern "C++"` function declared inside `namespace mylib` refers to the C++ function `mylib::name`.
- Declare it `noexcept` when it can't throw. Otherwise Walnut assumes it can, and it can't be called from a `noexcept` function or a destructor.

**Types that can cross**
- integers up to 64 bits (`long int` is C's `long`)
- `short float` and `short short float`, converted to and from C's `double` and `float`
- `bool`, `char`, `string` (C's `const char*`, or C++'s `std::string`) and `const string&`
- `void`, as a return type
- for `extern "C++"` only: integers wider than 64 bits and floats other than `short float` and `short short float` (64 and 32 bits). They arrive in C++ as Walnut's runtime types, such as `walnut_rt::i2048` for `long long integer` or `walnut_rt::f2048` for `long long double`, declared in `walnut_runtime.hpp`

### Using C and C++ files

`from` after the language names the files that hold the code, so nothing has to be passed on the command line:

```
extern "C" from "vec.h", "vec.c" {
    function vec_dot(long int: ax, long int: ay, long int: bx, long int: by) -> long int;
} end extern

namespace words {
    extern "C++" from "shout.hpp" {
        function shout(const string&: s) -> string;
    } end extern
} end namespace words
```

- Paths are relative to the folder of the file that names them. A file that doesn't exist is an error.
- **Source files** (`.c`, `.cpp`, `.cc`, `.cxx`) are compiled and linked into the program. `.c` files are compiled as C.
- **Object files and libraries** (`.o`, `.obj`, `.a`, `.so`, `.dylib`, `.lib`, `.dll`) are linked in.
- **Headers** (`.h`, `.hpp`, `.hh`, `.hxx`) are included in the generated C++, so header-only code works: inline functions, and templates called with the types you declare. The functions are then called with the header's own declarations, so the Walnut declaration only has to be callable the same way; it doesn't have to repeat the C++ signature exactly. A `.h` named in an `extern "C"` block is included as C.
- A header only declares functions, so if the code lives in a separate source file, name both, as with `vec.h` and `vec.c` above.
- Each file is compiled or included once, however many declarations name it.
- A header in angle brackets, such as `from "<vector>"`, is a system or library header: it is included as `#include <vector>` and found on the C++ compiler's include path instead of next to the Walnut file.
- `from` also works on a single declaration: `extern "C" from "vec.c" function vec_dot(...) -> long int;`.
- With a block, each function you use needs a Walnut declaration. To have Walnut read them from the header instead, see the next section.

### Reading C and C++ headers

Ending an `extern ... from` with `;` instead of a block makes Walnut read the headers and declare every function in them that Walnut can call:

```
extern "C" from "vec.h", "vec.c";
extern "C++" from "shapes.hpp", "shapes.cpp";

function main() -> int {
    return static_cast<int>(vec_dot(1, 2, 3, 4)) + geo::width_of(5);
} end function main
```

- Only functions declared in the named headers themselves are taken, not ones from headers they include. Source files in the list are compiled and linked as usual.
- C++ namespaces become Walnut namespaces: `geo::width_of` above is the C++ function `geo::width_of`. Read C++ headers at file level, not inside a Walnut namespace.
- C functions are declared `noexcept`. C++ functions are `noexcept` when the header says so.
- Parameter and return types are matched to Walnut types by their size: `int` is `int`, `long` is `long int` on Linux and `int` on Windows, `unsigned char` is `unsigned short short int`, `float` is `short short float` and `double` is `short float`. A C `const char*` is a `string`, and in C++ `std::string` and `const std::string&` are `string` and `const string&`.
- Functions Walnut can't use are left out: ones that take `...`, templates, operators, and ones whose types can't cross (pointers, structs, references to other types). Using one gives an error that says why, such as "'ml_sum' is declared in mathlib.h, but Walnut can't use it: it takes a variable number of arguments ('...')".
- A template can still be used by declaring the versions you need in an `extern "C++" from "file.hpp" { ... } end extern` block.
- Headers are read with libclang, which Walnut looks for on the system (for example after `sudo apt install libclang-dev`). The `WALNUT_LIBCLANG` environment variable can point at it directly.
- Walnut reads the headers with the same include folders and settings as the C++ compiler it uses, plus the `-I` folders.

### Using C++ classes

An `extern "C++"` block can also declare a C++ class, so Walnut code can create its objects and call their methods. The class's fields stay in C++; the Walnut declaration lists the constructors, methods and operators Walnut may use. Given this header:

```cpp
#pragma once
#include <string>

namespace geo {
    class Counter {
    public:
        Counter() = default;
        explicit Counter(long start) : m_count(start) {}
        void add(long amount) { m_count += amount; }
        long count() const { return m_count; }
        static Counter starting_at(long start) { return Counter(start); }
        bool operator<(const Counter& other) const { return m_count < other.m_count; }
        std::string label() const { return "count " + std::to_string(m_count); }
    private:
        long m_count = 0;
    };

    inline long doubled(const Counter& c) { return c.count() * 2; }
}
```

the class is used from Walnut like this:

```walnut
namespace geo {
    extern "C++" from "geo/counter.hpp" {
        class Counter {
        public:
            constructor();
            constructor(long int: start);
            function add(long int: amount) -> void;
            function count() const -> long int;
            static function starting_at(long int: start) -> Counter;
            operator(<) function(const Counter&: other) const -> bool;
            function label() const -> string;
            function doubled() const -> long int as "geo::doubled";
            function add_twice(long int: amount) -> void {
                add(amount);
                add(amount);
            } end function add_twice
        } end class Counter
    } end extern
} end namespace geo

function main() -> int {
    geo::Counter: a;
    a.add(5);
    geo::Counter: b = geo::Counter::starting_at(10);
    b.add_twice(1);
    bool: smaller = a < b;
    string: shown = b.label();
    long int: twice = a.doubled();
    return 0;
} end function main
```

Here `a.count()` is `5`, `b.count()` is `12`, `smaller` is `true`, `shown` is `"count 12"` and `twice` is `10`.

**Declaring the class**
- The C++ name comes from the Walnut namespaces around the block, as for functions: `geo::Counter` above. `as` gives a different name: `class List as "std::vector"`.
- A method or constructor without a body ends with `;` and calls the C++ method of the same name. `as "name"` calls a C++ method with another name (`as "data"`, or `as "operator[]"`), and `as "ns::function"` (a name with `::`) calls a free C++ function with the object as its first argument, like `geo::doubled` above. An operator without `as` uses the same C++ operator.
- A constructor's arguments go to the C++ constructor. A constructor with `as "ns::make"` initializes the object with what that C++ function returns.
- In a class template, an `as` name can use the template's parameters, which are replaced by the class's arguments: in `List<T>`, `constructor(T...: items) as "ns::make_list<T>";` calls `ns::make_list<std::string>` for a `List<string>`.
- A method can also have a Walnut body, like `add_twice` above, to add methods the C++ class doesn't have or to check arguments first. A Walnut body can call the other methods, but can't reach the C++ fields.
- A class with no constructors can be created without arguments. A C++ class can't declare fields, a destructor (the C++ destructor runs on its own), virtual methods or base classes.
- Only the methods a program calls are generated, so a template can declare methods that only compile for some types, the same as in C++.

**Templates.** A class template becomes a C++ class template with the same arguments: `List<string>` below is `std::vector<std::string>`, and an `int` template parameter passes an integer, as in `SmallVector<T, 8>`.

```walnut
import module.io

extern "C++" from "<vector>" {
    template <typename T>
    class List as "std::vector" {
    public:
        constructor();
        constructor(long int: count, const T&: value);
        function size() const -> long int;
        function push_back(const T&: value) -> void;
        function data() -> T*;
        function begin() -> T* as "data";

        function end() -> T* {
            return data() + size();
        } end function end

        operator([]) function(long int: index) -> T& {
            if (index < 0 || index >= size()) then {
                throw "index out of range";
            }
            return data()[index];
        }
    } end class List
} end extern

function main() -> int {
    List<string>: names(2, "?");
    names.push_back("ann");
    names[0] = "bob";
    for (const string&: n in names) {
        std::io::print(n, " ");
    }
    std::io::println();
    return 0;
} end function main
```

This prints `bob ? ann`.

**Values crossing.** Integers, floats, `bool` and `char` are converted to and from the C++ types of the same size, as for functions. A parameter or result whose type is a template parameter (`const T&`, `T&`) is passed as Walnut's own type, so `List<int>` holds Walnut `int`s. A Walnut object of a C++ class passes as the C++ object. A method that returns a pointer can also return a C++ iterator into a container whose elements are stored in one block (`std::vector`, `std::string`), as `begin` does above.

**Using the objects**
- Objects of C++ classes copy and move with the C++ class's own copy and move.
- `for (x in c)` works when the class has `begin()` and `end()` that return pointers.
- `==` and other operators work only when they are declared in the block; Walnut doesn't generate them for C++ classes.
- `sizeof` gives the C++ size.
- C++ code can't call Walnut operators: a C++ algorithm that compares elements with `<` or `==` works on numbers and strings, but not on Walnut classes. Write that method in Walnut instead, as the [collections](Documentation/Library/collections.md) module does for `sort` and `find`.

### Writing C and C++ code in Walnut files

`extern "C" code { ... } end extern` and `extern "C++" code { ... } end extern` hold C or C++ code written directly in a `.wal` or `.walh` file. Walnut doesn't read the code itself: it compiles each block as its own C or C++ file and links it into the program. To call the functions from Walnut, declare them in an extern block, as for any C or C++ function:

```
extern "C++" code {
    #include <string>

    std::string reverse_text(const std::string& s) {
        return std::string(s.rbegin(), s.rend());
    }
} end extern

extern "C++" {
    function reverse_text(const string&: s) -> string;
} end extern
```

- A block can only be at file or namespace level. The code inside is compiled as written, so a namespace around the block doesn't put the C++ code in that namespace.
- Errors in the code are reported at the right line of the `.wal` file.
- `#include "..."` inside the block searches the folder of the `.wal` file that contains it.
- Each block is compiled once, even when a header with a block is included in several files.
- Because each block is compiled as its own file, Walnut can only call functions the block really defines: not `inline` functions, and not templates.
- The block ends at the `}` that matches its `{`. Braces inside strings, character literals and comments don't count.

**Providing the code on the command line.** Instead of `from`, the C or C++ files can be passed on the command line, or a library linked with `-L <dir> -l <name>`:

```
walnut run main.wal native.c
walnut run main.wal -L ./lib -l mylib
```

A Walnut module can wrap native functions behind a Walnut interface, so other files just `import` it.

## The preprocessor

The preprocessor runs before everything else, as in C.

| Directive | Does |
|---|---|
| `#include "file"` | Inserts a `.wal` or `.walh` file. Searches the current file's folder, then `-I` folders, then the working folder |
| `#include <file>` | Searches `-I` folders, then the working folder |
| `#pragma once` | Includes this file only once |
| `#define NAME value` | A macro |
| `#define F(a, b) body` | A macro with parameters; `...` with `__VA_ARGS__` takes extra arguments |
| `#undef NAME` | Removes a macro |
| `#if`, `#elif`, `#else`, `#endif` | Conditional code |
| `#ifdef NAME`, `#ifndef NAME` | Code kept only if `NAME` is or isn't defined |
| `#error "msg"`, `#warning "msg"` | Reports an error or a warning |

**`#if` conditions**
- `defined(NAME)` tests whether a macro exists.
- Integer arithmetic and comparison operators work.
- `true` and `false` are allowed.
- A name that isn't defined counts as 0.

**Inside a macro body**
- `#x` turns a parameter into a string.
- `a ## b` glues two tokens together.

**Built-in macros**
- Position: `__LINE__`, `__FILE__`, `__FILE_PATH__`, `__FILE_RELATIVE_PATH__`.
- Operating system: `__OS__` (for example `"Linux"`), and `__IS_OS_LINUX__`, `__IS_OS_WINDOWS__`, `__IS_OS_MACOS__` and so on, which are `true` or `false`.
- Architecture: `__ARCH__` (for example `"x86_64"`) and `__IS_ARCH_X86_64__` and so on.
- SIMD support: `__IS_SIMD_...__`.

`-D NAME=value` on the command line defines a macro; a bare `-D NAME` defines it as 1.

```walnut
#define SQUARE(x) ((x) * (x))
#define STR(x) #x
#define JOIN(a, b) a ## b

function main() -> int {
    int: JOIN(my, value) = SQUARE(4);
    string: spelled = STR(a + b);
#ifdef DEBUG
    string: build = "debug";
#else
    string: build = "release";
#endif
    return 0;
} end function main
```

Here `myvalue` is `16`, `spelled` is `"a + b"`, and `build` is `"release"` (or `"debug"` with `-D DEBUG`).

## The command line

```
walnut <command> [options] <main.wal> [more.wal ...] [C/C++ files] [-- program arguments]
```

The files can be `.wal` files, C and C++ source files (`.c`, `.cpp`, `.cc`, `.cxx`), object files (`.o`, `.obj`) and libraries (`.a`, `.so`, `.dylib`, `.lib`, `.dll`). Anything else is an error, including `.walh` headers and C or C++ headers, which are included instead.

| Command | Does |
|---|---|
| `check` | Reports errors and warnings without building |
| `emit` | Prints the generated C++ (or writes it with `-o`) |
| `build` | Builds an executable |
| `run` | Builds the program and runs it |
| `clean` | Deletes the build cache |
| `highlight` | Prints what each name and operator in the file is, and its errors and warnings, as JSON for editors. With `--stdin`, the file's text comes from standard input. It also takes a `.walh` file. The format is in [the VS Code extension's README](Tools/vscode-walnut/README.md#the-highlight-command) |
| `complete` | With `--at LINE:COL` (from 1, the column in bytes), prints what can be typed at that spot as JSON, for editors' autocomplete. After `.` or `->` it lists the members of the value's type, after `::` what's inside the namespace, class or enum, and otherwise every name in scope there (locals declared further down are left out). It works on unfinished code, so the line being typed doesn't have to compile. The answer is `{"start": column, "mode": "member" \| "scope" \| "name" \| "none", "items": [{"label", "kind", "detail"}]}`, where `start` is the byte column (from 0) where the word being completed starts and `detail` is a type or signature. Also takes `--stdin` and `.walh` files |
| `library` | Lists the library files built into `walnut`. `walnut library io.walh` prints one, and `walnut library --extract <dir>` writes them all into a folder, so an editor can open them |
| `init` | Sets up a C++ compiler: on Windows it downloads a pinned compiler matching this `walnut` into `%LOCALAPPDATA%\walnut\toolchain` (checked with SHA-256), on Linux it installs `g++` with the package manager, on macOS it opens Apple's Command Line Tools installer. `--yes` doesn't ask, `--force` downloads on Windows even when another compiler is on `PATH`, `--status` shows the compiler in use, `--print-compiler` prints just its path, `--remove` deletes the download. The compiler is picked from `--cxx`, then `WALNUT_CXX`, then the `walnut init` download, then `c++` on `PATH` |
| `help`, `-h`, `--help` | Shows the usage |

| Option | Meaning |
|---|---|
| `-o <path>` | Output path; with `build` on Windows, `.exe` is added when the path has no extension |
| `-O0`, `-O1`, `-O2`, `-O3`, `-Os` | Optimization (default `-O2`) |
| `-I <dir>` | Folder for `#include` |
| `-D <name>[=value]` | Defines a macro |
| `-M <dir>`, `--module-dir <dir>` | Folder searched by `import module.<name>` |
| `-L <dir>`, `-l <lib>` | Library folder and library for native code |
| `--cxx <compiler>` | The C++ compiler to use |
| `--cxx-flag <flag>` | Passes a flag to the C++ compiler |
| `--runtime <dir>` | Where the Walnut runtime is |
| `--keep-cpp` | Keeps the generated C++ next to the program (builds without the cache) |
| `-u`, `--unbuffered` | Makes the program flush `cout` and `clog` after every write, like `python -u` (setting `WALNUT_UNBUFFERED=1` when it runs does the same) |
| `--no-cache` | Builds from scratch in a temporary folder, without the build cache |
| `--random-seed <text>` | Makes `std::random` calls in `constexpr` code give the same values every build (also `WALNUT_RANDOM_SEED`); without it they're new each build, from the operating system's secure random source. See [random](Documentation/Library/random.md#while-compiling) |
| `-g` | Builds for a debugger, which then shows `.wal` lines (defaults to `-O0`) |
| `--debug-cpp` | Builds for a debugger that shows the generated C++ |
| `--line-map[=file]` | Writes which `.wal` line each block of C++ came from |
| `--no-warnings` | Hides warnings |
| `--warn-unused` | Warns about local variables that are defined but never used in their scope |
| `--errors <dest>`, `--warnings <dest>` | Sends errors or warnings to `stderr`, `stdout` or a file |
| `--tokens <file.txt>`, `--ast <file.txt>` | Writes the token list or syntax tree |
| `--time` | Shows how long each stage takes |
| `-v`, `--verbose` | Prints the C++ compiler command |
| `--check-bounds`, `--check-null`, `--check-overflow`, `--check-dangling`, `--checks` | Runtime safety checks; see [Safety checks](#safety-checks) |
| `--leak-check[=warn\|error]` | Lists memory from `new` that was never deleted when the program ends |
| `--gc` | Turns on the garbage collector |
| `--ownership` | Compile-time checks for moved, deleted and dangling values |
| `--safe` | `--checks`, `--leak-check` and `--ownership` together |

`-I`, `-D`, `-L` and `-l` also accept their value attached, as in `-Iinclude`.

Arguments after `--` go to the program.

### Unused variables

`--warn-unused` (off unless asked for) warns about every variable or array declared inside a function, constructor, destructor, operator or lambda that nothing in its scope ever reads or writes again:

```walnut
function main() -> int {
    int: total = 0;
    int: spare = 1;
    int: _later = 2;
    return total;
} end function main
```

`walnut check --warn-unused` reports `variable 'spare' is defined but never used`. Names starting with `_` are left alone, so `_later` is skipped, and so are function parameters, record fields, global variables and the files of the built-in library. A structured binding is reported only when none of its names are used. The flag works with `check`, `build`, `run` and `emit`, and editors that run `walnut check` can pass it too.

### Safety checks

Walnut programs are as fast as C++ by default, which also means a wrong index, a null pointer or a forgotten `delete` behaves the way it would in C++. Each safety feature below is off unless you ask for it, because each one makes the program slower or bigger. Turn them on while writing and testing, and leave them off for the final build, or keep the ones you want.

| Flag | What it does | Cost |
|---|---|---|
| `--check-bounds` | Stops the program when an index reaches outside memory made with `new T[n]`, or into memory that was already deleted | Every `new` is recorded, and pointer indexing looks it up |
| `--check-null` | Stops the program when `*p`, `p->` or `p[i]` uses a null or deleted pointer, and when something is deleted twice or wasn't made with `new` | Every `new` is recorded, and each dereference checks the pointer |
| `--check-overflow` | Stops the program when `+`, `-`, `*` or negation of fixed-width integers doesn't fit | A check after each of those operations |
| `--check-dangling` | Stops the program when `*p`, `p->` or `p[i]` uses a pointer to a local variable of a function that has already returned | Each dereference compares the pointer with the stack |
| `--checks` | All four above | |
| `--leak-check` | When the program ends, lists the memory from `new` that was never deleted, grouped by the line that made it. `--leak-check=error` also makes the program exit with code 3. Setting `WALNUT_LEAKS` to `off`, `warn` or `error` when the program runs overrides it | Every `new` is recorded |
| `--gc` | A garbage collector frees objects made with `new` once nothing points to them anymore. `delete` still works and frees them right away | Collection pauses, and every allocation is a little larger |
| `--ownership` | Compile-time checks; see below | Only compile time |
| `--safe` | `--checks`, `--leak-check` and `--ownership` | |

A runtime check stops the program with the `.wal` file and line, the way an uncaught exception does, and exit code 70. A crash such as a segmentation fault also prints the last `.wal` line that ran when any check is on:

```
walnut runtime error: shop.wal:7: index 5 is outside the 3 ints made with new (made at shop.wal:4)
```

```
walnut leak warning: 3 objects (72 bytes) made with new were never deleted
  shop.wal:10: new Node (3 times, 72 bytes)
```

**The garbage collector.** With `--gc`, objects made with `new` are collected when the program can no longer reach them. It looks for pointers in local variables, globals and other memory from `new`, and treats anything that looks like a pointer as one, so an object is sometimes kept a little longer than needed but never freed while in use. Destructors of collected objects run when they're collected, in no particular order, so a destructor shouldn't use other objects that may already be gone. Collection only happens while a single thread is running; while threads from `std::threading` run, memory grows until they finish. Pointers hidden from it, such as a pointer stored only inside C or C++ code or written into a file, don't keep an object alive.

**Ownership checks.** `--ownership` looks at each function while compiling and reports:

- Using a variable after `std::core::move` gave its value away. Assigning it a new value makes it usable again.
- Using a pointer through `*`, `->` or `[]` after it was deleted, and deleting it a second time. This follows copies: after `int*: alias = p;` or `int&: r = *p;` (or `p->field`, `p[i]`), `delete p;` makes `*alias`, `r` and `delete alias;` errors too.
- Returning a reference to a local variable or by-value parameter, or a pointer to one (`return &value;`, or a pointer that was set to `&value`), including a field or element of one (`return local.field;` from a function returning a reference, `return &numbers[1];` for a local array).
- Using a pointer to a variable, or to one of its fields or elements, after the block that variable was declared in has ended.
- `delete` of something that wasn't made with `new`, such as `delete &value;` or a local array.
- As warnings: memory from `new` that a function never deletes, returns, stores or passes to another function, and a pointer that is given a new value while still holding memory from `new`.

```walnut
import module.io as io

function main() -> int {
    string: a = "hello";
    string: b = std::core::move(a);
    io::println(a);

    int*: q = nullptr;
    {
        int: inner = 5;
        q = &inner;
    }
    io::println(*q);
    return 0;
} end function main
```

`walnut check --ownership` reports:

```
[Semantic] moved.wal:6: 'a' is used after it was moved on line 5
[Semantic] moved.wal:13: 'q' points to 'inner' (declared on line 10), which no longer exists after its block ended
```

Pointers kept in a struct or class, including one pointing to the same type (`Node*: next;` inside `Node`), are normal and fine. What's reported is a pointer to a local variable stored somewhere that outlives that variable:

- `holder.field = &local;` or `list.push_back(&local);` (also `insert`, `emplace_back`, `push`, `add`, `append`, `set` and similar), where `holder` or `list` was declared in an outer block: using `*holder.field`, `holder.field->...`, `*list[i]`, or a pointer from `for (T*: p in list)` after the local's block has ended is an error. Giving the field a new value, or calling `list.clear()`, before that is fine.
- Returning a struct whose field points to a local of the function is an error.
- Storing the address of a local in a field of `this`, a global, or an object reached through a reference, pointer or parameter is a warning, since that object is still there after the function returns.

What the compile-time checks can't see (a pointer passed through several functions, kept in a container that's copied, or a pointer into a `Vector` that has since grown) is left to the runtime checks: `--check-null` catches memory from `new` used after it was deleted, through any pointer to it, and `--check-dangling` catches a pointer to a local variable used after its function returned. `--check-dangling` compares the pointer with the part of the stack still in use, so it catches the pointer when it's used in the function that called the one that returned, or anywhere above that, but not after a deeper call has reused that part of the stack.

The checks follow `if`, loops, `switch`, `break`, `continue`, `return` and `try`. When a variable is moved or deleted on only some of the paths, it isn't reported. A pointer passed to a function or stored somewhere else counts as handed off, so it's never reported as leaked. Functions that use `goto` stop being checked at the `goto`. The library's own files aren't checked.

**Reading errors.** Each error names the file and line, says what is wrong and, where it can, how to fix it, then shows the line itself. A `^` marks the exact spot when Walnut knows it, so a forgotten `;` points at the end of the line that is missing it rather than at the next line:

```
[Parser] shop.wal:4: Syntax Error on line 4: Expected ';' at the end of this line, after the number '5'
Context: Variable declaration must end with a semicolon
  4 |     int: count = 5
    |                   ^
```

A call that matches no overload lists the candidates and why each one doesn't fit (too few arguments, an argument of the wrong type, a temporary passed to a `T&`), an unknown name or member suggests a close match (`did you mean 'count'?`), and a keyword used as a name, such as `arr` or `txt`, says which keyword it is another spelling of.

**The build cache.** `build` and `run` keep what they compile in a cache, so a build only redoes the work for what changed:
- The C++ for each Walnut file is compiled separately, several at once, and each compiled file is kept. A file whose generated C++ is unchanged isn't compiled again. Changing a function's body only recompiles its own file; changing what other files see (a signature, a class, a global) recompiles every file.
- The runtime header is precompiled once for each compiler and set of options.
- C and C++ files named with `from` or on the command line, and `code` blocks, are kept too, and are recompiled when they or the headers they include change.
- The finished program is kept, so building or running an unchanged program only takes the time to check it.
- Nothing has to be set up: there are no build files, and headers never need compiling by hand.
- The cache is in `~/.cache/walnut` (`%LOCALAPPDATA%\walnut\cache` on Windows), or in the folder named by `WALNUT_CACHE_DIR`. Entries that haven't been used for 30 days are deleted, and `walnut clean` deletes everything.
- `--keep-cpp`, `--debug-cpp`, `--line-map` and `--no-cache` build the old way, as a single C++ file in a temporary folder.

**Environment variables**
- `WALNUT_CXX`: the C++ compiler
- `WALNUT_RUNTIME_DIR`: the runtime folder
- `WALNUT_MODULE_PATH`: extra module folders
- `WALNUT_CACHE_DIR`: where the build cache is kept
- `WALNUT_LIBCLANG`: the libclang library used to read C and C++ headers

## Left out on purpose

These C++ features aren't in Walnut, and aren't planned, because Walnut does the same job another way.

- **`enum class`.** Enums are always scoped, so plain `enum` already behaves like C++'s `enum class`.
- **`sizeof...(pack)`.** Use `countof(pack)`, which also counts arrays, strings and enumerators.
- **`= 0` for abstract methods.** Write `abstract` after the parameters instead, as in `function area() const abstract -> int;`.
- **C-style octal literals.** `017` is seventeen. Write `0o17` for octal.
