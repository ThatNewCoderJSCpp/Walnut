# optional

`optional` has `Optional<T>`: a value of type `T` that may or may not be there. Use it instead of a special value, a `bool` beside the result, or a pointer that might be `nullptr`, when "nothing" is a normal answer, such as a search that finds no match. It is C++'s `std::optional`, with the methods Walnut programmers expect. It also has [`Result<T, E>`](#result), which holds either a value or an error.

```walnut
import module.optional
```

Every program gets this library automatically, because the [`?`, `?.` and `??` operators](../../documentation.md#missing-values-and-errors) are built on it, so the `import` is optional. Everything is in the namespace `std::optional`. `using std::optional::Optional;` brings in the class under its short name.

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

## Result

`Result<T, E>` holds either a value of type `T` or an error of type `E`. Return one from a function that can fail, so the caller gets the reason as well as the failure. Inside a function that returns a `Result`, [`?`](../../documentation.md#missing-values-and-errors) unwraps a `Result` and hands its error on to the caller.

```walnut
using std::optional::Result;

function divide(int: a, int: b) -> Result<int, string> {
    if (b == 0) then {
        return std::optional::error("division by zero");
    }
    return std::optional::ok(a / b);
} end function divide

function average(int: total, int: count) -> Result<int, string> {
    int: each = divide(total, count)?;
    return std::optional::ok(each);
} end function average

function main() -> int {
    Result<int, string>: good = average(10, 2);
    Result<int, string>: bad = average(10, 0);
    int: shown = bad.value_or(-1);
    return 0;
} end function main
```

Here `good` holds `5`, `bad` holds the error `"division by zero"`, and `shown` is `-1`.

**Making one**

| Written | Holds |
|---|---|
| `std::optional::ok(value)` | `value`. It converts to any `Result` whose value type is the type of `value`, so it can be returned directly |
| `std::optional::error(e)` | The error `e`. It converts to any `Result` whose error type is the type of `e` |
| `Result<T, E>::ok(value)`, `Result<T, E>::err(e)` | The same, with the types written out |
| `Result<T, E>: r;` | Neither: `is_ok()` is `false` and `error()` throws until a value or an error is assigned |

**Methods**

| Method | Gives |
|---|---|
| `is_ok()`, `is_err()` | Whether it holds a value, or an error |
| `static_cast<bool>(r)` | The same as `is_ok()` |
| `value()` | The value. Throws a `std::errors::optional_error` when it holds an error |
| `error()` | The error. Throws a `std::errors::optional_error` when it holds a value |
| `value_or(fallback)` | The value, or `fallback` |
| `ok_value()`, `err_value()` | The value or the error as an `Optional` |
| `map(func)` | A `Result` holding `func(value)`, or the same error |
| `map_error(func)` | A `Result` holding the same value, or `func(error)` |
| `to_string()` | `"ok(...)"` or `"error(...)"` |
| `r == other`, `r != other` | Equal when both hold equal values or equal errors |

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
