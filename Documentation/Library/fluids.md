# fluids

`fluids` is a 2D fluid sandbox from Fizmo's `FluidWorld2D`: falling sand, water that flows and settles, a wave line for side views, and particle fluids that pour, splash and layer. It's made for sandbox games and toys, in the spirit of Powder Toy and Noita.

```walnut
import module.fluids
```

Everything is in the namespace `std::fluids`. Like [physics](physics.md), numbers cross as `std::hardware_float` and counts and cell numbers as `long int`. Lengths are in metres and times in seconds, with y going up. For 3D fluids, use `add_fluid` on a [physics](physics.md#fluids) `World`.

## Contents

1. [A first example](#a-first-example)
2. [Making a fluid](#making-a-fluid)
3. [Brushes](#brushes)
4. [Falling sand](#falling-sand)
5. [Reading it](#reading-it)
6. [Settings](#settings)
7. [Emitters](#emitters)
8. [Drawing](#drawing)
9. [Errors](#errors)

## A first example

```walnut
import module.fluids
import module.io

function main() -> int {
    std::fluids::Fluid2D: box = std::fluids::Fluid2D::make("blocky", 0.0, 0.0, 1.28, 1.28, "water", 0.0, 0.01);
    box.paint_sand(0.4, 1.0, 0.1, "sand");
    box.pour(0.9, 1.0, 0.1);
    box.paint_wall(0.64, 0.2, 0.05);
    for (long int: i = 0l; i < 120l; i += 1l) {
        box.step(1.0 / 60.0);
    }
    std::io::println(box.count_cells("sand"), " sand, ", box.count_cells("water"), " water");
    std::images::Image: picture = box.to_image(256);
    picture.save("sandbox.png");
    return 0;
} end function main
```

## Making a fluid

`Fluid2D::make(level, x0, y0, x1, y1, preset = "water", fill = 0.0, cell_size = 0.0, heat = false)` makes a fluid in the box from `(x0, y0)` to `(x1, y1)`, filled to `fill` (0 to 1) of its height. `Fluid2D()` is an empty 128 by 128 falling-sand box from `(0, 0)` to `(1.28, 1.28)`.

| Level | Feels like | How it works |
|---|---|---|
| `"blocky"` | Powder Toy, Noita | Falling sand: a grid of 13 kinds of cell that fall, slide, flow, burn and react |
| `"still"` | A flat pool | A flat surface (it can bob) |
| `"flowing"` | Modded Minecraft | Each cell holds part of a block, the total is kept exactly, and water falls, spreads and keeps moving |
| `"waves"` | A water line | A row of columns that flow and carry ripples, with optional swell |
| `"realistic"` | A particle sandbox | Particles on a grid with a pressure solve: many fluids at once, viscosity, cohesion and splashes; oil floats on water and honey sinks |

- **Preset:** `"water"`, `"sea-water"`, `"oil"`, `"honey"`, `"lava"`, `"mud"` or `"mercury"`. A `blocky` fluid fills with a cell instead: any [falling-sand cell](#falling-sand) but `empty` and `wall` (`"sea-water"` gives water and `"mud"` sand).
- **Cell size:** 0 picks one (256 cells across the longest side for `blocky`, 128 for the rest). A fluid can have at most 4 million cells.
- **Heat:** `true` gives each `blocky` cell a temperature, so water boils, ice melts, lava cools to stone and wood catches fire from the heat around it. It can only be chosen when the fluid is made; `heat()` says whether it's on.
- **Up:** gravity starts as 9.81 m/s² down, and that decides which way is up for good. `set_gravity(x, y)` later only tilts the `realistic` level, so it sloshes.

`level()`, `cells_x()`, `cells_y()`, `cell_size()`, `columns()` and `column_size()` (for `waves`), and `min_x()`, `min_y()`, `max_x()` and `max_y()` describe it.

**Time.** `step(seconds)` moves it on, and `step()` by 1/60 s. Cell levels tick at a fixed rate and the others take as many small steps as they need, capped so a slow frame never runs away. Settled areas sleep and cost nothing, and `sleeping()` says when the whole fluid has settled.

## Brushes

Each brush takes a point and a radius in metres.

| Method | Effect |
|---|---|
| `pour(x, y, radius)` | Adds fluid. `blocky` paints the fill cell into empty cells, `flowing` fills cells, and `realistic` adds particles where there aren't any yet, so holding the brush still never over-fills. Gives how many cells or particles it added |
| `pour(x, y, radius, preset)` | The same with another preset. In `realistic`, each preset is a fluid of its own and they layer and mix; in `blocky` it's a cell name such as `"oil"` or `"acid"` |
| `erase(x, y, radius)` | Removes fluid (walls stay) and gives how much it removed |
| `clear()` | Removes all of it |
| `push(x, y, radius, vx, vy, strength)` | Pushes the fluid towards that velocity; `strength` from 0 to 1 is how much |
| `swirl(x, y, radius, spin, strength)` | Stirs it (`realistic`) |
| `explode(x, y, radius, strength)` | Blasts it outwards; in `blocky` it also turns cells to fire and smoke |
| `paint_wall(x, y, radius, on = true)` | Paints walls, or with `false` erases them. Fluid inside a new wall is pushed out |

## Falling sand

A `blocky` fluid is a grid of cells, each one of these:

| Number | Cell | Kind |
|---|---|---|
| 0 | `empty` | Nothing |
| 1 | `wall` | Solid |
| 2 | `sand` | Powder |
| 3 | `water` | Liquid |
| 4 | `oil` | Liquid that burns and floats on water |
| 5 | `lava` | Slow hot liquid that glows |
| 6 | `steam` | Gas that turns back into water |
| 7 | `smoke` | Gas that fades away |
| 8 | `fire` | Fire |
| 9 | `ice` | Solid that melts |
| 10 | `stone` | Powder that piles up steeply |
| 11 | `wood` | Solid that burns |
| 12 | `acid` | Liquid that eats sand, stone, wood and ice |

Powders fall and slide and sink through liquids, liquids spread and settle, denser liquids sink through lighter ones, and gases rise. Fire spreads to wood and oil, water puts it out as steam, lava and water make stone and steam, and acid dissolves what it touches.

- `paint_sand(x, y, radius, cell)` paints cells of that name over whatever is there, and `paint_sand(x, y, radius, cell, probability)` paints only that share of them (0.3 gives a sprinkle). Each gives how many cells it painted.
- `add_heat(x, y, radius, degrees)` heats (or, below 0, cools) an area. With heat on it raises the temperature; without it, it boils, melts, ignites or freezes what's there straight away.
- `count_cells(cell)` counts the cells of that name, and `temperature(i, j)` reads a cell's temperature (with heat on).

## Reading it

**Cells.** `cell(i, j)` gives cell `(i, j)`'s name and `cell_id(i, j)` its number, counted from the bottom left. In a `blocky` fluid they're from the table above. In the other levels a cell is `"empty"` (0), `"wall"` (1) or `"fluid"` (13). `cell_fill(i, j)` is how full it is: 0 or 1 in `blocky`, the share of a block in `flowing`, and in `realistic` how many particles it holds compared with fluid at rest (about 1 when full).

**Particles** (`realistic`): `particle_count()`, `particle_x(i)`, `particle_y(i)`, `particle_color(i)` (a [`Color`](colors.md)), `particle_material(i)` (0 for the fluid's own preset, then 1, 2, ... for each other preset in the order they were first poured) and `particle_radius()`. Their order changes from step to step.

**At a point:**

- `surface_height(x)` is the height of the surface above `x`.
- `depth_at(x, y)` is how far below the surface the point is (negative above it, and the lowest `std::hardware_float` outside the box). `inside(x, y)` says whether the point is in the fluid.
- `velocity_x(x, y)` and `velocity_y(x, y)` are the flow there, and `density_at(x, y)` the density of what's there.

`total_volume()` is the amount of fluid in square metres, and `step_ms()` how long the last step took.

## Settings

- `set_property(property, value)` takes `"density"`, `"viscosity"`, `"surface-tension"`, `"drag"`, `"quadratic-drag"`, `"angular-drag"`, `"friction"`, `"bounce"`, `"stiffness"`, `"temperature"`, `"opacity"` or `"wave-height"` (the swell for `waves`, the bob for `still`), for the fluid's own preset.
- `set_color(r, g, b)` takes 0 to 1 each.
- `set_current(x, y)` makes the fluid carry things along.
- `set_quality(0 to 3)` (low, medium, high, ultra; medium to begin with) sets substeps and solver work.
- `set_threads(count)` splits the work over that many threads (0 uses every core). The results are the same for any count.

## Emitters

`add_emitter(x, y, radius, vx, vy, rate)` adds a nozzle that pours `rate` particles (or, in `blocky`, cells) a second at velocity `(vx, vy)`, and gives its number. `add_emitter(..., rate, preset)` pours another preset. `set_emitter_active(emitter, on)` turns one on and off, and `emitter_count()` and `clear_emitters()` do what they say.

## Drawing

`to_image(width)` draws the fluid into a new [`Image`](images.md) `width` pixels across, as tall as the box's shape needs, with row 0 at the top. `to_image(width, height)` sets both, and `to_image(0)` gives one pixel per cell (or column). Particles are blended into a smooth surface, so mixed fluids blend their colours, and sand cells get a little colour noise. Turn it into a `Texture` with `Texture::from_image` to draw it with a [`Renderer`](graphics.md).

```walnut
std::fluids::Fluid2D: tank = std::fluids::Fluid2D::make("realistic", 0.0, 0.0, 2.0, 1.0, "water", 0.3);
while (win.poll()) {
    if (mouse_down) then {
        tank.pour(mouse_x, mouse_y, 0.05, "oil");
    }
    tank.step(1.0 / 60.0);
    r.begin_frame();
    r.draw_texture(std::graphics::Texture::from_image(tank.to_image(400)), 0, 0);
    r.present();
}
```

## Errors

Bad input throws a runtime error with a message naming the problem: an unknown level, preset, cell or property name, a box with no size, a fill outside 0 to 1, a radius or time step that isn't above 0, too many cells, a cell, particle or emitter number out of range, or a falling-sand method on a fluid that isn't `blocky`.
