# The Walnut Library

These pages document Walnut's library, one page per module. The language itself is described in [documentation.md](../../documentation.md).

The library's source is built into the `walnut` program, so it is not a separate download. A module is used with `import module.<name>`, except `core`, which every program gets automatically.

When a module is used, its contents are in the namespace `std::<name>`: `core` is `std::core`, so its `move` is `std::core::move`, and `import module.io` makes `std::io::cout`, `std::io::ifstream` and the rest of `io` available. `import module.<name> as <alias>` also makes `<alias>` another name for `std::<name>`:

```walnut
import module.io as io

function main() -> int {
    io::cout << "hi" << io::endl;
    std::io::cout << "also hi" << std::io::endl;
    return 0;
} end function main
```

| Module | Contents |
|---|---|
| [algorithms](algorithms.md) | Searching and sorting over iterator ranges, C++-style: `sort`, `stable_sort`, `partial_sort`, `nth_element`, many named sorts (`heap_sort`, `radix_sort`, `counting_sort` and more), `find`, `count_if`, `lower_bound`, `binary_search`, `exponential_search`, `reverse`, `rotate`, `remove_if`, `unique`, `partition`, `merge`, set operations, heaps and more |
| [collections](collections.md) | `Vector`, `DynamicArray`, `SmallVector`, `Span`, `LinkedList`, `Deque`, `Stack`, `Queue`, `PriorityQueue`, `UnorderedMap`, `UnorderedSet`, `OrderedMap`, `OrderedSet`, `Counter`, `DefaultMap`, `Map`, `Set`, `TrieMap`, `Trie`, `Cache`, `Pair`, `Tuple`, `make_tuple`, `hash_of`; sorting, searching and editing methods on the array containers; `WhereQuery` |
| [colors](colors.md) | `Color` with hex, HSL and HSV conversions, mixing, lightening and contrast, and the 148 CSS named colors |
| [combinatorics](combinatorics.md) | `ncr`, `npr`, `combinations`, `permutations`, `factorial`, `lock_combinations`, `lock_permutations`, combinations and permutations with repetition, `multinomial`, `derangements`, and helpers for combining probabilities (`bayes`, `conditional_probability` and more) |
| [constants](constants.md) | `pi`, `e`, `phi`, `sqrt2`, `euler_mascheroni` and other constants, and numbered ones such as `bernoulli(n)`, for every float type and `BigFloat` |
| [core](core.md) | `std::core::move`, `std::core::forward` and `std::core::to_string`; the function types `std::Predicate`, `std::Compare`, `std::Transform` and others, and ready-made comparators such as `std::less_than` and `std::by`; checks on number values (`is_integer`, `is_even`, `is_odd`, `is_finite`, `is_nan`, `is_power_of_two` and more); type traits and type operations in `std::traits`; concepts in `std::concepts`; `std::true_type`, `std::false_type` and the other constant types; `std::enable_if`; `std::initializer_list` |
| [crypto](crypto.md) | Hashes (`sha256` and the rest of SHA-1 and SHA-2), `Hasher`, HMAC, PBKDF2 and HKDF, hex, base64, base64url, base32 and URL encoding, secure random bytes, tokens and UUIDs, `secure_equals` and `strequal`, `crc32` and other checksums |
| [game](game.md) | `Game`, a ready-made main loop that opens a window and renderer and calls your update and draw each frame, with frame timing; `Assets` for loading and hot-reloading files; `Scene` and `Node`, a 2D scene graph |
| [graphics](graphics.md) | `Renderer` for drawing into a window: shapes, `Paint`, gradients, paths, images, `Texture`, `Sprite`, text, transforms, clipping, cameras and render targets; 3D with `Camera3D`, `Mesh`, `Material`, `Lighting`, `Model` and `Animator`; `TileMap` |
| [graphs](graphs.md) | `Graph` with values on nodes and weighted `Edge`s, `Path`, breadth-first and depth-first search (with loop detection built in), multithreaded breadth-first search, shortest paths (Dijkstra, Bellman–Ford, A*), components, topological sort, spanning trees |
| [images](images.md) | `Image`: loading and saving PNG, JPEG, WebP, GIF and other formats, pixel access, cropping, resizing, rotating, and filters such as blur, grayscale and sepia |
| [integration](integration.md) | Exact antiderivatives and definite integrals of `symbolic` expressions (`integrate`, `antiderivative`, `try_antiderivative`), and numeric integrals in any float type or `BigFloat`: `value`, `tanh_sinh`, `adaptive_gauss`, `adaptive_simpson`, `romberg`, `gauss_legendre`, `trapezoid`, `midpoint`, `simpson` |
| [input](input.md) | `Key`, `MouseButton`, `PointerType`, `Modifiers`, `GamepadButton` and `GamepadAxis`, the names used by window events and input checks |
| [iterators](iterators.md) | Iterator classes with a compile-time `Reverse` switch (`Pointer`, `Indexed`, `Linked`, `Tree`, `Sparse`, `Keys`, `Values`), `Range`, Python-style looping helpers (`range`, `enumerate`, `zip`, `chain`, `take`, `skip`, `filter`, `map`, `pairwise`, `running`, `repeat`, `cycle`), and functions over any `iterable<T>`: `count_if`, `any_of`, `contains`, `index_of`, `sum`, `fold`, `min_of`, `max_of`, `each` |
| [io](io.md) | Printing (`print`, `println`, `printf`, `sprintf`), reading answers (`input_integer`, `input_float`, `input_string` and more), parsing (`parse_int`, `parse_float`, `parse_bool`, `parse_string` and more), and streams: `cout`, `cin`, `cerr`, `clog`, file, string, text, span and synchronized streams, stream buffers, manipulators |
| [linalg](linalg.md) | `Vector2`, `Vector3`, `Vector4`, `VectorN`, `DynVector`, `Matrix`, `SquareMatrix`, `DynMatrix`, `KVector` (bivectors and other k-vectors, with wedge products and Hodge duals), `Quaternion`, `Octonion`; determinants, inverses, LU, QR, Cholesky and SVD, eigenvalues and eigenvectors, graphics transforms |
| [math](math.md) | Trigonometry, logarithms, roots, rounding, gamma, zeta, error functions, elliptic integrals and other special functions for every float type; `BigFloat`, `BigComplex` and `Complex<T>` |
| [memory](memory.md) | Smart pointers (`UniquePtr`, `SharedPtr`, `WeakPtr`, `EnableSharedFromThis`, `make_unique`, `make_shared`, pointer casts), `Arena`, a fast allocator for objects that are freed together, with generation-checked `ArenaPtr` and `ArenaArray`, and memory tracking while the program runs (`live_objects`, `live_bytes`, `allocation_report`, `collect_garbage`) |
| [networking](networking.md) | `TcpStream`, `TcpListener`, `UdpSocket`, `TlsStream`, DNS lookups, `Url`, an HTTP client (`get`, `post` and more) and `HttpServer`, `WebSocket` and `WebSocketServer`, and an event loop with timers |
| [numerics](numerics.md) | Ordinary differential equations (`solve`, `solve_stiff`, `solve_fixed`, `solve_until` with events, `solve_symplectic` for mechanics) with `OdeSolution`, `newton` for nonlinear systems, `solve_linear`, and `SparseMatrix` with banded, GMRES, conjugate gradient and BiCGSTAB solves |
| [optional](optional.md) | `Optional<T>`, a value that may be missing, with `none`, `some`, `value_or`, `map`, `and_then` and more |
| [random](random.md) | `random_int`, `random_float`, `random_bool`, `random_char`, `random_string`, `random_text`, `shuffle`, and the seeded `Generator` |
| [regex](regex.md) | `Regex` for classic regular expressions (named groups, lookarounds, Unicode classes), `Pattern` for building the same thing from named steps (`Pattern::digit().times(4)`), `CharSet` for allowing and blocking kinds of characters (`disable_letters()`, `disable_special()`), and `explain()`, which says what any pattern does in plain English |
| [statistics](statistics.md) | Descriptive statistics, `Normal` and the other distributions, t, z and chi-squared tests, confidence intervals, power and sample sizes, linear regression |
| [strings](strings.md) | `split`, `join`, `replace`, `trim_left`, `pad_left`, `title_case`, `levenshtein`, `is_digits`, `matches_glob` and many more helpers for `string` and `text`, usable as methods (`s.split(",")`); character tests such as `c.is_digit()`; `where` on strings |
| [symbolic](symbolic.md) | `Expression`, exact symbolic math: symbols, exact fractions, `pi` and `e` kept as symbols, over 70 functions, `simplify`, `full_simplify`, `derivative`, `substitute`, MathText output (`to_mathtext`, `to_mathtext_infix`), and `evaluate<T>` in any float type or `BigFloat` with `Values<T>` |
| [threading](threading.md) | `Thread`, `this_thread`, `Mutex` and the other mutexes, `LockGuard`, `UniqueLock`, `SharedLock`, `ConditionVariable`, `Atomic` (for any copyable type, with memory orders and `wait`/`notify`), `AtomicFlag` |
| [time](time.md) | `Duration`, `Date`, `TimeOfDay`, `DateTime`, `ZonedDateTime`, clocks, `Stopwatch` and timers, sleeping and precise waits, formatting and parsing, ISO weeks, leap seconds |
| [typography](typography.md) | `TextStyle` (font, size, color, weight, decorations, shadows) and `TextMetrics`, for drawing text |
| [ui](ui.md) | Widgets for windowed interfaces: `Label`, `Button`, `Checkbox`, `Toggle`, `Slider`, `ProgressBar`, `Dropdown`, `TextField`, `IntField`, `FloatField`, `ColorPicker`, radio buttons, tabs, scroll areas, stacks and panels, with dark and light themes |
| [units](units.md) | `convert` between units of measurement, `Quantity` values with units, composite and custom units, and kinds that keep apart quantities with the same dimension (energy and torque, hertz and becquerels); over 1,700 units |
| [windows](windows.md) | `Window`, its events and input state, gamepads, monitors, and the system message boxes and file pickers |
