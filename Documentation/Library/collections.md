# collections

`collections` has the containers most programs need: growable arrays, linked lists, double-ended queues, stacks, queues, priority queues, hash maps and sets, sorted maps and sets, pairs and tuples, and views over memory.

`Vector` is C++'s `std::vector`, and `DynamicArray` and `SmallVector` are Fizmo's arrays of the same names, used from Walnut as [C++ classes](../../documentation.md#using-c-classes). The other containers are written in Walnut.

```walnut
import module.collections
```

Everything in `collections` is in the namespace `std::collections`. `import module.collections as col` makes `col::` a shorter name for it, and `using std::collections::Vector;` brings in one name.

## Contents

1. [A first example](#a-first-example)
2. [Choosing a container](#choosing-a-container)
3. [What every container has](#what-every-container-has)
4. [Vector](#vector)
5. [DynamicArray](#dynamicarray)
6. [SmallVector](#smallvector)
7. [Span](#span)
8. [LinkedList](#linkedlist)
9. [Deque](#deque)
10. [Stack and Queue](#stack-and-queue)
11. [PriorityQueue](#priorityqueue)
12. [UnorderedMap](#unorderedmap)
13. [UnorderedSet](#unorderedset)
14. [Map and Set](#map-and-set)
15. [Pair](#pair)
16. [Tuple](#tuple)
17. [Searching, sorting and more](#searching-sorting-and-more)
18. [WhereQuery](#wherequery)
19. [Hashing](#hashing)
20. [Errors](#errors)
21. [What the element type needs](#what-the-element-type-needs)

## A first example

```walnut
import module.collections
import module.io

using std::collections::Vector;
using std::collections::UnorderedMap;

function main() -> int {
    Vector<string>: words{"pear", "fig", "pear", "apple"};
    words.push_back("fig");

    UnorderedMap<string, int>: counts;
    for (const string&: w in words) {
        counts[w] += 1;
    }

    words.sort();
    for (const string&: w in words) {
        std::io::print(w, " ");
    }
    std::io::println();
    std::io::println("pears: ", counts["pear"], ", kinds: ", counts.size());
    return 0;
} end function main
```

This prints `apple fig fig pear pear` and `pears: 2, kinds: 3`.

## Choosing a container

| Container | Is | Good at |
|---|---|---|
| `Vector<T>` | A growable array (C++'s `std::vector`) | The default choice: fast indexing, fast adding and removing at the end |
| `DynamicArray<T>` | Fizmo's growable array | Like `Vector`, plus adding and removing at the front |
| `SmallVector<T, N>` | Fizmo's array that keeps its first `N` elements inside itself | Many small lists, without allocating memory for each |
| `Span<T>` | A view of elements stored somewhere else | Passing part or all of an array to a function without copying |
| `LinkedList<T>` | A doubly linked list | Adding and removing at both ends; elements never move |
| `Deque<T>` | A ring buffer | Adding and removing at both ends with fast indexing |
| `Stack<T>` | Last in, first out | `push`, `pop`, `top` |
| `Queue<T>` | First in, first out | `push`, `pop`, `front` |
| `PriorityQueue<T>` | A binary heap | Always taking the largest (or smallest) element next |
| `UnorderedMap<K, V>` | A hash table | Looking up a value by key in constant time |
| `UnorderedSet<T>` | A hash table of keys only | Fast membership tests |
| `Map<K, V>` | A red-black tree of keys and values | Visiting keys in order; finding the nearest key |
| `Set<T>` | A red-black tree of values | Visiting values in order; the `i`th smallest value |
| `Pair<A, B>` | Two values | Returning or storing two things together |
| `Tuple<Ts...>` | Any number of values, each of its own type | Returning or storing several things together |

## What every container has

- `size()` gives the number of elements as a `long int`, and `empty()` is `true` when there are none.
- `clear()` removes every element (not on `Span`, which doesn't own its elements).
- `for (T: x in c)` visits every element in order. Containers that hand out references (`Vector`, `DynamicArray`, `SmallVector`, `Span`, `LinkedList`, `Deque`, `UnorderedMap`, `Map`) let the loop change elements with `T&: x`. See [Looping over your own classes](../../documentation.md#looping-over-your-own-classes) for how this works.
- Containers that own their elements copy deeply: `Vector<int>: b = a;` makes an independent copy. Moving one with `std::core::move` takes over its memory and leaves the source empty.
- Indexes are `long int`, start at `0`, and are checked: an index out of range throws a [`std::errors::collection_error`](#errors), and so does taking an element from an empty container.
- `find(value)` gives the index of the first match, or `undefined` when there is none (check with `i is undefined`). `contains(value)` gives a `bool`.

## Vector

```walnut
import module.collections

function main() -> int {
    std::collections::Vector<int>: v{5, 3, 9};
    v.push_back(1);
    v.insert(0, 7);
    int: last = v.pop_back();
    v.sort();
    long int: where = v.find(9);
    bool: has = v.contains(4);
    auto: zeros = std::collections::Vector<int>::filled(3, 0);
    return 0;
} end function main
```

Here `last` is `1`, `v` holds `3 5 7 9`, `where` is `3`, `has` is `false` and `zeros` holds three `0`s.

A `Vector<T>` is a C++ `std::vector`, so it grows and moves its elements the same way. The one difference is `Vector<bool>`, which stores one byte per element instead of packing bits like C++'s `std::vector<bool>`, so `v[i]` gives a real `bool&`.

| Method | Does |
|---|---|
| `Vector<T>: v;`, `Vector<T>: v{a, b, c};` | An empty vector, or one holding the given values |
| `Vector<T>::filled(n, value)` | `n` copies of `value` |
| `v[i]`, `at(i)` | The element at `i` |
| `front()`, `back()` | The first and last elements |
| `push_back(value)`, `pop_back()` | Add at the end, or remove the last element and give it back |
| `insert(i, value)`, `erase(i)` | Add before position `i`, or remove the element at `i` and give it back |
| `remove(value)` | Remove every element equal to `value`; gives how many were removed |
| `resize(n)`, `resize(n, value)` | Grow (filling with `T()` or `value`) or shrink to `n` elements |
| `reserve(n)`, `capacity()`, `shrink_to_fit()` | Control how much memory is set aside |
| `extend(other)` | Append all of another vector |
| `reverse()`, `sort()` | Reverse, or sort from smallest to largest with `<` |
| `swap(other)` | Exchange contents with another vector |
| `data()`, `begin()`, `end()` | Pointers to the elements |
| `==`, `!=` | Compare element by element |

## DynamicArray

`DynamicArray<T>` is Fizmo's `fizmo::collections::DynamicArray`. It grows like a `Vector`, and also adds and removes at the front.

```walnut
import module.collections

function main() -> int {
    long int: n = 4;
    std::collections::DynamicArray<int>: grid(n, -1);
    grid[2] = 5;
    grid.push_front(9);
    int: first = grid.pop_front();
    auto: fixed = std::collections::DynamicArray<string>::of("a", "b");
    grid.resize(6);
    return 0;
} end function main
```

Here `first` is `9` and `grid` ends as `-1 -1 5 -1 0 0`.

- `DynamicArray<T>: a(n)` makes `n` elements of `T()`, and `a(n, value)` makes `n` copies of `value`. `DynamicArray<T>::of(a, b, c)` makes one from values.
- It has the methods of [`Vector`](#vector) except `filled` and `extend`, plus `push_front(value)`, `pop_front()` and `fill(value)`.
- `as_span()` gives a [`Span`](#span) over its elements.

## SmallVector

`SmallVector<T, N>` is Fizmo's `fizmo::collections::SmallVector`. It works like `Vector<T>`, but its first `N` elements live inside the object itself, so a short list needs no memory allocation. Past `N`, it moves everything to the heap and grows like a `Vector`. `N` is `8` when it's left out.

```walnut
import module.collections

function main() -> int {
    std::collections::SmallVector<int, 4>: s{1, 2, 3};
    bool: before = s.is_inline();
    s.push_back(4);
    s.push_back(5);
    bool: after = s.is_inline();
    return 0;
} end function main
```

Here `before` is `true` and `after` is `false`.

It has the methods of [`Vector`](#vector) except `filled` and `extend`, plus `is_inline()`, which is `true` while the elements still fit inside the object.

## Span

A `Span<T>` is a pointer and a length: it looks at elements that belong to something else, and copying it doesn't copy them. Changing an element through a span changes the original. The span must not outlive the elements it looks at.

```walnut
import module.collections

function total(std::collections::Span<int>: s) -> int {
    int: t = 0;
    for (int: x in s) {
        t += x;
    }
    return t;
} end function total

function main() -> int {
    std::collections::Vector<int>: v{4, 1, 3, 2};
    std::collections::Span<int>: all(v);
    int: everything = total(all);
    int: middle = total(all.subspan(1, 2));
    all.sort();
    return 0;
} end function main
```

Here `everything` is `10`, `middle` is `4`, and sorting the span sorts `v`.

- `Span<T>(pointer, count)` or `Span<T>(vector)`.
- `subspan(offset, count)`, `first(n)` and `last(n)` give smaller views.
- `[]`, `at`, `front`, `back`, `data`, `find`, `contains`, `fill(value)` and `sort()`.

## LinkedList

```walnut
import module.collections

function main() -> int {
    std::collections::LinkedList<string>: list{"b", "c"};
    list.push_front("a");
    list.push_back("d");
    string: first = list.pop_front();
    list.reverse();
    return 0;
} end function main
```

Here `first` is `"a"` and `list` ends as `d c b`.

- `push_front`, `push_back`, `pop_front` and `pop_back` take constant time.
- `insert(i, value)`, `erase(i)`, `list[i]` and `at(i)` walk to position `i`, so they take time proportional to `i`.
- `remove(value)`, `find`, `contains`, `reverse()`, `front()` and `back()`.

## Deque

A `Deque<T>` adds and removes at both ends in constant time, and also indexes in constant time.

```walnut
import module.collections

function main() -> int {
    std::collections::Deque<int>: d{2, 3};
    d.push_front(1);
    d.push_back(4);
    int: second = d[1];
    int: front = d.pop_front();
    return 0;
} end function main
```

Here `second` is `2` and `front` is `1`. It has `push_front`, `push_back`, `pop_front`, `pop_back`, `front`, `back`, `[]`, `at`, `find`, `contains` and `clear`.

## Stack and Queue

```walnut
import module.collections

function main() -> int {
    std::collections::Stack<int>: stack;
    stack.push(1);
    stack.push(2);
    int: top = stack.pop();

    std::collections::Queue<string>: queue;
    queue.push("first");
    queue.push("second");
    string: next = queue.pop();
    return 0;
} end function main
```

Here `top` is `2` and `next` is `"first"`.

- `Stack<T>`: `push`, `pop` (removes and gives back the top), `top`, `size`, `empty`, `clear`.
- `Queue<T>`: `push` (at the back), `pop` (removes and gives back the front), `front`, `back`, `size`, `empty`, `clear`.

## PriorityQueue

`PriorityQueue<T>` always gives back its largest element first, using `<`. `PriorityQueue<T, false>` gives back the smallest first.

```walnut
import module.collections

function main() -> int {
    std::collections::PriorityQueue<int>: high{5, 1, 9};
    std::collections::PriorityQueue<int, false>: low{5, 1, 9};
    int: biggest = high.pop();
    int: smallest = low.pop();
    return 0;
} end function main
```

Here `biggest` is `9` and `smallest` is `1`. It has `push`, `pop`, `top`, `size`, `empty` and `clear`. Adding and removing take time proportional to the logarithm of the size.

## UnorderedMap

`UnorderedMap<K, V>` is a hash table from keys to values. Keys are found with [`hash_of`](#hashing) and `==`.

```walnut
import module.collections

function main() -> int {
    std::collections::UnorderedMap<string, int>: ages;
    ages["ann"] = 31;
    ages.set("bob", 25);
    ages["ann"] += 1;
    bool: added = ages.insert("bob", 99);
    int: missing = ages.get("zed", -1);
    int*: found = ages.find("bob");
    int: total = 0;
    for (auto: [name, age] in ages) {
        total += age;
    }
    ages.remove("bob");
    return 0;
} end function main
```

Here `ages["ann"]` is `32`, `added` is `false` (the key was already there, so nothing changed), `missing` is `-1`, `*found` is `25` and `total` is `57`.

| Method | Does |
|---|---|
| `m[key]` | The value for `key`, adding `V()` first if the key is new |
| `at(key)` | The value for `key`; throws a `std::errors::collection_error` if it's missing |
| `get(key, fallback)` | The value for `key`, or `fallback` if it's missing |
| `find(key)` | A pointer to the value, or `nullptr` if it's missing |
| `set(key, value)` | Add the key or replace its value |
| `insert(key, value)` | Add the key only if it's new; `true` if it was added |
| `contains(key)`, `remove(key)` | Check for, or remove, a key |
| `keys()`, `values()` | All keys or all values, as a `Vector` |
| `reserve(n)` | Make room for `n` keys without growing again |

- Looping over a map gives a [`Pair<K, V>&`](#pair) for each key, in no particular order. Change values through it freely, but don't change keys that way.
- Adding keys can move entries around, so a pointer from `find` or a reference from `m[key]` is only good until the next key is added.

## UnorderedSet

```walnut
import module.collections

function main() -> int {
    std::collections::UnorderedSet<int>: seen{3, 1, 3, 2};
    bool: fresh = seen.insert(5);
    bool: again = seen.insert(3);
    seen.remove(1);
    return 0;
} end function main
```

Here `seen` holds `3`, `2` and `5`, `fresh` is `true` and `again` is `false`. It has `insert`, `contains`, `remove`, `clear` and `to_vector()`, and loops visit its values in no particular order.

## Map and Set

`Map<K, V>` and `Set<T>` keep their keys sorted with `<`, like C++'s `std::map` and `std::set`, and loops visit them from smallest to largest. They are red-black trees: finding, adding and removing a key each take time proportional to the logarithm of the size, however large the map grows. Each key lives in its own node, so adding and removing keys never moves the others: a pointer from `find` or a reference from `m[key]` stays good until that key is removed.

```walnut
import module.collections
import module.io

function main() -> int {
    std::collections::Map<string, int>: stock;
    stock["pear"] = 3;
    stock["apple"] = 1;
    stock["fig"] = 2;
    for (auto: [name, count] in stock) {
        std::io::print(name, "=", count, " ");
    }
    std::io::println();

    std::collections::Set<int>: s{5, 1, 4, 1};
    int: smallest = s[0];
    long int: place = s.index_of(5);
    int: after = *s.upper_bound(1);
    return 0;
} end function main
```

This prints `apple=1 fig=2 pear=3`. `smallest` is `1`, `place` is `2` (`5` is the third smallest) and `after` is `4`.

- `Map` has the same methods as `UnorderedMap`, plus `first()` and `last()` for the entries with the smallest and largest keys, and `entry_at(i)` for the entry with the `i`th smallest key.
- `Set` has `insert`, `contains`, `remove`, `clear`, `to_vector()`, `first()`, `last()`, `s[i]` for the `i`th smallest value, and `index_of(value)` for where a value is in that order (`undefined` if it's missing). `s[i]` and `index_of` also take logarithmic time.
- Both have `lower_bound(key)`, an iterator to the first key that isn't less than `key`, and `upper_bound(key)`, the first key greater than `key`. Either one equals `end()` when there is no such key; `*it` gives the entry, and `++it` moves to the next one.

**Choosing between a tree and a hash table.** `UnorderedMap` finds a key in constant time and is usually the fastest map when the order of the keys doesn't matter. Use `Map` when you need the keys in order, the smallest or largest key, or the nearest key to a value. Compared with keeping the keys in a sorted array, the tree is much faster at adding and removing keys once there are more than a few hundred of them, and about as fast at finding them, but looping over it is slower, because each step follows a pointer to a different node.

## Pair

`Pair<A, B>` holds `first` and `second`. It has `==` and `!=`, and works with structured bindings: `auto: [a, b] = p;`.

```walnut
import module.collections

function main() -> int {
    std::collections::Pair<string, int>: p("age", 30);
    auto: [label, value] = p;
    return 0;
} end function main
```

## Tuple

`Tuple<Ts...>` holds one value of each of its types, in order, in `items`. `items[0]` is the first value, `items[1]` the second, and so on; the index has to be a constant, because each value can have a different type. `size()` (or `countof(t.items)`) is the number of values.

```walnut
import module.collections
import module.io

using std::collections::Tuple;
using std::collections::make_tuple;

function main() -> int {
    Tuple<string, int, bool>: t{"age", 30, true};
    t.items[1] += 1;
    auto: [label, value, flag] = t;
    auto: u = make_tuple("age", 31, true);
    std::io::println(label, " ", t.items[1], " ", t.size(), " ", t == u);
    return 0;
} end function main
```

This prints `age 31 3 1`.

- A tuple is made with braces, `Tuple<A, B>: t{a, b};`, or with `make_tuple(a, b)`, which works out the types from the values.
- It has `==` and `!=`, and works with structured bindings: `auto: [a, b, c] = t;`.
- `Tuple<>` is allowed and holds nothing.
- A function can take any tuple with `template <typename... Ts> function f(const Tuple<Ts...>&: t)`.

## Searching, sorting and more

`Vector`, `DynamicArray` and `SmallVector` have these methods on top of their own. They are built on the [algorithms](algorithms.md) library, which has the same operations for any range. A `comp` takes two elements and is `true` when the first goes before the second, a `pred` takes one element and gives a `bool`, and a `key` turns an element into something to sort by.

```walnut
import module.collections

function main() -> int {
    std::collections::Vector<int>: v{8, 3, 5, 3, 9, 1};
    v.sort([](int: a, int: b) -> bool { return a > b; });
    v.sort();
    long int: slot = v.insert_sorted(4);
    long int: big = v.count_if([](int: x) -> bool { return x > 4; });
    long int: dropped = v.remove_if([](int: x) -> bool { return x % 2 == 0; });
    long int: repeats = v.unique();
    auto: words = v.map([](int: x) -> string { return std::core::to_string(x); });
    int: total = v.sum();
    return 0;
} end function main
```

Here `slot` is `3`, `big` is `3`, `dropped` is `2` (the `4` and the `8`), `repeats` is `1`, `v` ends as `1 3 5 9`, `words` is a `Vector<string>` holding `"1" "3" "5" "9"`, and `total` is `18`.

**Sorting**

| Method | Does |
|---|---|
| `sort()`, `sort(comp)` | Sorts, smallest first or by `comp` |
| `sort_descending()` | Sorts largest first |
| `sort_by(key)`, `stable_sort_by(key)` | Sorts by `key(element)` |
| `stable_sort()`, `stable_sort(comp)` | Sorts, keeping equal elements in their order |
| `heap_sort()`, `quick_sort()`, `merge_sort()`, `insertion_sort()`, `selection_sort()`, `bubble_sort()`, `shell_sort()` | Sorts with the named algorithm |
| `counting_sort()`, `counting_sort(true)`, `radix_sort()` | Sorts integers or characters without comparing them; `true` sorts largest first |
| `partial_sort(n)` | Puts the `n` smallest elements, in order, at the front |
| `nth_element(i)` | Puts the element that belongs at `i` there, smaller ones before and larger ones after |
| `is_sorted()`, `is_sorted(comp)` | Whether it is in order |
| `sorted()`, `sorted(comp)`, `reversed()` | A sorted or reversed copy |
| `shuffle()` | Random order |
| `rotate_left(n)`, `rotate_right(n)` | Rotates by `n` places (`1` if left out) |
| `partition(pred)`, `stable_partition(pred)` | Moves the elements passing `pred` to the front; gives how many there are |

**Searching**

| Method | Gives |
|---|---|
| `binary_search(value)` | Whether a sorted container has `value` |
| `lower_bound(value)`, `upper_bound(value)` | The first and last index where `value` could go in a sorted container |
| `binary_find(value)` | The index of `value` in a sorted container, or `-1` |
| `find_if(pred)`, `find_last(value)`, `find_last_if(pred)` | The index of a match, or `-1` |
| `find_all(value)` | A `Vector<long int>` of every index holding `value` |
| `count(value)`, `count_if(pred)` | How many match |
| `contains_if(pred)`, `all_of(pred)`, `any_of(pred)`, `none_of(pred)` | `bool` |
| `min()`, `max()`, `min_index()`, `max_index()` | The smallest or largest element, or its index (`-1` when empty) |
| `starts_with(other)`, `ends_with(other)` | Whether it begins or ends with the elements of another container of the same kind |

**Changing and building**

| Method | Does |
|---|---|
| `insert_sorted(value)`, `insert_sorted(value, comp)` | Inserts into a sorted container, keeping it sorted; gives the index |
| `append(value)`, `prepend(value)` | Adds at the end or the front |
| `append_n(n, value)`, `insert_n(i, n, value)` | Adds `n` copies |
| `insert_all(i, other)` | Inserts all of another container of the same kind before `i` |
| `insert_unordered(i, value)` | Puts `value` at `i` and moves the old element there to the end, which is faster than `insert` |
| `remove_at(i)` | Removes the element at `i` and gives it back (the same as `erase`) |
| `remove_unordered(i)`, `swap_remove(i)` | Removes the element at `i` by moving the last element into its place |
| `remove_first(value)` | Removes the first element equal to `value`; gives whether there was one |
| `remove_if(pred)`, `retain_if(pred)` | Removes the elements that pass, or that fail, `pred`; gives how many were removed |
| `unique()` | Removes repeats that are next to each other; gives how many |
| `deduplicate()` | Removes every repeat, keeping the first of each value; gives how many |
| `distinct()` | A copy with every repeat removed |
| `replace(old_value, new_value)`, `replace_if(pred, value)` | Replaces matches; gives how many |
| `transform(func)` | Replaces each element `x` with `func(x)` |
| `for_each(func)`, `for_each_indexed(func)` | Calls `func(x)`, or `func(index, x)`, for each element; `x` is a reference, so `func` can change it |
| `swap_items(i, j)` | Swaps two elements |
| `filter(pred)` | A copy holding the elements that pass `pred` |
| `map(func)` | A `Vector` of `func(x)` for each element |
| `take(n)`, `skip(n)`, `slice(start, stop)` | A copy of the first `n`, all but the first `n`, or the elements from `start` up to `stop` |
| `sum()` | All the elements added with `+` (`T()` when empty) |
| `accumulate(init, op)` | `op(op(init, a), b)` and so on |
| `where(...)` | A [WhereQuery](#wherequery) over the matching elements |

`LinkedList` and `Deque` have the methods that don't need random access: `count`, `count_if`, `find_if`, `contains_if`, `all_of`, `any_of`, `none_of`, `for_each`, `transform`, `sort`, `stable_sort`, `is_sorted`, `min`, `max`, `to_vector` and `where`.

## WhereQuery

`where(...)` picks out some of a container's elements and gives a `WhereQuery`, which remembers which positions were picked. A query can be narrowed further, read, or used to change or remove just those elements. It is a port of Fizmo's `where` proxy, and works on `Vector`, `DynamicArray`, `SmallVector`, `LinkedList` and `Deque`, and on `string` and `text` through the [strings](strings.md#wherequery-on-strings) library.

```walnut
import module.collections

function main() -> int {
    std::collections::Vector<int>: v{5, 12, 7, 30, 2, 18};
    auto: big = v.where([](int: x) -> bool { return x > 6; });
    long int: how_many = big.count();
    long int: first = big.first_index();
    int: largest = big.max_value();
    big.where([](int: x) -> bool { return x % 2 == 0; }).transform([](int: x) -> int { return x / 2; });
    long int: gone = v.where(2).remove();
    return 0;
} end function main
```

Here `how_many` is `4` (`12 7 30 18`), `first` is `1`, `largest` is `30`, the even ones among them were halved, `gone` is `1`, and `v` ends as `5 6 7 15 9`.

- `v.where(pred)` picks the elements that pass `pred`, and `v.where(value)` those equal to `value`. `v.where_indices(list)` picks given positions, and `v.all()` picks everything.
- A query refers to its container, so it must not outlive it, and adding or removing elements in the container another way leaves the query's positions out of date.

**Narrowing**, each giving a new query: `where(pred)`, `where(value)`, `where_not(pred)`, `where_indexed(pred)` (with `pred(index, x)`), `first(n)`, `take(n)`, `last(n)`, `skip(n)`, `skip_last(n)`, `slice(a, b)` (by position in the query), `range(lo, hi)` (by index in the container), `step(n)`, `even()`, `odd()`, `invert()` (everything not picked), `or_where(pred)`, `union_with(q)`, `intersect_with(q)` and `except(q)`.

**Reading**: `count()`, `size()`, `empty()`, `index(k)` and `indices()` (positions in the container), `first_index()` and `last_index()` (`-1` when empty), `contains_index(i)`, `q[k]`, `at(k)`, `first_value()`, `last_value()`, `values()` (a container of the same kind), `to_vector()`, `count_if(pred)`, `all_of(pred)`, `any_of(pred)`, `none_of(pred)`, `sum()`, `min_index()`, `max_index()`, `min_value()`, `max_value()` and `map(func)`.

**Changing** the picked elements in their places: `for_each(func)`, `for_each_indexed(func)`, `transform(func)`, `replace(value)`, `fill(value)`, `sort()`, `sort(comp)`, `sort_descending()`, `stable_sort()`, `sort_by(key)`, `reverse()`, `rotate_left(n)`, `rotate_right(n)` and `shuffle()`. Sorting a query sorts only the picked elements, among the positions they already have.

**Removing and moving**: `remove()` takes the picked elements out of the container and gives how many; `pop()` and `extract()` do the same and give back a container of them; `copy_to(dest)` and `move_to(dest)` add them to the end of another container; `insert_before(value)` and `insert_after(value)` put `value` next to each one.

## Hashing

`UnorderedMap` and `UnorderedSet` find keys with `std::collections::hash_of(value)`, which gives an `unsigned long int`:

- integers, `bool`, `char` and enums hash their value;
- `string` and `text` hash their characters;
- floats hash their written form;
- pointers hash their address;
- a `Pair` combines its two halves;
- a class or struct uses its own `hash()` method.

So a type of your own can be a key once it has `==` and a `hash()` method:

```walnut
import module.collections

struct Cell {
    int: row;
    int: col;
    operator(==) function(const Cell&: o) const -> bool {
        return row == o.row && col == o.col;
    }
    function hash() const -> unsigned long int {
        return std::collections::hash_of(row) * 31ul + std::collections::hash_of(col);
    } end function hash
} end struct Cell

function main() -> int {
    std::collections::UnorderedMap<Cell, string>: board;
    board[Cell{1, 2}] = "x";
    bool: taken = board.contains(Cell{1, 2});
    return 0;
} end function main
```

Here `taken` is `true`.

## Errors

Mistakes that can only be found while the program runs throw a `std::errors::collection_error`. Its `what()` (or its `message` field) says what happened:

- an index out of range: `index 9 is out of range for a collection of size 3`;
- taking an element from an empty container: `pop() on an empty collection`;
- `at(key)` on a map without that key: `the key isn't in the map`.

```walnut
import module.collections
import module.io

function main() -> int {
    std::collections::Vector<int>: v{1, 2, 3};
    try {
        int: x = v[9];
    } catch (const std::errors::collection_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

## What the element type needs

- Every container needs to be able to create its element type without arguments (`T()`) and to assign it, because it sets aside space for elements before they are filled in.
- `find`, `contains`, `remove` and `==` need `==` on the element type.
- `sort`, `PriorityQueue`, `Map` and `Set` need `<`. `sort` uses C++'s `std::sort` for numbers, `bool`, `char`, enums and strings, and a Walnut introsort, built on the class's own `<`, for classes.
- `UnorderedMap` and `UnorderedSet` need `==` and [`hash_of`](#hashing).

A method that needs an operator is only checked when it's used, so `Vector<Point>` works even if `Point` has no `<`, as long as `sort()` isn't called on it.
