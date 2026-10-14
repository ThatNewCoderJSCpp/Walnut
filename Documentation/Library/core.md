# core

`core` holds the parts of the library that almost every program needs. Every Walnut program gets it automatically: there is nothing to `import`, and its names are always available. Everything in `core` is in the namespace `std`.

## Contents

1. [std::move](#stdmove)

## std::move

```walnut
template <typename T>
constexpr function move(T&: value) noexcept -> T&&;

template <typename T>
constexpr function move(T&&: value) noexcept -> T&&;
```

`std::move(x)` lets `x` be moved from: it gives `x` back as an rvalue reference (`T&&`), so a move constructor or move assignment is chosen instead of a copy. It doesn't move anything itself; the move happens when the result is used to construct or assign another object.

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
    Track: c = std::move(b);
    return 0;
} end function main
```

Here `a.size` and `c.size` are `8`, and `b.size` is `0`: `b` was copied from `a`, then `c` was moved from `b`.

- After a move, the moved-from object is still valid, but its value is whatever the move constructor or move assignment left in it.
- `std::move` on a temporary is allowed and has no extra effect.
- `std::move(x)` does the same thing as `static_cast<T&&>(x)`.
- It is `constexpr`, so it can be used in constant expressions, and `noexcept`, so it can be used in `noexcept` functions and destructors.
- Because it is in `std`, a function of your own called `move` doesn't clash with it.

```walnut
constexpr function shifted(int: n) -> int {
    int: copy = std::move(n);
    return copy + 6;
} end function shifted

static_assert(shifted(4) == 10, "shifted adds 6");

function main() -> int {
    constexpr int: seven = std::move(7);
    return 0;
} end function main
```

Here `seven` is `7`.
