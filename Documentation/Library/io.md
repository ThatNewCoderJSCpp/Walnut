# io

`io` is Walnut's input and output library. It follows C++'s `<iostream>` family: streams that you write to with `<<` and read from with `>>`, the standard streams `cout`, `cin`, `cerr` and `clog`, and streams over files, strings, text, memory spans and synchronized output.

```walnut
import module.io
```

Everything in `io` is in the namespace `std::io`. With `import module.io as io`, `io::` works as a shorter name for `std::io::`.

## Contents

1. [A first example](#a-first-example)
2. [Printing](#printing)
3. [Parsing values](#parsing-values)
4. [Reading input](#reading-input)
5. [The standard streams](#the-standard-streams)
6. [Writing with <<](#writing-with-)
7. [Reading with >>](#reading-with-)
8. [Manipulators](#manipulators)
9. [Stream state](#stream-state)
10. [Formatting flags](#formatting-flags)
11. [File streams](#file-streams)
12. [String and text streams](#string-and-text-streams)
13. [Span streams](#span-streams)
14. [Synchronized output](#synchronized-output)
15. [Stream buffers](#stream-buffers)
16. [Your own types](#your-own-types)
17. [Differences from C++](#differences-from-c)

## A first example

```walnut
import module.io

function main() -> int {
    std::io::cout << "What's your name? ";
    string: name = "";
    std::io::getline(std::io::cin, name);
    int: age = 0;
    std::io::cout << "And your age? ";
    if (std::io::cin >> age) then {
        std::io::cout << `Hello ${name}, next year you'll be ${age + 1}.` << std::io::endl;
    } else then {
        std::io::cerr << "That isn't a number." << std::io::endl;
        return 1;
    }
    return 0;
} end function main
```

## Printing

Four functions print without writing out a stream and `<<`:

| Function | Does |
|---|---|
| `print(values...)` | Writes each value, one after another, with nothing between them |
| `println(values...)` | The same, then a line break. `println()` alone writes just the line break |
| `printf(format, values...)` | Writes the values into the `%` placeholders of `format`, like C's `printf` |
| `sprintf(format, values...)` | The same as `printf`, but gives the result back as a `string` (or a `text` when `format` is a `text`) instead of writing it |

All of them write to `cout` unless the first argument is a stream, in which case they write there instead: `println(cerr, "oops")`, `printf(file, "%d\n", n)`, `print(some_stringstream, x)`. Any stream works, including files and string streams.

```walnut
import module.io as io

function main() -> int {
    string: name = "Ada";
    int: score = 97;
    io::print("no line break, ");
    io::println("name: ", name, ", score: ", score);
    io::printf("%-8s|%5d|%6.2f|%#x\n", name, score, 3.14159, 255);
    io::println(io::cerr, "this goes to standard error");
    string: label = io::sprintf("%03d", 7);
    io::println(label);
    return 0;
} end function main
```

This prints:

```
no line break, name: Ada, score: 97
Ada     |   97|  3.14|0xff
007
```

and `this goes to standard error` on standard error.

`print` and `println` work with anything `<<` works with, including [your own types](#your-own-types). Text interpolation often reads better than either: ``io::println(`${name} scored ${score}`)``.

**Flushing.** Output to `cout` is collected in a buffer and written out in larger pieces, which is much faster than a system call for every write. Writes to a terminal still appear line by line, but when the output goes into a pipe or a file, or a line isn't finished yet (a prompt, a progress dot), it can sit in the buffer for a while. Like Python's `print(..., flush=True)` and `python -u`, there are three ways to get it out at once:

- **One call.** Put `flush` among the values: `print("Loading", flush)` or `println("step 3 done", flush)` writes everything, line break included, and then flushes. `cout << flush` and `cout.flush()` do the same with `<<`.
- **From here on.** `set_unbuffered()` makes `cout` and `clog` flush after every write until `set_unbuffered(false)`. `is_unbuffered()` tells which mode `cout` is in.
- **The whole program.** Build or run it with `-u` (`walnut run -u main.wal`), or set the environment variable `WALNUT_UNBUFFERED` to anything but `0` when it runs, and `cout` and `clog` start out unbuffered.

`cerr` is always unbuffered, and `cout` is flushed before `cin` reads and when the program ends, so prompts and final output are never lost.

```walnut
import module.io
import module.threading

using namespace std::io;

function main() -> int {
    for (int: i = 1; i <= 3; i++) {
        print("working ", i, "... ", flush);
        std::threading::this_thread::sleep_for(200);
    }
    println("done");
    return 0;
} end function main
```

Each `working ...` shows up as soon as it is printed, even when the output goes into a pipe, instead of all of them appearing together at the end.

**Placeholders.** A placeholder is `%`, then optional flags, a width, a precision and the conversion letter: `%[flags][width][.precision]letter`.

| Letter | Takes | Writes |
|---|---|---|
| `d`, `i`, `u` | An integer of any width, or a `bool` (`1` or `0`). A `char` gives its code point | Base 10 |
| `x`, `X` | An integer | Base 16, lower or upper case |
| `o` | An integer | Base 8 |
| `b`, `B` | An integer | Base 2 |
| `f`, `F` | A float of any width | Fixed point: `3.141590` |
| `e`, `E` | A float | Scientific: `3.141590e+00` |
| `g`, `G` | A float | Whichever of the two is shorter, like C |
| `c` | A `char` | The character |
| `s` | Anything `<<` can write. A `bool` is written as `true` or `false` | The value |
| `%%` | Nothing | A `%` |

| Flag | Effect |
|---|---|
| `-` | Pad on the right instead of the left |
| `+` | Write `+` before positive numbers |
| space | Write a space before positive numbers |
| `0` | Pad numbers with zeros after the sign (and after `0x` or `0b`) |
| `#` | Add `0x`, `0` or `0b` to `x`, `o` and `b`; always write a point for `f`, `e` and `g` |

- The width is the smallest number of characters (code points) to write.
- For floats the precision is the number of digits after the point (`f`, `e`) or of significant digits (`g`), `6` when it's left out. For `%s` it is the most characters to write, so `%.3s` writes at most three.
- C's size letters (`l`, `ll`, `h`, `z`, `j`, `t`, `L`) are accepted and ignored, because Walnut already knows each argument's type: `%ld` and `%d` are the same.
- The format can be a `string` or a `text`.

**Checking.** Every argument is checked against its placeholder. A mismatch throws a `std::errors::format_error` instead of printing garbage: `%d` given a `string`, too few arguments for the placeholders, too many arguments, an unknown letter, or a format that ends in the middle of a placeholder. `e.what()` (or the field `e.message`) says what went wrong, such as `argument 2 is string, but %d needs an integer`. The check happens when the call runs, not when the program is compiled.

```walnut
import module.io as io

function main() -> int {
    try {
        io::printf("%d apples\n", "three");
    } catch (const std::errors::format_error&: e) {
        io::println("format problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `format problem: argument 1 is string, but %d needs an integer`.

## Parsing values

The parse functions turn a `string` (or a `text`) into a value. Spaces and line breaks around the value are ignored, but the rest of the string must be the value and nothing else: `parse_int("12abc")` fails.

| Function | Gives | When the string isn't a value |
|---|---|---|
| `parse_int(s)`, `parse_int(s, base)` | `int` | `undefined` |
| `parse_int<T>(s)`, `parse_int<T>(s, base)` | Any signed integer type `T`, such as `long int` or `long long integer` | `undefined` |
| `parse_uint(s)`, `parse_uint<T>(s, base)` | `unsigned int`, or the unsigned type `T` | `undefined` |
| `parse_float(s)`, `parse_float<T>(s)` | `float`, or the float type `T` | `undefined` |
| `parse_bool(s)` | `special(bool)`: `true` for `true` or `1`, `false` for `false` or `0`, in any case | `undefined` |
| `parse_char(s)` | `special(char)`: the single character in `s`, or the character of a quoted literal such as `'\n'` | `undefined` |
| `parse_string(s)` | `special(string)` | `undefined` |
| `parse_text(s)` | `special(text)` | `undefined` |

```walnut
import module.io as io

function main() -> int {
    int: a = io::parse_int(" 42 ");
    int: b = io::parse_int("ff", 16);
    int: c = io::parse_int("0b1011", 0);
    long long integer: big = io::parse_int<long long integer>("123456789012345678901234567890");
    int: bad = io::parse_int("12abc");
    int: huge = io::parse_int("99999999999");
    float: f = io::parse_float("2.5e3");
    special(bool): yes = io::parse_bool("TRUE");
    special(string): s = io::parse_string("\"tab\\tend\"");
    if (bad is undefined && huge is inf) then {
        io::println(a, " ", b, " ", c, " ", big, " ", f, " ", yes as bool);
    }
    return 0;
} end function main
```

Here `a` is `42`, `b` is `255`, `c` is `11`, `f` is `2500`, `yes` holds `true`, and `s` holds `tab`, a tab and `end`.

- **Failure is a special value, not an exception.** Numbers can hold [special values](../../documentation.md#special-values), so a failed `parse_int` or `parse_float` gives `undefined`. Check with `x is undefined`. `bool`, `char`, `string` and `text` can't hold one, so their parse functions give a [`special(T)`](../../documentation.md#specialt): check it with `is undefined`, then take the value with `as bool` (or `as char`, and so on).
- **Too big for the type.** A whole number that doesn't fit gives `inf` (or `-inf` for a negative one), so `parse_int("99999999999")` is `inf` for a 32-bit `int`; parse into a wider type with `parse_int<long int>` to keep it. A float that's too big becomes `inf` the same way.
- **Special words.** `inf`, `-inf`, `infinity`, `nan`, `undefined` and `indeterminate` parse to those values, in any case.
- **Bases.** `base` is `2` to `36`, and `10` when it's left out. A `0x`, `0o` or `0b` prefix matching the base is allowed (`parse_int("0xff", 16)`). Base `0` reads the base from the prefix: `0x` is 16, `0o` is 8, `0b` is 2, and anything else is 10. `_` and `'` may separate digits, as in literals.
- **Strings and text.** If `s` is a quoted literal (in `"`, `` ` `` or `'` quotes), `parse_string` and `parse_text` remove the quotes and turn its escapes (`\n`, `\t`, `\"`, `\u{1F600}`, `\s{alpha}` and the rest, the same as in Walnut literals) into the characters they stand for. A bad escape gives `undefined`. If `s` isn't quoted, it is given back unchanged, spaces included.

To read values one at a time from a longer string, use a [string stream](#string-and-text-streams) and `>>` instead.

## Reading input

The input functions ask a question and give back the answer as a value. Each one prints a prompt to `cout`, reads one line from a stream (`cin` when no stream is given), and turns the line into the type it names:

```walnut
import module.io

using namespace std::io;

function main() -> int {
    int: age = input_integer<int>(cin, "How old are you? ");
    float: height = input_float("Your height in meters: ");
    string: name = input_string("Your name: ");
    bool: again = input_bool("Play again? ");
    println(name, " is ", age, " and ", height, " m tall. Again: ", again);
    return 0;
} end function main
```

With the answers `abc`, `30`, `1.75`, `Sam` and `yes`, the program prints `That isn't a whole number. Please try again.` after `abc`, asks for the age again, and ends with `Sam is 30 and 1.75 m tall. Again: 1`.

| Function | Gives |
|---|---|
| `input_integer<T>(stream, prompt)`, `input_integer<T>(prompt)` | A whole number of type `T`, `int` when left out. `T` can be any integer type, signed or unsigned |
| `input_float<T>(stream, prompt)`, `input_float<T>(prompt)` | A number of the float type `T`, `float` when left out |
| `input_string(stream, prompt)`, `input_string(prompt)` | The whole line, as typed |
| `input_text(stream, prompt)`, `input_text(prompt)` | The whole line, as `text` |
| `input_bool(stream, prompt)`, `input_bool(prompt)` | `true` for `yes`, `y`, `true` or `1`, and `false` for `no`, `n`, `false` or `0`, in any case |
| `input_char(stream, prompt)`, `input_char(prompt)` | A single character |

- **Wrong answers are asked again.** When the line isn't a value of the right kind, a short message is printed (such as `That isn't a whole number. Please try again.`) and the prompt is shown again, until the answer is usable. A whole number that doesn't fit in `T`, a negative number for an unsigned type, and `inf` or `nan` for a float all count as wrong. An empty line just asks again, except for `input_string` and `input_text`, where it is a valid answer.
- **Numbers are read like `parse_int` and `parse_float`.** Spaces around the number are ignored, and the number is read in base 10, so `0x1f` isn't accepted. See [Parsing values](#parsing-values).
- **The prompt can be left out**, in which case nothing is printed before reading: `input_integer()` reads an `int` from `cin`.
- **When the input ends** (the user presses Ctrl+D or Ctrl+Z, or a file runs out) before a usable answer, a `std::errors::input_error` is thrown. Its `what()` names the function.
- **Mixing with `>>`.** `cin >> x` leaves the rest of its line, often just the line break, waiting. The next `input_string` reads that rest, so it gives an empty string. Call `cin.ignore(1000, '\n')` after `>>` to skip it.

## The standard streams

| Name | Type | Goes to |
|---|---|---|
| `cout` | `ostream` | Standard output |
| `cin` | `istream` | Standard input. It is tied to `cout`, so `cout` is flushed before `cin` reads |
| `cerr` | `ostream` | Standard error, flushed after every write (`unitbuf`), tied to `cout` |
| `clog` | `ostream` | Standard error, buffered, tied to `cout` |

The buffers behind them are the `filebuf`s `stdin_buf`, `stdout_buf` and `stderr_buf`.

## Writing with <<

`ostream` has `<<` for `string`, `text`, `char`, `bool`, every integer type (signed and unsigned, of any width), every float type, `var` (written the way text interpolation writes it), `streambuf*` (copies everything the buffer holds) and the manipulators below. Each returns the stream, so writes chain.

```walnut
import module.io

function main() -> int {
    long long integer: big = 123456789012345678901234567890i;
    std::io::cout << 42 << " " << -7 << " " << 255u << " " << big << std::io::endl;
    std::io::cout << 2.5 << " " << 1.0s / 3.0s << " " << 1e30 << std::io::endl;
    std::io::cout << `text works too` << ' ' << true << std::io::endl;
    return 0;
} end function main
```

This prints:

```
42 -7 255 123456789012345678901234567890
2.5 0.33333333333333334 1e+30
text works too 1
```

- By default a float is written with enough digits to read back as exactly the same value, so a `short float` (64 bits) shows up to 17 digits and wider floats show more. Trailing zeros are left out, so `2.5` is `2.5`. Very large and very small values switch to scientific notation (`1e+30`). After `setprecision`, `fixed` or `scientific`, floats are written as in C++.
- [Special values](../../documentation.md#special-values) are written by name: `inf`, `-inf`, `nan`, `undefined` and `indeterminate`.
- `bool` is written as `1` or `0`, or as `true` and `false` after `boolalpha`.
- `char` is written as its UTF-8 bytes.

Other `ostream` methods:

| Method | Does |
|---|---|
| `write(s)` | Writes the string `s` exactly, ignoring width and fill |
| `put(c)` | Writes one character |
| `flush()` | Sends buffered output to its destination |
| `tellp()` | The current write position, or `-1` |
| `seekp(pos)`, `seekp(offset, dir)` | Moves the write position. `dir` is `ios_base::seek_beg`, `seek_cur` or `seek_end` |

## Reading with >>

`istream` has `>>` for `string` and `text` (one word), `char` (one character), `bool`, every integer type, every float type and the manipulators. Leading spaces are skipped unless `noskipws` is set.

```walnut
import module.io

function main() -> int {
    std::io::istringstream: input = std::io::istringstream("12 -3 4.5 word 999999999999");
    int: a = 0;
    int: b = 0;
    float: f = 0.0;
    string: w = "";
    input >> a >> b >> f >> w;
    std::io::cout << a + b << " " << f << " " << w << std::io::endl;
    short int: small = 0;
    input >> small;
    std::io::cout << "too big: " << input.fail() << std::io::endl;
    return 0;
} end function main
```

This prints `9 4.5 word` and `too big: 1`.

- A number that doesn't parse, or doesn't fit the target type, sets `failbit` and leaves the stream failed until `clear()`.
- Integers and floats can also read the words `inf`, `nan`, `undefined` and `indeterminate`.
- `bool` reads `1` and `0`, and also `true` and `false` with `boolalpha`.
- Reading past the end sets `eofbit` and `failbit`.

Other `istream` methods:

| Method | Does |
|---|---|
| `get(c)` | Reads one character, without skipping spaces |
| `peek(c)` | Looks at the next character without reading it. Returns `false` at the end |
| `unget()`, `putback(c)` | Steps back one character |
| `ignore()`, `ignore(n)`, `ignore(n, delim)` | Skips `n` characters, stopping after `delim` |
| `getline(s)`, `getline(s, delim)` | Reads up to the end of the line (or `delim`) into a `string` or `text`, dropping the delimiter |
| `read(s, n)` | Reads `n` characters into `s` |
| `gcount()` | How many characters the last unformatted read took |
| `tellg()`, `seekg(pos)`, `seekg(offset, dir)` | The read position, as for `ostream` |
| `sync()` | Drops buffered input |

The free functions `std::io::getline(stream, s)` and `std::io::getline(stream, s, delim)` do the same as the method.

## Manipulators

A manipulator is written into (or read from) a stream to change how it works.

| Manipulator | Effect |
|---|---|
| `endl` | Writes a line break and flushes |
| `ends` | Writes a `\0` |
| `flush` | Flushes |
| `ws` | (input) skips spaces |
| `boolalpha`, `noboolalpha` | `true`/`false` instead of `1`/`0` |
| `base10`, `hex`, `oct`, `bin` | Integer base. `base10` is C++'s `dec`, renamed because `dec` is a Walnut type keyword |
| `setbase(n)` | Base `2`, `8`, `10` or `16` |
| `showbase`, `noshowbase` | Prefix `0x`, `0` or `0b` |
| `uppercase`, `nouppercase` | Upper-case hex digits, prefixes and exponents |
| `showpos`, `noshowpos` | `+` before positive numbers |
| `showpoint`, `noshowpoint` | Always write a decimal point |
| `fixed`, `scientific`, `defaultfloat` | Float style |
| `setprecision(n)` | Float digits (after the point for `fixed` and `scientific`) |
| `setw(n)` | Minimum width of the next item only |
| `setfill(c)` | Padding character |
| `left`, `right`, `internal` | Where padding goes. `internal` pads between the sign or base prefix and the digits |
| `skipws`, `noskipws` | Whether `>>` skips leading spaces |
| `unitbuf`, `nounitbuf` | Flush after every write |
| `setiosflags(f)`, `resetiosflags(f)` | Sets or clears raw flags |
| `emit_on_flush`, `noemit_on_flush`, `flush_emit` | For `osyncstream`; see [Synchronized output](#synchronized-output) |

```walnut
import module.io

function main() -> int {
    std::io::cout << std::io::hex << std::io::showbase << 255 << " " << std::io::base10 << 255 << std::io::endl;
    std::io::cout << std::io::fixed << std::io::setprecision(2) << 3.14159 << std::io::endl;
    std::io::cout << "[" << std::io::setw(6) << std::io::setfill('.') << 42 << "]" << std::io::endl;
    std::io::cout << "[" << std::io::internal << std::io::setw(6) << std::io::setfill('0') << -42 << "]" << std::io::endl;
    return 0;
} end function main
```

This prints `0xff 255`, `3.14`, `[....42]` and `[-00042]`.

## Stream state

Every stream has a state made of `ios_base::goodbit` (`0`), `badbit`, `eofbit` and `failbit`.

| Method | Result |
|---|---|
| `good()` | No bits are set |
| `eof()` | The end of the input was reached |
| `fail()` | `failbit` or `badbit` is set |
| `bad()` | `badbit` is set (the device failed) |
| `rdstate()` | All the bits |
| `clear()`, `clear(bits)` | Replaces the state |
| `setstate(bits)` | Adds bits |
| `if (stream)`, `!stream` | `true` when not `fail()` |

Because `>>` returns the stream, a read can be tested directly: `while (input >> n) { ... }`.

## Formatting flags

`ios_base` holds a stream's flags, width, precision and fill. The manipulators above are shortcuts for these methods.

| Method | Does |
|---|---|
| `flags()`, `flags(f)` | Reads or replaces all flags |
| `setf(f)`, `setf(f, mask)` | Sets flags, first clearing `mask` |
| `unsetf(mask)` | Clears flags |
| `width()`, `width(n)` | The minimum width of the next item |
| `precision()`, `precision(n)` | Float precision |
| `fill()`, `fill(c)` | The padding character |

The flag constants are `ios_base::boolalpha`, `base10`, `hex`, `oct`, `bin`, `fixed`, `scientific`, `left`, `right`, `internal`, `showbase`, `showpoint`, `showpos`, `skipws`, `unitbuf` and `uppercase`, with the masks `basefield`, `adjustfield` and `floatfield`.

`ios` adds:
- `rdbuf()` and `rdbuf(b)`, the stream's buffer
- `tie()` and `tie(s)`, an `ostream` flushed before this stream reads or writes
- `copyfmt(other)`, copying flags, width, precision and fill

## File streams

| Class | Default mode |
|---|---|
| `ofstream` | write (`ios_base::output`), replacing the file |
| `ifstream` | read (`ios_base::input`) |
| `fstream` | read and write |

```walnut
import module.io

function main() -> int {
    std::io::ofstream: out = std::io::ofstream("scores.txt");
    out << "alice " << 12 << std::io::endl << "bob " << 9 << std::io::endl;
    out.close();

    std::io::ifstream: input = std::io::ifstream("scores.txt");
    if (!input.is_open()) then {
        std::io::cerr << "can't open scores.txt" << std::io::endl;
        return 1;
    }
    string: name = "";
    int: score = 0;
    int: total = 0;
    while (input >> name >> score) {
        total += score;
    }
    std::io::cout << "total " << total << std::io::endl;
    return 0;
} end function main
```

This prints `total 21`.

- Constructors: `ofstream()`, `ofstream(path)`, `ofstream(path, mode)`, and the same for `ifstream` and `fstream`.
- `open(path)`, `open(path, mode)`, `is_open()` and `close()`. A file that can't be opened sets `failbit`.
- The file is closed when the stream is destroyed.
- Open modes are combined with `|`: `ios_base::input`, `output`, `app` (append), `trunc` (empty the file), `ate` (start at the end), `binary` and `noreplace` (fail if the file exists). They are C++'s `in`, `out` and so on, renamed because `in` is a Walnut keyword.

## String and text streams

| Class | Reads | Writes |
|---|---|---|
| `ostringstream` | | a `string` |
| `istringstream` | a `string` | |
| `stringstream` | a `string` | a `string` |
| `otextstream` | | a `text` |
| `itextstream` | a `text` | |
| `textstream` | a `text` | a `text` |

`contents()` gives what the stream holds, and `contents(s)` replaces it. (C++ calls this `str`, which is a Walnut keyword.) The text streams work the same as the string streams, but take and give `text`.

```walnut
import module.io

function main() -> int {
    std::io::ostringstream: out;
    out << "x=" << 5 << ", y=" << 2.25;
    string: made = out.contents();

    std::io::itextstream: numbers = std::io::itextstream(`3 4`);
    int: a = 0;
    int: b = 0;
    numbers >> a >> b;
    std::io::otextstream: t;
    t << `sum ${a + b}` << '!';
    std::io::cout << made << " / " << t.contents() << std::io::endl;
    return 0;
} end function main
```

This prints `x=5, y=2.25 / sum 7!`.

A `stringstream` or `textstream` has one position for reading and one for writing, both starting at the beginning, so writing into a stream that was given a starting value overwrites it from the start, as in C++. Pass `ios_base::input | ios_base::output | ios_base::ate` to start writing at the end.

## Span streams

`ospanstream`, `ispanstream` and `spanstream` read and write a fixed block of memory given as a `char*` and a size in bytes, without allocating. Writing past the end fails and sets `badbit`. `span_data()` and `span_size()` give the block back.

```walnut
import module.io

function main() -> int {
    char*: memory = new char[16];
    std::io::ospanstream: out = std::io::ospanstream(memory, 16);
    out << "id " << 77;
    std::io::ispanstream: input = std::io::ispanstream(memory, out.tellp());
    string: word = "";
    int: id = 0;
    input >> word >> id;
    std::io::cout << word << " " << id << std::io::endl;
    delete[] memory;
    return 0;
} end function main
```

This prints `id 77`.

## Synchronized output

`osyncstream` collects what is written to it and passes it to another stream all at once, when `emit()` is called or when the `osyncstream` is destroyed. Output from different threads that each use their own `osyncstream` doesn't interleave.

```walnut
import module.io

function main() -> int {
    std::io::osyncstream: log = std::io::osyncstream(std::io::cout);
    log << "one " << 1 << std::io::endl;
    log << "two " << 2 << std::io::endl;
    log.emit();
    return 0;
} end function main
```

- `emit()` passes on what has been collected.
- `get_wrapped()` gives the buffer it writes to.
- `emit_on_flush` makes every flush (including `endl`) emit; `noemit_on_flush` turns that off; `flush_emit` flushes and emits once.

## Stream buffers

A stream formats values; a buffer moves the characters. Every stream has one, reached with `rdbuf()`.

| Class | Holds |
|---|---|
| `streambuf` | The base class |
| `filebuf` | A file, or one of the standard streams |
| `stringbuf` | A `string` |
| `textbuf` | A `text` |
| `spanbuf` | A block of memory |
| `syncbuf` | Output waiting to be emitted to another buffer |

`streambuf` has `sputn(s)`, `sputc(c)`, `sgetc(c)`, `sbumpc(c)`, `snextc(c)`, `sungetc()`, `sgetn(n)`, `in_avail()`, `pubsync()`, `pubseekoff(offset, dir)` and `pubseekpos(pos)`. A class of your own that inherits `streambuf` and overrides the `virtual` ones (`sputn`, `sgetc`, `sbumpc`, `sungetc`, `in_avail`, `pubsync` and `pubseekoff`) can be given to `ostream`, `istream` or `iostream`:

```walnut
import module.io

class counter inherits std::io::streambuf {
public:
    long int: written = 0;
    function sputn(const string&: s) override -> long int {
        written += s.size();
        return s.size();
    } end function sputn
} end class counter

function main() -> int {
    counter: c;
    std::io::ostream: out = std::io::ostream(&c);
    out << "hello " << 12345;
    std::io::cout << c.written << std::io::endl;
    return 0;
} end function main
```

This prints `11`.

`ostream << buffer` (with a `streambuf*`) copies everything the buffer has left to read into the stream.

## Your own types

A type of your own gets `<<` and `>>` with an operator that takes `std::io::ostream&` or `std::io::istream&` first and returns it. It can be a free function or a `friend` inside the class. It then works with every stream: `cout`, files, string and text streams, span streams and `osyncstream`. See [Stream operators](../../documentation.md#stream-operators) for an example.

A class without its own `<<` that has a `to_string()` method is written with that method, so it works with `<<`, `print` and `println` without an operator:

```walnut
import module.io

class Point {
public:
    int: x = 0;
    int: y = 0;
    function to_string() const -> string {
        return std::io::sprintf("(%d, %d)", x, y);
    } end function to_string
} end class Point

function main() -> int {
    Point: p;
    p.x = 3;
    std::io::println("p = ", p);
    return 0;
} end function main
```

This prints `p = (3, 0)`. `to_string()` can return a `string` or a `text`, and [`std::core::to_string(p)`](core.md#stdcoreto_string) gives the same string. `BigFloat`, `BigComplex` and `Complex<T>` from [math](math.md) print this way.

## Differences from C++

- Names that clash with Walnut keywords are renamed: `dec` is `base10`, `in` and `out` are `input` and `output`, `beg`/`cur`/`end` are `seek_beg`/`seek_cur`/`seek_end`, and `str()` is `contents()`.
- `bin` (base 2) is added alongside `oct` and `hex`.
- `peek`, `sgetc`, `sbumpc` and `snextc` take a `char&` and return `bool`, instead of returning an `int` that might be `EOF`.
- Until a precision is set, floats are written with all the digits needed to read them back exactly, instead of with 6 significant digits.
- Characters are Unicode code points: `get`, `peek` and `>>` into a `char` read a whole UTF-8 character.
- There are no wide-character streams (`wcout` and so on); `char`, `string` and `text` are already Unicode.
- `print`, `println`, `printf` and `sprintf` are added. `printf` and `sprintf` follow C's `%` placeholders, but check every argument's type and throw `std::errors::format_error` on a mismatch.
- Locales (`imbue`) aren't supported.
