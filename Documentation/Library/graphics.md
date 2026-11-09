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

**Texture roles.** Colours in a picture are stored in sRGB, but normal, roughness, metal, occlusion and ORM maps hold plain numbers and have to be read as linear data, or bumps and shine come out wrong. `Texture::load` and `Texture::from_image` make sRGB textures, so for a 3D map use `Texture::load_as(path, role, filter = Filter::bilinear, wrap = Wrap::tile)` or `Texture::from_image_as(image, role, ...)`, which also turn on mipmaps. The role is `"color"`, `"emissive"` (both sRGB), `"normal"`, `"roughness"`, `"metallic"`, `"occlusion"`, `"orm"`, `"opacity"` or `"data"` (all linear). On any texture, `set_role(role)`, `set_color_space("srgb" or "linear")`, `color_space()`, `set_mipmaps(on)` and `mipmaps()` change and read it. A material keeps its own copy of each texture it's given, so set the role first.

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
- **Reflections:** `set_reflectivity(amount)` from 0 to 1, where 1 is a mirror and lower values look polished or wet (scaled by roughness). `reflectivity()` reads it.
- **Maps:** each one keeps the texture alive for as long as the material needs it.
  - `set_normal_map(texture, strength = 1.0)` adds surface detail. Load it with the `"normal"` role.
  - `set_orm_map(texture, has_occlusion = true)` reads occlusion from red, roughness from green and metal from blue. Pass `false` when the red channel isn't occlusion.
  - `set_emissive_map(texture)` makes parts glow. If the material wasn't glowing yet, the glow becomes white at strength 1, so `set_emissive` tints and scales it.
  - `set_opacity_map(texture)`, and `set_alpha_cutoff(cutoff)` from 0 to 1, which drops pixels whose alpha is below it (leaves and fences).
  - `has_normal_map()`, `has_orm_map()`, `has_emissive_map()`, `has_opacity_map()` and `clear_maps()` (which keeps the colour texture).
- **Texture placement:** `set_tiling(u, v)` repeats the textures, `set_offset(u, v)` slides them and `set_uv_rotation(degrees)` turns them. `set_triplanar(on, scale = 1.0)` maps the textures from the three axes instead of the mesh's own coordinates, for terrain and rocks with no UVs, and `set_triplanar_sharpness(sharpness)` (4 by default) sets how hard the blend between axes is.

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
- **Shadow tuning:** `set_shadow_distance(meters)` is how far real shadows reach (past it, use blob shadows from `Visibility`), `set_shadow_resolution(pixels)`, `set_soft_shadows(on)` and `set_layered_shadows(on)`. Layered shadows are on by default: things that haven't moved for a few frames are drawn once into a cached layer, and each frame only the moving casters are drawn on top of a copy of it.
- **Quality presets:** `Lighting::quality("low" | "medium" | "high" | "ultra")` builds lighting the way Voxelspire's presets do: soft sun shadows and capsule shadows from Medium, tone mapping with a little extra saturation and a sky glow and distance fog; High adds point-light shadows, light shafts, volumetric light and screen-space reflections; Ultra sharpens and widens the shadows and thickens the light. Fine-tune with `set_light_shafts(on)`, `set_volumetric_light(on, density)`, `set_point_shadows(on)`, `set_screen_reflections(on)`, `set_fog(density, start)`, `set_saturation(s)` and `set_max_light(brightest)`.
- **Ambient occlusion:** `set_ambient_occlusion(on, radius, strength)` darkens corners, creases and the ground under things (screen-space, half resolution, only the sky/ambient light). The quality presets turn it on from Medium.
- **Capsule shadows:** cheap soft shadows for characters: `add_capsule_shadow(start, finish, radius)` (gives its index), `move_capsule_shadow(index, start, finish)` each frame, `clear_capsule_shadows()`.
- **Point lights:** up to 64, with `add_point_light(position, color)` or `add_point_light(position, color, intensity, radius)`. It gives the light's number, which `move_point_light(number, position)` uses; `clear_point_lights()` removes them all.
- **Point light spread:** `set_point_light_falloff(number, spread, spill = 0.35, reference_distance = 0.0)`. A `"blob"` light (the default) is a compact pool that fades out by its radius. A `"natural"` light fades with the square of the distance, at full strength at `reference_distance` and smoothly gone by the radius, so it keeps lighting a whole room. `spill`, from 0 to 4, wraps light onto surfaces facing slightly away, which looks like light bouncing off nearby walls. A `reference_distance` of 0 leaves it as it was (1 metre to begin with). `point_light_falloff(number)`, `point_light_spill(number)` and `point_light_reference(number)` read them back.

