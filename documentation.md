# The Walnut Language

This is the reference for everything Walnut can do. For installing Walnut and running the compiler, see [README.md](README.md).

Walnut is statically typed and compiles to C++. Its numbers are [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) multiprecision numbers, so every integer and float has the same exact width on every platform.

This file describes the language itself. Walnut's library is documented separately, one page per module, in [Documentation/Library](Documentation/Library/README.md); so far it has one module, `core`, which every program gets without an `import`. Output, files and other system access come from C or C++ code you call directly; see [Calling C and C++](#calling-c-and-c).

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
29. [Not supported yet](#not-supported-yet)

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

**Blocks** are written with `{ }` and can't be empty. A block needs at least one statement; the only exception is a `catch` body.

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

**Reserved words.** Keywords can't be used as names. The ones most likely to clash with a name you'd want are `in`, `is`, `as`, `local`, `text`, `arr`, `inf`, `from` and `hidden`.

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

**Infinity.** `infinity` (or `inf`) is a float constant, and `-inf` is negative infinity. `inf - inf` is `nan`.

### Number literals

| Form | Example |
|---|---|
| Decimal | `42`, `1_000_000`, `1'000'000` |
| Hexadecimal | `0xFF`, `0xFF_FF` |
| Binary | `0b1010`, `0b1010_1010` |
| Float | `2.5`, `5.`, `1e3`, `2.5E-2` |

**Digit separators.** `_` and `'` can sit between two digits anywhere in a number, and are ignored.

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

**Rules**
- Each declaration has one name.
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
| `hoist` | A local that can be used before its declaration line |
| `alignas(N)` | Places the variable at an address that is a multiple of `N` bytes; see [Alignment](#alignment) |
| `global`, `local`, `hidden` | Visibility; see [Visibility](#visibility) |

Walnut rejects combinations that contradict each other:
- `const` with `mutable`
- `static` with `extern`
- `constexpr` with `constinit`, `volatile`, `mutable`, `thread_local` or `extern`
- `inline` with `extern`, `mutable`, `volatile`, `thread_local` or `constinit`
- `mutable` with `static`, `thread_local` or `extern`
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
- Escapes: `\n \t \r \a \b \f \v \e \0 \\ \' \" \? \``, `\xHH`, `\uXXXX`, `\UXXXXXXXX` and `\u{...}`.
- Source files are UTF-8, so literals can contain any character directly.

**Operations**
- `+` joins strings, text and characters in any order, and `+=` appends.
- `== != < > <= >=` compare strings, and compare `char`s by code point.
- `countof(s)` gives the number of characters (code points), not bytes.
- `for (char: c in s)` walks through the characters.
- There's no built-in conversion between numbers and strings; that belongs in a library.

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

**Runtime errors.** These stop the program with a `walnut runtime error` message:
- integer division or `%` by zero
- a shift by a negative amount, or by at least the type's width
- an integer raised to a negative power

**Which types each operator takes**
- `%` and the bitwise operators need integers. Bitwise operators also take two `bool`s, or two values of the same enum.
- `char` only does arithmetic with another `char`.
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
| `static_cast<T>(v)` | Number to number (narrowing, signed ↔ unsigned, float ↔ int), enum ↔ integer, `char` to integer, base class to derived class, explicit conversion operators |

**Numbers never become `char` or `bool`.** No cast turns an integer (or any number) into a `char` or a `bool`, not even `static_cast` or `bit_cast`. A `char` only comes from a character literal or another `char`, and a `bool` from a comparison such as `x != 0`. Going the other way is allowed with a cast: `static_cast<int>(c)` gives a character's code point.
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

`static_cast<T&&>(x)` lets `x` be moved from, so a move constructor or move assignment is chosen instead of a copy. The core library wraps this as `std::move(x)`. See [Copying and moving](#copying-and-moving).

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
- `for (T: x in items)` loops over arrays, strings and generators.
- `for (var: c in v)` loops over a `var` holding a string, giving each character as a `var`. If the `var` holds anything else, the program stops with a runtime error.
- `for (T: x in {1, 2, 3})` loops over a brace list. With `auto`, the elements must share a common type.
- `for (auto: [a, b] in pairs)` unpacks each element as it goes.

**Loop bodies.** `while`, `do ... while` and `for` can take a single statement instead of a block.

**Jumps**
- `break;` leaves the loop and `continue;` goes to the next iteration.
- `repeat;` runs the current iteration again from the top of the body, without checking the condition or running the `for` increment.

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

### switch

`switch` works on integers, characters and enums. A case that runs into the next case gets a warning, unless you end it with `fallthrough;` to show the fall-through is intended. `break;` ends a case.

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

## Exceptions

Any value can be thrown: a number, a string or an object.
- `catch (T: name)` catches values of type `T`. `catch (const T&: e)` avoids a copy.
- `catch (...)` catches anything, and must come last.
- `throw;` on its own rethrows the exception currently being handled.
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
function add(A: a, B: b) -> common_type(A, B) {
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
- A parameter can be a type (`typename T`) or a constant (`int N`).
- A parameter can have a default (`typename T = int`).
- At a call, the template arguments are deduced from the function's arguments, or given explicitly as in `biggest<string>(...)`.

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

### Concepts

A concept is a named requirement on a type. Its body can be:
- a `requires (params) { expressions; }` block, which checks that the expressions compile
- a type trait such as `is_arithmetic(T)`
- another concept
- any combination of these with `&&`, `||` and `!`

Use a concept in place of `typename`, or in a `requires` clause.

```walnut
template <typename T>
concept Addable = requires (T: a, T: b) { a + b; }

template <typename T>
concept Number = is_arithmetic(T) && !is_same(T, bool)

template <Addable T>
function twice(T: x) -> T {
    return x + x;
} end function twice

template <typename T>
requires Number<T> && is_integral(T)
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
- Calling a constrained template with a type that doesn't qualify names what failed, such as `Addable<P> is not satisfied` or `is_integral(float) is not satisfied`.

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
- `this` is a pointer to the current object: use `this->x` and `*this`.

**Construction**
- `Point{7, 8}` sets the fields in order (aggregate initialization).
- `Counter: c;` uses the default constructor.
- `Box: b = Box(5);` calls a constructor.

**Forward declaration.** `class Name;` declares a class before its definition.

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
    Resource: c = std::move(a);
    return 0;
} end function main
```

Here `b` is a copy (`b.copies` is `1`), `c` took `a`'s value (`c.id` is `1`), and `a.id` is now `-1`.

`std::move(x)` lets `x` be moved from; afterwards `x` is valid but its value is up to the move constructor. It comes from the [core library](Documentation/Library/core.md), and does the same as `static_cast<T&&>(x)`. Returning a local variable from a function moves it automatically.

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
    function area() const virtual -> int;
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
- A `virtual` method without a body is abstract. A class with an abstract method can't be created, and neither can a subclass that doesn't implement it.
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

**Friends.** `friend function f(...)` and `friend class C;` give access to private members.

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

C++'s `std::ostream` and `std::istream` are classes, and `<<` and `>>` are overloaded on them. Walnut works the same way: write a stream class whose `operator(<<)` members handle the built-in types (by calling C or C++ to do the actual output), and each type you define adds a free `operator(<<)` that takes the stream as its first parameter. Each overload returns the stream, so calls chain.

```walnut
extern "C++" function stream_write(const string&: s) noexcept -> void;
extern "C++" function stream_write_int(long int: v) noexcept -> void;

class OutStream {
public:
    operator(<<) function(const string&: s) -> OutStream& {
        stream_write(s);
        return *this;
    }
    operator(<<) function(long int: v) -> OutStream& {
        stream_write_int(v);
        return *this;
    }
} end class OutStream

OutStream: out;

struct Point {
    int: x;
    int: y;
} end struct Point

operator(<<) function(OutStream&: s, const Point&: p) -> OutStream& {
    return s << "(" << p.x << ", " << p.y << ")";
}

function main() -> int {
    out << "point " << Point{3, 4} << "\n";
    return 0;
} end function main
```

With a C++ file that defines `stream_write` and `stream_write_int` (for example with `std::cout`), this prints `point (3, 4)`. An input stream works the same way with `operator(>>)`, taking the target by reference.

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

**Traits.** A trait overload's body must be a single `return` of a constant. It returns `bool`, or `int` for the rank queries.

**Type operations.** A type operation overload has no body; the type after `->` is the answer. Only `common_type` takes a parameter: the other type.

```walnut
class Fixed {
public:
    int: raw;
    operator(is_arithmetic) function() -> bool { return true; }
    operator(is_integral) function() -> bool { return false; }
    operator(float_rank) function() -> int { return -1; }
    operator(promote) function() -> double;
    operator(common_type) function(int) -> Fixed;
} end class Fixed

function main() -> int {
    static_assert(is_arithmetic(Fixed), "Fixed counts as arithmetic");
    static_assert(!is_integral(Fixed), "but not as an integer");
    static_assert(float_rank(Fixed) == -1, "ranked like short float");
    static_assert(is_same(promote(Fixed), double), "promotes to double");
    return 0;
} end function main
```

| | Names |
|---|---|
| Can be overloaded | `is_integral`, `is_floating`, `is_arithmetic`, `is_signed`, `is_unsigned`, `is_string`, `integer_rank`, `float_rank`, `make_signed`, `make_unsigned`, `element_type`, `underlying_type`, `return_type`, `common_type`, `promote`, `demote`, `int_from_float`, `float_from_int` |
| Can be overloaded, with a warning | `is_pointer`, `is_array`, `is_bool`, `is_char`, `is_class`, `is_function` |
| Can't be overloaded | All other traits and type operations, and `sizeof`. Their answer depends only on how the type is written |

## Compile-time code

**`constexpr` functions** can run while compiling, and `constexpr` variables must be computed then. **`consteval` functions** must always run at compile time.

**`static_assert(condition, "message")`** stops compilation when the condition is false. The message is required.

**`if constexpr`** keeps only the branch whose condition is true, so the other branches may contain code that wouldn't compile for this type.

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
    if constexpr (is_integral(T)) then {
        return "integer";
    } else if constexpr (is_string(T)) then {
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

## Type operations and traits

Type operations and traits are evaluated at compile time. Type operations produce a type and traits produce a value. Both take types as arguments, and `typeof(value)` turns a value into its type.

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

| Operation | Result |
|---|---|
| `typeof(expr)` | The type of a value, without the reference |
| `decltype(expr)` | The declared type of a name, or `T&` for other lvalues |
| `discard_const(T)`, `discard_volatile(T)`, `discard_cv(T)` | `T` without `const`/`volatile` |
| `discard_ref(T)`, `discard_pointer(T)`, `decay(T)` | `T` without a reference or pointer; `decay` also turns arrays into pointers |
| `add_const(T)`, `add_volatile(T)`, `add_ref(T)`, `add_rvalue_ref(T)`, `add_pointer(T)` | `T` with that added |
| `make_signed(T)`, `make_unsigned(T)` | The signed or unsigned version of a number type, floats included |
| `promote(T)`, `demote(T)` | The next wider or narrower type of the same kind |
| `int_from_float(T)`, `float_from_int(T)` | The integer or float type of the same width |
| `element_type(T)` | The element type of an array, or `char` for strings |
| `underlying_type(E)` | The integer type behind an enum |
| `return_type(F)` | The return type of a function type |
| `common_type(A, B, ...)` | The type all the arguments convert to |
| `conditional(condition, A, B)` | `A` when the condition is true, otherwise `B` |

### Traits

| Trait | Result |
|---|---|
| `is_same(A, B)`, `is_convertible(From, To)`, `is_base_of(Base, Derived)` | `bool` |
| `is_const`, `is_volatile`, `is_ref`, `is_lvalue_ref`, `is_rvalue_ref`, `is_pointer`, `is_array`, `is_function`, `is_void` | `bool` |
| `is_integral`, `is_floating`, `is_arithmetic`, `is_signed`, `is_unsigned`, `is_bool`, `is_char`, `is_string`, `is_enum`, `is_class`, `is_dynamic` | `bool` |
| `integer_rank(T)` | The width step of an integer type: `int` is 0, `long int` 1, `short int` -1, and so on up to 6 for `long long integer` |
| `float_rank(T)` | The same for floats: `float` is 0, `short float` -1, `double` 2 |

```walnut
function main() -> int {
    int: x = 3;
    const int: cx = 4;
    int&: rx = x;
    decltype(rx): alias = x;
    typeof(rx): copy = x;
    make_unsigned(int): u = 7;
    conditional(sizeof(int) == 4, int, string): pick = 9;
    discard_const(typeof(cx)): changeable = 1;
    changeable = 2;
    static_assert(is_ref(decltype(rx)), "decltype keeps the reference");
    static_assert(!is_ref(typeof(rx)), "typeof drops it");
    static_assert(is_const(typeof(cx)), "typeof keeps const");
    static_assert(is_same(typeof(x + 2.5), float), "int + float is float");
    static_assert(is_unsigned(typeof(u)), "make_unsigned made it unsigned");
    static_assert(integer_rank(long int) == 1 && float_rank(double) == 2, "ranks");
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
| `import module.name` | A module found by name, as `name::...` |
| `import module.a.b as x` | The module `a/b`, as `x::...` |

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

`import module.name` looks for a module file called `name.walh` (or `name.wal`). In `import module.a.b`, the dots become folders, so it looks for `a/b.walh`. The module is reached through its last name part (`b::`), or through the name given with `as`. A module whose name is a keyword, such as `module.string`, must be imported with `as`.

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
- Several functions can share one `extern "C"` or `extern "C++"` in an extern block. It can only hold function declarations, can only appear at file or namespace level, and closes with `end extern`:

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
| `help`, `-h`, `--help` | Shows the usage |

| Option | Meaning |
|---|---|
| `-o <path>` | Output path |
| `-O0`, `-O1`, `-O2`, `-O3`, `-Os` | Optimization (default `-O2`) |
| `-I <dir>` | Folder for `#include` |
| `-D <name>[=value]` | Defines a macro |
| `-M <dir>`, `--module-dir <dir>` | Folder searched by `import module.<name>` |
| `-L <dir>`, `-l <lib>` | Library folder and library for native code |
| `--cxx <compiler>` | The C++ compiler to use |
| `--cxx-flag <flag>` | Passes a flag to the C++ compiler |
| `--runtime <dir>` | Where the Walnut runtime is |
| `--keep-cpp` | Keeps the generated C++ next to the program (builds without the cache) |
| `--no-cache` | Builds from scratch in a temporary folder, without the build cache |
| `-g` | Builds for a debugger, which then shows `.wal` lines (defaults to `-O0`) |
| `--debug-cpp` | Builds for a debugger that shows the generated C++ |
| `--line-map[=file]` | Writes which `.wal` line each block of C++ came from |
| `--no-warnings` | Hides warnings |
| `--errors <dest>`, `--warnings <dest>` | Sends errors or warnings to `stderr`, `stdout` or a file |
| `--tokens <file.txt>`, `--ast <file.txt>` | Writes the token list or syntax tree |
| `--time` | Shows how long each stage takes |
| `-v`, `--verbose` | Prints the C++ compiler command |

`-I`, `-D`, `-L` and `-l` also accept their value attached, as in `-Iinclude`.

Arguments after `--` go to the program.

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

## Not supported yet

These are planned or partly implemented. They are listed so you don't rely on them.

**Syntax that isn't accepted**
- Several variables in one declaration (`int: a = 1, b = 2;`).
- `T: x{v}` and `T: x(v)` declaration forms. Write `T: x = T(v);` instead.
- `enum class`. Enums are always scoped, so it isn't needed.
- Octal literals.
- `sizeof...(pack)`. Use `countof(pack)`.
- Labelled `break`.
- `using a::b;` for a single name.
- `= 0` for abstract methods. Leave out the body of a `virtual` method instead.
- Qualified member access such as `obj.Base::member`.
- Defining constructors, destructors and operators outside their class (methods can be).

**Accepted, but not working yet**
- Templates of conversion operators. Every other operator can be a template, inside or outside a class.
