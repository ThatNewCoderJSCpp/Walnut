# iterators

`iterators` has the iterator classes the library's containers use, a `Range` that holds a pair of iterators, and functions that work on anything a `for` loop can walk through, written with [`iterable<T>`](../../documentation.md#iterable-parameters) instead of templates.

```walnut
import module.iterators
```

Everything is in the namespace `std::iterators`. `import module.collections` imports it too.

## Contents

1. [A first example](#a-first-example)
2. [Iterator classes](#iterator-classes)
3. [Going backwards](#going-backwards)
4. [Range](#range)
5. [Functions on iterables](#functions-on-iterables)
6. [Using the iterators in your own containers](#using-the-iterators-in-your-own-containers)

## A first example

```walnut
import module.io
import module.collections
import module.iterators

function main() -> int {
    std::collections::DynamicArray<int>: scores = std::collections::DynamicArray<int>::of(7, 3, 9, 4);
    std::collections::LinkedList<string>: names = std::collections::LinkedList<string>("ann", "bob", "cy");

    for (int: s in scores.backwards()) {
        std::io::print(s, " ");
    }
    std::io::println("");

    std::io::println(std::iterators::count_if(scores, [](const int&: s) -> bool { return s > 5; }), " ",
                     std::iterators::max_of(scores), " ", std::iterators::min_of(names));
    return 0;
} end function main
```

This prints `4 9 3 7` and then `2 9 ann`.

## Iterator classes

Each class takes a `bool Reverse` template argument, `false` by default. With `true` it walks the other way: `++` goes backwards and `--` goes forwards. Every one has `==`, `!=`, prefix `++` and unary `*`, and a `value_type` alias for the element type `T`.

| Class | Walks over | `*it` gives |
|---|---|---|
| `Pointer<T, Reverse>` | Elements stored one after another, from a `T*` and an index | `base[index]` |
| `Indexed<C, T, Reverse>` | A container `C` with `operator([])`, from a `C*` and an index | `(*owner)[index]` |
| `Linked<N, T, Reverse>` | Nodes with `value`, `next` and `prev` fields | `node->value` |
| `Tree<N, T, Reverse>` | Nodes of a binary search tree with `left`, `right`, `parent` and `entry` fields, in order | `node->entry` |
| `Sparse<T, Reverse>` | An array of slots, skipping the ones whose flag in a parallel `const char*` array isn't the "filled" mark (`'f'` unless another is given) | `slots[index]` |
| `Keys<It, K>`, `Values<It, V>` | Another iterator over pairs | `(*inner).first`, `(*inner).second` |

`Pointer`, `Indexed`, `Linked` and `Tree` also have `--` and `operator(->)`. `Pointer` and `Indexed` have `position()`, the current index.

`Pointer`, `Indexed` and `Sparse` have static `first(...)` and `past(...)` functions that give the iterator at the first element and the one just past the last, for whichever direction `Reverse` picks, so the same code makes forward and backward ranges:

```walnut
using Back = std::iterators::Pointer<int, true>;
for (Back: it = Back::first(data, count); it != Back::past(data, count); ++it) {
    std::io::print(*it);
}
```

A `Pointer` going backwards starts at index `count - 1` and stops at `-1`.

## Going backwards

Every container in [collections](collections.md) has an `Iterator` and a `ReverseIterator`, made from these classes, plus:

| Method | Gives |
|---|---|
| `rbegin()`, `rend()` | `ReverseIterator`s at the last element and just before the first |
| `backwards()` | A `Range` of those two, so `for (T: x in c.backwards())` walks from the back |

`Vector`, `DynamicArray`, `SmallVector` and `Span` use `Pointer<T, true>` (`Pointer<const T, true>` on a `const` container), `LinkedList` uses `Linked`, `Deque` uses `Indexed`, `Map` and `Set` use `Tree`, `UnorderedMap` uses `Sparse`, and `UnorderedSet` uses `Keys` over its map's iterators. The array containers' existing `reversed()` still gives a reversed copy; `backwards()` copies nothing.

## Range

`Range<It>` holds two iterators, `first` and `last`, and has `begin()`, `end()` and `empty()`, so any pair of iterators can go in a `for` loop or be passed where an `iterable<T>` is wanted:

```walnut
std::iterators::Range<int*>: middle = std::iterators::Range<int*>(v.data() + 1, v.data() + 3);
```

## Functions on iterables

These take an `iterable<T>`: an array, a string, a generator, a container, a `Range`, or any class of yours with `begin()` and `end()`. `T` is the element type and is worked out from the argument.

| Function | Gives |
|---|---|
| `count(items)` | How many elements there are |
| `count_if(items, test)` | How many pass `test`, a `std::Predicate<T>` |
| `any_of(items, test)`, `all_of(...)`, `none_of(...)` | Whether some, every or no element passes `test` |
| `contains(items, value)` | Whether an element equals `value` |
| `index_of(items, value)`, `index_if(items, test)` | The position of the first match, or `-1` |
| `sum(items)` | The elements added up, starting from `T()` |
| `fold(items, start, step)` | `step(step(start, a), b)` and so on; `step` takes the running value and an element |
| `min_of(items)`, `max_of(items)`, each with an optional `std::Compare<T>` | The smallest or largest element; throws `std::errors::empty_range` when there are none |
| `each(items, act)` | Calls `act`, a `std::Action<T>`, on every element; `items` is an `iterable<T>&`, so `act` can change them |

## Using the iterators in your own containers

A container of yours can use these classes instead of writing its own iterator:

```walnut
template <typename T>
class Ring {
    T*: m_data = nullptr;
    long int: m_count = 0;
public:
    using Iterator = std::iterators::Pointer<T>;
    using ReverseIterator = std::iterators::Pointer<T, true>;

    function begin() const -> Iterator {
        return Iterator::first(m_data, m_count);
    } end function begin

    function end() const -> Iterator {
        return Iterator::past(m_data, m_count);
    } end function end

    function backwards() const -> std::iterators::Range<ReverseIterator> {
        return std::iterators::Range<ReverseIterator>(ReverseIterator::first(m_data, m_count), ReverseIterator::past(m_data, m_count));
    } end function backwards
} end class Ring
```

It then works in `for` loops, in the `std::iterators` functions and anywhere else an `iterable<T>` is taken.
