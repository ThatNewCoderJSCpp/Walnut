# core

`core` holds the parts of the library that almost every program needs. Every Walnut program gets it automatically: there is nothing to `import`, and its names are always available. It has `std::core` (`move`, `forward`, `to_string`, and checks on number values such as `is_integer`, `is_even` and `is_finite`), `std::traits` (type traits and type operations), `std::concepts` (ready-made concepts), and directly in `std` the function types (`std::Predicate`, `std::Compare` and the rest) with ready-made comparators, the constant types (`std::true_type`, `std::bool_constant` and the rest), `std::enable_if` and `std::initializer_list`.

## Contents

1. [std::core::move](#stdcoremove)
2. [std::core::forward](#stdcoreforward)
3. [std::core::to_string](#stdcoreto_string)
4. [Checking numbers](#checking-numbers)
5. [std::traits](#stdtraits)
6. [std::concepts](#stdconcepts)
7. [Constants](#constants)
8. [enable_if](#enable_if)
9. [std::initializer_list](#stdinitializer_list)
10. [Function types and comparators](#function-types-and-comparators)

## std::core::move

```walnut
template <typename T>
constexpr function move(T&: value) noexcept -> T&&;

template <typename T>
constexpr function move(T&&: value) noexcept -> T&&;
```

`std::core::move(x)` lets `x` be moved from: it gives `x` back as an rvalue reference (`T&&`), so a move constructor or move assignment is chosen instead of a copy. It doesn't move anything itself; the move happens when the result is used to construct or assign another object.

```walnut
class Track {
public:
    int: size;
    constructor(int: n) : size = n { }
    constructor(const Track&: other) : size = other.size { }
    constructor(Track&&: other) : size = other.size {
        other.size = 0;
    }
} end class Track

function main() -> int {
    Track: a = Track(8);
    Track: b = a;
    Track: c = std::core::move(b);
    return 0;
} end function main
```

Here `a.size` and `c.size` are `8`, and `b.size` is `0`: `b` was copied from `a`, then `c` was moved from `b`.

- After a move, the moved-from object is still valid, but its value is whatever the move constructor or move assignment left in it.
- `std::core::move` on a temporary is allowed and has no extra effect.
- `std::core::move(x)` does the same thing as `static_cast<T&&>(x)`.
- It is `constexpr`, so it can be used in constant expressions, and `noexcept`, so it can be used in `noexcept` functions and destructors.
- Because it is in `std::core`, a function of your own called `move` doesn't clash with it.

```walnut
constexpr function shifted(int: n) -> int {
    int: copy = std::core::move(n);
    return copy + 6;
} end function shifted

static_assert(shifted(4) == 10, "shifted adds 6");

function main() -> int {
    constexpr int: seven = std::core::move(7);
    return 0;
} end function main
```

Here `seven` is `7`.

## std::core::forward

```walnut
template <typename T>
constexpr function forward(std::traits::discard_ref<T>&: value) noexcept -> T&&;
template <typename T>
constexpr function forward(std::traits::discard_ref<T>&&: value) noexcept -> T&&;
```

`std::core::forward<T>(x)` passes on a [forwarding reference](../../documentation.md#copying-and-moving) the way it arrived: when the caller gave a variable, `T` is a reference and `forward` gives an lvalue, and when the caller gave a temporary, `forward` gives an rvalue so the value can be moved. `T` is always written out, because it is the template parameter of the function doing the forwarding.

```walnut
function take(int&: x) -> string {
    return "kept";
} end function take

function take(int&&: x) -> string {
    return "moved";
} end function take

template <typename T>
function relay(T&&: value) -> string {
    return take(std::core::forward<T>(value));
} end function relay

function main() -> int {
    int: n = 3;
    string: a = relay(n);
    string: b = relay(4);
    string: c = relay(std::core::move(n));
    return 0;
} end function main
```

Here `a` is `"kept"`, and `b` and `c` are `"moved"`.

- It does the same thing as `static_cast<T&&>(x)`.
- With a pack, forward each element: `make(std::core::forward<Ts>(args)...)`.
- It is mostly used in [template constructors](../../documentation.md#constructors-and-destructors) and wrapper functions that hand their arguments to something else.

## std::core::to_string

```walnut
template <typename T>
function to_string(const T&: value) -> string;
```

`std::core::to_string(x)` turns a value into a `string`:

- a class with a `to_string()` method gives what that method returns (a `string` or a `text`);
- a number, `bool`, `char`, `string` or `text` gives the same text a template string `${x}` would: `true`, `42`, `2.5`.

```walnut
class Point {
public:
    int: x = 0;
    int: y = 0;
    function to_string() const -> string {
        return "(" + std::core::to_string(x) + ", " + std::core::to_string(y) + ")";
    } end function to_string
} end class Point

function main() -> int {
    Point: p;
    p.x = 3;
    string: a = std::core::to_string(p);
    string: b = std::core::to_string(2.5);
    string: c = std::core::to_string(true);
    return 0;
} end function main
```

Here `a` is `"(3, 0)"`, `b` is `"2.5"` and `c` is `"true"`.

- A class without a `to_string()` method is an error, because there is nothing to call.
- [`std::concepts::has_to_string<T>`](#stdconcepts) is the concept it checks, so your own templates can require it: `template <std::concepts::has_to_string T>`.
- `std::io` uses the same check to print classes, so a class with `to_string()` works with `println` and `<<`. See [io](io.md#your-own-types).

## Checking numbers

These functions in `std::core` check a number's value. They take any built-in integer or float type (not `bool` or `char`), and almost all of them are `constexpr`, so they work in `static_assert`, `if constexpr` and other constants as well as while the program runs. Unlike [the traits](#stdtraits), which describe types, these look at the value they're given.

```walnut
import module.io

using namespace std::core;

function main() -> int {
    static_assert(is_integer(5) && is_integer(4.0) && !is_integer(4.5), "whole numbers");
    static_assert(is_even(10) && is_odd(-3) && is_even(6.0) && !is_odd(2.5), "parity");
    float: ratio = 0.0;
    ratio = ratio / 0.0;
    std::io::println(is_finite(ratio), " ", is_indeterminate(ratio), " ", is_power_of_two(64), " ", is_between(7, 1, 10));
    return 0;
} end function main
```

This prints `0 1 1 1`.

| Function | True when `x` |
|---|---|
| `is_integer(x)` | Is a whole number. For an integer type, true unless it holds a special value; for a float, true when it is finite and has no fractional part (`3.0`, `-12.0`, `1.0e40`) |
| `is_even(x)`, `is_odd(x)` | Is a whole number that is even or odd. A float with a fractional part is neither |
| `is_zero(x)`, `is_nonzero(x)` | Is `0`, or is a number other than `0` |
| `is_positive(x)`, `is_negative(x)` | Is above or below `0`. `inf` is positive and `-inf` negative |
| `is_nonnegative(x)`, `is_nonpositive(x)` | Is `0` or above, or `0` or below |
| `is_finite(x)` | Is an ordinary number, not one of the [special values](../../documentation.md#special-values) |
| `is_infinite(x)` | Is `inf` or `-inf` |
| `is_nan(x)`, `is_undefined(x)`, `is_indeterminate(x)` | Is that special value |
| `is_special(x)` | Is any special value |
| `is_power_of_two(x)` | Is a positive power of two: `1`, `2`, `4`, ... and, for floats, `0.5`, `0.25`, ... |
| `is_multiple_of(x, n)` | Is `n` times a whole number. `n` can't be `0` |
| `is_between(x, low, high)` | Is from `low` to `high`, both included |
| `is_normal(x)`, `is_subnormal(x)` | For floats only: is a nonzero finite value at or above `float_traits<T>::min()`, or a nonzero value below it. These two run only while the program runs, not at compile time |

- A special value is never a whole number, even, odd, zero or nonzero, and `nan`, `undefined` and `indeterminate` are neither positive nor negative.
- `x is inf`, `x is nan`, `x is undefined` and `x is indeterminate` also work in constants now, so they can be used in `constexpr` functions and `static_assert`.

## std::traits

Type traits answer questions about types while compiling. Each one is a `constexpr` [variable template](../../documentation.md#variable-templates), so `std::traits::is_integer<T>` is a `bool` constant that works in `static_assert`, `if constexpr`, `requires` clauses and concepts.

```walnut
template <typename T>
function describe(T: value) -> string {
    if constexpr (std::traits::is_integer<T>) then {
        return "an integer";
    } else if constexpr (std::traits::is_floating<T>) then {
        return "a float";
    } else then {
        return "something else";
    }
} end function describe

function main() -> int {
    static_assert(std::traits::is_same<int, int>, "same type");
    static_assert(!std::traits::is_pointer<int>, "not a pointer");
    static_assert(std::traits::integer_rank<long int> > std::traits::integer_rank<int>, "long int is wider");
    string: a = describe(3);
    string: b = describe(2.5);
    string: c = describe("x");
    return 0;
} end function main
```

Here `a` is `"an integer"`, `b` is `"a float"` and `c` is `"something else"`.

Every `bool` trait comes in two parts: the value `std::traits::is_integer<T>` that you use, and a struct `std::traits::extend::is_integer<T>` behind it whose `value` it reads. The struct is there so you can [extend a trait](#extending-a-trait) to your own types. The traits made from other traits, such as `is_number`, read those values, so extending `is_integer` also changes `is_number`, `is_scalar` and the rest.

**Kinds of value**

| Trait | `true` when `T` is |
|---|---|
| `is_integer<T>` | An integer type: `int`, `long int`, `unsigned short int` and the rest. Never `bool` or `char` |
| `is_signed_integer<T>`, `is_unsigned_integer<T>` | A signed or an unsigned integer type |
| `is_float<T>` (`is_floating<T>`) | A float type: `float`, `short float`, `unsigned long float` and the rest |
| `is_signed_float<T>`, `is_unsigned_float<T>` | A signed or an unsigned float type |
| `is_number<T>` (`is_arithmetic<T>`) | An integer or float type |
| `is_signed<T>`, `is_unsigned<T>` | A signed or an unsigned number type |
| `is_bool<T>` (`is_boolean<T>`) | `bool` |
| `is_char<T>` (`is_character<T>`) | `char` |
| `is_string<T>` | `string` or `text` |
| `is_str<T>`, `is_text<T>` | Only `string`, or only `text` |
| `is_text_like<T>` | `string`, `text` or `char` |
| `is_enum<T>` | An enum |
| `is_void<T>` | `void` |
| `is_dynamic<T>` | `var` |
| `is_special<T>` | A [special value](../../documentation.md#special-values) type such as `undefined` |
| `is_null_pointer<T>` | The type of `nullptr` |
| `is_scalar<T>` | A number, `bool`, `char`, enum, pointer or `nullptr` |
| `is_fundamental<T>` | A scalar, `void`, a string type or `var` |
| `is_compound<T>` | Not fundamental: an array, class, function, reference and so on |

**Kinds of type**

| Trait | `true` when `T` is |
|---|---|
| `is_const<T>`, `is_volatile<T>` | `const` or `volatile` |
| `is_ref<T>` (`is_reference<T>`), `is_lvalue_ref<T>`, `is_rvalue_ref<T>` | A reference (`T&` or `T&&`), only `T&`, only `T&&` |
| `is_pointer<T>` | A pointer |
| `is_array<T>`, `is_fixed_array<T>`, `is_runtime_array<T>` | An array, one with a size known while compiling, or one whose size is only known while running |
| `is_function<T>`, `is_closure<T>`, `is_callable_type<T>` | A function type, a lambda's type, or either |
| `is_class<T>` (`is_record<T>`), `is_struct<T>`, `is_union<T>` | A class, struct or union (`is_class` is all three), only a `struct`, only a `union` |
| `is_variant<T>` | A `variant(...)` |
| `is_native<T>` | A [C++ class](../../documentation.md#using-c-classes) used from Walnut |
| `is_abstract<T>` | A class with a method that has no body (`abstract`, or `virtual` without a body) |
| `is_polymorphic<T>` | A class with a virtual method or destructor, of its own or inherited |
| `is_empty<T>` | A class with no fields (of its own or inherited) and no virtual methods |
| `is_object<T>` | Not a reference, function or `void` |

**What a type can do**

| Trait | `true` when `T` |
|---|---|
| `is_default_constructible<T>` | Can be made with no arguments |
| `is_copy_constructible<T>`, `is_move_constructible<T>` | Can be copied, or moved, into a new object |
| `is_copy_assignable<T>`, `is_move_assignable<T>` | Can be copied, or moved, into an existing object |
| `is_copyable<T>`, `is_movable<T>` | Both of the above |
| `is_constructible<T, Args...>` | Can be made from values of the types `Args` (none means with no arguments), the way `T(args...)` would. A type like `int&` stands for a variable, `int` or `int&&` for a temporary |
| `is_nothrow_constructible<T, Args...>` | Can be made that way without throwing: the constructor it picks is `noexcept` or can't throw, the same answer `noexcept(T(args...))` gives. For a constructor Walnut writes itself, every base and field has to be made without throwing too |
| `is_constexpr_constructible<T, Args...>` | Can be made that way while compiling: the constructor it picks is `constexpr` (or `consteval`), or Walnut writes it and every base and field qualifies, with constant initializers. Built-in numbers, `bool`, `char`, strings, enums and pointers always can; `var`, variants and lambdas can't |
| `is_nothrow_default_constructible<T>`, `is_nothrow_copy_constructible<T>`, `is_nothrow_move_constructible<T>` | `is_nothrow_constructible` with no arguments, a `const T&` or a `T&&` |
| `is_constexpr_default_constructible<T>`, `is_constexpr_copy_constructible<T>`, `is_constexpr_move_constructible<T>` | The same for `is_constexpr_constructible` |
| `is_assignable<T, U>` | A `T` variable can be given a `U` with `=`: `T` isn't `const`, and the value converts, or `T` has an `operator(=)` that takes it, or `U` is a `T` and every field can be assigned (no `const` or reference fields) |
| `is_nothrow_assignable<T, U>`, `is_constexpr_assignable<T, U>` | That assignment can't throw, or is `constexpr` |
| `is_nothrow_copy_assignable<T>`, `is_nothrow_move_assignable<T>`, `is_constexpr_copy_assignable<T>`, `is_constexpr_move_assignable<T>` | The same with a `const T&` or a `T&&` |
| `is_destructible<T>` | Can be destroyed: not `void` or a function type, and no `destructor() = delete` of its own or in a base |
| `is_nothrow_destructible<T>` | The same as `is_destructible`, since destructors are always `noexcept` |
| `is_equality_comparable<T>` | Has `==` and `!=` |
| `is_totally_ordered<T>` | Has `==`, `!=`, `<`, `>`, `<=` and `>=` |
| `is_iterable<T>` | Has `begin()` and `end()` |
| `is_iterator<T>`, `is_random_access_iterator<T>` | Is an iterator, or one that can jump (see [std::concepts](#stdconcepts)) |
| `has_to_string<T>` | Has a `to_string()` method |
| `is_hashable<T>` | Has a `hash()` method |

**Comparing types**

| Trait | `true` when |
|---|---|
| `is_same<A, B>` | `A` and `B` are the same type |
| `is_same_ignoring_cv<A, B>`, `is_same_decayed<A, B>` | They are the same once `const` and `volatile` are removed, or once both are [decayed](#type-operations) |
| `is_convertible<From, To>` | A `From` converts to a `To` without a cast |
| `is_base_of<Base, Derived>`, `is_derived_from<Derived, Base>` | `Base` is `Derived` or one of its base classes |

**Numbers about types.** These are `int`s, not `bool`s:

| Trait | Is |
|---|---|
| `integer_bits<T>`, `float_bits<T>` | How many bits an integer or float type has |
| `float_mantissa_bits<T>`, `float_exponent_bits<T>` | How the bits of a float type are split |
| `integer_rank<T>`, `float_rank<T>` | The width step of an integer or float type, where `int` and `float` are `0`, each wider type adds one and each narrower one takes one away |
| `array_extent<T>` | The number of elements of a fixed-size array type |

See [Traits](../../documentation.md#traits) for details, and [Trait overloads](../../documentation.md#trait-overloads) for another way a class of your own can change the answers.

### Extending a trait

To make a trait `true` for a type of your own, specialize its struct in `std::traits::extend` and inherit from `std::true_type` (or `std::false_type` to turn it off):

```walnut
class Money {
public:
    long int: cents = 0;
} end class Money

namespace std {
    namespace traits {
        namespace extend {
            template <>
            struct is_integer<Money> inherits std::true_type {
            } end struct is_integer
        } end namespace extend
    } end namespace traits
} end namespace std

template <typename T>
requires std::traits::is_number<T>
function needs_number(const T&: x) -> bool {
    return true;
} end function needs_number

function main() -> int {
    bool: whole = std::traits::is_integer<Money>;
    bool: number = std::traits::is_number<Money>;
    bool: ok = needs_number(Money());
    return 0;
} end function main
```

Here `whole`, `number` and `ok` are all `true`: `is_number` is built from `is_integer`, so it follows. A partial specialization works the same way, as in `template <typename T> struct is_integer<Wrapper<T>> inherits std::true_type { }`. The [math](math.md) library uses this to make `is_float` true for `BigFloat`.

### integer_traits, float_traits and numeric_traits

`std::traits::integer_traits<T>` and `std::traits::float_traits<T>` describe a built-in integer or float type, like C++'s `std::numeric_limits` but split in two, so each only has what makes sense for its kind. Using the wrong one is an error.

```walnut
function main() -> int {
    int: biggest = std::traits::integer_traits<int>::max;
    long int: smallest = std::traits::integer_traits<long int>::min;
    int: bits = std::traits::float_traits<short float>::bits;
    int: precise = std::traits::float_traits<short float>::digits10;
    short float: eps = std::traits::float_traits<short float>::epsilon();
    static_assert(std::traits::integer_traits<unsigned short int>::max == 65535, "16 bits");
    return 0;
} end function main
```

Here `biggest` is `2147483647`, `smallest` is `-9223372036854775808`, `bits` is `64`, `precise` is `15` and `eps` is about `1.1e-16` (Walnut floats keep one more mantissa bit than IEEE `double`).

**`integer_traits<T>`**, all `static constexpr`:

| Member | Is |
|---|---|
| `bits`, `bytes` | The size in bits and bytes |
| `rank` | The same as `integer_rank<T>` |
| `is_signed` | Whether it has negative values |
| `digits`, `digits10` | How many binary digits, or whole decimal digits, it can hold without a sign |
| `radix` | `2` |
| `min`, `max`, `lowest` | The smallest and largest values (`lowest` is the same as `min`) |
| `zero`, `one` | `0` and `1` of type `T` |
| `has_special_values` | `true`: every Walnut integer can also hold [special values](../../documentation.md#special-values) |
| `is_integer`, `is_exact`, `is_bounded` | `true` |
| `type`, `signed_type`, `unsigned_type` | `T`, and its signed and unsigned forms |

**`float_traits<T>`**. The sizes are `static constexpr`; the values are `static` functions, because they are floats:

| Member | Is |
|---|---|
| `bits`, `bytes`, `rank` | The size in bits and bytes, and `float_rank<T>` |
| `mantissa_bits`, `exponent_bits` | How the bits are split |
| `digits`, `digits10`, `max_digits10` | Binary digits of precision (with the hidden bit), decimal digits that always survive, and decimal digits needed to write any value back exactly |
| `radix` | `2` |
| `exponent_bias`, `min_exponent`, `max_exponent`, `min_exponent10`, `max_exponent10` | The range of the exponent, in powers of 2 and of 10 |
| `is_signed`, `has_infinity`, `has_nan`, `has_subnormals`, `has_special_values` | `bool` |
| `is_integer`, `is_exact` | `false` |
| `is_bounded` | `true` |
| `epsilon()` | The gap between `1` and the next value up |
| `min()`, `max()`, `lowest()` | The smallest positive normal value, the largest value, and the most negative value |
| `subnormal_min()` | The smallest positive value |
| `infinity()`, `nan()` | Infinity and NaN |
| `round_error()` | `0.5` |

**`numeric_traits<T>`** works for both kinds, like C++'s `std::numeric_limits`, which is handy in a template that takes either an integer or a float. It has the same names for both: the sizes (`bits`, `bytes`, `digits`, `digits10`, `max_digits10`, `radix`, the exponent ranges), the `bool`s (`is_signed`, `is_integer`, `is_exact`, `is_bounded`, `has_infinity`, `has_nan`, `has_subnormals`, `has_special_values`), and the values as functions: `max()`, `min()`, `lowest()`, `epsilon()`, `round_error()`, `subnormal_min()`, `infinity()` and `nan()`.

```walnut
import module.io

using std::traits::numeric_traits;

template <typename T>
function show_range() -> void {
    std::io::println(numeric_traits<T>::lowest(), " to ", numeric_traits<T>::max(), ", ", numeric_traits<T>::digits10, " digits");
} end function show_range

function main() -> int {
    static_assert(numeric_traits<int>::max() == 2147483647, "int max");
    show_range<short int>();
    show_range<short float>();
    return 0;
} end function main
```

This prints `-32768 to 32767, 4 digits` and `-1.7976931348623158e+308 to 1.7976931348623158e+308, 15 digits`.

- As in C++, `min()` is the most negative value for an integer but the smallest positive normal value for a float; use `lowest()` for the most negative value of either.
- For an integer, the functions are `constexpr`; `epsilon()`, `round_error()` and `subnormal_min()` are `0`, and `infinity()` and `nan()` give those special values, which Walnut integers can hold. For a float, the functions run while the program runs, as in `float_traits`.
- Exponent members are `0` for integers, and `max_digits10` is `0`.

### Type operations

The type operations are [alias templates](../../documentation.md#alias-templates) in `std::traits`: each one names a type made from the types it's given.

```walnut
template <typename T>
function copy_of(const T&: value) -> std::traits::decay<T> {
    return value;
} end function copy_of

function main() -> int {
    std::traits::make_unsigned<int>: u = 7;
    std::traits::common_type<int, long int>: wide = 5;
    std::traits::conditional<sizeof(int) == 4, string, int>: pick = "four bytes";
    static_assert(std::traits::is_same<std::traits::discard_cv<const int>, int>, "const removed");
    int: c = copy_of(3);
    return 0;
} end function main
```

| Operation | Is |
|---|---|
| `discard_volatile<T>`, `discard_cv<T>` | `T` without `volatile`, or without `const` and `volatile` |
| `discard_ref<T>`, `discard_pointer<T>`, `decay<T>` | `T` without a reference, without a pointer, or as it would be passed by value (arrays become pointers) |
| `add_const<T>`, `add_volatile<T>`, `add_ref<T>`, `add_rvalue_ref<T>`, `add_pointer<T>` | `T` with that added |
| `make_signed<T>`, `make_unsigned<T>` | The signed or unsigned number type of the same width |
| `promote<T>`, `demote<T>` | The next wider or narrower type of the same kind |
| `int_from_float<T>`, `float_from_int<T>` | The integer or float type of the same width |
| `element_type<T>` | The element type of an array, or `char` for strings |
| `underlying_type<E>` | The integer type behind an enum |
| `return_type<F>` | The return type of a function type |
| `common_type<Ts...>` | The type all of `Ts` convert to |
| `conditional<condition, A, B>` | `A` when the `bool` condition is true, otherwise `B`. A condition with `<` or `>` in it goes in parentheses: `conditional<(sizeof(T) > 4), A, B>` |

`discard_const(T)` is a keyword of the language rather than part of `std::traits`, like `typeof` and `decltype`.

## std::concepts

Ready-made [concepts](../../documentation.md#concepts), to use in place of `typename` or in a `requires` clause.

```walnut
template <std::concepts::integer_type T>
function half(T: x) -> T {
    return x / 2;
} end function half

template <typename T>
requires std::concepts::totally_ordered<T>
function smaller(T: a, T: b) -> T {
    return a < b ? a : b;
} end function smaller

function main() -> int {
    int: h = half(9);
    string: s = smaller("pear", "apple");
    return 0;
} end function main
```

Here `h` is `4` and `s` is `"apple"`. `half(2.5)` would be an error saying `integer_type<float> is not satisfied`.

| Concept | Satisfied when |
|---|---|
| `same_as<T, U>` | `T` and `U` are the same type |
| `convertible_to<From, To>` | a `From` converts to a `To` |
| `derived_from<Derived, Base>` | `Derived` is `Base` or inherits from it |
| `integer_type<T>`, `signed_integer<T>`, `unsigned_integer<T>` | `T` is an integer type (never `bool` or `char`), a signed one, an unsigned one |
| `floating_point<T>`, `real_float<T>` | `T` is a float type |
| `number<T>`, `arithmetic<T>` | `T` is an integer or float type |
| `bool_type<T>`, `char_type<T>`, `string_like<T>` | `T` is `bool`, `char`, or `string` or `text` |
| `enumeration<T>`, `class_type<T>`, `pointer<T>`, `scalar<T>` | `T` is an enum, a class, a pointer, or a [scalar](#stdtraits) |
| `default_constructible<T>`, `copy_constructible<T>`, `move_constructible<T>` | `T` can be made with no arguments, by copying, or by moving |
| `copy_assignable<T>`, `move_assignable<T>`, `copyable<T>`, `movable<T>` | `T` can be copied or moved into an existing object, or both constructed and assigned that way |
| `hashable<T>` | `T` has a `hash()` method |
| `equality_comparable<T>` | `T` has `==` and `!=` |
| `totally_ordered<T>` | `T` has `==`, `!=`, `<`, `>`, `<=` and `>=` |
| `has_to_string<T>` | `T` has a `to_string()` method, which is what [`std::core::to_string`](#stdcoreto_string) and printing in `io` look for |
| `iterable<C>` | `C` has `begin()` and `end()` methods, so `for ... in` and the container forms of the [algorithms](algorithms.md) work on it |
| `iterator<I>` | `I` can be read with `*it`, moved on with `++it` and compared with `!=` |
| `random_access_iterator<I>` | `I` is an `iterator` that can also jump (`it + n`, `it - n`), be subtracted from another one (`last - first`) and be indexed (`it[k]`), as pointers can |

`using std::concepts::integer_type;` brings one in under its short name, so it can be written `template <integer_type T>`. The concepts follow the [traits](#stdtraits), so extending a trait also changes the concepts built on it.

## Constants

A constant type carries one value as part of its type. Each one has `value`, a `static constexpr` member holding that value; `value_type`, its type; a conversion to that type; and `()`, which gives the value.

| Type | Holds |
|---|---|
| `std::bool_constant<V>` | a `bool` |
| `std::integer_constant<T, V>` | a value of the integer type `T` |
| `std::float_constant<T, V>` | a value of the float type `T` |
| `std::char_constant<V>` | a `char` |
| `std::string_constant<V>`, `std::text_constant<V>` | a `string` or a `text` |
| `std::value_constant<T, V>` | a value of any type `T` that can be a constant |
| `std::true_type`, `std::false_type` | `std::bool_constant<true>` and `std::bool_constant<false>` |

```walnut
template <typename T>
struct is_small inherits std::bool_constant<sizeof(T) <= 4> {
} end struct is_small

template <typename T>
struct is_text_like inherits std::false_type {
} end struct is_text_like

template <>
struct is_text_like<string> inherits std::true_type {
} end struct is_text_like

function main() -> int {
    static_assert(is_small<int>::value && !is_small<long long int>::value, "sizes");
    static_assert(is_text_like<string>::value && !is_text_like<int>::value, "text");
    static_assert(std::integer_constant<long int, 7>::value == 7, "seven");
    std::string_constant<"hello">: greeting;
    string: s = greeting;
    char: c = std::char_constant<'q'>()();
    bool: yes = std::true_type();
    return 0;
} end function main
```

Here `s` is `"hello"`, `c` is `'q'` and `yes` is `true`.

- Inheriting from `std::true_type` or `std::false_type` is the usual way to write a yes-or-no question about types, with [specializations](../../documentation.md#specialization) giving the answers that differ. `is_text_like<T>::value` works in `static_assert`, `if constexpr`, `requires` clauses and [`enable_if`](#enable_if).
- The value is a template argument, so it has to be a constant: `std::string_constant<"hi">` works, `std::string_constant<name>` only if `name` is `constexpr`.

## enable_if

`std::enable_if<condition, T>` has a member type `type`, which is `T` (or `void` if `T` is left out), only when the condition is `true`. `std::enable_if_t<condition, T>` is that type. Using it in a function template's signature removes the template from a call whenever the condition is `false`; see [Type flags and SFINAE](../../documentation.md#type-flags-and-sfinae).

```walnut
template <typename T>
function half(T: x) -> std::enable_if_t<std::traits::is_integer<T>, T> {
    return x / 2;
} end function half

template <typename T>
function half(T: x) -> std::enable_if_t<std::traits::is_floating<T>, T> {
    return x / 2.0;
} end function half

function main() -> int {
    int: a = half(9);
    float: b = half(9.0);
    return 0;
} end function main
```

Here `a` is `4` and `b` is `4.5`.

## std::initializer_list

`std::initializer_list<T>` holds the values of a brace list, like C++'s `std::initializer_list`. It is how a function or constructor takes any number of values written as `{...}`:

```walnut
import module.io
import module.collections

class Bag {
public:
    std::collections::Vector<int>: items;

    constructor(int: a, int: b) {
        items.push_back(a * b);
    }

    constructor(std::initializer_list<int>: values) {
        for (int: v in values) {
            items.push_back(v);
        }
    }
} end class Bag

function total(std::initializer_list<int>: values) -> int {
    int: sum = 0;
    for (int: v in values) {
        sum += v;
    }
    return sum;
} end function total

function main() -> int {
    Bag: listed = {2, 3, 4};
    Bag: paired(2, 3);
    std::io::println(listed.items.size(), " ", paired.items[0], " ", total({1, 2, 3, 4}), " ", total({}));
    return 0;
} end function main
```

This prints `3 6 10 0`.

| Member | Gives |
|---|---|
| `size()`, `empty()` | How many values it holds |
| `begin()`, `end()`, `data()` | Pointers to the first value and past the last, so `for (T: x in list)` works |
| `[i]`, `at(i)` | The value at index `i`; an index out of range stops the program with an error |

- A brace list becomes an `initializer_list<T>` wherever one is expected: a variable (`std::initializer_list<int>: xs = {1, 2, 3};`), a function argument, or a constructor argument. Each value has to convert to `T`.
- When a class has a constructor that takes an `initializer_list<T>`, a brace list whose values all convert to `T` uses that constructor, even when another constructor takes the same number of values. Use parentheses to pick the other one: `Bag: paired(2, 3);` above. See [Classes and structs](../../documentation.md#classes-and-structs).
- The values are read-only. Copying an `initializer_list` is cheap: the copies share the same values instead of copying them.

## Function types and comparators

These names are [function types](../../documentation.md#function-types), so a parameter can say what kind of function it wants without a template. The library uses them wherever it takes a function, and they work in your own code the same way.

| Name | Is |
|---|---|
| `std::Predicate<T>` | `function(const T&) -> bool`, a test on one value |
| `std::Compare<T>` | `function(const T&, const T&) -> bool`, `true` when the first value goes before the second |
| `std::Action<T>` | `function(T&) -> void`, something done to a value, which may change it |
| `std::Transform<T, U>` | `function(const T&) -> U`, turning a `T` into a `U` |
| `std::Combine<U, T>` | `function(const U&, const T&) -> U`, folding a `T` into a running `U` |
| `std::Supplier<T>` | `function() -> T`, making a value |

A lambda or function fits when it can be called with the parameters of the type and gives something that converts to its result without loss, so `[](int: x) -> bool { ... }` is a `std::Predicate<int>` as well as `[](const int&: x) -> bool { ... }`. A `void` result accepts any function. When a function-type parameter is only called inside the function, the compiler passes the lambda straight through, so calling it costs the same as calling a template's `Compare`.

**Ready-made comparators**

| Function | Is `true` when |
|---|---|
| `std::less_than<T>(a, b)` | `a < b` |
| `std::leq<T>(a, b)` | `a <= b` (worked out as `!(b < a)`) |
| `std::greater_than<T>(a, b)` | `a > b` (worked out as `b < a`) |
| `std::geq<T>(a, b)` | `a >= b` (worked out as `!(a < b)`) |
| `std::equal<T>(a, b)` | `a == b` |
| `std::not_equal<T>(a, b)` | `!(a == b)` |

Each is a `std::Compare<T>`, so it can be passed by name: `v.sort(std::greater_than<int>)`.

| Function | Gives |
|---|---|
| `std::by<T, K>(key)` | A `std::Compare<T>` that orders values by `key(value)` |
| `std::reverse_order<T>(comp)` | A `std::Compare<T>` with `comp`'s order turned around |
| `std::negate<T>(test)` | A `std::Predicate<T>` that is `true` when `test` is `false` |

```walnut
import module.collections

struct Person {
    string: name;
    int: age = 0;
} end struct Person

function main() -> int {
    std::collections::Vector<Person>: people;
    people.push_back(Person{"Zoe", 41});
    people.push_back(Person{"Al", 19});
    people.sort(std::by<Person, int>([](const Person&: p) -> int { return p.age; }));
    people.sort(std::reverse_order<Person>(std::by<Person, string>([](const Person&: p) -> string { return p.name; })));
    std::Predicate<int>: odd = [](int: x) -> bool { return x % 2 == 1; };
    bool: even_seven = std::negate<int>(odd)(7);
    return 0;
} end function main
```

After the first `sort`, `people[0]` is `Al`; after the second, it is `Zoe`, and `even_seven` is `false`.
