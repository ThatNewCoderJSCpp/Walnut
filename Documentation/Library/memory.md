# memory

`memory` has the smart pointers `UniquePtr`, `SharedPtr` and `WeakPtr`, which own objects made with `new` and delete them when they're no longer used, and `Arena`, a fast allocator for many objects that are thrown away together, such as the nodes of a tree built for one frame or one request. Making an object in an arena is a bump of a pointer, and `reset()` frees everything at once; it's Fizmo's `fizmo::Arena`.

```walnut
import module.memory
```

Everything is in the namespace `std::memory`.

## Contents

1. [Smart pointers](#smart-pointers)
2. [UniquePtr](#uniqueptr)
3. [SharedPtr](#sharedptr)
4. [WeakPtr](#weakptr)
5. [EnableSharedFromThis](#enablesharedfromthis)
6. [Arenas](#arenas)
7. [Tracking memory](#tracking-memory)
8. [Errors](#errors)

## Smart pointers

A smart pointer owns an object and deletes it at the right time, so the program doesn't need `delete`:
- `UniquePtr<T>` is the only owner. It deletes the object when it goes out of scope, and it can't be copied, only moved to a new owner.
- `SharedPtr<T>` shares ownership. Copies point to the same object, which is deleted when the last copy is gone.
- `WeakPtr<T>` watches an object owned by `SharedPtr`s without keeping it alive. It is how two objects can point to each other without keeping each other alive forever.

```walnut
import module.memory
import module.io

using namespace std::memory;

class Shape {
public:
    function name() const virtual -> string {
        return "shape";
    } end function name
} end class Shape

class Circle inherits Shape {
public:
    float: radius = 1.0;
    constructor(float: r) : radius = r { }
    function name() const override -> string {
        return "circle";
    } end function name
} end class Circle

function main() -> int {
    UniquePtr<Circle>: c = make_unique<Circle>(2.0);
    UniquePtr<Shape>: s = std::core::move(c);
    std::io::println(s->name(), " ", c.valid());

    SharedPtr<Circle>: a = make_shared<Circle>(3.0);
    SharedPtr<Shape>: b = a;
    WeakPtr<Circle>: w = a;
    std::io::println(b->name(), " ", a.use_count(), " ", a->radius);
    a.reset();
    b.reset();
    std::io::println(w.expired(), " ", w.lock().valid());
    return 0;
} end function main
```

This prints `circle 0`, `circle 2 3` and `1 0`. Both circles are deleted on the way: the second one by `b.reset()`, and the first one when `s` goes out of scope.

- **Base classes.** A smart pointer to a class converts to a smart pointer to one of its base classes, as above. Deleting through a base class pointer runs the right destructor, because a class with virtual methods has a virtual destructor. `static_pointer_cast<U>(p)` and `dynamic_pointer_cast<U>(p)` convert a `SharedPtr` to a derived class; `dynamic_pointer_cast` checks the object's real type and gives an empty pointer when it doesn't match.
- **Checked.** `*p` and `p->` on an empty smart pointer throw a [`std::errors::pointer_error`](#errors) instead of crashing.
- **Threads.** The counts that `SharedPtr` and `WeakPtr` keep are atomic, so copies of one `SharedPtr` can be made, dropped and locked from several threads at once. The object itself isn't protected; use a [`Mutex`](threading.md) for that.
- **Arrays.** For many objects, use a [`Vector`](collections.md) inside a smart pointer, as in `SharedPtr<Vector<int>>`, or a `Vector` of smart pointers.

## UniquePtr

| Written | Does |
|---|---|
| `make_unique<T>(args...)` | Makes a `T` with `new T(args...)` and gives a `UniquePtr<T>` that owns it |
| `UniquePtr<T>: p;` | An empty pointer |
| `UniquePtr<T>(raw)` | Takes over a `T*` made with `new` |
| `UniquePtr<T>: q = std::core::move(p);`, `q = std::core::move(p);` | Moves ownership to `q`; `p` becomes empty. Copying is an error |
| `UniquePtr<Base>: b = std::core::move(p);` | Moves ownership into a pointer to a base class. A named `UniquePtr` has to be moved; a temporary such as `make_unique<Derived>()` converts on its own |
| `*p`, `p->member`, `p.value()` | The object |
| `get()` | The plain `T*`, or `nullptr`. It still belongs to the `UniquePtr` |
| `release()` | Gives up ownership and returns the `T*`, which the caller must now delete |
| `reset()`, `reset(raw)` | Deletes the object, and takes over `raw` if given |
| `swap(other)` | Swaps the two objects |
| `valid()`, `empty()`, `if (p)` | Whether it points to an object |
| `==` | Whether two pointers point to the same object |

## SharedPtr

| Written | Does |
|---|---|
| `make_shared<T>(args...)` | Makes a `T` with `new T(args...)` and gives a `SharedPtr<T>` that owns it |
| `SharedPtr<T>: p;` | An empty pointer |
| `SharedPtr<T>(raw)` | Takes over a `T*` made with `new` |
| `SharedPtr<T>(std::core::move(unique))` | Takes over the object of a `UniquePtr<T>` |
| `SharedPtr<T>: q = p;` | Another owner of the same object |
| `SharedPtr<Base>: b = p;` | Another owner, as a pointer to a base class |
| `SharedPtr<T>(owner, raw)` | A pointer to `raw` (often a member of the owned object) that keeps `owner`'s object alive |
| `*p`, `p->member`, `p.value()`, `get()` | The object, or the plain `T*` |
| `use_count()` | How many `SharedPtr`s own the object; 0 for an empty pointer |
| `unique()` | Whether this is the only owner |
| `reset()`, `reset(raw)` | Stops owning the object (deleting it if this was the last owner), and takes over `raw` if given |
| `swap(other)`, `valid()`, `empty()`, `if (p)`, `==` | As for `UniquePtr` |
| `same_owner(other)`, `owner_before(other)` | Whether two pointers share ownership of the same object, and an order on owners for sorting |
| `static_pointer_cast<U>(p)`, `dynamic_pointer_cast<U>(p)` | `p` as a `SharedPtr<U>`, sharing ownership with `p` |

## WeakPtr

A `WeakPtr<T>` is made from a `SharedPtr<T>` (`WeakPtr<T>: w = p;`) and doesn't count as an owner. To use the object, `lock()` it, which gives a `SharedPtr<T>` that is empty if the object is already gone. A `WeakPtr` converts to a `WeakPtr` of a base class, like the other pointers.

| Method | Does |
|---|---|
| `lock()` | A `SharedPtr<T>` to the object, or an empty one |
| `expired()` | Whether the object is gone |
| `use_count()` | How many `SharedPtr`s own the object |
| `reset()`, `swap(other)` | Stops watching, or swaps with another `WeakPtr` |

```walnut
import module.memory
import module.io

using namespace std::memory;

class Parent;

class Child {
public:
    WeakPtr<Parent>: parent;
} end class Child

class Parent {
public:
    SharedPtr<Child>: child;
    destructor() {
        std::io::println("parent deleted");
    }
} end class Parent

function main() -> int {
    SharedPtr<Parent>: p = make_shared<Parent>();
    p->child = make_shared<Child>();
    p->child->parent = p;
    std::io::println(p.use_count(), " ", p->child->parent.lock().valid());
    return 0;
} end function main
```

This prints `1 1` and then `parent deleted`. If `parent` were a `SharedPtr`, the parent and child would keep each other alive and neither would be deleted.

## EnableSharedFromThis

A method sometimes needs a `SharedPtr` to its own object, to hand to someone else. A class that inherits `EnableSharedFromThis<Self>` gets `shared_from_this()`, which gives a `SharedPtr` sharing ownership with the ones that already own the object, and `weak_from_this()`, which gives a `WeakPtr`.

```walnut
import module.memory
import module.io

using namespace std::memory;

class Task inherits EnableSharedFromThis<Task> {
public:
    function handle() -> SharedPtr<Task> {
        return shared_from_this();
    } end function handle
} end class Task

function main() -> int {
    SharedPtr<Task>: t = make_shared<Task>();
    SharedPtr<Task>: h = t->handle();
    std::io::println(t.use_count(), " ", h == t);
    return 0;
} end function main
```

This prints `2 1`. Calling `shared_from_this()` on an object that no `SharedPtr` owns, such as a plain `Task: t;`, throws a `pointer_error`.

## Arenas

### A first example

```walnut
import module.memory
import module.io

using std::memory::Arena;
using std::memory::ArenaPtr;
using std::memory::ArenaArray;

struct Node {
    int: value = 0;
    string: name = "";
} end struct Node

function main() -> int {
    Arena: arena;
    ArenaPtr<Node>: first = arena.make(Node{1, "one"});
    ArenaPtr<Node>: second = arena.make<Node>();
    second->value = 2;
    ArenaArray<int>: numbers = arena.make_array<int>(4, 10);
    numbers[1] = 5;
    int: total = 0;
    for (int: n in numbers) {
        total += n;
    }
    std::io::println(first->name, " ", second->value, " ", total, " ", arena.object_count());
    arena.reset();
    std::io::println(first.valid(), " ", first.expired(), " ", arena.object_count());
    return 0;
} end function main
```

This prints `one 2 35 6` and then `0 1 0`.

### Arena

| Written | Does |
|---|---|
| `Arena: a;`, `Arena(block_size)`, `Arena(block_size, max_block_size, thread_safe)` | Makes an arena. It takes memory from the system in blocks, 64 KB at first, doubling up to 16 MB by default. A thread-safe arena can be used from several threads at once |
| `make<T>()`, `make(value)` | Makes an object in the arena, empty or as a copy of `value`, and gives an `ArenaPtr<T>` to it |
| `make_array<T>(count)`, `make_array(count, fill)`, `make_array(vector)` | Makes an array in the arena, and gives an `ArenaArray<T>` |
| `reset()`, `clear()` | Destroys everything made in the arena and frees it for reuse. Every pointer and array from before becomes expired |
| `reserve(bytes)` | Makes sure that much memory is ready, so the next allocations don't need new blocks |
| `shrink_to_fit()` | Gives back blocks that aren't in use |
| `owns(ptr)` | Whether a pointer was made by this arena |
| `bytes_used()`, `bytes_reserved()`, `object_count()`, `block_count()`, `handle_count()` | Statistics: bytes in use and held, objects alive, blocks, and how many `ArenaPtr`s and `ArenaArray`s still refer to the arena |
| `generation()` | How many times the arena has been reset, plus 1 |
| `block_size()`, `max_block_size()`, `thread_safe()` | The settings it was made with |

Copies of an `Arena` variable are the same arena, and `==` checks for that. The memory is freed when the last copy, and the last pointer into it, is gone.

### ArenaPtr and ArenaArray

An `ArenaPtr<T>` points to one object in an arena. `p->field`, `*p` and `p.value()` reach the object. `ArenaPtr` remembers which reset of its arena it belongs to, so after `reset()` it is expired, and using it throws a [`std::errors::arena_error`](#errors) instead of reading freed memory.

| Method | Gives |
|---|---|
| `valid()`, `if (p)` | Whether it points to a live object |
| `expired()` | Whether its arena was reset since it was made |
| `get()` | A plain `T*`, or `nullptr` when it isn't valid |
| `reset()` | Makes it empty |
| `same_arena(other)`, `==` | Whether two pointers come from the same arena, or point to the same object |
| `generation()`, `use_count()` | The arena generation it belongs to, and how many pointers and arrays (plus the arena itself) share the arena's memory |

An `ArenaArray<T>` is a fixed-size array in an arena. It has `size()`, `empty()`, `valid()`, `expired()`, `[]`, `at`, `front`, `back`, `element(i)` (an `ArenaPtr<T>` to one element), `data()` (a `T*`), `to_vector()`, and `begin()`/`end()`, so `for (T: x in array)` works. Indexes are checked: one out of range, or any use after a reset, throws an `arena_error`.

## Tracking memory

These functions let a program look at its own memory while it runs, the way Python's `gc` and `tracemalloc` modules do. They report on objects made with `new` (including the ones smart pointers own), and only have something to report when the program is built with a [safety option](../../documentation.md#safety-checks) that records allocations: `--leak-check`, `--check-bounds`, `--check-null`, `--safe` or `--gc`.

```walnut
import module.io
import module.memory

struct Node {
    int: value;
    Node*: next;
} end struct Node

function main() -> int {
    Node*: a = new Node();
    Node*: b = new Node();
    std::io::println(std::memory::live_objects(), " objects, ", std::memory::live_bytes(), " bytes");
    std::io::print(std::memory::allocation_report());
    delete a;
    delete b;
    std::io::println(std::memory::live_objects(), " left, peak ", std::memory::peak_bytes(), " bytes");
    return 0;
} end function main
```

With `walnut run --leak-check nodes.wal` this prints:

```
2 objects, 64 bytes
2 objects (64 bytes) made with new are alive
  nodes.wal:10: new Node (32 bytes)
  nodes.wal:11: new Node (32 bytes)
0 left, peak 64 bytes
```

| Function | Gives |
|---|---|
| `tracking_allocations()` | Whether this build records allocations, so the functions below have answers |
| `live_objects()`, `live_bytes()` | How many objects made with `new` are alive now, and their size |
| `peak_bytes()` | The most memory from `new` alive at once so far |
| `allocations_made()` | How many times `new` has run so far |
| `allocation_report()` | A `string` listing the live objects grouped by the line that made them, the same as the leak report at the end of a `--leak-check` run; empty when nothing is recorded |
| `gc_running()` | Whether the program was built with `--gc` |
| `collect_garbage()` | Runs the garbage collector now and gives how many objects it freed; `0` without `--gc` |

- When nothing is recorded, the numbers are `-1`.
- With `--gc` alone, `live_objects()` and `live_bytes()` count every block the collector looks after, including ones the library made, and `peak_bytes()` and `allocations_made()` are `-1`; add `--leak-check` for the full picture.
- Objects in an [`Arena`](#arenas) aren't made with `new`, so they aren't counted; the arena's own `bytes_used()` and `object_count()` report them.

## Errors

`std::errors::pointer_error` is thrown for `*`, `->` or `value()` on an empty `UniquePtr` or `SharedPtr`, and for `shared_from_this()` on an object that no `SharedPtr` owns.

`std::errors::arena_error` is thrown for a pointer or array used after its arena was reset, an empty pointer, an index out of range, a negative array size, and a block size under 64 bytes. Its `what()` (or its `message` field) says which.
