# strings

`strings` has the string helpers that C++ and Fizmo programs reach for: splitting, joining, replacing, padding, changing case, testing what a string holds, comparing without case, and more, plus tests for single characters.

```walnut
import module.strings
```

Everything is in the namespace `std::strings`. Every function also works as a method on the string (or character) it takes first, so these two lines do the same thing:

```walnut
import module.strings

function main() -> int {
    auto: a = std::strings::split("a,b,c", ",");
    auto: b = "a,b,c".split(",");
    return 0;
} end function main
```

The functions work on both `string` and `text`, and give back the same type they were given. Positions and counts are in characters (code points), as everywhere else in Walnut, and the letter tests and case changes follow Unicode, using Fizmo's Unicode tables. The [built-in methods](../../documentation.md#methods-on-built-in-values) (`length`, `substr`, `find`, `contains`, `starts_with`, `ends_with`, `to_upper`, `to_lower`, `trim` and the rest) always come first when a name is the same, so `s.to_upper()` is the built-in one and `std::strings::to_upper(s)` is the full Unicode one (`"straße"` becomes `"STRASSE"`).

## Contents

1. [A first example](#a-first-example)
2. [Searching](#searching)
3. [Splitting and joining](#splitting-and-joining)
4. [Replacing and removing](#replacing-and-removing)
5. [Trimming and case](#trimming-and-case)
6. [Building and cutting](#building-and-cutting)
7. [Testing](#testing)
8. [Comparing](#comparing)
9. [Characters one at a time](#characters-one-at-a-time)
10. [WhereQuery on strings](#wherequery-on-strings)
11. [Single characters](#single-characters)

## A first example

```walnut
import module.strings
import module.io

function main() -> int {
    string: line = "  apples, pears ,figs  ";
    auto: parts = line.split(",");
    for (long int: i = 0; i < parts.size(); i++) {
        parts[i] = parts[i].trim().capitalize();
    }
    string: joined = std::strings::join(parts, " | ");
    std::io::println(joined);
    std::io::println("42".pad_left(6, '0'), " ", "snake_case_name".snake_to_camel(), " ", "kitten".levenshtein("sitting"));
    return 0;
} end function main
```

This prints `Apples | Pears | Figs` and `000042 snakeCaseName 3`.

## Searching

`needle` can be a `string`, a `text` or a `char`. A position of `-1` means "not found".

| Function | Gives |
|---|---|
| `find(s, needle)`, `find_from(s, needle, start)` | The position of the first match, from the start or from `start` |
| `rfind(s, needle)`, `find_last(s, needle)` | The position of the last match |
| `find_nth(s, needle, n)` | The position of match number `n` (counting from `0`) |
| `find_all(s, needle)` | A `Vector<long int>` of every position |
| `count(s, needle)` | How many times `needle` appears, without overlaps |
| `contains(s, needle)`, `starts_with(s, needle)`, `ends_with(s, needle)` | `bool` |
| `find_first_of(s, chars)`, `find_last_of(s, chars)` | The first or last position of any character in `chars` |
| `find_first_not_of(s, chars)`, `find_last_not_of(s, chars)` | The first or last position of a character not in `chars` |
| `contains_any(s, chars)` | Whether any character of `chars` is in `s` |

As methods, `find` is the built-in one, which gives `undefined` instead of `-1` when there's no match; `find_from` and the others come from `strings`.

## Splitting and joining

Splitting gives a [`Vector`](collections.md#vector) of the same string type.

| Function | Gives |
|---|---|
| `split(s, separator)` | The pieces between each `separator`, keeping empty ones: `"a,,b".split(",")` is `"a" "" "b"`. An empty separator splits into single characters |
| `split_n(s, separator, n)` | At most `n` pieces; the last one holds the rest |
| `split_any(s, chars)` | Splits at any character in `chars` |
| `split_whitespace(s)` | The pieces between runs of spaces, tabs and line breaks, without empty ones |
| `lines(s)` | The lines, without their `\n` or `\r\n` |
| `words(s)` | The words: runs of letters, digits, `_` and `'` |
| `chunks(s, n)` | Pieces of `n` characters (the last may be shorter) |
| `chars(s)` | A `Vector<char>` of the characters |
| `join(parts, separator)`, `separator.join(parts)` | The pieces with `separator` between them |
| `concat(parts)` | The pieces with nothing between them |

## Replacing and removing

| Function | Gives |
|---|---|
| `replace(s, old_text, new_text)` | Every `old_text` replaced |
| `replace_first(s, old_text, new_text)`, `replace_last(...)` | Only the first or last one |
| `replace_n(s, old_text, new_text, n)` | The first `n` |
| `remove(s, needle)` | Every `needle` taken out |
| `remove_prefix(s, prefix)`, `remove_suffix(s, suffix)` | `s` without the prefix or suffix, if it has it |
| `remove_if(s, pred)` | The characters that fail `pred` |
| `filter(s, pred)` | The characters that pass `pred` |

## Trimming and case

| Function | Gives |
|---|---|
| `trim(s)`, `trim_left(s)`, `trim_right(s)` (`ltrim`, `rtrim`) | Without whitespace at both ends, the start or the end |
| `trim_chars(s, chars)` | Without any of `chars` at either end |
| `normalize_whitespace(s)` | Trimmed, with every run of whitespace made one space |
| `to_upper(s)`, `to_lower(s)` | Every letter changed, including ones that change length (`ß` becomes `SS`) |
| `capitalize(s)` | The first letter in title case and the rest lower case |
| `title_case(s)` | Each word capitalized |
| `swap_case(s)` | Upper case made lower and lower case made upper |
| `camel_to_snake(s)`, `snake_to_camel(s)` | `myName` ↔ `my_name` |

## Building and cutting

| Function | Gives |
|---|---|
| `repeated(s, n)` | `n` copies of `s` |
| `pad_left(s, width, fill)`, `pad_right(...)`, `center(...)` | `s` padded with `fill` (a space if left out) to `width` characters |
| `zfill(s, width)` | Padded with zeros on the left, after any sign: `"-7".zfill(4)` is `"-007"` |
| `insert(s, index, piece)` | `piece` put in before position `index` |
| `erase(s, index, count)` | `count` characters (`1` if left out) taken out at `index` |
| `slice(s, start, stop)`, `substring(...)` | The characters from `start` up to `stop`; negative positions count from the end, so `"hello".slice(1, -1)` is `"ell"` |
| `left(s, n)`, `right(s, n)` | The first or last `n` characters |
| `truncate(s, width)`, `truncate(s, width, ellipsis)` | At most `width` characters, ending in `"..."` (or `ellipsis`) when cut |
| `indent(s, n)` | Every line indented by `n` spaces |
| `wrap(s, width)` | Line breaks added between words so no line is longer than `width` |
| `reverse(s)`, `reversed(s)` | The characters in reverse order |
| `sorted(s)` | The characters in order |
| `map_chars(s, func)` | Each character `c` replaced with `func(c)` |
| `escape(s)`, `unescape(s)` | Special characters written as `\n`, `\t`, `\"` and so on, and back |
| `url_encode(s)`, `url_decode(s)` | Percent-encoding for URLs |
| `char_at(s, i)` | The character at `i` |

## Testing

| Function | `true` when |
|---|---|
| `is_blank(s)` | `s` is empty or only whitespace |
| `is_digits(s)` | `s` is one or more of `0`–`9` |
| `is_integer(s)`, `is_number(s)` | `s` is a whole number, or any number such as `-2.5e3` |
| `is_alpha(s)`, `is_alnum(s)`, `is_hex(s)` | Every character is a letter, a letter or digit, or a hex digit (and there is at least one) |
| `is_upper(s)`, `is_lower(s)` | `s` has letters, all upper or all lower case |
| `is_ascii(s)` | Every character is ASCII |
| `is_palindrome(s)` | The letters and digits read the same both ways, ignoring case |
| `matches_glob(s, pattern)` | `s` matches a pattern with `*` (any run) and `?` (any one character) |

## Comparing

| Function | Gives |
|---|---|
| `equals_ignore_case(a, b)` | Whether they are equal ignoring case |
| `compare_ignore_case(a, b)` | `-1`, `0` or `1`, ignoring case |
| `starts_with_ignore_case(a, b)`, `ends_with_ignore_case(a, b)`, `contains_ignore_case(a, b)` | `bool`, ignoring case |
| `levenshtein(a, b)`, `distance(a, b)` | How many single-character changes turn one into the other |
| `common_prefix(a, b)`, `common_suffix(a, b)` | The start or end they share |
| `word_count(s)`, `line_count(s)` | How many words or lines |

## Characters one at a time

`count_if(s, pred)`, `all_of(s, pred)`, `any_of(s, pred)`, `none_of(s, pred)`, `find_if(s, pred)` and `for_each(s, func)` call `pred` or `func` with each `char`:

```walnut
import module.strings

function main() -> int {
    string: code = "A1-B22-C3";
    long int: digits = code.count_if([](char: c) -> bool { return c.is_digit(); });
    string: letters = code.filter([](char: c) -> bool { return c.is_alpha(); });
    long int: dash = code.find_if([](char: c) -> bool { return c == '-'; });
    return 0;
} end function main
```

Here `digits` is `4`, `letters` is `"ABC"` and `dash` is `2`.

## WhereQuery on strings

`where(s, pred)` and `where(s, c)` give a [WhereQuery](collections.md#wherequery) over the characters that pass `pred` or equal `c`, so they can be counted, read or changed in place. The string must be a variable, since the query changes it:

```walnut
import module.strings

function main() -> int {
    string: word = "banana";
    word.where('a').replace('o');
    long int: ns = word.where('n').count();
    word.where([](char: c) -> bool { return c == 'b'; }).remove();
    return 0;
} end function main
```

Here `ns` is `2` and `word` ends as `"onono"`.

## Single characters

These take one `char`, and also work as methods on a `char`, as in `c.is_digit()`.

| Function | Gives |
|---|---|
| `is_digit(c)`, `is_hex_digit(c)` | Whether `c` is a decimal digit, or a hex digit |
| `is_alpha(c)`, `is_letter(c)`, `is_alnum(c)` | Whether `c` is alphabetic, a letter, or a letter or digit |
| `is_space(c)`, `is_punct(c)` | Whether `c` is whitespace, or punctuation |
| `is_upper(c)`, `is_lower(c)` | Whether `c` is an upper or lower case letter |
| `is_ascii(c)` | Whether `c` is ASCII |
| `to_upper(c)`, `to_lower(c)` | `c` in upper or lower case |
