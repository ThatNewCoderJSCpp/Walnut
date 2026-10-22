# Walnut

Walnut is a statically typed programming language that compiles to C++. The `walnut` tool checks your program, turns it into C++, and hands that C++ to a normal C++ compiler to produce an executable. Walnut's numbers come from [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo)'s multiprecision library, so `int` is exactly 32 bits everywhere, `float` is 128 bits, and widths go up to 2048 bits.

```
extern "C" function puts(string: line) -> int;

function main() -> int {
    puts("Hello, Walnut!");
    return 0;
} end function main
```

Programs reach the screen, files and the rest of the system by calling C or C++ directly, as `puts` does above. The full language is described in [documentation.md](documentation.md). Walnut's library has its own documentation, one page per module, in [Documentation/Library](Documentation/Library/README.md). `core` comes with every program; the other modules, such as `io`, `collections`, `linalg`, `statistics`, `combinatorics`, `units`, `memory`, `graphs`, `windows`, `graphics`, `images`, `ui`, `game`, `time`, `networking` and `crypto`, are imported by name.

## Requirements

Walnut needs a **C++17 compiler** to build programs, because it compiles to C++. The download contains Walnut and Fizmo but not a compiler; **`walnut init` sets one up for you** (see [Setting up a compiler](#setting-up-a-compiler)).

| Platform | What you need |
|---|---|
| Linux (x86_64, arm64, i686) | GCC 9 or newer, or Clang 9 or newer, available as `c++`; `walnut init` installs `g++` with your package manager |
| Windows (x86_64, i686, arm64) | nothing: the `-with-compiler` downloads include llvm-mingw, and for the others `walnut init` downloads it |
| macOS | Not supported yet (planned) |

Visual Studio's compiler (MSVC) can't be used to build Walnut programs, because Walnut calls the compiler with GCC-style options.

Fizmo can use some extra system libraries (X11, Pango, Cairo, OpenSSL and others). **A Walnut program does not need them.** Walnut only links the ones that are installed, and only into programs that actually use them.

## Installing

1. Download the archive for your platform from the [Releases](https://github.com/ThatNewCoderJSCpp/Walnut/releases) page:

   | File | For |
   |---|---|
   | `Walnut-<version>-linux-x86_64.tar.gz` | 64-bit Intel/AMD Linux |
   | `Walnut-<version>-linux-arm64.tar.gz` | 64-bit ARM Linux |
   | `Walnut-<version>-linux-i686.tar.gz` | 32-bit Linux |
   | `Walnut-<version>-windows-ucrt64.zip` | 64-bit Intel/AMD Windows |
   | `Walnut-<version>-windows-i686.zip` | 32-bit Windows |
   | `Walnut-<version>-windows-clangarm64.zip` | ARM Windows |

   Each Windows download also comes as `...-with-compiler.zip` (about 200 MB more), which has the C++ compiler inside, so it works offline as soon as it's unpacked and step 4 isn't needed. The smaller download gets the same compiler with `walnut init` the first time it's needed. Walnut for Windows doesn't need MSYS2.

2. Extract it anywhere. Keep the folders together: `walnut` finds its runtime and Fizmo next to itself.

   ```
   Walnut-<version>-<platform>/
     bin/walnut            the compiler
     runtime/              the Walnut runtime header
     fizmo/                Fizmo's library and headers
     docs/                 the language and library documentation
     toolchain/            the C++ compiler (only in the -with-compiler downloads)
   ```

3. Add the `bin` folder to your `PATH`.

4. Set up a C++ compiler:

   ```
   walnut init
   ```

   It tells you what it's going to do and asks first. If you already have a compiler, it says so and changes nothing. [Setting up a compiler](#setting-up-a-compiler) explains what it does on each system.

   You can also install a compiler yourself. On Linux:

   ```
   sudo apt install g++          # Debian, Ubuntu
   sudo dnf install gcc-c++      # Fedora
   sudo pacman -S gcc            # Arch
   ```

   On Windows, Walnut's libraries are built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (Clang with libc++), so programs have to be compiled with llvm-mingw too; other MinGW compilers such as MSYS2's GCC won't link them. Use `walnut init` or the `-with-compiler` download rather than installing one yourself.

5. Check that it works:

   ```
   walnut run hello.wal
   ```

## Setting up a compiler

`walnut init` gets a C++ compiler ready for Walnut.

| System | What `walnut init` does |
|---|---|
| Windows | With a `-with-compiler` download, nothing: Walnut uses the compiler in its own `toolchain` folder. Otherwise it downloads the compiler that matches your Walnut download, checks its SHA-256, and unpacks it into `%LOCALAPPDATA%\walnut\toolchain`. It needs no administrator rights, installs nothing system-wide and doesn't change `PATH`. |
| Linux | Installs your distribution's `g++` with its package manager (`apt`, `dnf`, `pacman`, `zypper`, `apk` or `xbps`), using `sudo`, so it asks for your password. |
| macOS | Opens Apple's installer for the Command Line Tools. |

The Windows compilers are pinned, so every Walnut version uses the exact compiler it was tested with:

| Download | Compiler | Size |
|---|---|---|
| `windows-ucrt64` | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20261020 (Clang 23.1.4), UCRT, x86_64 | about 190 MB |
| `windows-i686` | llvm-mingw 20261020 (Clang 23.1.4), UCRT, 32-bit x86 | about 195 MB |
| `windows-clangarm64` | llvm-mingw 20261020 (Clang 23.1.4), UCRT, ARM64 | about 185 MB |

Walnut looks for a compiler in this order: `--cxx`, `WALNUT_CXX`, the compiler in a `-with-compiler` download's `toolchain` folder, the one `walnut init` downloaded, then `c++` on `PATH`.

Programs built with the downloaded compiler are linked statically, so the `.exe` runs on other Windows machines without extra DLLs.

If `walnut build` or `walnut run` can't find a compiler and you're at a terminal, Walnut offers to run `walnut init` for you. When it isn't at a terminal (an editor or a script calling it), it explains what's missing and stops instead.

| Command | |
|---|---|
| `walnut init` | set up a compiler, asking first |
| `walnut init --yes` | the same, without asking (for scripts and CI) |
| `walnut init --force` | on Windows, download Walnut's own compiler even when another one is already on `PATH` |
| `walnut init --status` | show which compiler Walnut uses, where it came from and its version |
| `walnut init --print-compiler` | print only the compiler's path, for editors and build tools such as Canopy and Grove |
| `walnut init --remove` | delete the compiler `walnut init` downloaded |

Walnut picks its compiler in this order:

1. `--cxx <compiler>`
2. the `WALNUT_CXX` environment variable
3. the compiler `walnut init` downloaded
4. `c++` on `PATH`

`WALNUT_TOOLCHAIN_DIR` keeps the downloaded compiler somewhere other than the default folder (`%LOCALAPPDATA%\walnut\toolchain` on Windows, `~/.local/share/walnut/toolchain` elsewhere).

## Using walnut

```
walnut <command> [options] <main.wal> [more.wal ...] [C/C++ files] [-- program arguments]
```

| Command | What it does |
|---|---|
| `walnut check main.wal` | Report errors and warnings without building anything |
| `walnut run main.wal` | Build and run the program |
| `walnut build main.wal` | Build an executable (named after the input, or `-o <path>`) |
| `walnut emit main.wal` | Print the generated C++ (or write it with `-o file.cpp`) |
| `walnut clean` | Delete the build cache |
| `walnut init` | Set up a C++ compiler for Walnut ([details](#setting-up-a-compiler)) |
| `walnut highlight main.wal` | Print what each name and operator is, and the errors, as JSON for editors (`--stdin` reads the text from standard input) |
| `walnut complete main.wal --at LINE:COL` | Print what can be typed at that spot, as JSON for editors' autocomplete: the members of the value before `.` or `->`, what's inside the namespace, class or enum before `::`, or every name in scope there (locals declared further down are left out). Works on unfinished code; `--stdin` reads the text from standard input |
| `walnut library` | List the library files built into `walnut`; `walnut library io.walh` prints one and `walnut library --extract <dir>` writes them all to a folder, so editors can open them |

Arguments after `--` go to your program: `walnut run main.wal -- input.txt 3`.

`build` and `run` keep what they compile in a cache (`~/.cache/walnut`, or `WALNUT_CACHE_DIR`). Each Walnut file is compiled on its own, several at once, and only files whose generated C++ changed are compiled again; an unchanged program is ready almost at once. There is nothing to set up.

Common options:

| Option | Meaning |
|---|---|
| `-o <path>` | Output path; with `build` on Windows, `.exe` is added when the path has no extension |
| `-O0` … `-O3`, `-Os` | Optimization level (default `-O2`) |
| `-I <dir>` | Include directory for `#include` |
| `-M <dir>` | Folder searched by `import module.<name>` (also `--module-dir`, or the `WALNUT_MODULE_PATH` environment variable) |
| `-D <name>[=value]` | Define a preprocessor macro |
| `-L <dir>`, `-l <lib>` | Library folder and library for `extern "C"` / `extern "C++"` code |
| `--cxx <compiler>` | Use a different C++ compiler (also the `WALNUT_CXX` environment variable) |
| `--cxx-flag <flag>` | Pass an extra flag to the C++ compiler |
| `--runtime <dir>` | Where the runtime is (also the `WALNUT_RUNTIME_DIR` environment variable) |
| `--keep-cpp` | Keep the generated C++ next to the executable (builds without the cache) |
| `--no-cache` | Build from scratch, without the build cache |
| `--no-warnings` | Hide warnings |
| `--warn-unused` | Warn about local variables that are defined but never used in their scope (off unless asked for; names starting with `_` are skipped) |
| `--errors <dest>`, `--warnings <dest>` | Send errors or warnings to `stderr` (default), `stdout` or a file |
| `--tokens <file>`, `--ast <file>` | Write the token list or syntax tree, for debugging |
| `--time` | Show how long each compiler stage takes |
| `-g` | Build for a debugger (gdb, lldb, an IDE) that steps through your `.wal` lines |
| `-u`, `--unbuffered` | Make the program flush `cout` and `clog` after every write, like `python -u`; see [Flushing](Documentation/Library/io.md#printing) |
| `--debug-cpp` | Build for a debugger that steps through the generated C++ instead; the C++ is kept next to the program |
| `--line-map[=file]` | Write which `.wal` line each line of generated C++ came from (to the terminal when no file is given) |
| `-v`, `--verbose` | Print the C++ compiler command |
| `--checks` | Stop with the `.wal` line on an index outside memory from `new[]`, a null or deleted pointer, a pointer to a local of a function that already returned, or integer overflow (or pick one: `--check-bounds`, `--check-null`, `--check-dangling`, `--check-overflow`) |
| `--leak-check[=warn\|error]` | List memory from `new` that was never deleted when the program ends |
| `--gc` | Free objects made with `new` automatically once nothing points to them |
| `--ownership` | Report use after move or delete, double deletes, leaks and dangling pointers or references while compiling |
| `--safe` | `--checks`, `--leak-check` and `--ownership` together. All of these are off unless asked for; see [Safety checks](documentation.md#safety-checks) |

`walnut --help` lists everything.

### Programs with several files

Pass every `.wal` file, starting with the main one:

```
walnut run main.wal shapes.wal util.wal
```

A file can also import another one directly, either a single name or everything it exports:

```
import hello from "lib.wal"
import namespace shapes from "lib.wal"
import "lib.wal" as lib
```

The exporting file lists what it shares with `export hello`, `export namespace shapes`, or `export this` for all of its global declarations. A file can also group exports into named modules and import one of them:

```
module greet {
    hello
    namespace shapes
    function triple(int: n) -> int { return n * 3; } end function triple
}
```

```
import module "greet" from "lib.wal"
```

Shared declarations can also go in a header file, `name.walh`, which other files `#include`. A header holds the code as well as the declarations, is pasted into each file at most once, and is never compiled or linked on its own.

### Modules

`import module.<name>` loads a module by name instead of by path:

```
import module.greet
import module.math.extra as mx

function main() -> int {
    return greet::hello(1) + mx::square(7);
} end function main
```

A module is a Walnut header file named after the module (`greet.walh`, or `math/extra.walh` for `module.math.extra`); a `.wal` file also works. Its exported names are reached through the module's name, or the name after `as`. The file usually just `#include`s its implementation files and lists what it exports:

```
#include "greet_impl/hello.walh"
#include "greet_impl/shout.walh"
export hello
export shout
```

Walnut looks for modules in each `-M` folder first, then in the folders listed in `WALNUT_MODULE_PATH`, then in the `runtime` folder next to `walnut_runtime.hpp`, and last in its own library, which is built into `walnut`. A module whose name is a keyword, such as `module.string`, needs `as <name>`.

### Debugging

`walnut build main.wal -g` makes a program that debuggers show as Walnut: breakpoints like `break main.wal:12` work, and the call stack and stepping use your `.wal` files and line numbers. Variables still have their generated names, such as `v_total` for `total`. Without `-O`, `-g` builds with `-O0`.

To debug the generated C++ instead, use `--debug-cpp`. `--line-map` connects the two by listing which `.wal` line each block of C++ came from:

```
walnut build main.wal --keep-cpp --line-map=main.map
```

### Calling C and C++

Declare the functions in an `extern "C"` (or `extern "C++"`) block, and name the C or C++ files that hold them with `from`. Walnut compiles and links source files, and includes headers, by itself:

```
extern "C" from "native.c" {
    function add_c(int: a, int: b) -> int;
} end extern

function main() -> int {
    return add_c(2, 3);
} end function main
```

Without `from`, pass the C or C++ file on the command line, or link the library:

```
walnut run main.wal native.c
walnut run main.wal -L ./lib -l mylib
```

C or C++ code can also be written straight into a `.wal` file, in an `extern "C++" code { ... } end extern` block. See [documentation.md](documentation.md#writing-c-and-c-code-in-walnut-files).

## Editor support

`Tools/vscode-walnut` is a Visual Studio Code extension that colors Walnut code (`.wal` and `.walh` files), underlines the compiler's errors as you type, and sets up comment toggling and bracket matching. It asks the compiler what each name and operator is with `walnut highlight`, so variables, types and overloaded operators get their exact colors. The colors are in `Tools/vscode-walnut/colors.json`, and saving that file recolors the editor at once. Walnut files get their own icons too; `Tools/icons` has those icons as SVG, PNG and `.ico`, both as plain symbols and as file-document icons. [Its README](Tools/vscode-walnut/README.md) covers installing it from a `.vsix` file, editing the colors, and using the grammar in other editors.

## A short tour

```
extern "C" function puts(string: line) -> int;

class Point {
public:
    int: x;
    int: y;
    constructor(int: a, int: b) : x = a, y = b { }
    operator(+) function(const Point&: o) const -> Point { return Point(x + o.x, y + o.y); }
} end class Point

template <typename T>
function largest(T: a, T: b) -> T {
    if (a > b) then { return a; }
    return b;
} end function largest

function main() -> int {
    Point: p = Point(1, 2) + Point(3, 4);
    if (p == Point(4, 6) && largest(3, 9) == 9) then {
        puts("it adds up");
    }
    return 0;
} end function main
```

This prints `it adds up`. `==` was generated for `Point` member by member.

## Building Walnut from source

You need CMake 3.18 or newer and a C++17 compiler. Fizmo is downloaded automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That `walnut` uses the runtime and Fizmo from the build tree, so it only works on the machine that built it. To make a download like the ones on the Releases page, configure with `-DWALNUT_PACKAGE=ON` and run `cpack` in the build folder.

The compilers `walnut init` downloads on Windows are pinned in `Source/Driver/toolchain.cpp` (the URL, SHA-256 and size of each). The Windows releases are cross-compiled on Linux with the same llvm-mingw version (`LLVM_MINGW_VERSION` in `.github/workflows/release.yml`), so update both together. `cmake/llvm-mingw.cmake` is the toolchain file, and `.github/scripts/windows-deps.sh` builds zlib, libjpeg-turbo and the Vulkan headers for it. To build a Windows Walnut on Linux:

```
export PATH=/path/to/llvm-mingw/bin:$PATH LLVM_MINGW_TARGET=x86_64-w64-mingw32 LLVM_MINGW_DEPS=$PWD/windows-deps
bash .github/scripts/windows-deps.sh $LLVM_MINGW_TARGET $LLVM_MINGW_DEPS
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake \
  -DJPEG_LIBRARY=$LLVM_MINGW_DEPS/lib/libjpeg.a -DZLIB_LIBRARY=$LLVM_MINGW_DEPS/lib/libzs.a
cmake --build build-windows
```

The release workflow tests each Windows package on a real Windows machine (x86_64 and ARM): it runs a program with the bundled compiler, and runs `walnut init` with the plain package and builds a program with what it downloads, so a mismatch fails the release instead of reaching users.

## Changing the built-in character names

The names `\s{...}` knows (`alpha`, `rarr`, `le`, ...) all come from [`Data/special_names.json`](Data/special_names.json). Each character is one line, grouped by type, with the character itself so you can see what you're naming:

```json
{ "symbol": "ċ", "code": "U+010B", "names": ["cdot"], "unicode": "LATIN SMALL LETTER C WITH DOT ABOVE" },
```

Change, add or remove names in `names`, then rebuild: the build turns the file into the compiler's table and the runtime's `walnut_special_names.hpp` (for `parse_string`), sorted for you. Canopy builds its own copy from the same file. To add a character, add a line with its `symbol`, `code` and `names` to any category (or a new category); `unicode` is only there to help you read the file. Mistakes stop the build with the line they're on: a name used twice, a name with characters other than letters, digits and `_`, a `symbol` that isn't the character `code` says, or a character listed twice. Invisible characters (tab, no-break space, ...) are written as JSON escapes like `"\u0009"`.

## Troubleshooting

**`walnut: cannot find the C++ compiler 'c++'`**
Run `walnut init` (see [Setting up a compiler](#setting-up-a-compiler)), or point Walnut at a compiler with `--cxx` or `WALNUT_CXX`.

**`walnut init` says the download failed or isn't the expected file**
The download was interrupted or changed on the way. Walnut deletes a file whose SHA-256 doesn't match instead of using it. Run `walnut init` again. Behind a proxy that blocks GitHub downloads, use the `-with-compiler` download instead.

**`walnut: runtime header not found`**
The `runtime` folder isn't next to `bin` any more. Keep the extracted folder together, or point at it with `--runtime <dir>` or `WALNUT_RUNTIME_DIR`.

**Linker errors on Windows**
The compiler isn't llvm-mingw (for example MSYS2's GCC, whose C++ library doesn't match Walnut's). `walnut init --status` shows which compiler Walnut is using, and `walnut init --force` switches to the matching one.

**`undefined reference` errors mentioning X11, Pango, Cairo or OpenSSL**
Your C or C++ code uses a part of Fizmo that needs those libraries, and they aren't installed. Walnut lists the ones it couldn't find; install their development packages (for example `libx11-dev` on Debian or Ubuntu).
