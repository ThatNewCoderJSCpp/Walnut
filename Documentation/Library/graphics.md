# graphics

`graphics` draws into a [window](windows.md): shapes, paths, gradients, images, textures, sprites and text, with transforms, clipping, cameras, blend modes and off-screen render targets; 3D meshes, models, materials and lighting; and tilemaps. The `Renderer` picks the best backend it can (Vulkan, OpenGL or software) and falls back on its own. It's Fizmo's `fizmo::graphics`.

```walnut
import module.graphics
```

Everything is in the namespace `std::graphics`. It uses [`Color`](colors.md), [`Image`](images.md), [`Window`](windows.md) and [`TextStyle`](typography.md), which it also makes available as `std::graphics::Color` and so on.

## A first example

```walnut
import module.graphics
import module.colors
import module.io

using std::graphics::Renderer;
using std::graphics::Paint;
using std::graphics::Point;
using std::colors::Color;

function main() -> int {
    std::windows::Window: win = std::windows::Window::open("Shapes", 320, 240);
    Renderer: r = Renderer::make(win);
    std::typography::TextStyle: style = std::typography::TextStyle::make(20.0, std::colors::white);
    while (win.poll()) {
        r.begin_frame();
        r.clear(std::colors::midnight_blue);
        r.fill_rect(20, 20, 100, 60, std::colors::tomato);
        r.draw_circle(220, 70, 40, Paint::fill_and_stroke(std::colors::gold, std::colors::white, 3));
        std::collections::Vector<Point>: tri = {Point(40, 200), Point(140, 200), Point(90, 120)};
        r.draw_polygon(tri, Paint::fill(std::colors::lime_green));
        r.draw_text(170, 180, "Walnut", style);
        r.present();
        std::images::Image: shot = r.capture();
        std::io::println(shot.width(), "x", shot.height(), " ", shot.pixel(30, 30), " ", shot.pixel(220, 70));
        win.close();
    }
    return 0;
} end function main
```

This prints `320x240 rgb(255, 99, 71) rgb(255, 215, 0)`: the tomato rectangle and the gold circle, read back from the frame it showed. A real program keeps looping until the window is closed; [`game`](game.md) has a ready-made loop.

## The renderer

