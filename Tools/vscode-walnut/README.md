# Walnut for Visual Studio Code

Coloring and error underlines for Walnut (`.wal` and `.walh` files), plus `Ctrl+/` comment toggling, bracket matching and auto-closing quotes.

The coloring works in two layers:

- **The grammar** (`syntaxes/walnut.tmLanguage.json`) colors the text as soon as a file opens, from its shape alone: keywords, strings and `${}` interpolation, numbers, comments, declarations (`int: total`), calls, `self`, the name after `end function` and so on.
- **The compiler** fills in what only the type checker knows. A moment after you stop typing, the extension runs `walnut highlight` on the file (unsaved changes included). The compiler says what every name really is (a variable, a parameter, a field, a type, a namespace, an enum member, a function) and which operators call an operator function you wrote, such as the `+` in `a + b` for two `Mesh` values. The same run underlines the compiler's errors and warnings, and lists them in the **Problems** panel.

Without the compiler, the grammar still colors everything; only the exact coloring and the underlines are missing.

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

## Themes

The extension also has two color themes, **Walnut Dark** (based on VS Code's Dark Modern) and **Walnut Dark+** (based on Dark+), with the same Walnut colors built in. They're optional, since the colors from `colors.json` apply with any theme. Press `Ctrl+K` and then `T` to pick a theme.

## Settings

| Setting | Does |
|---|---|
| `walnut.compilerPath` | The `walnut` compiler; empty means `walnut` on your PATH |
| `walnut.colorsFile` | The colors file, as described above |
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

Each token is `[line, column, length, kind, modifiers]`, with lines counted from 0 and columns and lengths in UTF-16 code units, as editors count them. The kinds are `namespace`, `class`, `struct`, `enum`, `type`, `typeParameter`, `concept`, `function`, `method`, `variable`, `parameter`, `property`, `enumMember`, `operator` and `unresolved`. When the compiler lists no `unresolved` names, the extension treats any name in the file that the compiler didn't list as unresolved. The modifiers are `declaration`, `overloaded` and `templateQualifier`. Built-in operators aren't listed, so any operator in the list calls an operator function. `semantic` is `false` when the file has a syntax error, since then only the errors are known. A diagnostic with `len` 0 covers its whole line.

## Other editors

`syntaxes/walnut.tmLanguage.json` is a plain TextMate grammar. Sublime Text, JetBrains IDEs (with the TextMate Bundles plugin), and other editors that read TextMate grammars can use it as it is. Editors that can run a command on a file can use `walnut highlight` the same way the VS Code extension does.
