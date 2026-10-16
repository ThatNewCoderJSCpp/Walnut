# input

`input` names the keys and mouse buttons, and has the `Modifiers` set for Shift, Control, Alt and the rest. A [`Window`](windows.md) uses these names in its events and in its `key_down`, `mouse_pressed` and other input checks.

```walnut
import module.input
```

Everything is in the namespace `std::input`.

## A first example

```walnut
import module.input
import module.io

using std::input::Key;

function main() -> int {
    std::io::println(std::input::key_name(Key::space), " ", std::input::key_name(Key::f5), " ", std::input::is_arrow(Key::left_arrow));
    std::input::Modifiers: mods = std::input::Modifiers(std::input::modifier::control + std::input::modifier::shift);
    std::io::println(mods, " ", mods.control(), " ", mods.alt());
    return 0;
} end function main
```

This prints `Space F5 1` and `Ctrl+Shift 1 0`.

## Keys

`Key` names a physical key, by where it sits on a US keyboard:

| Group | Names |
|---|---|
| Letters | `a` to `z` |
| Number row | `num0` to `num9` |
| Function keys | `f1` to `f24` |
| Editing | `escape`, `enter`, `tab`, `backspace`, `space`, `insert`, `delete_key`, `home`, `end_key`, `page_up`, `page_down` |
| Arrows | `left_arrow`, `right_arrow`, `up_arrow`, `down_arrow` |
| Punctuation | `minus`, `equals`, `left_bracket`, `right_bracket`, `backslash`, `semicolon`, `quote`, `grave`, `comma`, `period`, `slash`, `non_us_backslash` |
| Locks and system | `caps_lock`, `scroll_lock`, `num_lock`, `print_screen`, `pause`, `menu` |
| Number pad | `numpad0` to `numpad9`, `numpad_divide`, `numpad_multiply`, `numpad_subtract`, `numpad_add`, `numpad_enter`, `numpad_decimal`, `numpad_equals` |
| Modifiers | `left_shift`, `right_shift`, `left_control`, `right_control`, `left_alt`, `right_alt`, `left_meta`, `right_meta` |
| Media and browser | `volume_mute`, `volume_down`, `volume_up`, `media_play_pause`, `media_stop`, `media_next`, `media_previous`, `browser_back`, `browser_forward` |
| Other | `unknown` |

`end`, `delete` and `down` are Walnut keywords, so those keys are `end_key`, `delete_key` and `down_arrow` (and the other arrows match).

| Function | Gives |
|---|---|
| `key_name(key)` | The key's name, such as `"Space"` or `"LeftArrow"` |
| `key_from_name(name)` | The key with that name, ignoring case, or `Key::unknown`. Common other spellings such as `"Esc"` work too |
| `is_letter(key)`, `is_digit(key)`, `is_function_key(key)`, `is_arrow(key)`, `is_modifier(key)` | Which group a key is in. `is_digit` counts the number row and the number pad |
| `key_from_index(n)` | The key with that number, or `Key::unknown` |

## Mouse buttons and pointers

`MouseButton` is `no_button`, `left`, `right`, `middle`, `back` or `forward`. `PointerType` is `mouse`, `touch` or `pen`.

## Gamepads

`GamepadButton` names buttons by position, so the same name works for every brand of controller:

- **Face buttons:** `south`, `east`, `west` and `north` (A, B, X and Y on an Xbox pad).
- **Other buttons:** `back`, `guide`, `start`, `left_stick`, `right_stick`, `left_shoulder` and `right_shoulder`.
- **D-pad:** `dpad_up`, `dpad_down`, `dpad_left` and `dpad_right`.
- **Extras:** `misc1`, `paddle1` to `paddle4`, and `touchpad`.

`GamepadAxis` is `left_x`, `left_y`, `right_x`, `right_y`, `left_trigger` or `right_trigger`. A window reads gamepads; see [Gamepads](windows.md#gamepads).

## Modifiers

`Modifiers` is a set of the modifier keys that were held. Its methods are `shift()`, `control()`, `alt()`, `meta()`, `caps_lock()`, `num_lock()`, `alt_gr()`, `none()` and `has(flag)`, and its `bits` field holds the raw flags. The flags are the constants in `std::input::modifier`: `shift`, `control`, `alt`, `meta`, `caps_lock`, `num_lock` and `alt_gr`. Add them together to make a set, as in the example above. `to_string()` gives text such as `"Ctrl+Shift"`, or `"none"`.
