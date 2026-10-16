# typography

`typography` describes how text looks: `TextStyle` holds the font, size, color, weight and decorations, and `TextMetrics` holds the measured size of some text. The [`graphics`](graphics.md) renderer draws and measures text with them. It's Fizmo's `fizmo::text`. (The module isn't called `text` because `text` is a Walnut keyword.)

```walnut
import module.typography
```

Everything is in the namespace `std::typography`.

## A first example

```walnut
import module.typography
import module.colors
import module.io

using std::typography::TextStyle;
using std::typography::FontWeight;

function main() -> int {
    TextStyle: title = TextStyle::make(32.0, std::colors::navy);
    title.set_font("Georgia");
    title.set_weight(FontWeight::bold);
    title.set_underline();
    std::io::println(title.font(), " ", title.size(), " ", title.color(), " ", title.bold(), " ", title.underline());
    std::io::println(TextStyle::is_font("Comic Sans MS"), " ", TextStyle::is_font("Not A Font"));
    return 0;
} end function main
```

This prints `Georgia 32 rgb(0, 0, 128) 1 1` and `1 0`.

## Styles

| Written | Gives |
|---|---|
| `TextStyle()` | A style with nothing set, so the defaults apply |
| `TextStyle::make(size, color = black)` | A size in pixels and a color |

| Method | Does |
|---|---|
| `set_font(name)`, `font()`, `clear_font()` | The font, by name: `"Arial"`, `"Times New Roman"`, `"Consolas"` and the rest. Case, spaces, `-` and `_` don't matter. Throws a [`typography_error`](#errors) for a name Fizmo doesn't know; `std::typography::font_names()` lists them and `TextStyle::is_font(name)` checks one |
| `set_size(px)`, `size()` | The size in pixels |
| `set_color(color)`, `color()` | The text color |
| `set_background(color)`, `clear_background()` | A color behind the text |
| `set_weight(weight)`, `weight()` | A `FontWeight`: `thin`, `extra_light`, `light`, `normal`, `medium`, `semi_bold`, `bold`, `extra_bold` or `black` |
| `set_bold(on = true)`, `bold()`, `set_italic(on = true)`, `italic()` | Shortcuts for bold and italic |
| `set_underline(on = true)`, `set_strikethrough(on = true)`, `set_overline(on = true)` | Lines through or around the text, with `underline()`, `strikethrough()` and `overline()` to read them |
| `set_letter_spacing(px)`, `set_word_spacing(px)`, `set_line_height(multiple)` | Spacing |
| `set_align(align)`, `align()` | A `TextAlign` for text in a box: `left`, `center`, `right` or `justify` |
| `set_overflow(overflow)` | A `TextOverflow` for text too big for its box: `visible`, `clip`, `ellipsis` or `word_wrap` |
| `set_transform(transform)` | A `TextTransform`: `none`, `uppercase`, `lowercase` or `capitalize` |
| `set_opacity(opacity)` | From 0.0 to 1.0 |
| `set_outline(color, width = 1.0)`, `clear_outline()` | An outline around each letter |
| `set_shadow(dx, dy, blur = 0.0, color = translucent black)`, `clear_shadow()` | A drop shadow |
| `set_max_lines(count)`, `set_indent(px)` | Limits and indents for text in a box |

## Measuring

`TextMetrics` is what the renderer's `measure_text` gives: the fields `width` and `height` in pixels, `ascent` (above the baseline) and `descent` (below it), and `is_empty()`.

## Errors

`set_font` throws a `std::errors::typography_error` for an unknown font. Its `what()` (or its `message` field) says which name it was.
