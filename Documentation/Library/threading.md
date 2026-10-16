# threading

`threading` runs code on several threads at once and keeps them from getting in each other's way. It is C++'s `<thread>`, `<mutex>`, `<shared_mutex>`, `<condition_variable>` and `<atomic>`, used from Walnut as [C++ classes](../../documentation.md#using-c-classes).

```walnut
import module.threading
```

Everything in `threading` is in the namespace `std::threading`.

## Contents

1. [A first example](#a-first-example)
2. [Thread](#thread)
3. [this_thread](#this_thread)
4. [Mutexes](#mutexes)
5. [Locks](#locks)
6. [ConditionVariable](#conditionvariable)
7. [Atomic](#atomic)
8. [Errors](#errors)
9. [Differences from C++](#differences-from-c)

## A first example

```walnut
import module.threading
import module.collections
import module.io

using std::threading::Thread;
using std::threading::Mutex;
using std::threading::LockGuard;

function main() -> int {
    Mutex: m;
    long int: total = 0;
    std::collections::Vector<Thread>: workers;

    for (int: t = 0; t < 4; t++) {
        workers.push_back(Thread([&]() -> void {
            for (int: i = 0; i < 1000; i++) {
                LockGuard<Mutex>: guard(m);
                total += 1;
            }
        }));
    }

    for (Thread&: w in workers) {
        w.join();
    }
    std::io::println("total: ", total);
    return 0;
} end function main
```

This prints `total: 4000`. Each thread adds 1000, and the `LockGuard` makes sure only one of them changes `total` at a time.

## Thread

A `Thread` starts running a function as soon as it is created. The function takes no arguments and returns nothing; a lambda that captures what it needs with `[&]` or `[x]` is the usual way to give it some.

| Method | Does |
|---|---|
| `Thread(body)` | Starts a new thread running `body` |
| `join()` | Waits for the thread to finish |
| `detach()` | Lets the thread keep running on its own; it can't be joined afterwards |
| `joinable()` | `true` while the thread can still be joined |
| `id()` | A number that identifies the thread (`0` once it has been joined or detached) |

- A `Thread` can't be copied, but it can be moved, so it can be stored in a `Vector<Thread>` with `push_back(Thread(...))`.
- A `Thread` that is still running when it goes out of scope is joined there, so the program waits for it instead of stopping. (C++'s `std::thread` would end the program instead.)
- `std::threading::hardware_threads()` gives the number of threads the computer can run at the same time.
- An exception that escapes a thread's function stops the program with a message, because nothing outside the thread can catch it.

## this_thread

Functions about the thread that calls them, in `std::threading::this_thread`:

| Function | Does |
|---|---|
| `sleep_for(milliseconds)` | Pauses for at least that many milliseconds |
| `sleep_for_micros(microseconds)` | The same, in microseconds |
| `yield()` | Lets other threads run first |
| `id()` | The calling thread's number, the same one `Thread::id()` gives |

## Mutexes

A mutex lets only one thread at a time into the code it protects.

| Class | Is |
|---|---|
| `Mutex` | `lock()`, `unlock()` and `try_lock()` |
| `RecursiveMutex` | A `Mutex` the same thread can lock again; it has to unlock it as many times |
| `TimedMutex` | A `Mutex` with `try_lock_for(milliseconds)`, which gives up after that long |
| `RecursiveTimedMutex` | Both |
| `SharedMutex` | Many readers or one writer: `lock_shared()`, `unlock_shared()` and `try_lock_shared()` as well as the exclusive ones |

`try_lock` and `try_lock_for` give `true` when they got the lock. Mutexes can't be copied or moved.

## Locks

Locks unlock their mutex when they go out of scope, even when an exception leaves the scope early, so a mutex can't stay locked by mistake.

```walnut
import module.threading

using std::threading::SharedMutex;
using std::threading::SharedLock;
using std::threading::UniqueLock;

function main() -> int {
    SharedMutex: m;
    int: value = 1;
    {
        SharedLock<SharedMutex>: reading(m);
        int: copy = value;
    }
    {
        UniqueLock<SharedMutex>: writing(m);
        value = 2;
        writing.unlock();
        bool: held = writing.owns_lock();
    }
    return 0;
} end function main
```

Here `held` is `false`, because the lock was unlocked before the end of its scope.

| Lock | Does |
|---|---|
| `LockGuard<M>: g(m)` | Locks `m` for the rest of the scope |
| `UniqueLock<M>: l(m)` | Locks `m`, and can also `unlock()`, `lock()` again, `try_lock()` and say whether it `owns_lock()`. `UniqueLock<M>: l(m, false)` starts without locking |
| `SharedLock<M>: l(m)` | The same as `UniqueLock`, but takes a `SharedMutex`'s shared (reading) lock |

`M` can be any of the mutex classes. Locks can't be copied.

## ConditionVariable

A `ConditionVariable` lets a thread sleep until another one says something has changed.

```walnut
import module.threading
import module.io

using std::threading::Thread;
using std::threading::Mutex;
using std::threading::UniqueLock;
using std::threading::ConditionVariable;

function main() -> int {
    Mutex: m;
    ConditionVariable: changed;
    bool: ready = false;
    int: result = 0;

    Thread: worker([&]() -> void {
        UniqueLock<Mutex>: lock(m);
        result = 42;
        ready = true;
        lock.unlock();
        changed.notify_one();
    });

    UniqueLock<Mutex>: lock(m);
    changed.wait(lock, [&]() -> bool { return ready; });
    std::io::println("result: ", result);
    lock.unlock();
    worker.join();
    return 0;
} end function main
```

This prints `result: 42`.

| Method | Does |
|---|---|
| `wait(lock)` | Unlocks `lock`'s mutex, sleeps until notified, then locks it again |
| `wait(lock, ready)` | Keeps waiting until `ready()` gives `true`. This is the form to use: a thread can wake up without being notified |
| `wait_for(lock, milliseconds)` | Waits at most that long; `true` if it was notified in time |
| `wait_for(lock, milliseconds, ready)` | `true` if `ready()` became `true` in time |
| `notify_one()`, `notify_all()` | Wakes one waiting thread, or all of them |

The lock is a `UniqueLock<Mutex>`, `UniqueLock<TimedMutex>` or `UniqueLock<SharedMutex>`, or a `SharedLock<SharedMutex>`, and it must hold its mutex when `wait` is called.

## Atomic

`Atomic<T>` holds a value that several threads can read and change at once without a mutex. It works like C++'s `std::atomic`.

```walnut
import module.threading

function main() -> int {
    std::threading::Atomic<long int>: count;
    ++count;
    count += 5;
    long int: before = count.fetch_add(10);
    long int: expected = 16;
    bool: swapped = count.compare_exchange(expected, 100);
    long int: now = count.load();
    return 0;
} end function main
```

Here `before` is `6`, `swapped` is `true` and `now` is `100`.

**What `T` can be.** Any type that can be copied:
- Integers of up to 64 bits, `bool`, `char`, enums and pointers use the processor's own atomic instructions, so they never wait for a lock (`is_lock_free()` is `true`).
- Everything else, such as `float` and the other float types, wider integers, `string` and your own classes and structs, is kept behind a small spin lock inside the `Atomic`. Each operation is still a single indivisible step, but `is_lock_free()` is `false`.

| Method | Does |
|---|---|
| `load()`, `store(value)` | Read or write the value |
| `exchange(value)` | Write `value` and give back the old one |
| `compare_exchange(expected, desired)` | If the value equals `expected`, replace it with `desired` and give `true`; otherwise copy the current value into `expected` and give `false` |
| `compare_exchange_weak(expected, desired)` | The same, but it may fail even when the value matches, which can be faster in a retry loop |
| `fetch_add`, `fetch_sub` | Add or subtract and give back the old value. For numbers and pointers; a pointer moves by whole elements, so `p.fetch_add(1)` steps to the next element |
| `fetch_and`, `fetch_or`, `fetch_xor` | Combine bits and give back the old value. For integers |
| `fetch_min`, `fetch_max` | Keep the smaller or larger of the value and the argument, and give back the old value. For numbers and pointers |
| `wait(old)` | Sleep until the value is no longer `old` and another thread calls `notify_one()` or `notify_all()` |
| `notify_one()`, `notify_all()` | Wake threads that are waiting in `wait` |
| `is_lock_free()` | Whether the `Atomic` uses atomic instructions rather than a lock |
| `++a`, `--a`, `a += n`, `a -= n`, `a &= n`, `a \|= n`, `a ^= n` | Change the value and give back the new one |
| `a++`, `a--` | Change the value and give back the old one |
| `a = value` | The same as `store(value)` |
| `T: x = a;` | The same as `load()` |

- `compare_exchange`, `compare_exchange_weak` and `wait` compare with `==`, so `T` must have one; for a struct, the automatic `==` compares the fields.
- Using an operation the type doesn't support, such as `fetch_add` on an `Atomic<string>`, is a compile error that names it.
- Integers of up to 64 bits wrap around on overflow, as in C++, instead of becoming `inf`.
- An `Atomic` can't be copied.

**Memory orders.** Every method that reads or writes takes an optional last argument, a `MemoryOrder`: `relaxed`, `acquire`, `release`, `acq_rel` or `seq_cst`. It is `seq_cst` when left out, which is the safe choice. A weaker order can be faster when you know which ordering the program needs, such as `count.fetch_add(1, MemoryOrder::relaxed)` for a counter that is only read at the end. An order that doesn't apply to an operation, such as `release` on a `load`, is treated as `seq_cst`. The operators always use `seq_cst`.

**Waiting.** `wait` and `notify` let threads sleep instead of spinning:

```walnut
import module.threading
import module.collections
import module.io

using namespace std::threading;

function main() -> int {
    Atomic<bool>: go(false);
    Atomic<int>: done;
    std::collections::Vector<Thread>: workers;
    for (int: i = 0; i < 3; i++) {
        workers.push_back(Thread([&]() -> void {
            go.wait(false);
            done += 1;
        }));
    }
    go.store(true);
    go.notify_all();
    for (Thread&: t in workers) {
        t.join();
    }
    std::io::println(done.load());
    return 0;
} end function main
```

This prints `3`.

### AtomicFlag

`AtomicFlag` is a single atomic `bool` that starts out `false`, for simple locks and one-time signals.

| Method | Does |
|---|---|
| `test_and_set()` | Set the flag and give back what it was |
| `clear()` | Set it back to `false` |
| `test()` | Read it |
| `wait(old)`, `notify_one()`, `notify_all()` | As for `Atomic` |

Each also takes an optional `MemoryOrder`.

## Errors

Misusing a thread or a lock stops the program with a runtime error, like an array index out of range:

- `join()` or `detach()` on a thread that isn't running, or a thread joining itself;
- `unlock()` on a lock that doesn't hold its mutex, or `lock()` on one that already does;
- `wait()` with a lock that doesn't hold its mutex.

## Differences from C++

- A `Thread` joins itself when it goes out of scope instead of ending the program, like C++20's `std::jthread`.
- Thread ids are numbers (`unsigned long int`), not a separate id type.
- Times are whole milliseconds (or microseconds for `sleep_for_micros`) instead of `std::chrono` durations.
- `Atomic` takes any copyable type, not only trivially copyable ones; types without lock-free instructions use a spin lock inside the `Atomic` instead of a hidden global lock. `fetch_min` and `fetch_max` are there for all numbers and pointers, as in C++26, and `fetch_add` works on floats, as in C++20. There is no `atomic_ref`.
- `ConditionVariable` works with `UniqueLock` (and `SharedLock` on a `SharedMutex`), so it is C++'s `std::condition_variable_any`.