| Written | Does |
|---|---|
| `Renderer::make(window)`, `Renderer::make(window, backend, vsync = false)` | Starts drawing into a window. `Backend` is `best` (the default), `gpu`, `software`, `vulkan` or `opengl`. Throws a [`graphics_error`](#errors) when the window isn't open |
| `begin_frame()`, `present()` | Start and finish a frame; `present()` shows it |
| `clear(color = black)` | Fills the frame with one color |
| `capture()` | The last frame shown by `present()`, as an `Image`. The software backend can't read frames back, so there it gives an empty image |
| `backend_name()`, `is_gpu()`, `is_ready()` | Which backend it's using, and whether it can draw |
| `set_vsync(on)`, `vsync()` | Waiting for the display before showing a frame |
| `width()`, `height()` | The size of the area being drawn |
| `frame_ms()` | How long the last frame took |

A frame is drawn between `begin_frame()` and `present()`. Everything drawn while the window is closed is ignored.

## Paint

`Paint` says how a shape is filled and outlined:

| Written | Gives |
|---|---|
| `Paint::fill(color)` | A solid fill |
| `Paint::stroke(color, width = 1.0)` | An outline only |
| `Paint::fill_and_stroke(fill, stroke, width = 1.0)` | Both |
| `Paint::gradient(gradient)` | A fill with a `Gradient` |

It can be changed afterwards with `set_fill_color`, `set_stroke(color, width)`, `set_style(PaintStyle::fill / stroke / fill_and_stroke)`, `set_line_cap(LineCap::flat / round / square)`, `set_line_join(LineJoin::miter / round / bevel)`, `set_opacity`, `set_origin(Origin::...)` (which point of a rectangle `x` and `y` give, `top_left` by default) and `set_stroke_gradient`. `set_angles(start, end)` limits circles and ellipses to an arc, in degrees.

A `Gradient` is made with `Gradient::linear(x0, y0, x1, y1)` or `Gradient::radial(cx, cy, radius)`, either with two more color arguments for the ends or with `add_stop(position, color)` calls. `set_spread(Spread::pad / tile / mirror)` says what happens past the ends, `set_focal(x, y)` moves a radial gradient's focus, and `color_at(t)` samples it.

## Drawing shapes

Coordinates are in pixels from the top-left corner, as floats (whole numbers work too).

| Method | Draws |
|---|---|
| `draw_pixel(x, y, color)` | One pixel |
| `draw_line(x1, y1, x2, y2, paint)`, `draw_line(x1, y1, x2, y2, color, width = 1.0)` | A line |
| `draw_rect(x, y, w, h, paint)`, `fill_rect(x, y, w, h, color)`, `stroke_rect(x, y, w, h, color, width = 1.0)` | A rectangle |
| `draw_rounded_rect(x, y, w, h, radius, paint)` | A rectangle with round corners |
| `draw_circle(cx, cy, radius, paint)`, `fill_circle(cx, cy, radius, color)`, `stroke_circle(cx, cy, radius, color, width = 1.0)` | A circle |
| `draw_ellipse(cx, cy, rx, ry, paint)`, `draw_arc(cx, cy, rx, ry, paint)` | An ellipse, or part of one (set the angles on the paint) |
| `draw_triangle(x1, y1, x2, y2, x3, y3, paint)` | A triangle |
| `draw_polygon(points, paint)`, `draw_polyline(points, paint, closed = false)` | A shape through a `Vector<Point>` |
| `draw_path(path, paint)` | A `Path` |

`Point` is a struct with `x` and `y`, and `+`, `-` and `*`.

A `Path` is built with `move_to`, `line_to`, `quad_to`, `cubic_to`, `arc_to(cx, cy, rx, ry, start_degrees, sweep_degrees)` and `close`, or made whole with `Path::circle`, `Path::ellipse` or `Path::rounded_rect`.

## Images, textures and sprites

| Method | Draws |
|---|---|
| `draw_image(image, x, y)`, `draw_image(image, x, y, w, h)` | An [`Image`](images.md), at its own size or stretched |
| `draw_texture(texture, x, y, opacity = 1.0)`, `draw_texture(texture, x, y, w, h, opacity = 1.0)` | A `Texture` |
| `draw_texture_part(texture, x, y, w, h, sx, sy, sw, sh, tint = white, opacity = 1.0)` | Part of a texture (a tile from a sheet, say), tinted |
| `draw_sprite(sprite)` | A `Sprite` |

A `Texture` is an image prepared for drawing over and over, which is faster than drawing the `Image` each frame. Make one with `Texture::from_image(image, filter = Filter::nearest, wrap = Wrap::clamp)` or `Texture::load(path, ...)`. `Filter::bilinear` smooths it when it's scaled.

A `Sprite` is a texture with a position, rotation, scale and more, for things that move. `Sprite(texture)` or `Sprite::at(texture, x, y)` makes one. Its methods are:

- **Position:** `set_position`, `move`, `move_forward(forward, sideways)`, `x`, `y`.
- **Rotation:** `set_rotation`, `rotate` and `look_at(x, y)`, all in degrees.
- **Size:** `set_scale(s)` or `set_scale(sx, sy)`, `set_size(w, h)`, `fit_size(w, h)`, `width`, `height`.
- **Appearance:** `set_flip(horizontal, vertical)`, `set_origin(Origin::...)`, `set_pivot(nx, ny)`, `set_opacity`, `set_visible`, `set_tint(color)`, `set_blend_mode`, `set_z_order`.
- **Sprite sheets:** `set_source_rect(x, y, w, h)` and `set_frame(index, frame_width, frame_height, columns = 0)`.
- **Hit testing:** `contains(x, y)` and `overlaps(other)`.

## Text

| Method | Does |
|---|---|
| `draw_text(x, y, text, style)` | Draws a line of text with a [`TextStyle`](typography.md); `(x, y)` is the top-left corner |
| `draw_text(x, y, w, h, text, style)` | Draws text wrapped and aligned inside a box |
| `measure_text(text, style, max_width = 0)` | A `TextMetrics` with the size it would take, wrapped at `max_width` when that's above 0 |
| `load_font(file)` | Makes a font file (`.ttf`, `.otf`) available by its name |

## Transforms, clipping and cameras

| Method | Does |
|---|---|
| `translate(dx, dy)`, `rotate(degrees)`, `scale(sx, sy)`, `scale(s)` | Changes how later drawing is placed |
| `push()`, `pop()`, `reset_transform()` | Saves and restores the transform |
| `set_clip(x, y, w, h)`, `reset_clip()` | Limits drawing to a rectangle |
| `set_blend_mode(mode)`, `blend_mode()` | `BlendMode::normal`, `add`, `multiply` or `screen` |
| `begin_camera(camera)`, `end_camera()` | Draws a 2D world through a `Camera` |

`Camera` is a struct with `x` and `y` (the world point at the center of the view), `zoom` and `rotation`, and the methods `move(dx, dy)` and `zoom_by(factor)`.

## Render targets

A `RenderTarget` is an off-screen picture to draw into, for effects, minimaps or caching. Make one with `RenderTarget::make(width, height, filter = Filter::bilinear)`. Render targets need a GPU backend (Vulkan or OpenGL); on the software backend `begin_target` throws a `graphics_error`.

| Method | Does |
|---|---|
| `begin_target(target)`, `begin_target(target, clear_color)` | Sends drawing into the target, clearing it first if a color is given |
| `end_target()` | Goes back to the window |
| `draw_target(target, x, y, opacity = 1.0)`, `draw_target(target, x, y, w, h, opacity = 1.0)` | Draws the target like an image |
| `read_target(target)` | The target's picture as an `Image` |

## 3D

Between `begin_3d(camera)` and `end_3d()`, a frame draws in 3D: meshes, models loaded from files, lines and boxes, lit by a light or a full `Lighting` setup. 2D drawing before and after works as usual, so a 3D scene can have a 2D overlay.

```walnut
import module.graphics
import module.colors
import module.io

using std::graphics::Renderer;
using std::graphics::Camera3D;
using std::graphics::Mesh;
using std::graphics::Transform3D;
using std::graphics::Vec3;

function main() -> int {
    std::windows::Window: win = std::windows::Window::open("3D", 400, 300);
    Renderer: r = Renderer::make(win);
    Camera3D: cam = Camera3D::at(0.0, 2.0, 6.0);
    cam.look_at(Vec3(0.0, 0.0, 0.0));
    Mesh: cube = Mesh::cube(1.5, std::colors::tomato);
    Mesh: floor = Mesh::plane(10.0, 10.0, std::colors::gray);
    if (win.poll()) then {
        r.begin_frame();
        r.clear(std::colors::midnight_blue);
        r.begin_3d(cam);
        r.set_light(Vec3(-0.4, -1.0, -0.6));
        r.draw_mesh(floor, Transform3D::at(0.0, -1.0, 0.0));
        r.draw_mesh(cube, Transform3D::at(0.0, 0.0, 0.0).rotated(0.0, 30.0, 0.0));
        r.end_3d();
        r.present();
    }
    std::graphics::ScreenPoint: p = cam.to_screen(Vec3(0.0, 0.0, 0.0));
    std::io::println(cube.triangle_count(), " triangles; the origin is at ", p.x, ", ", p.y);
    return 0;
} end function main
```

This prints `12 triangles; the origin is at 200, 150`.

**Vectors and transforms.** `Vec3` is a struct with `x`, `y` and `z`, `+`, `-`, `*`, `dot`, `cross`, `length` and `normalized`. `Transform3D` places a mesh or model: a `position`, a `rotation` (degrees around x, y and z) and a `scale`. `Transform3D::at(x, y, z)` makes one, and `.rotated(rx, ry, rz)` and `.scaled(k)` give changed copies.

**Camera3D.** `Camera3D::at(x, y, z, fov_degrees = 60.0)` makes a perspective camera. It has:

- **Position:** `set_position`, `position`, `move`, `move_local(right, up, forward)` and `move_flat(forward, right, up)`, which moves without changing height, for walking.
- **Aim:** `set_rotation(yaw, pitch, roll)`, `turn(dyaw, dpitch)` for mouse look, `look_at(point)`, `set_pitch_limit`, and `forward()`, `right()` and `up()` to read where it points.
- **Projection:** `set_perspective(fov, near, far)` and `set_orthographic(height, near, far)`.
- **Screen tests:** `to_screen(point)` gives a `ScreenPoint` with `x`, `y`, `depth`, `in_front` and `on_screen`. `visible_point` and `visible_sphere` test against the view. `begin_3d` sets the camera's viewport to the window, and `set_viewport(width, height)` sets it by hand.

**Mesh.** Ready-made shapes are `Mesh::cube(size, color)`, `Mesh::box(w, h, d, color)`, `Mesh::plane(w, d, color, divisions = 1)`, `Mesh::sphere(radius, color)` (or with `segments` and `rings`) and `Mesh::cylinder(radius, height, color, segments = 32)`. A custom mesh starts empty with `Mesh()`:

- `add_vertex(position, normal, color)` or `add_vertex(position, normal, color, u, v)` adds a vertex and gives its number.
- `add_triangle(a, b, c)` and `add_quad(a, b, c, d)` join vertices by number.
- `vertex_count()`, `triangle_count()`, `empty()` and `clear()` work as usual.

**Material.** A material says how a surface is shaded:

- **Presets:** `Material::make(preset)` takes a `MaterialPreset`: `plain`, `double_sided`, `transparent`, `unlit`, `metal`, `polished_metal`, `brushed_metal`, `plastic`, `glossy_plastic`, `rubber`, `wood`, `stone`, `ceramic` or `fabric`.
- **Other ways to make one:** `Material::glowing(color, strength)`, `Material::physical(metallic, roughness)` and `Material::textured(texture)`.
- **Setters:** `set_texture`, `set_cull(Cull::back / none / front)`, `set_blend(Blend::opaque / alpha / additive)`, `set_lit`, `set_depth(test, write)`, `set_shadow(Shadow::cast / no_shadow / cast_only)`, `set_fog`, `set_pbr(metallic, roughness)`, `set_emissive(color, strength)` and `set_occlusion`.

**Drawing in 3D:**

| Method | Draws |
|---|---|
| `draw_mesh(mesh)`, `draw_mesh(mesh, material)`, `draw_mesh(mesh, transform, material = Material())` | A mesh |
| `draw_model(model, transform = Transform3D())` | A loaded `Model`, or an `Animator` playing one |
| `draw_line_3d(a, b, color, width = 1.0, depth_test = true)` | A line between two points |
| `draw_box_3d(low, high, color, width = 1.0, depth_test = true)` | The edges of a box |
| `set_light(direction)`, `set_light(direction, ambient, diffuse)` | A simple sun light |
| `set_lighting(lighting)` | A full `Lighting` setup |

**Lighting.** `Lighting::make(preset)` takes a `LightingPreset`: `off`, `daylight`, `golden_hour`, `overcast`, `night`, `indoor` or `studio`.

- **Sun and sky:** `set_sun(direction, color, intensity)`, `set_sky(color, intensity)` and `set_ambient`.
- **Effects:** `set_shadows(on)`, `set_atmosphere(on)` and `set_exposure`.
- **Point lights:** up to 64, with `add_point_light(position, color)` or `add_point_light(position, color, intensity, radius)`. It gives the light's number, which `move_point_light(number, position)` uses; `clear_point_lights()` removes them all.

**Models.** `Model::load(path, scale = 1.0, z_up = true)` reads a glTF (`.gltf`, `.glb`) or OBJ file, with its materials and textures, and throws a `graphics_error` when it can't. `vertex_count()`, `bounds_min()`, `bounds_max()` and `find_animation(name)` describe it.

An `Animator(model)` plays a model's animations:

- **Playing:** `play(name)` or `play(index)`, with `loop`, `fade_seconds` and `speed` as optional extra arguments. `stop()` ends it.
- **Each frame:** call `update(dt)`, then draw it with `draw_model`.
- **Reading and changing:** `time()`, `duration()`, `finished()`, `set_speed()` and `set_time()`.

## Tilemaps

A `TileMap` is a grid of tiles drawn from tile sheets, with layers, solid tiles for collision, and animated tiles. It can also load maps saved as JSON by the Tiled editor.

```walnut
import module.graphics
import module.colors
import module.io

using std::graphics::TileMap;
using std::graphics::Texture;
using std::images::Image;

function main() -> int {
    Image: sheet = Image::make(32, 16, std::colors::white);
    sheet.fill_rect(0, 0, 16, 16, std::colors::forest_green);
    sheet.fill_rect(16, 0, 16, 16, std::colors::saddle_brown);
    TileMap: map = TileMap(16, 16);
    const long int: first = map.add_tileset(Texture::from_image(sheet), 16, 16);
    const long int: ground = map.add_layer("ground", 20, 15);
    map.fill_layer(ground, first);
    for (long int: x = 0; x < 20; x++) {
        map.set_tile(ground, x, 12, first + 1);
    }
    map.set_solid(first + 1, true);
    std::graphics::TileMove: fall = map.move(40.0, 150.0, 12.0, 12.0, 0.0, 60.0);
    std::io::println("landed at y = ", fall.y, ", on the ground: ", fall.on_ground);
    return 0;
} end function main
```

This prints `landed at y = 180, on the ground: 1`: the 12-pixel box falling 60 pixels stops on top of the row of solid tiles at y = 192. Draw a map with the renderer's `draw_tilemap(map)` or `draw_tilemap(map, x, y)`.

Tiles are numbered by "gid": each tile sheet added with `add_tileset(texture, tile_width, tile_height)` (or with `margin` and `spacing` too) gets a range of numbers and gives back its first one, and 0 means no tile.

| Method | Does |
|---|---|
| `TileMap(tile_width, tile_height)`, `TileMap::load_tiled(path)` | Makes an empty map, or loads a Tiled `.json` map |
| `add_layer(name, width, height)`, `add_layer_csv(name, csv)` | Adds a layer and gives its number |
| `layer_count()`, `layer_index(name)`, `layer_name(layer)`, `layer_width(layer)`, `layer_height(layer)` | Layer details |
| `tile(layer, x, y)`, `set_tile(layer, x, y, gid)`, `fill_layer(layer, gid)`, `fill_rect(layer, x, y, w, h, gid)` | Reads and writes tiles |
| `set_layer_visible`, `set_layer_collides`, `set_layer_opacity`, `set_layer_tint`, `set_layer_offset`, `set_layer_parallax` | Layer settings |
| `set_solid(gid, on)`, `solid(gid)`, `solid_at(x, y)`, `solid_at_tile(x, y)`, `overlaps_solid(x, y, w, h)` | Collision |
| `move(x, y, w, h, dx, dy)` | Moves a box as far as the solid tiles allow and gives a `TileMove`: the new `x` and `y`, `hit_x`, `hit_y`, `on_ground`, `on_ceiling`, `on_left` and `on_right` |
| `raycast(x, y, dx, dy, max_distance)` | A `TileHit`: `hit`, `tile_x`, `tile_y`, `gid`, `distance`, the point `x` and `y`, and the surface `normal_x` and `normal_y` |
| `set_animation(gid, frames, seconds)` | Makes a tile cycle through other tiles; `update(dt)` advances it |
| `world_to_tile_x`, `world_to_tile_y`, `tile_to_world_x`, `tile_to_world_y`, `pixel_width()`, `pixel_height()` | Converting between pixels and tiles |

## Errors

`Renderer::make`, `Texture::from_image`, `RenderTarget::make`, `load_font`, `begin_target`, `begin_3d`, `Model::load`, `TileMap::load_tiled` and the tile and mesh methods given a wrong number throw a `std::errors::graphics_error` when they can't do what was asked. `Texture::load` throws a [`std::errors::image_error`](images.md#errors) when the file can't be read. Their `what()` (or their `message` field) says why.
