# algorithms

`algorithms` has searching, sorting and the other everyday operations on ranges of elements, in the style of C++'s `<algorithm>`. Each one takes a range as two iterators, `first` and `last`, where `last` is one past the end, so it works on any container whose `begin()` and `end()` give iterators: the [collections](collections.md), plain arrays through pointers, and your own classes.

```walnut
import module.algorithms
```

Everything is in the namespace `std::algorithms`. `import module.algorithms as alg` makes `alg::` a shorter name for it. The [collections](collections.md#searching-sorting-and-more) also have most of these as methods, such as `v.sort()` and `v.binary_search(x)`, and [strings](strings.md) have their own versions.

## Contents

1. [A first example](#a-first-example)
2. [Ranges and iterators](#ranges-and-iterators)
3. [Comparing and testing](#comparing-and-testing)
4. [Searching](#searching)
5. [Sorted ranges](#sorted-ranges)
6. [Sorting](#sorting)
7. [Changing a range](#changing-a-range)
8. [Copying, merging and sets](#copying-merging-and-sets)
9. [Counting and adding up](#counting-and-adding-up)
10. [Heaps](#heaps)
11. [Whole containers](#whole-containers)
12. [Your own containers](#your-own-containers)

## A first example

```walnut
import module.algorithms
import module.collections
import module.io

function main() -> int {
    std::collections::Vector<int>: v{5, 3, 9, 1, 7};
    std::algorithms::sort(v.begin(), v.end());
    bool: has = std::algorithms::binary_search(v.begin(), v.end(), 7);
    long int: at = std::algorithms::lower_bound(v.begin(), v.end(), 6) - v.begin();
    std::algorithms::sort(v.begin(), v.end(), [](int: a, int: b) -> bool { return a > b; });
    long int: odd = std::algorithms::count_if(v.begin(), v.end(), [](int: x) -> bool { return x % 2 == 1; });
    std::io::println(v[0], " ", has, " ", at, " ", odd);
    return 0;
} end function main
```

This prints `9 1 3 5`: after the second sort `v` is `9 7 5 3 1`, `7` was found, `6` belongs at position `3` of the ascending order, and all five numbers are odd.

## Ranges and iterators

- A range is `first, last`: every element from `first` up to, but not including, `last`. `v.begin(), v.end()` is all of `v`, and `v.begin() + 2, v.end()` skips the first two.
- Most functions only step forward with `++it`, so they work with any iterator, including those of `LinkedList`, `Deque`, `Map` and `Set`. The ones marked *random access* below jump around (`it + n`, `last - first`, `it[k]`), so they need pointers (from `Vector`, `DynamicArray`, `SmallVector`, `Span` or an array) or an iterator class with those operators.
- A function that finds a position gives back an iterator; `last` means "not found". Subtract `begin()` to get an index.
- Wherever a function compares elements, it uses `<` (or `==`). The overloads that take a `comp` use `comp(a, b)` instead, which must be `true` when `a` goes before `b`. A `pred` takes one element and gives a `bool`.
- `comp` is a [`std::Compare<E>`](core.md#function-types-and-comparators), `pred` a `std::Predicate<E>`, `key` a `std::Transform<E, K>` and `func` a `std::Action<E>`, where `E` is the element type, worked out from the lambda or function passed. Ready-made ones such as `std::less_than<int>`, `std::greater_than<int>` and `std::by<Person, int>(key)` fit too. A class object with an `operator(())` works when the call's other arguments already fix `E`; otherwise wrap it in a lambda.
- `distance(first, last)` gives the number of elements, `next(it, n)` and `prev(it, n)` move an iterator, and `iter_swap(a, b)` swaps two elements.

## Comparing and testing

| Function | Gives |
|---|---|
| `all_of(first, last, pred)`, `any_of(...)`, `none_of(...)` | Whether every, some or no element passes `pred` |
| `equal(first, last, other)`, `equal(first, last, o_first, o_last)` | Whether two ranges hold equal elements |
| `mismatch(first, last, o_first, o_last)` | The index of the first difference, or `-1` if the ranges are equal |
| `starts_with(first, last, p_first, p_last)`, `ends_with(...)` | Whether the range begins or ends with another one |
| `lexicographical_compare(first, last, o_first, o_last)` | Whether the first range comes before the second, like comparing words in a dictionary |
| `is_sorted(first, last)`, `is_sorted(first, last, comp)` | Whether the range is in order |
| `is_sorted_until(first, last)` | Where the order first breaks |
| `is_partitioned(first, last, pred)` | Whether every element passing `pred` comes before every one that doesn't |

## Searching

| Function | Gives |
|---|---|
| `find(first, last, value)`, `find_if(first, last, pred)`, `find_if_not(...)` | The first match |
| `find_last(first, last, value)`, `find_last_if(first, last, pred)` | The last match |
| `index_of(first, last, value)`, `index_if(first, last, pred)` | The index of the first match, or `-1` |
| `contains(first, last, value)`, `contains_if(first, last, pred)` | Whether there is a match |
| `adjacent_find(first, last)`, `adjacent_find(first, last, pred)` | The first of two equal neighbours |
| `find_first_of(first, last, s_first, s_last)` | The first element that is also in the second range |
| `search(first, last, s_first, s_last)` | Where the second range first appears inside the first |
| `search_n(first, last, n, value)` | Where `n` copies of `value` first appear in a row |
| `min_element(first, last)`, `max_element(...)`, each with an optional `comp` | The smallest or largest element |
| `minmax_element(first, last)` | Both, as a [`Range`](#copying-merging-and-sets) whose `first` is the smallest and `last` the largest |

## Sorted ranges

These are *random access*, and need the range to be sorted by the same order they search with.

| Function | Gives |
|---|---|
| `lower_bound(first, last, value)` | The first position where `value` could go without breaking the order |
| `upper_bound(first, last, value)` | The last such position |
| `equal_range(first, last, value)` | Both, as a `Range`; its size is how many elements equal `value` |
| `binary_search(first, last, value)` | Whether `value` is there |
| `binary_find(first, last, value)` | Where `value` is, or `last` |
| `exponential_search(first, last, value)` | The same as `binary_find`, faster when `value` is near the start |
| `fibonacci_search(first, last, value)` | The same as `binary_find`, using only additions to pick positions |
| `interpolation_search(first, last, value)` | The same for numbers, guessing positions from the values; very fast on evenly spread data |

Each also takes a `comp` as its last argument, except `interpolation_search`.

## Sorting

All sorts are *random access*, put the range in increasing order by `<`, and take an optional `comp`.

| Function | How it sorts |
|---|---|
| `sort(first, last)` | Introsort: quicksort that switches to heap sort if it goes badly, and insertion sort for short stretches. The usual choice |
| `stable_sort(first, last)`, `merge_sort(...)` | Merge sort. Equal elements keep their order |
| `sort_descending(first, last)` | From largest to smallest |
| `sort_by(first, last, key)`, `stable_sort_by(...)` | By `key(element)`, as in `sort_by(b, e, [](const string&: s) -> long int { return s.length(); })` |
| `partial_sort(first, middle, last)` | Only the smallest `middle - first` elements, in order at the front |
| `nth_element(first, nth, last)` | Puts the element that belongs at `nth` there, with smaller ones before it and larger ones after |
| `quick_sort`, `heap_sort`, `insertion_sort`, `shell_sort` | The named algorithm |
| `selection_sort`, `bubble_sort`, `cocktail_sort`, `gnome_sort` | The named algorithm. Slow on large ranges, but simple to follow |
| `counting_sort(first, last)`, `counting_sort(first, last, true)` | Integers or characters, by counting each value; fast when the values are close together. `true` sorts largest first |
| `radix_sort(first, last)` | Integers or characters, digit by digit; fast on large ranges |

```walnut
import module.algorithms

function main() -> int {
    array[6] int: raw = {4, 2, 6, 1, 5, 3};
    int*: start = &raw[0];
    std::algorithms::partial_sort(start, start + 3, start + 6);
    int: three = raw[0] + raw[1] + raw[2];
    std::algorithms::sort_descending(start, start + 6);
    int: top = raw[0];
    return 0;
} end function main
```

Here `three` is `6` (`1 + 2 + 3`) and `top` is `6`.

## Changing a range

| Function | Does |
|---|---|
| `fill(first, last, value)` | Sets every element to `value` |
| `iota(first, last, start)` | Sets the elements to `start`, `start + 1`, `start + 2` and so on |
| `generate(first, last, gen)` | Sets each element to `gen()` |
| `transform_in_place(first, last, op)` | Replaces each element `x` with `op(x)` |
| `replace(first, last, old_value, new_value)`, `replace_if(first, last, pred, new_value)` | Replaces matches; gives how many |
| `remove(first, last, value)`, `remove_if(first, last, pred)` | Moves the elements to keep to the front and gives the new end; the container still has to be shrunk, as the [collection methods](collections.md#searching-sorting-and-more) do for you |
| `unique(first, last)`, `unique(first, last, same)` | Removes repeats that are next to each other, the same way, and gives the new end |
| `reverse(first, last)` | Reverses the order |
| `rotate(first, middle, last)` | Moves `middle` to the front, keeping the order; gives where `first`'s element ended up |
| `rotate_left(first, last, n)`, `rotate_right(first, last, n)` | Rotates by `n` places (`1` if left out) |
| `shuffle(first, last)` | Puts the elements in a random order, using [`random`](random.md) |
| `partition(first, last, pred)`, `stable_partition(...)` | Moves the elements passing `pred` to the front and gives where they stop; the stable form keeps the order on both sides |
| `swap_ranges(first, last, other)` | Swaps the elements of two ranges |

## Copying, merging and sets

`out` is an iterator to write to, with room for everything written; each function gives back the position after the last element it wrote.

| Function | Does |
|---|---|
| `copy(first, last, out)`, `copy_n(first, n, out)`, `copy_if(first, last, out, pred)` | Copies |
| `move_to(first, last, out)` | Moves |
| `transform(first, last, out, op)` | Writes `op(x)` for each element |
| `prefix_sum(first, last, out)` | Writes the running totals |
| `merge(first, last, o_first, o_last, out)` | Merges two sorted ranges into one sorted range |
| `inplace_merge(first, middle, last)` | Merges the sorted halves `first, middle` and `middle, last` |
| `set_union(...)`, `set_intersection(...)`, `set_difference(...)` | The union, intersection or difference of two sorted ranges, each written once |
| `includes(first, last, o_first, o_last)` | Whether every element of the second sorted range is in the first |

`std::algorithms::Range<It>` is a pair of iterators with `first`, `last`, `begin()`, `end()` and `empty()`, so `for (int: x in r)` visits it. `equal_range` and `minmax_element` give one.

## Counting and adding up

| Function | Gives |
|---|---|
| `count(first, last, value)`, `count_if(first, last, pred)` | How many match |
| `accumulate(first, last, init)` | `init` plus every element, in order |
| `accumulate(first, last, init, op)` | `op(op(op(init, a), b), c)` and so on |
| `reduce(...)` | The same as `accumulate` |
| `for_each(first, last, func)`, `for_each_indexed(first, last, func)` | Calls `func(x)`, or `func(index, x)`, on each element |

## Heaps

A heap is a range arranged so that the largest element (by `<`, or by `comp`) is first and the rest can be kept that way cheaply. These are *random access*.

| Function | Does |
|---|---|
| `make_heap(first, last)` | Arranges the range as a heap |
| `push_heap(first, last)` | Adds the element at `last - 1` to the heap before it |
| `pop_heap(first, last)` | Moves the largest element to `last - 1`, keeping the rest a heap |
| `sort_heap(first, last)` | Sorts a heap |
| `is_heap(first, last)` | Whether the range is a heap |

## Whole containers

For a container `c` with `begin()` and `end()`, these take `c` itself instead of two iterators: `sort(c)`, `sort(c, comp)`, `stable_sort(c)`, `stable_sort(c, comp)`, `reverse(c)`, `count(c, value)`, `count_if(c, pred)`, `contains(c, value)`, `all_of(c, pred)`, `any_of(c, pred)`, `none_of(c, pred)`, `accumulate(c, init)`, `binary_search(c, value)`, `is_sorted(c)` and `for_each(c, func)`. They are chosen when the argument satisfies [`std::concepts::iterable`](core.md#stdconcepts).

## Your own containers

A class works with the algorithms when its iterators have the operators they use. A forward iterator needs `*it`, `++it` and `!=` (and `==` for a few); a random access one also needs `it + n`, `it - n`, `it - other` (a `long int`) and `it[k]`. The simplest way is to hand out pointers, as `Vector` does:

```walnut
import module.algorithms

class Scores {
public:
    array[5] int: items = {40, 10, 50, 20, 30};

    function begin() -> int* {
        return &items[0];
    } end function begin

    function end() -> int* {
        return &items[0] + 5;
    } end function end
} end class Scores

function main() -> int {
    Scores: s;
    std::algorithms::sort(s);
    int: lowest = s.items[0];
    bool: found = std::algorithms::binary_search(s, 30);
    return 0;
} end function main
```

Here `lowest` is `10` and `found` is `true`, and `for (int: x in s)` works on `Scores` too. An iterator class works the same way once it has the operators; see [Looping over your own classes](../../documentation.md#looping-over-your-own-classes).
