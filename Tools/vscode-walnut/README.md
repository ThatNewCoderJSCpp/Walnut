# Walnut for Visual Studio Code

Coloring, error underlines and hover info for Walnut (`.wal` and `.walh` files), plus `Ctrl+/` comment toggling, bracket matching and auto-closing quotes.

The coloring works in two layers:

- **The grammar** (`syntaxes/walnut.tmLanguage.json`) colors the text as soon as a file opens, from its shape alone: keywords, strings and `${}` interpolation, numbers, comments, declarations (`int: total`), calls, `self`, the name after `end function` and so on.
- **The compiler** fills in what only the type checker knows. A moment after you stop typing, the extension runs `walnut highlight` on the file (unsaved changes included). The compiler says what every name really is (a variable, a parameter, a field, a type, a namespace, an enum member, a function) and which operators call an operator function you wrote, such as the `+` in `a + b` for two `Mesh` values. The same run underlines the compiler's errors and warnings, and lists them in the **Problems** panel.

`.wal` files get a walnut icon in the explorer and on tabs, and `.walh` (or `.hwal`) headers get the same walnut with an "h" badge, whenever your file icon theme doesn't have its own icon for them. Headers are the separate **Walnut Header** language so they can have their own icon; they're colored, underlined and hovered exactly like `.wal` files.

Without the compiler, the grammar still colors everything; only the exact coloring, the underlines and the hover info are missing.

The compiler only runs when VS Code asks for semantic coloring, which some color themes turn off. If there are no underlines or hovers, add `"editor.semanticHighlighting.enabled": true` to your settings.

## Hovers

Hold the mouse over code to see what the compiler knows about it:

| Over | Shows |
|---|---|
| A variable, parameter or field | Its declaration with the real type (`auto` and `typeof(...)` are worked out), and its value if it's `const`, `constexpr` or `inline` with a constant initializer |
| A function name | Its signature |
| A call | The signature, the type the call gives, and the result if the call can run at compile time, as in `fib(12)` |
| A `constexpr` function with no parameters, where it's declared | Its result |
| A variable template, as in `factorial<5>` | Its value |
| An operator | The type of that expression, and its value if it's constant, as in the `+` of `2 + 3 * 4` |
| `sizeof`, `countof`, `alignof`, `typeid` | The value |
| `typeof`, `decltype` | The type it stands for |
| A type, alias or enum | What it is: `using Name = string`, `struct geo::Point`, `enum Color : short int` |
| An enum member | Its underlying type and value: `short int: Color::Green = 5` |
| A variable made with a constructor, as in `Pair: p(3, 4)` | Which constructor made it |
| A keyword, builtin type or literal | What it is and how it's written: `if (condition) then { ... }` with "Walnut keyword", `long int` as a 64-bit signed integer |
| `std`, a library module or an `import module.io` | Which library module it is and the file it comes from |

Inside a template, a name or operator lists the type it has in each instantiation, such as `int, float`. Turn hovers off with the `walnut.hover` setting.

### Overloads

When a function, constructor or operator has more than one overload, its hover has arrows under the signature, with `1 of 3` between them. The first one is the overload the compiler picked for that call (or the one declared there, on a declaration); the arrows step through the rest, in order of how well they fit the call's arguments, with the ones that can't take those arguments at all last. Clicking an arrow moves the cursor onto the name so the hover can open again on the next page.

## Go to Definition

`Ctrl+Click` a name (or press `F12`) to go to what it really refers to: the overload, constructor or operator function the compiler picked for that call, the variable or type the name resolves to, or the library file a `std::` name comes from. On a variable made with a constructor (`Pair: p(3, 4)`), it goes to that constructor. On `import module.io`, an imported file or an `#include`, it opens the file, and on the name after `end` it goes to what that `end` closes. On a function's definition, it goes to its earlier declarations (`function area(int: w) -> int;`) instead; on such a declaration, it goes to the definition. **Go to Declaration** always lists the declarations. Library files are found in the `Library` folder of the Walnut folder the compiler was built in; when that isn't there, the extension asks the compiler for them (`walnut library --extract`) and opens a copy from your temp folder.

## Installing

1. In VS Code, press `Ctrl+Shift+X` to open the Extensions panel.
2. Click the `...` at the top of the panel and choose **Install from VSIX...**.
3. Pick the `.vsix` file. VS Code keeps the extension after that; the `.vsix` file itself can be deleted.

