# optional

`optional` has `Optional<T>`: a value of type `T` that may or may not be there. Use it instead of a special value, a `bool` beside the result, or a pointer that might be `nullptr`, when "nothing" is a normal answer, such as a search that finds no match. It is C++'s `std::optional`, with the methods Walnut programmers expect.

```walnut
import module.optional
```

Everything is in the namespace `std::optional`. `using std::optional::Optional;` brings in the class under its short name.

## A first example

```walnut
import module.optional
import module.io

using std::optional::Optional;

function first_even(const array[4] int&: xs) -> Optional<int> {
    for (int: x in xs) {
        if (x % 2 == 0) then {
            return Optional<int>(x);
        }
    }
    return std::optional::none;
} end function first_even

function main() -> int {
    array[4] int: some = {3, 5, 8, 9};
    array[4] int: odd = {1, 3, 5, 7};
    Optional<int>: a = first_even(some);
    Optional<int>: b = first_even(odd);
    std::io::println(a.value(), " ", b.value_or(-1), " ", b.is_none());
    auto: label = a.map([](int: v) -> string { return std::core::to_string(v * 10); });
    std::io::println(label.value(), " ", b.to_string());
    return 0;
} end function main
```

This prints `8 -1 1` and `80 none`.

## Making one

| Written | Holds |
|---|---|
| `Optional<T>: o;`, `Optional<T>()` | Nothing |
| `Optional<T>(value)` | `value` |
| `std::optional::none` | Nothing; it converts to an `Optional` of any type, so it can be returned or assigned |
| `std::optional::some(value)`, `std::optional::make_optional(value)` | `value`, with `T` worked out from it |
| `std::optional::nothing<T>()` | Nothing, as an `Optional<T>` |

## Methods

**Checking**

| Method | Gives |
|---|---|
| `has_value()`, `is_some()` | `true` when there is a value |
| `empty()`, `is_none()` | `true` when there isn't |
| `static_cast<bool>(o)` | The same as `has_value()`. The conversion is `explicit`, so an `Optional<bool>` is never mistaken for its value |
| `contains(value)` | Whether it holds a value equal to `value` |
| `o == other`, `o != other` | Equal when both are empty, or both hold equal values |
| `o == std::optional::none` | Whether it is empty |

**Reading**

| Method | Gives |
|---|---|
| `value()`, `*o` | The value. Throws a [`std::errors::optional_error`](#errors) when there is none |
| `expect(message)` | The value, or throws an `optional_error` with your `message` |
| `value_or(fallback)` | The value, or `fallback` when there is none |
| `value_or_else(make)` | The value, or `make()` |
| `get()` | A pointer to the value, or `nullptr` |
| `to_string()` | The value as a string, or `"none"` |

**Changing**

| Method | Does |
|---|---|
| `set(value)`, `emplace(value)` | Stores `value`, replacing any old one (`emplace` gives back a reference to it) |
| `replace(value)` | Stores `value` and gives back the old `Optional` |
| `reset()` | Empties it |
| `take()` | Moves the value out and leaves it empty. Throws when there is none |
| `take_or(fallback)` | The same, or `fallback` when there is none |
| `swap(other)` | Exchanges with another `Optional<T>` |

**Chaining**. Each takes a function or lambda:

| Method | Gives |
|---|---|
| `map(func)` | `Optional` of `func(value)`, or an empty one. The result type is whatever `func` returns |
| `and_then(func)` | `func(value)`, where `func` itself returns an `Optional`; or an empty one |
| `or_else(func)` | Itself when it has a value, otherwise `func()`, which returns an `Optional<T>` |
| `or_value(other)` | Itself when it has a value, otherwise `other` |
| `filter(pred)` | Itself when it has a value that passes `pred`, otherwise an empty one |
| `if_present(func)` | Calls `func(value)` when there is a value |

## Errors

`value()`, `*o`, `expect(...)` and `take()` on an empty `Optional` throw a `std::errors::optional_error`. Its `what()` (or its `message` field) says what happened:

```walnut
import module.optional
import module.io

function main() -> int {
    std::optional::Optional<string>: name;
    try {
        string: n = name.value();
    } catch (const std::errors::optional_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `problem: value() on an empty Optional`.
