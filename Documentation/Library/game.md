# game

`game` has `Game`, a ready-made main loop: it opens a [window](windows.md), starts a [renderer](graphics.md), and then each frame handles events, calls your update with the time since the last frame, clears the screen, calls your draw, and shows the result. It also keeps the frame rate and other timing numbers. Alongside it are `Assets`, which loads and caches images, textures and text files (and reloads them when they change on disk), and `Scene`, a 2D scene graph of nodes that move, rotate and scale with their parents.

```walnut
import module.game
```

Everything is in the namespace `std::game`. Importing it also imports `windows`, `graphics`, `input` and `colors`.

## A first example

```walnut
import module.game
import module.io

using std::game::Game;
using std::input::Key;

function main() -> int {
    Game: game = Game("Bouncing", 400, 300);
    game.clear_color = std::colors::dark_slate_gray;
    float: x = 50.0;
    float: speed = 120.0;
    game.run([&](float: dt) -> void {
        x = x + speed * dt;
        if (x > 380.0 || x < 20.0) then {
            speed = -speed;
        }
        if (game.window.key_pressed(Key::escape) || game.frame_count() >= 30) then {
            game.quit();
        }
    }, [&](std::graphics::Renderer&: r) -> void {
        r.fill_circle(x, 150.0, 20.0, std::colors::orange);
    });
    std::io::println("ran ", game.frame_count(), " frames");
    return 0;
} end function main
```

This prints `ran 30 frames`. Without the frame limit it would keep going until Escape is pressed or the window is closed.

## Making and running

| Written | Does |
|---|---|
| `Game(title, width, height)`, `Game(title, width, height, backend, vsync = false)` | Opens the window and starts the renderer. Throws a [`window_error`](windows.md#errors) or a [`graphics_error`](graphics.md#errors) when it can't |
| `run(update, draw)` | Runs the loop until the window closes or `quit()` is called. `update` takes the seconds since the last frame (a `float`); `draw` takes the `Renderer&` |
| `run(update, draw, on_event)` | The same, also calling `on_event` with each `WindowEvent` before the update |
| `quit()` | Ends the loop. Called from `update`, it stops before that frame is drawn |
| `is_running()` | Whether the loop is going |

The window and renderer are the fields `window` and `renderer`, so input is read with `game.window.key_down(...)` and the like. The other fields are:

- `clear_color`, which the screen is cleared to each frame (black by default).
- `auto_clear`, which can be set to `false` to skip the clearing.
- `max_fps`, which caps the frame rate. It's 0 by default, meaning no cap: with vsync off (the default) the game draws as many frames as it can, and with vsync on it waits for the display.

## Timing

| Method | Gives |
|---|---|
| `delta_seconds()` | The time the last frame took; it never goes above 0.25, so a pause doesn't make things jump |
| `fps()`, `fps_average()` | Frames per second, for the last frame and smoothed |
| `elapsed_seconds()` | The total time the loop has run |
| `frame_count()` | How many frames have been drawn |
| `width()`, `height()` | The window size |

## Assets

`Assets` finds files by name in a list of folders, loads each one once, and hands back the cached copy after that.

```walnut
import module.game
import module.io

function main() -> int {
    std::game::Assets: assets;
    std::images::Image::make(8, 8, std::colors::red).save("red.png");
    assets.add_folder(".");
    std::graphics::Texture: t = assets.texture("red.png");
    std::io::println(t.width(), " ", assets.loaded("red.png"), " ", assets.image_count());
    return 0;
} end function main
```

This prints `8 1 1`. (The example saves its own image first, so there is something to load.)

| Method | Does |
|---|---|
| `add_folder(folder, search_first = false)`, `clear_folders()` | Where names are looked up. By default these are searched: the folders in the `FIZMO_ASSET_PATH` environment variable, then `assets` and the program's own folder, then `assets` and the current folder |
| `image(name)`, `texture(name, filter = Filter::nearest, wrap = Wrap::clamp)` | Loads (or reuses) an image or a texture. Throws an [`image_error`](images.md#errors) when the file can't be read |
| `read_text(name)` | A text file's contents. Throws a `game_error` when it can't be read |
| `loaded(name)`, `unload(name)`, `clear()`, `collect()` | Checking and dropping cached files; `collect()` drops the ones nothing else is using |
| `image_count()`, `text_count()`, `image_bytes()`, `reload_count()` | Cache statistics |
| `enable_hot_reload()`, `check_reloads()`, `disable_hot_reload()` | Watches the loaded files; each `check_reloads()` call (once per frame) reloads the ones that changed |
| `last_error()` | Why the last load failed |

## Scene graph

A `Scene` is a tree of `Node`s. Each node has a position, rotation, scale, opacity, draw order and visibility, all relative to its parent, so moving a parent moves everything under it. Nodes can be groups, sprites, shapes or text.

```walnut
import module.game
import module.io

using std::game::Scene;
using std::game::Node;
using std::graphics::Paint;

function main() -> int {
    Scene: scene;
    Node: ship = scene.root().add_group("ship");
    ship.set_position(100.0, 50.0);
    Node: body = ship.add_rect(40.0, 20.0, Paint::fill(std::colors::silver), "body");
    body.set_position(10.0, 5.0);
    ship.move(5.0, 0.0);
    std::io::println(body.world_x(), ", ", body.world_y(), " ", scene.root().find("body").name());
    return 0;
} end function main
```

This prints `115, 55 body`. Each frame, call `scene.update(dt)` and then `scene.draw(renderer)`, or `scene.draw(renderer, camera)` to view it through a `std::graphics::Camera`.

| Method | Does |
|---|---|
| `scene.root()` | The top node |
| `add_group(name = "")`, `add_sprite(texture, name = "")`, `add_rect(w, h, paint, name = "")`, `add_rounded_rect(w, h, radius, paint, name = "")`, `add_ellipse(w, h, paint, name = "")`, `add_path(path, paint, name = "")`, `add_text(text, style, name = "")` | Adds a child node and gives it back |
| `set_position`, `move`, `x`, `y`, `set_rotation`, `rotate`, `rotation`, `set_scale`, `scale_x`, `scale_y`, `set_origin` | Placement, relative to the parent |
| `world_x()`, `world_y()`, `world_rotation()` | Placement on screen, after all the parents |
| `set_z_order`, `set_visible`, `set_active`, `set_opacity` and their getters | Draw order, hiding, pausing and fading; they apply to the children too |
| `name`, `set_name`, `find(name)`, `child(index)`, `child_count()`, `parent()` | Finding nodes |
| `reparent(new_parent)`, `remove()`, `valid()` | Moving and deleting nodes; after `remove()`, the node and its children are no longer `valid()` |
| `hit(x, y)`, `scene.pick(x, y)` | Hit testing, in world and screen coordinates |
| `sprite()`, `set_sprite(sprite)` | The [`Sprite`](graphics.md#images-textures-and-sprites) of a sprite node |
| `set_paint(paint)`, `set_size(w, h)` | A shape node's look |
| `set_text(text)`, `text_value()`, `set_style(style)` | A text node's text and [`TextStyle`](typography.md) |