To build the `.vsix` yourself from this folder (it needs [Node.js](https://nodejs.org)), run:

```
npx @vscode/vsce package --allow-missing-repository --skip-license
```

### The compiler

The extension runs `walnut` from your PATH. If it isn't on your PATH, run **Walnut: Choose Compiler...** from the command palette (`Ctrl+Shift+P`) and pick the `walnut` program in Walnut's build folder; that sets `walnut.compilerPath`. If the compiler needs extra options to find your modules, such as `-M <folder>`, add them to `walnut.extraArguments`.

## Colors

The colors are in `colors.json`, a list of names and colors:

```json
{
    "declarations": "#FFFFFF",
    "variables": "#9CDCFE",
    "self": "#F44747",
    "overloaded operators": { "color": "#DCDCAA", "bold": true }
}
```

A color is `"#RRGGBB"`, or an object with `"color"` and any of `"bold"`, `"italic"` and `"underline"`. An empty string `""` leaves that kind of code in the color theme's own color.

| Name | Colors |
|---|---|
| `declarations` | A variable, parameter or field where it's declared: `total` in `int: total = 0;` |
| `variables` | Variables, parameters and fields where they're used afterwards |
| `variable templates` | Variable templates, such as `is_arithmetic` in `std::traits::is_arithmetic<T>`; leave it out to use the `variables` color |
| `unresolved` | Names the compiler couldn't find, such as a typo or a name that was never declared |
| `self`, `this` | The keywords `self` and `this` |
| `cast expressions` | `static_cast`, `const_cast`, `dynamic_cast`, `reinterpret_cast` and `bit_cast` |
| `built-in operators` | Operators on numbers, pointers and other built-in types |
| `overloaded operators` | Operators that call an `operator(...)` function, and the operator in `operator(+) function` |
| `functions` | Function and method names |
| `template scopes` | A template before `::`, such as `float_traits` in `float_traits<T>::digits` |
| `types` | Classes, structs, enums, aliases, template parameters and concepts |
| `built-in types` | `int`, `float`, `string` and the other built-in types |
| `namespaces` | Namespace names |
| `enum members` | `Red` in `Color::Red` |
| `keywords` | `function`, `class`, `const`, `public` and other declaration keywords |
| `control keywords` | `if`, `for`, `return`, `end` and the other control-flow keywords |
| `constants` | `true`, `false`, `nullptr` and the other built-in constants |
| `strings`, `escapes`, `numbers`, `comments`, `preprocessor` | What they say |
| `error underlines` | The squiggle under the compiler's errors |
| `warning underlines` | The squiggle under the compiler's warnings |
| `error lines` | The text of every line that has a compiler error; an object can also give a `"background"` |

`unresolved` only shows up when the file has no syntax error, since a syntax error stops the compiler before it looks names up. A line with a syntax error still turns the `error lines` color.

The squiggle colors are VS Code's own error and warning colors (`editorError.foreground` and `editorWarning.foreground` in `workbench.colorCustomizations`), so they change the squiggles in other languages too. Leave them out, or set them to `""`, to keep VS Code's colors.

The other colors only change Walnut code, and they work with whatever color theme you use. The extension writes them into your VS Code settings (`editor.tokenColorCustomizations` and `editor.semanticTokenColorCustomizations`, as rules named `Walnut: ...` and keys ending in `:walnut`) and leaves the rest of those settings alone. **Walnut: Remove Colors From Settings** takes them out again.

### Editing the colors

Run **Walnut: Edit Colors** from the command palette. It opens the colors file in use, and every time you save it, the new colors show up at once.

The extension uses the first of these that exists:

1. The file named in the `walnut.colorsFile` setting. **Walnut: Choose Colors File...** sets it.
2. `Tools/vscode-walnut/colors.json` in the Walnut folder that the compiler was built in. When the compiler runs from Walnut's build folder, the extension finds this file by itself, so your colors live in the Walnut folder.
3. The copy that came with the extension. Changes to this copy are lost when the extension is reinstalled.

### Color themes

`colors.json` is one set of colors; a color theme is another, kept in a `color-themes` folder. The extension looks in two of them: the one next to your colors file (`Tools/vscode-walnut/color-themes`), and the one that came with it. A theme file looks just like `colors.json`, plus an optional `"name"` (otherwise the file name is used) and an optional `"extends"`, which starts from another theme and changes only what's listed:

```json
{
    "name": "Neon Blue",
    "extends": "Walnut Neon",
    "functions": "#33AAFF",
    "overloaded operators": { "color": "#33AAFF", "bold": true }
}
```

`colors.json` can use `"extends"` too.

Run **Walnut: Choose Color Theme...** to switch: moving through the list shows each theme on your open files right away, `Enter` keeps it, and `Escape` goes back. Picking `colors.json` goes back to your own file. The choice is the `walnut.colorTheme` setting. **Walnut: Save Colors as Theme...** saves the colors in use as a new theme, and **Walnut: Edit Colors** opens whichever file is in use.

The extension comes with these, each in its own file you can copy and change:

| Theme | Looks like |
|---|---|
| `Walnut Neon` | The bright colors `colors.json` started with |
| `Walnut Dark Modern` | VS Code's own Dark Modern colors |
| `Walnut Soft` | Pastels on a dark background |
| `Walnut Ember` | Warm oranges, reds and greens |
| `Walnut Light` | VS Code's Light+ colors, for light color themes |

### The color designer

**Walnut: Open Color Designer** opens a page with a swatch, a hex box and bold, italic and underline buttons for every color name, next to a piece of Walnut code that uses all of them, so each change shows at once. Pointing at a color (or clicking into its boxes) lights up, in orange, every piece of the code that color changes; clicking a word in the code jumps to its color. Under it is the JSON for those colors: copy it into `colors.json`, or paste colors into it to load them. **Start from** loads any theme or your colors file, **Show in the editor** puts the colors on your open Walnut files while you work (they go back if you close the page without saving), **Save to colors.json** writes them to your colors file, and **Save as Theme...** makes a new theme from them.

## Themes

The extension also has two color themes, **Walnut Dark** (based on VS Code's Dark Modern) and **Walnut Dark+** (based on Dark+), with the same Walnut colors built in. They're optional, since the colors from `colors.json` apply with any theme. Press `Ctrl+K` and then `T` to pick a theme.

## Settings

| Setting | Does |
|---|---|
| `walnut.compilerPath` | The `walnut` compiler; empty means `walnut` on your PATH |
| `walnut.colorsFile` | The colors file, as described above |
| `walnut.colorTheme` | The color theme to use instead of the colors file; empty means the colors file |
| `walnut.colors.enabled` | Whether to apply the colors file (default on) |
| `walnut.semanticHighlighting` | Whether to ask the compiler about names and operators (default on) |
| `walnut.markUnresolved` | Whether names the compiler didn't recognize get the `unresolved` color (default on) |
| `walnut.diagnostics` | Whether to underline errors and warnings (default on) |
| `walnut.highlightDelay` | How long to wait after typing stops before asking the compiler, in milliseconds (default 300) |
| `walnut.extraArguments` | Extra compiler options, such as `-M` module folders |

## The highlight command

`walnut highlight <file.wal>` analyzes a file and prints, as JSON, what each name and operator in it is, along with the errors and warnings. With `--stdin`, it reads the file's text from standard input instead of from the file, which is how the extension sends unsaved changes; imports are still found next to the named file.

```json
{
    "semantic": true,
    "tokens": [[20, 8, 1, "operator", ["overloaded"]], [21, 9, 6, "variable", ["declaration"]]],
    "diagnostics": [{ "severity": "error", "line": 21, "col": 0, "len": 0, "message": "..." }]
}
```

Each token is `[line, column, length, kind, modifiers]`, with lines counted from 0 and columns and lengths in UTF-16 code units, as editors count them. The kinds are `namespace`, `class`, `struct`, `enum`, `type`, `typeParameter`, `concept`, `function`, `method`, `variable`, `parameter`, `property`, `enumMember`, `operator` and `unresolved`. When the compiler lists no `unresolved` names, the extension treats any name in the file that the compiler didn't list as unresolved. The modifiers are `declaration`, `overloaded`, `templateQualifier` and `variableTemplate`. Built-in operators aren't listed, so any operator in the list calls an operator function. `semantic` is `false` when the file has a syntax error, since then only the errors are known. A diagnostic with `len` 0 covers its whole line.

The result also has `hovers`, `locations` and `links`. A hover is `{ "line", "col", "len", "code", "overloads", "guess", "types", "values", "results", "doc" }`, where `overloads` (only there when there is more than one) lists the signatures, best fit first. `guess` is `true` on a call the compiler couldn't settle, because it's an error or it depends on a template parameter; then `overloads` lists every candidate, most likely first, and `code` is the most likely one. `code` shows initializers and default values as they're written (`int: b = 2`, `T*: slots = nullptr`, `template <typename T, bool Reverse = false>`). `doc` is a line of Markdown saying what something is, such as `**Walnut keyword.** ...` on a keyword or which library module a `std::` namespace is. `locations` is a list of `[file, line, column, length]`, where `file` is `""` for the file itself, or a path, or `<walnut>/name.walh` for a library file. Each link is `[line, column, length, definition, [declarations]]`, where `definition` and `declarations` are indexes into `locations`.

## Other editors

`syntaxes/walnut.tmLanguage.json` is a plain TextMate grammar. Sublime Text, JetBrains IDEs (with the TextMate Bundles plugin), and other editors that read TextMate grammars can use it as it is. Editors that can run a command on a file can use `walnut highlight` the same way the VS Code extension does.
