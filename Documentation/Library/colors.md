# colors

`colors` has `Color`, an RGBA color with 8-bit channels, plus conversions to and from hex, HSL and HSV, color math (mixing, lightening, contrast) and the 148 CSS named colors. The `images`, `windows`, `graphics` and `typography` modules all use it.

```walnut
import module.colors
```

Everything is in the namespace `std::colors`. `using std::colors::Color;` brings in the class under its short name.

## A first example

```walnut
import module.colors
import module.io

using std::colors::Color;

function main() -> int {
    Color: orange = Color::from_hex("#ff8000");
    std::io::println(orange, " ", orange.to_hex(), " ", orange.to_hsl());
    std::io::println(orange.lighten(0.2), " ", std::colors::named("Rebecca Purple"), " ", std::colors::sky_blue);
    std::io::println(std::colors::red.lerp(std::colors::blue, 0.5), " ", std::colors::black.contrast_ratio(std::colors::white));
    return 0;
} end function main
```

This prints `rgb(255, 128, 0) #ff8000 hsl(30.11764705882352941176470588235294, 1, 0.5, 1)`, then `rgb(255, 179, 102) rgb(102, 51, 153) rgb(135, 206, 235)` and `rgb(128, 0, 128) 21`.

## Making one

| Written | Gives |
|---|---|
| `Color()` | Opaque black |
| `Color(r, g, b)`, `Color(r, g, b, a)` | Channels from 0 to 255; values outside that range are clamped |
| `Color::rgb(r, g, b)`, `Color::rgba(r, g, b, a)` | The same |
| `Color::gray(level)`, `Color::gray(level, a)` | A gray with all three channels at `level` |
| `Color::from_unit(r, g, b, a = 1.0)` | Channels from 0.0 to 1.0 |
| `Color::from_hex(text)` | `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa`; the `#` is optional. Throws a [`color_error`](#errors) for anything else |
| `Color::from_hsl(hue, saturation, lightness, alpha = 1.0)`, `Color::from_hsl(hsl)` | Hue in degrees; the rest from 0.0 to 1.0 |
| `Color::from_hsv(hue, saturation, value, alpha = 1.0)`, `Color::from_hsv(hsv)` | The same, from hue, saturation and value |
| `Color::from_packed(n)` | From the 32-bit number `0xRRGGBBAA` that `packed()` gives |
| `Color::random()`, `Color::random(true)` | A random opaque color, or one with a random alpha too |
| `std::colors::named(name)` | A CSS color name. Case, spaces, `_` and `-` don't matter, so `"Rebecca Purple"` and `"rebeccapurple"` both work. Throws a `color_error` for an unknown name |
| `std::colors::parse(text)` | A hex color or a name |

Every CSS color is also a constant, in snake case: `std::colors::alice_blue`, `std::colors::dark_slate_gray`, `std::colors::white` and so on, plus `std::colors::transparent`. `std::colors::is_named(name)` checks a name.

## Reading it

| Method | Gives |
|---|---|
| `r`, `g`, `b`, `a` (fields), `red()`, `green()`, `blue()`, `alpha()` | The channels, 0 to 255 |
| `red_unit()`, `green_unit()`, `blue_unit()`, `alpha_unit()` | The channels as floats from 0.0 to 1.0 |
| `to_hex()`, `to_hex(true)` | `"#rrggbb"`, or `"#rrggbbaa"` with the alpha |
| `to_string()` | `"rgb(r, g, b)"`, or `"rgba(r, g, b, a)"` when it isn't opaque |
| `packed()` | The color as one number, `0xRRGGBBAA` |
| `to_hsl()`, `to_hsv()` | An `HSL` or `HSV` struct, with the fields `hue`, `saturation`, `lightness` or `value`, and `alpha` |
| `hue()`, `saturation()`, `lightness()`, `brightness()` | Single parts of those |
| `luminance()` | Relative luminance from 0.0 to 1.0, as the WCAG contrast rules define it |
| `contrast_ratio(other)` | The WCAG contrast ratio, from 1 to 21 |
| `is_dark()`, `is_light()` | Whether black or white text would read better on it |
| `is_opaque()`, `is_transparent()` | Alpha of 255 or 0 |
| `distance(other)` | How far apart two colors are in RGB |
| `==`, `!=` | All four channels equal |

## New colors from old ones

These leave the color alone and give back a new one.

| Method | Gives |
|---|---|
| `with_red(v)`, `with_green(v)`, `with_blue(v)`, `with_alpha(v)` | One channel changed |
| `with_opacity(unit)` | The alpha set from 0.0 to 1.0 |
| `lerp(other, t)`, `mix(other, t = 0.5)` | Part way to `other`; `t` is from 0.0 to 1.0. `std::colors::lerp(x, y, t)` is the same |
| `lighten(amount)`, `darken(amount)` | Lightness moved by `amount` (0.0 to 1.0) |
| `saturate(amount)`, `desaturate(amount)` | Saturation moved by `amount` |
| `rotate_hue(degrees)`, `complement()` | The hue turned, or turned by 180 degrees |
| `grayscale()`, `inverted()` | Gray by perceived brightness, or each channel flipped |
| `blend_over(background)` | This color painted over `background`, using both alphas |
| `premultiplied()` | Each channel multiplied by the alpha |
| `+`, `-` | Channel by channel, clamped; the alpha is kept |
| `* k` | Each channel scaled by `k`; the alpha is kept |

## Errors

`from_hex`, `named` and `parse` throw a `std::errors::color_error` for text they can't read. Its `what()` (or its `message` field) says what was wrong:

```walnut
import module.colors
import module.io

function main() -> int {
    try {
        std::colors::Color: c = std::colors::named("blurple");
    } catch (const std::errors::color_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `problem: 'blurple' is not a color name`.
