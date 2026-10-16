# windows

`windows` opens desktop windows and reads what happens in them: mouse, keyboard, touch, pen, gamepads, dropped files, resizing and closing. It also has the screens attached to the computer (`monitors`) and the system's message boxes and file pickers (`dialogs`). Drawing into a window is the job of the [`graphics`](graphics.md) module. It's Fizmo's `fizmo::windows`, and works on Windows and Linux (X11).

```walnut
import module.windows
```

Everything is in the namespace `std::windows`. Keys and mouse buttons are named in [`std::input`](input.md), and colors are [`std::colors::Color`](colors.md).

## A first example

```walnut
import module.windows
import module.input
import module.io

using std::windows::Window;
using std::windows::WindowEvent;
using std::windows::EventType;
using std::input::Key;

function main() -> int {
    Window: win = Window::open("Hello", 640, 480);
    long int: frames = 0;
    while (win.poll() && frames < 3) {
        WindowEvent: e;
        while (win.next_event(e)) {
            if (e.type == EventType::key_press && e.key == Key::escape) then {
                win.close();
            }
        }
        frames = frames + 1;
    }
    std::io::println(win.title(), " ", win.width(), "x", win.height());
    return 0;
} end function main
```

This prints `Hello 640x480`. A real program loops until `poll()` gives `false`, which happens when the user closes the window or the program calls `close()`.

## Opening and closing

