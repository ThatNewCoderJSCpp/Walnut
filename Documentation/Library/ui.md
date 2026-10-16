# ui

`ui` builds windowed user interfaces from widgets: labels, buttons, checkboxes, toggles, sliders, progress bars, dropdowns, text and number fields, radio buttons, color pickers, tabs and scroll areas, laid out in stacks and panels. It handles the mouse, keyboard, focus, tooltips and text entry, and draws with a dark or light theme. It's Fizmo's `fizmo::ui`.

```walnut
import module.ui
```

Everything is in the namespace `std::ui`. It uses a [`Window`](windows.md) and a [`Renderer`](graphics.md).

## A first example

```walnut
import module.ui
import module.colors
import module.io

using std::ui::Ui;
using std::ui::Panel;
using std::ui::Label;
using std::ui::Button;
using std::ui::Slider;
using std::ui::TextField;

function main() -> int {
    std::windows::Window: win = std::windows::Window::open("Settings", 400, 260);
    std::graphics::Renderer: r = std::graphics::Renderer::make(win);
    Ui: ui = Ui::make(win);
    Panel: panel = Panel::make();
    Label: title = Label::make("Settings");
    title.set_bold();
    TextField: name = TextField::make("", "Your name");
    Slider: volume = Slider::make(0.5, 0.0, 1.0);
    Button: save = Button::make("Save");
    panel.add(title);
    panel.add(name);
    panel.add(volume);
    panel.add(save);
    ui.add(panel);
    long int: frames = 0;
    while (win.poll() && frames < 3) {
        if (save.clicked()) then {
            std::io::println("saved ", name.text_value(), " at volume ", volume.value());
        }
        if (frames == 1) then {
            name.set_text("Ada");
            save.click();
        }
        ui.update(0.016);
        r.begin_frame();
        r.clear(std::colors::black);
        ui.draw(r);
        r.present();
        frames = frames + 1;
    }
    return 0;
} end function main
```

This prints `saved Ada at volume 0.5`. `save.click()` stands in for the user clicking the button; in a real program the clicks come from the mouse and keyboard.

## Running a Ui

| Written | Does |
|---|---|
| `Ui::make(window)` | Makes a Ui that listens to the window's mouse and keyboard |
| `add(widget)`, `remove(widget)`, `root()` | Widgets go into the root, a vertical `Stack` filling the window |
| `update(dt)` | Advances animations, tooltips and key repeat; call it once per frame |
| `draw(renderer)` | Lays the widgets out to the window's size and draws them, between `begin_frame()` and `present()` |
| `set_dark()`, `set_light()`, `set_accent(color)`, `set_font(name)`, `set_font_size(px)`, `set_scale(scale)` | The look |
| `wants_mouse()`, `wants_keyboard()` | Whether the Ui is using the mouse or keyboard, so a game behind it can ignore them |
| `alert(title, message)`, `modal_open()` | A message shown over everything, with an OK button |
| `clear_focus()` | Takes the keyboard focus away from any widget |

## Widgets

Each widget is made with a static `make` function (`Button::make("OK")`), changed with its methods, and then added to a container or the Ui. Copies of a widget variable refer to the same widget.

Widgets don't take callbacks. Instead, each one remembers what happened, and the program asks once per frame: `button.clicked()` is `true` once for each click, and `changed()` is `true` once after the value changes. This fits the usual `while (win.poll())` loop.

| Widget | Made with | Main methods |
|---|---|---|
| `Label` | `Label::make(text)` | `set_text`, `text_value`, `set_color`, `set_font_size`, `set_bold`, `set_muted`, `set_align` |
| `Button` | `Button::make(text)` | `clicked()`, `click()`, `set_text`, `text_value`, `set_style(ButtonStyle::normal / primary / danger / flat)` |
| `Checkbox`, `Toggle` | `make(text, on = false)` | `checked()`, `set_checked`, `changed()`, `set_text` |
| `Slider` | `make()`, `make(value, min, max)`, `make(value, min, max, step)` | `value()`, `set_value`, `set_range`, `set_step`, `set_show_value(on, decimals)`, `changed()`, `released()`, `dragging()` |
| `ProgressBar` | `ProgressBar::make(value)` | `set_value` (0.0 to 1.0), `value()`, `set_indeterminate`, `set_show_text`, `set_text`, `set_color` |
| `Spinner` | `Spinner::make(size)` | A busy indicator; `set_color` |
| `Dropdown` | `Dropdown::make(items)` | `selected()`, `selected_text()`, `set_selected`, `add_item`, `set_items`, `item_count()`, `set_placeholder`, `set_searchable`, `changed()` |
| `TextField` | `make(text = "")`, `make(text, placeholder)` | `text_value()`, `set_text`, `changed()`, `submitted()` (Enter), `set_password`, `set_read_only`, `set_max_length`, `set_allowed_chars`, `select_all` |
| `IntField` | `make(value)`, `make(value, min, max, step = 1)` | `value()`, `set_value`, `set_range`, `set_spin_buttons`, `changed()`, `submitted()` |
| `FloatField` | `make(value)`, `make(value, min, max, step)` | The same with floats, plus `set_step` and `set_decimals`. Both number fields also accept typed expressions such as `2*3+1` |
| `ColorPicker` | `ColorPicker::make(color, alpha = true)` | `color()`, `set_color`, `set_alpha_enabled`, `changed()` |
| `RadioGroup` | `RadioGroup::make()` | `option(text, value)` makes a `RadioButton` to add to a container; `selected()` gives the chosen value, and `set_selected` and `changed()` work as usual |
| `Separator` | `Separator::make(vertical = false)` | A thin line |
| `Spacer` | `Spacer::make(flex = 1.0)` | Empty space that takes up the room left over |

Every widget also has `set_size`, `set_width`, `set_height`, `set_min_size`, `set_max_size`, `set_flex` (how much spare room it takes), `set_visible`, `set_enabled`, `set_tooltip`, `set_id`, `hovered()`, `focused()`, `focus()`, and `x()`, `y()`, `width()` and `height()` from the last layout.

## Containers

| Container | Made with | Does |
|---|---|---|
| `Stack` | `Stack::vertical(spacing = 6.0)`, `Stack::horizontal(spacing = 6.0)` | Lays its children out in a column or a row |
| `Panel` | `Panel::make(horizontal = false)` | A stack with a background, border and padding |
| `ScrollView` | `ScrollView::make()` | Scrolls one child set with `set_content(widget)`; `scroll_to`, `scroll_x()`, `scroll_y()`, `set_scroll_directions(horizontal, vertical)` |
| `TabView` | `TabView::make()` | `add_tab(title, widget)`, `selected()`, `set_selected`, `tab_count()`, `changed()` |

Stacks and panels have `add(widget)`, `remove(widget)`, `child_count()`, `set_spacing`, `set_padding`, `set_background(color)`, `set_border(color)`, `set_radius`, and `set_main_align` and `set_cross_align` with an `Align`: `start`, `center`, `finish` or `stretch`. (`end` is a Walnut keyword, so the last one is `finish`.) A widget can be in one container at a time; adding it somewhere else moves it.

## Errors

`Ui::make`, `add` and `remove` throw a `std::errors::ui_error` when they can't do what was asked. Its `what()` (or its `message` field) says why.
