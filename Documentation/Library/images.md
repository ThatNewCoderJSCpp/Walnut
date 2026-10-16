# images

`images` has `Image`, a picture held in memory as RGBA pixels. Images load from and save to PNG, JPEG, BMP, GIF, WebP, TGA, QOI, ICO, PNM and other formats, and have pixel access, cropping, resizing, flipping, rotating and filters such as blur, grayscale and sepia. It's Fizmo's `BitmapImage`. The `graphics` module draws images and makes textures from them.

```walnut
import module.images
```

Everything is in the namespace `std::images`. Colors are [`std::colors::Color`](colors.md).

## A first example

```walnut
import module.images
import module.colors
import module.io

using std::images::Image;
using std::colors::Color;

function main() -> int {
    Image: img = Image::make(64, 32, std::colors::white);
    img.fill_rect(8, 8, 16, 16, std::colors::red);
    img.set_pixel(40, 10, Color(0, 128, 255));
    std::io::println(img.width(), " by ", img.height(), ", pixel (10, 10) is ", img.pixel(10, 10));
    Image: small = img.resized(16, 8).grayscale();
    std::io::println(small.width(), " ", small.pixel(4, 4));
    img.save("example.png");
    std::io::println(Image::load("example.png").pixel(40, 10));
    return 0;
} end function main
```

This prints `64 by 32, pixel (10, 10) is rgb(255, 0, 0)`, then `16 rgb(76, 76, 76)` and `rgb(0, 128, 255)`.

## Making and loading

| Written | Gives |
|---|---|
| `Image()` | An empty image (`is_empty()` is `true`) |
| `Image::make(width, height)`, `Image::make(width, height, fill)` | A new image, transparent or filled with a color. Throws an [`image_error`](#errors) for a size of 0 or less |
| `Image::load(path)` | The image in a file; the format is worked out from the file's contents. Throws an `image_error` when it can't be read |
| `Image::can_read(extension)`, `Image::can_write(extension)` | Whether a format such as `"png"` or `".webp"` can be loaded or saved |

## Reading and writing pixels

| Method | Does |
|---|---|
| `width()`, `height()`, `is_empty()` | The size |
| `contains(x, y)` | Whether `(x, y)` is inside the image |
| `pixel(x, y)` | The color at `(x, y)`. Throws an `image_error` outside the image |
| `set_pixel(x, y, color)` | Changes one pixel. Throws an `image_error` outside the image |
| `fill(color)` | Sets every pixel |
| `fill_rect(x, y, width, height, color)` | Sets a rectangle; the parts outside the image are skipped |
| `draw(other, x = 0, y = 0)` | Paints `other` on top, with its top-left corner at `(x, y)`, blending by alpha |
| `save(path)`, `save(path, quality)` | Writes the image. The format comes from the extension (`.png`, `.jpg`, `.bmp`, `.webp`, `.qoi` and the rest); `quality` (default 90) is for lossy formats. Throws an `image_error` when it can't be written |

## New images from old ones

These leave the image alone and give back a new one, so they can be chained: `img.cropped(0, 0, 32, 32).flipped_horizontally().blurred(2)`.

| Method | Gives |
|---|---|
| `resized(width, height)` | Stretched to a new size |
| `scaled(factor)` | Both sides multiplied by `factor` |
| `cropped(x, y, width, height)` | A rectangle cut out; the parts outside the image are left off |
| `flipped_horizontally()`, `flipped_vertically()` | Mirrored |
| `rotated(quarter_turns)`, `rotated_right()`, `rotated_left()` | Turned clockwise by 90-degree steps |
| `grayscale(amount = 1.0)` | Drained of color; `amount` from 0.0 (unchanged) to 1.0 (fully gray) |
| `sepia(amount = 1.0)` | A sepia tone, with the same kind of `amount` |
| `inverted()` | Every channel flipped |
| `blurred(radius)` | A box blur |
| `brightened(amount)` | `amount` (from -255 to 255) added to each channel |
| `with_contrast(factor)` | Contrast scaled by `factor`, from 0.0 (flat gray) to 1.0 (unchanged) |
| `with_saturation(amount)` | Saturation scaled by `amount`, from 0.0 to 1.0 |
| `hue_rotated(degrees)` | Every hue turned |
| `with_opacity(amount)` | Alpha scaled by `amount`, from 0.0 to 1.0 |
| `replace_color(from, to)` | Every pixel that is exactly `from` changed to `to` |

## Errors

`make`, `load`, `save`, `pixel`, `set_pixel`, `resized`, `scaled` and `cropped` throw a `std::errors::image_error` when they can't do what was asked. Its `what()` (or its `message` field) says why:

```walnut
import module.images
import module.io

function main() -> int {
    try {
        std::images::Image: img = std::images::Image::load("no-such-file.png");
    } catch (const std::errors::image_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `problem: can't load 'no-such-file.png': ` followed by the reason the file couldn't be read.