**Terrain-style blending.** A material can blend up to 8 textures by a weight texture (a splat map): `set_layer(index, texture, tiling = 1.0, tint)` sets a layer (tiling is how many times it repeats across the mesh's UVs), `set_layer_normal_map(index, normal)` gives it bumps, `set_splat_map(weights)` holds the weights of layers 0 to 3 in red, green, blue and alpha (sampled with the mesh's UVs), `set_second_splat_map(weights)` those of 4 to 7. Layers blend by height (the texture's brightness) for natural edges, `set_height_blend(depth)` sets how deep that is, and `set_splat_triplanar(on)` keeps cliffs from stretching. `clear_layers()` removes them. `set_soft(on, distance, near_fade)` fades a see-through material where it meets the scene behind it (smoke, fog cards, glows).

**Models.** `Model::load(path, scale = 1.0, z_up = true)` reads a glTF (`.gltf`, `.glb`), FBX (`.fbx`, binary or ASCII, version 7 from 2011 on, with skeletons and animations) or OBJ file, with its materials and textures, and throws a `graphics_error` when it can't. `vertex_count()`, `bounds_min()`, `bounds_max()` and `find_animation(name)` describe it.

An `Animator(model)` plays a model's animations:

- **Playing:** `play(name)` or `play(index)`, with `loop`, `fade_seconds` and `speed` as optional extra arguments. `stop()` ends it.
- **Each frame:** call `update(dt)`, then draw it with `draw_model`.
- **Reading and changing:** `time()`, `duration()`, `finished()`, `set_speed()` and `set_time()`.

**Visibility.** A `Visibility` decides what's worth drawing each frame. Give it each object's box with `add(x0, y0, z0, x1, y1, z1)` (or `add(..., occluder, caster)`), which gives the object's number. Call `update(number, ...)` when the box moves and `remove(number)` when it's gone. Each frame, `cull(camera)` works out:

- **Hidden objects:** outside the camera's view, past `set_max_distance(meters)`, smaller than `set_min_pixels(pixels)` on screen, or behind an occluder. Occluders are objects added with `occluder = true` whose box is solid, like walls and buildings. They're drawn into a small depth buffer, and anything fully behind them is hidden. `set_occlusion(false)` turns that last test off.
- **Level of detail:** `set_lods(number, first, second, third)` gives the distances where an object switches to its next level, and `lod(number)` gives the level to draw (0 is the most detailed). A little slack stops it flickering between two levels at an edge.
- **Shadow level:** `set_shadow_distances(real, blob)`. Then `shadow(number)` is 0 for a real shadow, 1 for a blob and 2 for none. The blobs to draw are `blob_count()`, with `blob_x(i)`, `blob_y(i)`, `blob_z(i)`, `blob_radius(i)` and `blob_opacity(i)`.

`visible(number)`, `occluded(number)`, `visible_count()`, `occluded_count()` and `culled_count()` report the result. `set_always(number, true)` keeps an object drawn whatever the tests say. `set_up_axis(0, 1 or 2)` says which way is up for blobs.

**LodGrid.** A `LodGrid` is a quadtree of ground tiles that are fine near the camera and coarse far away, the way terrain and voxel worlds stream their chunks.

- **Setting up:** `setup(world_size, base_distance, levels, up_axis)` and `set_origin(x, y, z)`.
- **Each frame:** `update(x, y, z)` with the camera's position. It's `true` when the tile set changed.
- **Reading tiles:** `tile_count()`, then `tile_x/y/z(i)`, `tile_size(i)`, `tile_level(i)` and `tile_key(i)`.
- **Changes:** `added_count()` with `added_key(i)`, and `removed_count()` with `removed_key(i)`, so you only build or free the tiles that changed. `level_at(x, y, z)` gives the tile level under a point.

**Particles.** `Particles` is Fizmo's particle system: emitters with budgets, distance detail levels, culling, threads and pooled one-shot effects. `Particles()` holds up to 100,000 particles and `Particles(budget)` sets the limit.

```walnut
std::graphics::Particles: fx;
fx.add_ground(0.0);
long int: torch = fx.add_emitter("fire");
fx.set_position(torch, 2.0, 0.0, -4.0);
long int: boom = fx.add_effect("explosion");
while (win.poll()) {
    fx.step(1.0 / 60.0, camera);
    r.begin_frame();
    r.begin_3d(camera);
    fx.draw(r);
    r.end_3d();
    r.present();
}
```

- **Emitters:** `add_emitter(preset)`, `add_emitter(preset, dimension)` or `add_emitter(preset, dimension, pixels_per_unit)` gives the emitter's number. The presets are `sparks`, `smoke`, `fire`, `explosion`, `dust`, `rain`, `snow`, `magic`, `debris`, `bubbles`, `steam`, `embers` and `impact`. The dimension is `"3d"` (metres, y up, the default) or `"2d"` (pixels, y down, 50 pixels to the metre unless given).
- **Settings:** `settings_text(emitter)` gives every setting as `key: value` lines, and `add_emitter_text(text)` makes an emitter from that text (keys left out keep their defaults). `set_setting(emitter, key, value)` changes one, like `set_setting(torch, "rate", "200")` or `set_setting(torch, "color", "1, 0.5, 0.2, 1")`, and `setting(emitter, key)` reads one. The keys are Fizmo's `EmitterSettings` fields, such as `rate`, `lifetime`, `speed`, `size`, `color`, `gravity_scale`, `drag`, `shape`, `blend` and `collide`. Soft particles fade where they meet walls and floors instead of cutting a hard line: `set_setting(smoke, "soft", "true")` and `set_setting(smoke, "soft_distance", "0.6")` (metres); `near_fade` fades them as they reach the camera, and `pivot_x` / `pivot_y` move the quad's anchor (0.36 makes flames rise from their base).
- **Placing:** `set_position(emitter, x, y, z)` (or `x, y` in 2D, or a `Vec3`) leaves a trail between the old and new place, and `teleport(emitter, x, y, z)` doesn't. `set_direction(emitter, x, y, z)` turns the emission axis that way, `set_orientation(emitter, forward..., up...)` sets both axes, and `set_angle(emitter, degrees)` turns a 2D emitter. `set_velocity(emitter, x, y, z)` is the speed handed on to particles that inherit it; `clear_velocity(emitter)` goes back to measuring it from movement.
- **Playing:** `play`, `stop` (live particles finish), `pause(emitter, on)`, `restart`, `burst(emitter, count)` (gives how many fit), `clear(emitter)`, `remove(emitter)`, `exists`, `playing` and `finished`.
- **Counting:** `count(emitter)` (with its sub-emitters), `count()` for everything, `emitter_count()`, `spawned()` and `limited()` (spawns the budget refused in the last step), and `step_ms()`.
- **One-shot effects:** `add_effect(preset, dimension = "3d", pixels_per_unit = 50.0)` or `add_effect_text(text)` gives an effect number. `spawn(effect, x, y, z)` and `spawn_2d(effect, x, y, angle = 0.0)` fire one off, reusing finished ones so nothing is allocated.
- **Colliders:** `add_ground(height)`, `add_plane(normal..., point...)`, `add_box(x0, y0, z0, x1, y1, z1)`, `add_sphere(x, y, z, radius)` and `clear_colliders()`. Emitters with `collide: true` bounce off them.
- **Speed:** `set_budget(count)`, `set_threads(count)` (0 uses every core) and `set_origin(x, y, z)` for large worlds.
- **Texture:** `set_texture(emitter, texture)`; without one, particles are soft round dots.
- **Each frame (3D):** `step(seconds, camera)` updates everything and culls what the camera can't see, and `draw(renderer)` draws with the camera from the last step (or `draw(renderer, camera)` with another one), between `begin_3d` and `end_3d`. `step(seconds)` updates without culling.
- **Each frame (2D):** `step_2d(seconds, x0, y0, x1, y1)` with the visible rectangle, then `draw_2d(renderer)`, which follows the current 2D camera. Keep 2D and 3D emitters in separate `Particles`, because each step culls against one view.

Both `draw` methods give how many particles they drew.

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

Particle, material and lighting methods given an unknown preset, role, spread, setting or number throw a runtime error that says what was wrong. `Renderer::make`, `Texture::from_image`, `RenderTarget::make`, `load_font`, `begin_target`, `begin_3d`, `Model::load`, `TileMap::load_tiled` and the tile and mesh methods given a wrong number throw a `std::errors::graphics_error` when they can't do what was asked. `Texture::load` throws a [`std::errors::image_error`](images.md#errors) when the file can't be read. Their `what()` (or their `message` field) says why.
