# physics

`physics` is Walnut's front end to Fizmo's 3D rigid body engine, the same one Orchard runs on.

```walnut
import module.physics
```

Everything is in the namespace `std::physics`. Numbers cross as `std::hardware_float` (C++ `float`, the same as the physics engine uses) and counts and body numbers as `long int`. Literals and integers pass straight in, and every reading comes back as a `std::hardware_float` that turns into a `short float` or wider without a cast. A `short float` variable needs `static_cast<std::hardware_float>(...)` to go in, or can be declared as a `std::hardware_float` to begin with. Lengths are in metres, times in seconds and angles in radians.

## Contents

1. [A first example](#a-first-example)
2. [The world](#the-world)
3. [Bodies](#bodies)
4. [Surfaces](#surfaces)
5. [Layers, quality and flags](#layers-quality-and-flags)
6. [Queries](#queries)
7. [Joints](#joints)
8. [Cloth and ropes](#cloth-and-ropes)
9. [Contact events](#contact-events)
10. [Errors](#errors)

## A first example

```walnut
import module.physics
import module.io

std::physics::World: world;
world.add_ground(0.0);
long int: ball = world.add_sphere(0.0, 3.0, 0.0, 0.5);
for (long int: i = 0l; i < 120l; i += 1l) {
    world.step();
}
std::io::println(world.y(ball));
```

## The world

- **Time:** `step()` moves the world on by the time step (1/60 s unless `set_time_step(seconds)` changes it), and `step_by(seconds)` by any amount.
- **Gravity:** `set_gravity(x, y, z)`.
- **Accuracy:** `set_substeps(count)` and `set_iterations(velocity, position)`.
- **Solver:** `set_solver("classic")` or `set_solver("soft")`. The soft-step solver is steadier with tall stacks and joints at low iteration counts. `set_soft(substeps, contact_hertz, damping, push_speed)` tunes it.
- **Speed:**
  - `set_threads(count)` solves separate piles of bodies at the same time.
  - `set_wide_solver(true)` solves 8 contacts at once.
  - `set_reuse_contacts(true)`, on by default, skips collision tests for pairs that barely moved.
  - `set_broadphase("tree")`, the default, or `set_broadphase("octree")`.
- **Fast movers:** `set_continuous(on, everything)`.
- **Repeatable results:** `set_deterministic(true)` gives the same result on every run and with any number of threads.
- **Large worlds:** `shift_origin(x, y, z)` moves everything by minus that amount, so positions stay small and precise. Or `set_rebase_distance(meters)` and `set_focus(x, y, z)` each frame (usually the player), and the world shifts itself once the focus gets that far out. `origin_x/y/z()` is how far it has shifted in total, so a body's true position is its position plus the origin.
- **Stats:** `body_count()`, `awake_count()`, `pair_count()`, `island_count()`, `layer_rejects()` and `reused_contacts()`.

## Bodies

Every `add_...` gives a body number. The last argument is `"dynamic"` (the default where it's left out), `"static"` or `"kinematic"`.

| Method | Shape |
|---|---|
| `add_box(x, y, z, hx, hy, hz, kind)` | A box with half sizes `hx`, `hy`, `hz` |
| `add_sphere(x, y, z, radius, kind)` | A sphere |
| `add_capsule(x, y, z, half_height, radius, kind)` | An upright capsule |
| `add_cylinder(x, y, z, half_height, radius, kind)` | An upright cylinder |
| `add_cone(x, y, z, half_height, radius, kind)` | An upright cone, point up |
| `add_rounded_box(x, y, z, hx, hy, hz, rounding, kind)` | A box with rounded edges; the half sizes are the outside |
| `add_rounded_box_core(x, y, z, hx, hy, hz, rounding, kind)` | The same, but the half sizes are the flat inner box and the rounding is added on top |
| `add_ground(height)` | A flat static ground |
| `add_terrain(x, y, z, columns, rows, cell, heights)` | A static heightfield; `heights` is `columns × rows` numbers separated by commas |
| `add_mesh(x, y, z, points, triangles, two_sided, kind)` | A triangle mesh; `points` is x,y,z numbers separated by commas and `triangles` is 3 point numbers for each triangle, counted from 0. A one-sided mesh only blocks from the side its triangles face (counter-clockwise when you look at it) |

- **Reading:** `x`, `y`, `z`, `vx`, `vy`, `vz`, the rotation as `qx`, `qy`, `qz`, `qw`, and `mass`, each taking the body number.
- **Moving:** `set_position`, `set_velocity`, `set_spin` and `push` (an impulse).
- **Slowing:** `set_damping(body, linear, angular)`, `set_gravity_scale(body, scale)` and `set_speed_caps(body, speed, spin)`.
- **Removing:** `remove(body)` and `exists(body)`.

## Surfaces

- `set_friction(body, friction)` and `set_bounce(body, bounce)`.
- `set_rolling_friction(body, amount)` makes balls slow down as they roll, and `set_spinning_friction(body, amount)` makes spinning tops slow down.
- `set_grip(body, x, y, z)` scales friction along the body's own axes, so a wheel can grip one way and slide the other.
- `set_sensor(body, true)` makes it detect touches without being solid.

## Layers, quality and flags

- **Layers:** `set_layer(body, bits)` and `set_mask(body, bits)`. Two bodies collide only when each one's layer is in the other's mask. The check runs first, before any collision work, and a body on layer 0 isn't tested at all. `can_collide(a, b)` checks a pair.
- **Quality:** `set_quality(body, "default", "arcade", "normal", "high" or "ultra")`. `set_iterations_for(body, velocity, position)` overrides it with exact counts, and 0 means automatic.
- **Fast-move checks:** `set_fast_moves(body, "default", "on" or "off")`.
- **Sleep:** `set_sleep(body, speed, spin, delay)` sets how still a body has to be, and for how long, before it sleeps. `awake(body)` and `wake(body)` read and change it.
- **Flags:** `set_flag(body, name, on)` and `flag(body, name)` reach every Fizmo body flag by name:

| Group | Names |
|---|---|
| Forces | `gravity`, `forces`, `torque`, `wind`, `buoyancy`, `magnetism`, `springs`, `force-fields` |
| Motion | `moves`, `turns`, `drag`, `spin-drag`, `speed-cap`, `spin-cap` |
| Impulses | `pushes`, `twists`, `contact-impulses`, `joint-impulses` |
| Locks | `lock-x`, `lock-y`, `lock-z`, `lock-rot-x`, `lock-rot-y`, `lock-rot-z` |
| Collision | `collisions`, `friction`, `bounce`, `contacts`, `sensor-events`, `pre-solve`, `post-solve`, `broadphase`, `bullet`, `speculative`, `ghost` |
| Solver | `islands`, `warm-start`, `correction` |
| Lifetime | `enabled`, `sleep`, `awake`, `auto-disable`, `destroy-sleep`, `destroy-out`, `draggable` |
| Debug and effects | `visible`, `debug`, `show-velocity`, `show-contacts`, `impact-sounds`, `trail`, `impact-particles` |
| Yours | `user-1` to `user-10` |

## Queries

The `layers` argument picks which layers to look at; leave it out, or pass -1, for all of them.

- **Rays:** `raycast(ox, oy, oz, dx, dy, dz, max_distance, layers)` is `true` when it hits. Then `hit_body()`, `hit_distance()`, `hit_x/y/z()` and the surface normal `hit_nx/ny/nz()` describe the hit.
- **Sphere casts:** `sphere_cast(x, y, z, radius, dx, dy, dz, layers)` sweeps a sphere and gives how far along it got, from 0 to 1. The `hit_...` methods describe what it hit.
- **Overlaps:** `overlap_sphere(x, y, z, radius, layers)` and `overlap_box(x, y, z, hx, hy, hz, layers)` give how many bodies they found, and `found(i)` gives each one.

## Joints

`add_joint(kind, a, b, ax, ay, az, dx, dy, dz)` joins body `a` to body `b` at the anchor point `(ax, ay, az)`. Pass -1 for `b` to pin `a` to the world. `(dx, dy, dz)` is the axis for hinges, sliders and cone-twists. `kind` is `"ball"`, `"hinge"`, `"slider"`, `"fixed"`, `"cone-twist"` or `"distance"`.

`add_spring(a, b, ax, ay, az, rest, hertz, damping)` adds a spring from `a`'s centre to the point on `b`.

- **Motors and limits:** `set_motor(joint, on, speed, max_force)` and `set_limits(joint, on, lower, upper)`. A motor's speed is how fast `b` turns or slides relative to `a`.
- **Springy joints:** `set_joint_spring(joint, hertz, damping, target)`.
- **Breaking:** `set_break_force(joint, force, torque)` breaks the joint when it's pulled harder than that, and `joint_broken(joint)` says whether it has.
- **Reading:** `joint_angle(joint)` and `joint_translation(joint)`.
- **Removing:** `remove_joint(joint)`.

## Cloth and ropes

`add_cloth(x, y, z, columns, rows, spacing, mass)` makes a sheet of points starting at `(x, y, z)`, laid out along X and Z. `add_rope(x, y, z, dx, dy, dz, segments, length)` makes a rope in that direction. Both give a cloth number. They're simulated with XPBD in small substeps, collide with bodies (spheres, planes, terrain, meshes and every convex shape, and push dynamic ones back), and follow the world's gravity.

**Resolution.** The more points a cloth has, the more triangles it has and the finer its folds and wrinkles. A sheet of `columns × rows` points has `2 × (columns − 1) × (rows − 1)` triangles.
- `add_cloth_sheet(x, y, z, width, height, columns, rows, mass)` keeps the size and mass the same whatever the resolution, so you can turn the resolution up or down and get the same cloth with more or less detail. Its stiffness is scaled to the spacing, so it hangs and stretches the same at every resolution.
- `add_cloth_triangles(x, y, z, width, height, triangles, mass)` does the same but picks the columns and rows for you to get close to that many triangles with square cells.
- `triangle_count(cloth)`, `cloth_columns(cloth)` and `cloth_rows(cloth)` say what it got.

More points cost more time, roughly in step with the point count.

- **Points:** `point_count(cloth)`, `point_at(cloth, column, row)`, and `point_x/y/z(cloth, point)` for drawing.
- **Holding:** `pin_point` fixes a point where it is, `unpin_point` frees it, and `attach_point(cloth, point, body)` makes it follow a body. Removing the body lets go.
- **Feel:**
  - `set_cloth_stiffness(cloth, stretch, shear, bend)` sets the compliances: 0 is rigid, bigger is softer, and cloth's defaults are 0, 0.001 and 0.001.
  - `set_cloth_air(cloth, drag, lift, wind_x, wind_y, wind_z)` makes it catch the air and blow in the wind.
- **Removing:** `remove_cloth(cloth)`.

## Contact events

After each `step()`, `begin_count()` and `end_count()` give how many touches started and ended, and `begin_a(i)`, `begin_b(i)`, `end_a(i)` and `end_b(i)` give the two bodies. Only bodies with the `contacts` flag (on by default) report them. `set_event_callbacks(false)` skips Fizmo's per-contact callbacks, which the buffers don't need.

## Errors

Bad input throws with a message naming the problem: a body or joint number that doesn't exist, an unknown flag, kind, quality or solver name, a size or radius that isn't above 0, a terrain whose height list is the wrong length, or an index out of range.
