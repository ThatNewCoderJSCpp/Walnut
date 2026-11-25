# cache

`cache` keeps values that are expensive to make, and makes each one only when it's first needed. It works for any value: numbers, parsed files, compiled shaders, meshes, or a class of your own.

```walnut
import module.cache
```

Everything is in the namespace `std::cache`.

## A first example

A `LazyCache<K, V>` is given a loader, a function that makes the value for a key. Nothing is loaded until a key is asked for, and after that the same key gives back the kept value:

```walnut
import module.io
import module.cache

int: compiled = 0;

class Shader {
public:
    string: path;
    int: program = 0;
    constructor(const string&: p, int: id) : path = p, program = id { }
} end class Shader

function compile(const string&: path) -> Shader {
    compiled++;
    return Shader(path, compiled);
} end function compile

function main() -> int {
    std::cache::LazyCache<string, Shader>: shaders(compile);
    std::io::println(shaders.size());
    Shader&: lit = shaders.get("lit.frag");
    Shader&: same = shaders.get("lit.frag");
    std::io::println(lit.program, " ", same.program, " ", compiled);
    shaders.get("lit.frag", 2);
    std::io::println(shaders.get("lit.frag", 2).program, " ", compiled);
    shaders.invalidate_all();
    std::io::println(shaders["lit.frag"].program, " ", compiled);
    std::io::println(shaders.hits(), " hits, ", shaders.misses(), " misses");
    return 0;
} end function main
```

This prints:

```
0
1 1 1
2 2
3 3
2 hits, 3 misses
```

`compile` ran three times: once for the first `get`, once when the version changed to `2`, and once after `invalidate_all()`. The other two `get`s were hits.

## LazyCache

`LazyCache<K, V>(load)` makes values with `load(key)`, a `function(const K&) -> V`. The key type needs `==`; the value type can be anything the loader can return, including a type that can't be copied.

| Method | Does |
|---|---|
| `LazyCache<K, V>(load)` | A cache with no size limit |
| `LazyCache<K, V>(load, capacity)` | Keeps at most `capacity` values (`0` or less means no limit) |
| `LazyCache<K, V>(load, capacity, eviction)` | The same, with an [`Eviction`](#eviction) policy |
| `get(key)`, `cache[key]`, `cache(key)` | The value, made by the loader if it's missing or out of date |
| `get(key, version)` | The same, but the value is made again whenever `version` differs from the one it was made with |
| `peek(key)` | A pointer to an up-to-date value, or `nullptr`; never loads |
| `contains(key)` | Whether an up-to-date value is kept |
| `prefetch(key)`, `prefetch(keys)` | Make values now, such as during a loading screen, so later `get`s are hits |
| `put(key, value)` | Keep a value made some other way (the value type has to be copyable) |
| `invalidate(key)` | Mark one value out of date; the next `get` makes it again |
| `invalidate_all()` | Mark every value out of date, in constant time |
| `remove(key)`, `clear()` | Forget one value, or all of them |
| `pin(key)`, `unpin(key)`, `is_pinned(key)` | A pinned value is loaded if needed and is never evicted |
| `size()`, `empty()`, `capacity()`, `set_capacity(n)`, `policy()` | Size and limits; lowering the capacity evicts right away |
| `hits()`, `misses()`, `loads()`, `rebuilds()`, `evictions()`, `hit_rate()` | Counts since the cache was made or `reset_stats()` |
| `load_seconds()`, `saved_seconds()` | Time spent in the loader, and the loading time the hits saved |

`get` gives a reference to the kept value. Each value has its own place in memory, so the reference stays good while other keys are added; it ends when that value is evicted, removed, cleared, or made again after `invalidate`.

`get(key, version)` is for values that come from something that changes: pass a file's modification time, an edit counter, or a settings revision, and the value is rebuilt only when that number moves.

## What it does to be fast

- Nothing is made until it's asked for, and `invalidate` and `invalidate_all` don't make anything either: an out-of-date value is only rebuilt if it's asked for again, and its memory is reused.
- Keys that [`hash_of`](collections.md#hashing) works for are found in constant time in an open-addressing table. Each entry keeps its key's hash, so `==` is only called when the hashes match, which matters for long strings and big keys. Other keys with `==` are found by looking through the entries, so give a big key type a `hash()` method.
- Asking for the same key again checks the last key first, without hashing.
- Values are never copied once they're made: `get` returns a reference.
- The loader is timed, so `cheapest_to_rebuild` knows which values are worth keeping.

## Eviction

When a cache with a capacity is full, adding a value evicts one that isn't pinned. `Eviction` picks which:

| `std::cache::Eviction::` | Evicts |
|---|---|
| `least_recently_used` (the default) | The value that was used longest ago |
| `cheapest_to_rebuild` | The value that's quickest to make again and least used, weighed by how long its loader took and how often it was hit, while older choices fade out (GreedyDual) |
| `oldest_first` | The value that was made first |

```walnut
import module.io
import module.cache

function slow_square(const long int&: n) -> long int {
    return n * n;
} end function slow_square

function main() -> int {
    std::cache::LazyCache<long int, long int>: squares(slow_square, 2);
    squares.get(1);
    squares.get(2);
    squares.get(1);
    squares.get(3);
    std::io::println(squares.contains(1), " ", squares.contains(2), " ", squares.evictions());

    squares.pin(10);
    squares.get(4);
    squares.get(5);
    std::io::println(squares.contains(10), " ", squares.size());

    std::cache::Lazy<string>: banner([]() -> string { return "built once"; });
    std::io::println(banner.ready());
    std::io::println(banner.get(), " / ", *banner, " / ", banner.builds());
    return 0;
} end function main
```

This prints:

```
1 0 1
1 2
0
built once / built once / 1
```

`get(1)` made `1` the most recently used, so `get(3)` evicted `2`. The pinned `10` stayed while `4` and `5` were added.

## Lazy

`Lazy<T>(make)` is one value made by `make()` the first time it's needed.

| Method | Does |
|---|---|
| `get()`, `*lazy` | The value, made on the first call |
| `ready()` | Whether it has been made |
| `peek()` | A pointer to the value, or `nullptr` if it hasn't been made |
| `reset()` | Forget the value, so the next `get()` makes it again |
| `set(value)` | Replace the value (`T` has to be copyable) |
| `builds()`, `build_seconds()` | How many times `make` ran, and how long the last run took |

For a value that belongs to an object and depends on its fields, a [`lazy` field](../../documentation.md#lazy-fields) does the same job with less code.

## Notes

- `LazyCache` and `Lazy` can't be copied, since they own their values. Pass them by reference.
- They aren't safe to use from several threads at once; guard them with a `std::threading::Mutex`.
- If the loader throws, nothing is kept and the exception reaches the caller; the next `get` tries again.
- `std::collections::Cache` is the simpler choice when values are put in by hand and only a size limit is needed.