| Written | Does |
|---|---|
| `Window::open(title, width, height)`, `Window::open(title, width, height, background)` | Opens a window. Throws a [`window_error`](#errors) when it can't, for example when there is no display |
| `poll()` | Handles everything the system has sent the window since the last call, queues the events, and updates the input state. Gives `false` once the window is closed. Call it once per frame |
| `is_open()` | Whether the window is still open |
| `close()` | Closes the window. Any renderer drawing into it stops |

## Events

Each `poll()` queues what happened as `WindowEvent`s:

| Method | Does |
|---|---|
| `next_event(event)` | Moves the oldest queued event into `event` and gives `true`, or gives `false` when the queue is empty |
| `events()` | All queued events, as a `std::collections::Vector<WindowEvent>`, emptying the queue |
| `clear_events()` | Throws the queued events away |

`WindowEvent` is a struct. Its `type` is an `EventType`: `mouse_move`, `mouse_click`, `mouse_release`, `mouse_scroll`, `mouse_double_click`, `key_press`, `key_release`, `text_input`, `text_editing`, `window_resize`, `window_close`, `window_focus`, `window_blur`, `window_expose`, `window_move`, `window_minimize`, `window_maximize`, `window_restore`, `dpi_changed`, `monitors_changed`, `touch_down`, `touch_move`, `touch_up`, `touch_cancel`, `pen_down`, `pen_move`, `pen_up`, `gesture`, `files_dropped`, `drag_enter`, `drag_over`, `drag_leave`, `gamepad_connected`, `gamepad_disconnected`, `gamepad_button_down`, `gamepad_button_up` or `gamepad_axis_motion`.

| Field | Holds |
|---|---|
| `x`, `y` | The pointer position; for `window_resize`, the new width and height (`width()` and `height()` read them too) |
| `dx`, `dy` | How far the mouse moved since the last `mouse_move` |
| `precise_x`, `precise_y` | The pointer position with fractions, for pens and touch |
| `button` | The `MouseButton` for clicks |
| `key`, `logical_key`, `key_name` | For key events: the physical key, the key the keyboard layout makes it, and its name |
| `modifiers` | The `Modifiers` held at the time |
| `repeat_press` | Whether a `key_press` came from holding the key down |
| `text_value` | The characters typed, for `text_input` |
| `scroll_x`, `scroll_y` | Scroll amounts, for `mouse_scroll` |
| `pointer`, `pointer_id`, `pressure` | Which kind of pointer, which finger or pen, and how hard it presses |
| `window_x`, `window_y`, `dpi_scale` | For `window_move` and `dpi_changed` |
| `paths` | The files, for `files_dropped` (call `set_drop_enabled(true)` first) |
| `gamepad`, `gamepad_button`, `gamepad_axis`, `axis_value` | For gamepad events |

`is_key(key)`, `is_mouse()` and `is_keyboard()` are shortcuts for checking the type.

## Input state

Besides events, a window keeps track of what is held down, which is usually easier for games. "Pressed" and "released" mean since the last `poll()`.

| Method | Gives |
|---|---|
| `key_down(key)`, `key_pressed(key)`, `key_released(key)`, `key_double_pressed(key)` | The state of a physical key |
| `key_held_for(key)` | How many seconds the key has been held, or 0 |
| `logical_key_down(key)` | The same as `key_down`, by the key the layout makes |
| `modifiers()` | The `Modifiers` held now |
| `mouse_x()`, `mouse_y()`, `mouse_dx()`, `mouse_dy()` | The mouse position, and how far it moved this frame |
| `mouse_down(button)`, `mouse_pressed(button)`, `mouse_released(button)`, `mouse_double_clicked(button)` | Mouse buttons; the button defaults to `MouseButton::left` |
| `scroll_x()`, `scroll_y()` | Scrolling this frame |
| `typed_text()` | The characters typed this frame |

## Gamepads

A window reads the gamepads connected to the computer, updating them on each `poll()`. Connecting and disconnecting, button presses and stick movement also arrive as `gamepad_*` events.

| Method | Gives |
|---|---|
| `gamepad_count()`, `has_gamepad()` | How many are connected |
| `gamepads()` | The connected ones, as a `Vector<Gamepad>` |
| `primary_gamepad()` | The first connected one. Throws a `window_error` when there is none |
| `gamepad(slot)` | The gamepad in a slot, connected or not |
| `any_gamepad_button(button)` | Whether any connected gamepad holds the button |
| `add_gamepad_mappings(text)`, `load_gamepad_mappings(path)` | Adds controller layouts in the SDL `gamecontrollerdb.txt` format |

A `Gamepad` has these methods. Buttons and axes are named in [`std::input`](input.md#gamepads).

- **Buttons:** `button(b)`, `pressed(b)` and `released(b)`, where pressed and released mean since the last `poll()`.
- **Sticks and triggers:** `axis(a)` gives -1.0 to 1.0 for sticks and 0.0 to 1.0 for triggers, with a dead zone; `raw_axis(a)` is without it. `set_deadzones(stick, trigger)` changes the dead zones.
- **Rumble:** `has_rumble()`, `rumble(low, high, milliseconds)` and `stop_rumble()`.
- **Details:** `connected()`, `mapped()`, `name()`, `guid()` and `slot()`.

## Changing the window

| Method | Does |
|---|---|
| `title()`, `set_title(text)` | The title bar text |
| `width()`, `height()`, `set_size(width, height)`, `set_min_size(width, height)`, `set_resizable(on)` | The size of the drawing area |
| `x()`, `y()`, `set_position(x, y)` | Where the window is on the screen |
| `minimize()`, `maximize()`, `restore()`, `focus()` | The usual window actions |
| `set_mode(mode, monitor = -1)`, `mode()` | `WindowMode::windowed`, `borderless` or `fullscreen`, on a monitor (`-1` is the one it's on) |
| `set_fullscreen(on)`, `fullscreen()`, `set_borderless(on)` | Shortcuts for the modes |
| `background()`, `set_background(color)` | The color shown before anything is drawn |
| `dpi_scale()`, `monitor_index()` | The display scale (1.0 is 96 DPI) and which monitor the window is on |
| `set_cursor(cursor)` | A system `Cursor`: `arrow`, `ibeam`, `hand`, `crosshair`, `wait`, `progress`, `not_allowed`, `move`, `resize_horizontal`, `resize_vertical`, `resize_nwse`, `resize_nesw` or `help` |
| `set_cursor(image, hot_x = 0, hot_y = 0)` | A cursor drawn from a [`std::images::Image`](images.md) |
| `set_cursor_visible(on)`, `set_cursor_locked(on)`, `set_relative_mouse(on)` | Hiding the cursor, keeping it inside the window, and raw mouse movement for camera controls |
| `clipboard()`, `set_clipboard(text)` | The system clipboard |
| `set_drop_enabled(on)` | Lets files be dropped on the window |
| `start_text_input()`, `stop_text_input()`, `text_input_active()` | Turns on text entry (and the input method editor for languages that need one) |

## Monitors

`std::windows::monitors()` gives a `Vector<Monitor>` of the attached screens, and `std::windows::primary_monitor()` the main one. A `Monitor` has the fields `name`, `x`, `y`, `width`, `height`, the usable area without task bars (`work_x`, `work_y`, `work_width`, `work_height`), `refresh_rate`, `scale`, `dpi` and `primary`.

## Dialogs

`std::windows::dialogs` shows the system's message boxes and file pickers. A picker gives an `Optional<string>` that is empty when the user cancels (see [`optional`](optional.md)).

| Function | Does |
|---|---|
| `message_box(title, message, buttons = Buttons::ok, icon = Icon::info)` | Shows a message and gives the button chosen, as an `Answer` (`ok`, `cancel`, `yes`, `no` or `retry`). `Buttons` is `ok`, `ok_cancel`, `yes_no`, `yes_no_cancel` or `retry_cancel`; `Icon` is `info`, `warning`, `error` or `question` |
| `show_info(title, message)`, `show_warning(...)`, `show_error(...)` | A message with an OK button |
| `ask_yes_no(title, message)` | `true` when the user picks Yes |
| `open_file(title = "", start = "", filters = ...)` | Picks a file to open |
| `open_files(...)` | Picks several; gives a `Vector<string>`, empty when cancelled |
| `save_file(title = "", start = "", name = "", filters = ...)` | Picks where to save |
| `select_folder(title = "", start = "")` | Picks a folder |
| `available()` | Whether file pickers work here (on Linux they need `zenity` or `kdialog`) |

`filters` is a `Vector<FileFilter>`; a `FileFilter` is a name and a list of patterns, such as `FileFilter("Images", "*.png;*.jpg")`.

## Errors

`Window::open` throws a `std::errors::window_error` when the window can't be made. Its `what()` (or its `message` field) says why.
