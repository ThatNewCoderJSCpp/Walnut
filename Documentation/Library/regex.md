# regex

`regex` finds and checks text with patterns. It has three classes, from the most familiar to the easiest to read:

- [`Regex`](#regex) takes a classic regular expression, like `\d{4}-\d{2}`.
- [`Pattern`](#pattern) builds the same thing from named steps, so nothing has to be escaped or remembered: `Pattern::digit().times(4).and_then_literal("-")`.
- [`CharSet`](#charset) says which characters are allowed, with methods like `disable_letters()` and `disable_special()`. It's what to use for blocking keys in a text box.

```walnut
import module.regex
```

Everything is in the namespace `std::regex`. It all comes from Fizmo's regex engine, which understands UTF-8: `.` is one character, `\p{L}` is a letter in any script, and positions in matches count characters, like the rest of Walnut's strings. Any pattern can tell you what it does in plain English with `explain()`.

## Contents

1. [A first example](#a-first-example)
2. [Regex](#regex)
3. [Match](#match)
4. [Flags](#flags)
5. [Syntax](#syntax)
6. [Pattern](#pattern)
7. [CharSet](#charset)
8. [Shortcuts](#shortcuts)

## A first example

```walnut
import module.regex
import module.io

using std::regex::Regex;
using std::regex::Pattern;
using std::regex::CharSet;

function main() -> int {
    Regex: date = Regex("(?<year>\\d{4})-(?<month>\\d{2})-(?<day>\\d{2})");
    std::regex::Match: m = date.search("Due 2026-10-18, paid");
    if (m.found()) then {
        std::io::println("year ", m.named("year"), " at ", m.start());
    }
    std::io::println(date.replace("2026-10-18", "$<day>/$<month>/$<year>"));

    Pattern: price = Pattern::literal("$").and_then(Pattern::digits(1).capture_as("dollars"));
    std::io::println(price.to_regex(), " matches $25: ", price.matches("$25"));

    CharSet: amount = CharSet::everything().disable_letters().disable_special().disable_spaces();
    std::io::println(amount.accepts("3.14"), " ", amount.accepts("3,14"));
    std::io::println(date.explain());
    return 0;
} end function main
```

## Regex

`Regex(pattern)` or `Regex(pattern, flags)`. A pattern that doesn't make sense doesn't throw: `ok()` is `false`, `error()` says what's wrong (`"this ( is never closed"`) and `error_position()` says where.

| Method | Gives |
|---|---|
| `matches(text)` | `true` if the whole text fits |
| `contains(text)` | `true` if it fits anywhere |
| `whole(text)` | the [`Match`](#match) of the whole text |
| `search(text)`, `search_from(text, start)` | the first match, from the start or from a position |
| `match_at(text, position)` | a match that starts exactly at `position` |
| `find_all(text)` | every match as a `Vector<Match>` |
| `count(text)` | how many matches |
| `replace(text, with)`, `replace_first(text, with)`, `replace_count(text, with, limit)` | `with` can use `$1`, `$<name>`, `$&` (the whole match), `` $` `` and `$'` (the text before and after) and `$$` |
| `split(text)` | the pieces between matches, as a `Vector<string>`; captured groups are kept between them |
| `explain()` | the pattern in plain English |
| `pattern()`, `group_count()` | the pattern and how many groups it has (counting the whole match) |
| `set_step_limit(n)`, `gave_up()` | see below |
| `Regex::escape(text)` | `text` with every special character escaped, to match it literally |

Some patterns, like `^(a|aa)+$`, take an enormous time on some texts in every engine like this one. Instead of hanging, a search stops after 20 million steps (change it with `set_step_limit`) and finds nothing, and `gave_up()` is `true`. `x++` and `(?>...)` in a pattern avoid the problem.

`explain()` turns the hardest patterns into a list:

```text
1. the start of the text
2. group 1 ("user"), which is one or more of: a letter, digit or underscore (a-z, A-Z, 0-9, _) or '.'
3. '@'
4. one or more of:
   - group 2, which is:
     - one or more of: a letter, digit or underscore (a-z, A-Z, 0-9, _)
     - '.'
5. at least 2 of: 'a' to 'z'
6. the end of the text
```

## Match

A `Match` is what `search` and the others give back. `found()` (or using it as a `bool`) says whether there was one. It keeps its own copy of the text, so it stays valid however long you keep it.

| Method | Gives |
|---|---|
| `value()` | the matched text |
| `group(n)`, `named("name")` | a group's text, or `""` if it didn't take part |
| `has_group(n)`, `has_named("name")` | whether it took part |
| `start()`, `stop()`, `length()` | where the match is, in characters |
| `group_start(n)`, `group_stop(n)` | where a group is, or `-1` |
| `before()`, `after()` | the text before and after the match |
| `group_count()` | how many groups, counting the whole match as group 0 |

## Flags

Flags are numbers in `std::regex` that can be added together: `Regex("colou?r", std::regex::ignore_case + std::regex::multiline)`. The same letters work inside a pattern: `(?i)` for the rest of it, or `(?i:...)` for one part.

| Flag | Letter | Effect |
|---|---|---|
| `ignore_case` | `i` | `a` matches `A`, `é` matches `É` |
| `multiline` | `m` | `^` and `$` match at every line, not just the ends of the text |
| `dot_all` | `s` | `.` matches line breaks too |
| `extended` | `x` | spaces and `# comments` in the pattern are ignored |
| `unicode_classes` | `u` | `\d`, `\w`, `\s` and `\b` understand every script instead of only ASCII |

## Syntax

Inside a Walnut string, a backslash is written `\\`, so `\d` is `"\\d"`.

| Write | Matches |
|---|---|
| `abc` | that text |
| `.` | any character except a line break |
| `[abc]`, `[a-z]`, `[^abc]` | one of those, one in that range, anything but those |
| `[\p{L}--[aeiou]]`, `[a-z&&[^aeiou]]` | letters except vowels: `--` takes away, `&&` keeps what's in both |
| `[[:alpha:]]`, `[[:digit:]]`, ... | POSIX classes |
| `\d`, `\w`, `\s` | a digit, a letter/digit/underscore, whitespace; `\D`, `\W`, `\S` are the opposite |
| `\p{L}`, `\p{Lu}`, `\p{N}`, `\p{P}`, `\p{S}`, `\p{Emoji}`, `\p{ASCII}`, ... | a Unicode category; `\P{...}` is the opposite |
| `\t`, `\n`, `\x41`, `é`, `\u{1F600}` | a tab, a line feed, characters by number |
| `^`, `$` | the start and end of the text (or of a line, with `m`) |
| `\A`, `\z` | the start and end of the text, always |
| `\b`, `\B` | a word boundary, and not one |
| `x*`, `x+`, `x?` | zero or more, one or more, zero or one |
| `x{3}`, `x{2,5}`, `x{2,}` | counted |
| `x*?`, `x+?` | as few as possible |
| `x*+`, `x++` | as many as possible, never giving any back |
| `a\|b` | either |
| `(x)`, `(?<name>x)`, `(?:x)` | a numbered group, a named group, a group that doesn't capture |
| `(?=x)`, `(?!x)`, `(?<=x)`, `(?<!x)` | followed by, not followed by, preceded by, not preceded by |
| `(?>x)` | tried one way only |
| `\1`, `\k<name>` | the same text a group matched |

## Pattern

A `Pattern` is a regex built from named pieces. Every method gives a new pattern, so they chain. `to_regex()` shows the classic version (a good way to learn it), `compile()` gives a `Regex`, `matches(text)` checks the whole text, and `explain()` describes it.

```walnut
Pattern: code = Pattern::one_of(CharSet::uppercase()).times(3)
    .and_then_literal("-")
    .and_then(Pattern::digit().times(4))
    .whole();
code.to_regex();            // \A\p{Lu}{3}-\d{4}\z
code.matches("ABC-1234");   // true
```

| Pieces | |
|---|---|
| `literal(text)` | that text, exactly |
| `one_of(set)`, `one_of_chars("abc")`, `not_one_of("abc")` | one character from a [`CharSet`](#charset) or a list |
| `digit()`, `letter()`, `whitespace()`, `word_character()`, `any_character()`, `any_except_newline()` | one character |
| `start_of_text()`, `end_of_text()`, `start_of_line()`, `end_of_line()`, `word_boundary()` | places |
| `either(a, b)`, `a.or_else(b)` | one or the other |
| `same_as("name")`, `same_as_group(n)` | the text a group matched |
| `raw("\\d+")` | classic syntax, to mix in |
| `digits(n)`, `integer_number()`, `unsigned_integer()`, `decimal_number()`, `identifier()`, `hex_number()`, `hex_color()`, `email()`, `word()` | ready-made |

| Steps | |
|---|---|
| `a.and_then(b)`, `a.and_then_literal("text")` | one after the other |
| `optional()`, `one_or_more()`, `zero_or_more()` | repeating |
| `times(n)`, `times_between(min, max)`, `at_least(n)`, `at_most(n)` | counted |
| `as_few_as_possible()`, `no_backtracking()` | how a repeat behaves |
| `capture()`, `capture_as(name)`, `group()` | grouping |
| `followed_by(p)`, `not_followed_by(p)`, `preceded_by(p)`, `not_preceded_by(p)` | looking around without taking any text |
| `whole()` | the entire text has to match |

## CharSet

A `CharSet` is a set of characters. Start from `everything()` or `nothing()`, then allow or disable kinds of characters; later rules win over earlier ones. The `allow_` and `disable_` methods change the set and return it, so they chain.

```walnut
CharSet: amount = CharSet::everything().disable_letters().disable_special().disable_spaces();
amount.accepts("3.14");          // true: disable_special() keeps . for decimals
amount.accepts("3,14");          // false
amount.filter("a1b.2 c!");       // "1.2"
amount.describe();               // any character except: letters; punctuation and symbols other than '.'; spaces, tabs and line breaks

CharSet: name = CharSet::nothing().allow_letters().allow(" '-");
name.accepts("Anne-Marie O'Neil");   // true
```

| Starting points | |
|---|---|
| `everything()`, `nothing()`, `of("abc")`, `between('a', 'f')` | |
| `digits()`, `numbers()`, `letters()`, `uppercase()`, `lowercase()`, `spaces()`, `punctuation()`, `symbols()`, `special_characters()`, `emoji()`, `hex_digits()`, `word_characters()`, `ascii()` | ready-made sets |

| Changing it | |
|---|---|
| `allow_X()`, `disable_X()` for `digits`, `numbers`, `letters`, `uppercase`, `lowercase`, `spaces`, `newlines`, `punctuation`, `symbols`, `special`, `emoji`, `control` | add or take away a kind |
| `disable_special()` | punctuation and symbols, but keeps `.` so decimals still work |
| `disable_special_except(keep)`, `disable_punctuation_except(keep)`, `disable_symbols_except(keep)` | the same, keeping exactly the characters in `keep` (`""` keeps none) |
| `allow(chars)`, `disable(chars)`, `allow_char(c)`, `disable_char(c)`, `allow_range(a, b)`, `disable_range(a, b)`, `allow_set(other)`, `disable_set(other)` | specific characters, ranges and other sets |
| `only_ascii()`, `allow_hex_digits()` | |

| Using it | |
|---|---|
| `contains(c)` | whether one character is in it |
| `accepts(text)` | whether every character of `text` is |
| `first_rejected(text)` | where the first one that isn't is, or `-1` |
| `filter(text)` | `text` with the others taken out |
| `inverted()` | everything it doesn't contain |
| `describe()`, `to_regex()` | what it is, in English and as a regex class |

The kinds: **digits** are decimal digits in any script; **numbers** also include `½`, `²` and Roman numerals; **spaces** are all whitespace, including line breaks; **special** is punctuation and symbols together; **emoji** are pictographs and flags (not `#`, `*` or digits).

## Shortcuts

`std::regex::matches(pattern, text)`, `std::regex::search(pattern, text)`, `std::regex::replace(pattern, text, with)` and `std::regex::explain(pattern)` do one thing with a pattern without keeping a `Regex` around.
