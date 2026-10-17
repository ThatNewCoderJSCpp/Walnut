const vscode = require('vscode');
const crypto = require('crypto');

const descriptions = {
    'declarations': 'A variable, parameter or field where it is declared',
    'variables': 'Variables, parameters and fields where they are used',
    'variable templates': 'Variable templates such as std::traits::is_arithmetic<T>',
    'unresolved': 'Names the compiler could not find',
    'self': 'The self keyword',
    'cast expressions': 'static_cast, const_cast and the other casts',
    'this': 'The this keyword',
    'built-in operators': 'Operators on numbers, pointers and other built-in types',
    'overloaded operators': 'Operators that call an operator(...) function',
    'functions': 'Function and method names',
    'template scopes': 'A template before ::, as in float_traits<T>::digits',
    'types': 'Classes, structs, enums, aliases, template parameters, concepts',
    'built-in types': 'int, float, string and the other built-in types',
    'namespaces': 'Namespace names',
    'enum members': 'Red in Color::Red',
    'keywords': 'function, class, const, public and other declaration keywords',
    'control keywords': 'if, for, return, end and the other control keywords',
    'constants': 'true, false, nullptr and the other built-in constants',
    'strings': 'String and character literals',
    'escapes': 'Escapes such as \\n inside strings',
    'numbers': 'Number literals',
    'comments': 'Comments',
    'preprocessor': '#define and the other directives',
    'error underlines': 'The squiggle under errors (changes other languages too)',
    'warning underlines': 'The squiggle under warnings (changes other languages too)',
    'error lines': 'The text of every line with an error'
};

const sample = String.raw`⟦comments|// How each color looks in Walnut code⟧
⟦preprocessor|#define⟧ LIMIT ⟦numbers|100⟧
⟦keywords|import⟧ module.io

⟦keywords|namespace⟧ ⟦namespaces|geo⟧ {
    ⟦keywords|enum⟧ ⟦types|Color⟧ { ⟦enum members|Red⟧, ⟦enum members|Green⟧ }

    ⟦keywords|struct⟧ ⟦types|Vec⟧ {
        ⟦built-in types|int⟧: ⟦declarations|x⟧ = ⟦numbers|0⟧;

        ⟦keywords|operator⟧(⟦overloaded operators|+⟧) ⟦keywords|function⟧(⟦keywords|const⟧ ⟦types|Vec⟧&: ⟦declarations|o⟧) ⟦keywords|const⟧ ⟦built-in operators|->⟧ ⟦types|Vec⟧ {
            ⟦control keywords|return⟧ ⟦types|Vec⟧{ ⟦self|self⟧.⟦variables|x⟧ ⟦built-in operators|+⟧ ⟦variables|o⟧.⟦variables|x⟧ };
        }
    } ⟦control keywords|end⟧ ⟦keywords|struct⟧ ⟦types|Vec⟧
}

⟦keywords|template⟧ <⟦keywords|typename⟧ ⟦types|T⟧>
    ⟦keywords|requires⟧ ⟦namespaces|std⟧::⟦namespaces|traits⟧::⟦variable templates|is_arithmetic⟧<⟦types|T⟧>
⟦keywords|function⟧ ⟦functions|scale⟧(⟦types|T⟧: ⟦declarations|value⟧, ⟦built-in types|int⟧: ⟦declarations|times⟧) ⟦built-in operators|->⟧ ⟦types|T⟧ {
    ⟦keywords|const⟧ ⟦built-in types|int⟧: ⟦declarations|digits⟧ = ⟦template scopes|float_traits⟧<⟦types|T⟧>::⟦variables|digits⟧;
    ⟦control keywords|if⟧ (⟦variables|times⟧ ⟦built-in operators|>⟧ LIMIT) {
        ⟦control keywords|return⟧ ⟦cast expressions|static_cast⟧<⟦types|T⟧>(⟦numbers|0⟧);
    }
    ⟦namespaces|geo⟧::⟦types|Vec⟧: ⟦declarations|a⟧ = ⟦namespaces|geo⟧::⟦types|Vec⟧{ ⟦numbers|1⟧ };
    ⟦namespaces|geo⟧::⟦types|Vec⟧: ⟦declarations|b⟧ = ⟦variables|a⟧ ⟦overloaded operators|+⟧ ⟦variables|a⟧;
    ⟦namespaces|geo⟧::⟦types|Color⟧: ⟦declarations|c⟧ = ⟦namespaces|geo⟧::⟦types|Color⟧::⟦enum members|Green⟧;
    ⟦built-in types|string⟧: ⟦declarations|note⟧ = ⟦strings|"tab⟧⟦escapes|\t⟧⟦strings|$⦃⟧⟦variables|times⟧⟦strings|}"⟧;
    ⟦built-in types|bool⟧: ⟦declarations|ok⟧ = ⟦constants|true⟧ ⟦built-in operators|&&⟧ ⟦this|this⟧ ⟦built-in operators|!=⟧ ⟦constants|nullptr⟧;
!E    ⟦unresolved+error underlines|totl⟧ ⟦built-in operators|=⟧ ⟦numbers|3.5⟧;
    ⟦built-in types|int⟧: ⟦declarations+warning underlines|unused⟧ = ⟦functions|scale⟧(⟦numbers|2⟧, ⟦numbers|3⟧);
    ⟦control keywords|return⟧ ⟦variables|value⟧ ⟦built-in operators|*⟧ ⟦variables|times⟧;
} ⟦control keywords|end⟧ ⟦keywords|function⟧ ⟦functions|scale⟧`.replace(/\$⦃/g, '$' + '{');

let panel = null;

function open(api) {
    if (panel) {
        panel.reveal();
        return;
    }

    panel = vscode.window.createWebviewPanel('walnutColorDesigner', 'Walnut Colors', vscode.ViewColumn.Active, { enableScripts: true, retainContextWhenHidden: true });
    const nonce = crypto.randomBytes(16).toString('hex');
    panel.webview.html = page(nonce, panel.webview.cspSource);
    let previewed = false;
    let saved = false;

    const send = (theme, colors) => panel.webview.postMessage({ type: 'colors', theme, colors, themes: api.themes() });

    panel.webview.onDidReceiveMessage(async message => {
        try {
            switch (message.type) {
                case 'ready': {
                    let current = { theme: '', colors: {} };
                    try { current = api.current(); } catch (e) { vscode.window.showWarningMessage(`Walnut couldn't read the current colors: ${e.message}`); }
                    panel.webview.postMessage({ type: 'setup', names: api.names, descriptions, sample });
                    send(current.theme, current.colors);
                    break;
                }

                case 'load':
                    send(message.theme, api.colorsOf(message.theme));
                    break;

                case 'preview':
                    previewed = true;
                    await api.preview(message.colors);
                    break;

                case 'stopPreview':
                    if (previewed) await api.restore();
                    previewed = false;
                    break;

                case 'copy':
                    await vscode.env.clipboard.writeText(message.json);
                    vscode.window.setStatusBarMessage('Walnut colors copied as JSON', 2500);
                    break;

                case 'save':
                    if (await api.saveToColorsFile(message.colors)) { saved = true; previewed = false; }
                    break;

                case 'saveTheme':
                    await api.saveAsTheme(message.colors);
                    panel.webview.postMessage({ type: 'themes', themes: api.themes() });
                    break;
            }
        } catch (e) {
            vscode.window.showWarningMessage(`Walnut colors: ${e.message}`);
        }
    });

    panel.onDidDispose(() => {
        panel = null;
        if (previewed && !saved) api.restore();
    });
}

function page(nonce, csp) {
    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${csp} 'unsafe-inline'; script-src 'nonce-${nonce}';">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Walnut Colors</title>
<style>
    :root { --gap: 10px; }
    body { padding: 0 16px 16px; color: var(--vscode-foreground); font-family: var(--vscode-font-family); font-size: var(--vscode-font-size); }
    header { position: sticky; top: 0; z-index: 2; display: flex; flex-wrap: wrap; align-items: center; gap: var(--gap); padding: 12px 0; background: var(--vscode-editor-background); border-bottom: 1px solid var(--vscode-panel-border, transparent); }
    header h1 { font-size: 1.15em; margin: 0 8px 0 0; font-weight: 600; }
    header .spacer { flex: 1; }
    select, input[type=text], textarea { color: var(--vscode-input-foreground); background: var(--vscode-input-background); border: 1px solid var(--vscode-input-border, var(--vscode-panel-border, #555)); border-radius: 2px; font-family: inherit; }
    select { padding: 3px 6px; }
    button { color: var(--vscode-button-foreground); background: var(--vscode-button-background); border: none; border-radius: 2px; padding: 5px 11px; cursor: pointer; font-family: inherit; }
    button:hover { background: var(--vscode-button-hoverBackground); }
    button.secondary { color: var(--vscode-button-secondaryForeground); background: var(--vscode-button-secondaryBackground); }
    button.secondary:hover { background: var(--vscode-button-secondaryHoverBackground); }
    label.check { display: inline-flex; align-items: center; gap: 5px; cursor: pointer; }
    main { display: grid; grid-template-columns: minmax(330px, 420px) 1fr; gap: 20px; margin-top: 14px; }
    @media (max-width: 860px) { main { grid-template-columns: 1fr; } }
    .rows { display: flex; flex-direction: column; gap: 2px; }
    .row { display: grid; grid-template-columns: 1fr auto; align-items: center; gap: 8px; padding: 5px 8px; border-radius: 4px; }
    .row:hover, .row.lit { background: var(--vscode-list-hoverBackground); }
    .row .name { font-weight: 600; }
    .row .what { opacity: .7; font-size: .9em; margin-top: 1px; }
    .controls { display: flex; align-items: center; gap: 4px; }
    .swatch { width: 26px; height: 22px; padding: 0; border: 1px solid var(--vscode-panel-border, #555); border-radius: 3px; background: none; cursor: pointer; }
    .swatch.empty { background: repeating-linear-gradient(45deg, transparent 0 4px, rgba(128,128,128,.35) 4px 8px); }
    .controls input[type=text] { width: 76px; padding: 2px 4px; font-family: var(--vscode-editor-font-family); }
    .toggle { min-width: 24px; padding: 2px 6px; color: var(--vscode-foreground); background: transparent; border: 1px solid var(--vscode-panel-border, #555); }
    .toggle:hover { background: var(--vscode-toolbar-hoverBackground); }
    .toggle.on { color: var(--vscode-button-foreground); background: var(--vscode-button-background); border-color: transparent; }
    .toggle.b { font-weight: 700; } .toggle.i { font-style: italic; } .toggle.u { text-decoration: underline; }
    .clear { padding: 2px 6px; color: var(--vscode-foreground); background: transparent; opacity: .6; }
    .clear:hover { opacity: 1; background: var(--vscode-toolbar-hoverBackground); }
    .side { display: flex; flex-direction: column; gap: 14px; min-width: 0; position: sticky; top: 64px; align-self: start; max-height: calc(100vh - 80px); overflow-y: auto; }
    @media (max-width: 860px) { .side { position: static; max-height: none; } }
    .preview { margin: 0; padding: 14px 16px; overflow-x: auto; border-radius: 6px; border: 1px solid var(--vscode-panel-border, transparent); background: var(--vscode-editor-background); color: var(--vscode-editor-foreground); font-family: var(--vscode-editor-font-family); font-size: var(--vscode-editor-font-size); line-height: 1.5; }
    .preview span[data-k] { cursor: pointer; border-radius: 2px; }
    .preview span[data-k]:hover { outline: 1px dashed currentColor; outline-offset: 1px; }
    .preview .line { display: block; min-height: 1.5em; }
    .preview span.mark { background: rgba(255, 166, 77, .42); box-shadow: 0 0 0 2px rgba(255, 166, 77, .42); border-radius: 3px; }
    .preview .line.mark-line { background: rgba(255, 166, 77, .28); box-shadow: inset 3px 0 0 rgba(255, 166, 77, .9); }
    h2 { font-size: 1em; margin: 0 0 6px; font-weight: 600; }
    textarea { width: 100%; box-sizing: border-box; min-height: 220px; padding: 8px; font-family: var(--vscode-editor-font-family); font-size: var(--vscode-editor-font-size); resize: vertical; }
    .note { opacity: .7; font-size: .9em; margin: 4px 0 0; }
    .bad { color: var(--vscode-errorForeground); }
</style>
</head>
<body>
<header>
    <h1>Walnut Colors</h1>
    <label>Start from <select id="theme"></select></label>
    <label class="check" title="Show these colors in your open Walnut files while you edit; they go back if you close this without saving"><input type="checkbox" id="live"> Show in the editor</label>
    <span class="spacer"></span>
    <button class="secondary" id="copy">Copy JSON</button>
    <button class="secondary" id="saveTheme">Save as Theme…</button>
    <button id="save">Save to colors.json</button>
</header>
<main>
    <section class="rows" id="rows"></section>
    <section class="side">
        <div>
            <h2>Preview</h2>
            <pre class="preview" id="preview"></pre>
            <p class="note">Point at a color on the left to light up everything it changes; click a word here to jump to its color. The background is your current VS Code theme's.</p>
        </div>
        <div>
            <h2>JSON</h2>
            <textarea id="json" spellcheck="false"></textarea>
            <p class="note" id="jsonNote">Paste colors here to load them, or copy them into colors.json.</p>
        </div>
    </section>
</main>
<script nonce="${nonce}">
(() => {
    const vscode = acquireVsCodeApi();
    const squiggles = new Set(['error underlines', 'warning underlines']);
    let names = [];
    let descriptions = {};
    let lines = [];
    let colors = {};
    let liveTimer = null;
    let marked = '';
    const $ = id => document.getElementById(id);

    const hexOk = v => /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(v);

    function entry(name) {
        const v = colors[name];
        if (typeof v === 'string') return { color: v };
        if (v && typeof v === 'object') return Object.assign({}, v);
        return {};
    }

    function store(name, e) {
        const clean = {};
        if (e.color) clean.color = e.color;
        if (!squiggles.has(name)) {
            if (e.bold) clean.bold = true;
            if (e.italic) clean.italic = true;
            if (e.underline) clean.underline = true;
        }
        if (name === 'error lines' && e.background) clean.background = e.background;
        const keys = Object.keys(clean);
        if (!keys.length) colors[name] = '';
        else if (keys.length === 1 && clean.color) colors[name] = clean.color;
        else colors[name] = clean;
    }

    function ordered() {
        const out = {};
        for (const n of names) if (colors[n] !== undefined) out[n] = colors[n];
        for (const [k, v] of Object.entries(colors)) if (out[k] === undefined) out[k] = v;
        return out;
    }

    function parseSample(text) {
        return text.split('\\n').map(raw => {
            const error = raw.startsWith('!E');
            const body = error ? raw.slice(2) : raw;
            const parts = [];
            const re = /⟦([^|⟧]+)\\|([^⟧]*)⟧/g;
            let last = 0, m;
            while ((m = re.exec(body))) {
                if (m.index > last) parts.push({ text: body.slice(last, m.index), kinds: [] });
                parts.push({ text: m[2], kinds: m[1].split('+') });
                last = re.lastIndex;
            }
            if (last < body.length) parts.push({ text: body.slice(last), kinds: [] });
            return { error, parts };
        });
    }

    function styleOf(kinds, errorLine) {
        const css = [];
        const main = kinds.find(k => !squiggles.has(k));
        const squiggle = kinds.find(k => squiggles.has(k));
        const e = main ? entry(main) : {};
        if (e.color) css.push('color:' + e.color);
        if (e.bold) css.push('font-weight:bold');
        if (e.italic) css.push('font-style:italic');
        const deco = [];
        if (e.underline) deco.push('underline');
        if (errorLine) {
            const el = entry('error lines');
            if (el.color) css.push('color:' + el.color);
            if (el.bold) css.push('font-weight:bold');
            if (el.italic) css.push('font-style:italic');
            if (el.underline && !deco.length) deco.push('underline');
        }
        if (squiggle) {
            const s = entry(squiggle).color || (squiggle === 'error underlines' ? 'var(--vscode-editorError-foreground)' : 'var(--vscode-editorWarning-foreground)');
            css.push('text-decoration:underline wavy ' + s, 'text-underline-offset:3px', 'text-decoration-skip-ink:none');
        } else if (deco.length) {
            css.push('text-decoration:underline');
        }
        return css.join(';');
    }

    function escape(t) {
        return t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function renderPreview() {
        const el = entry('error lines');
        $('preview').innerHTML = lines.map(line => {
            const bg = line.error && el.background ? ' style="background:' + el.background + '"' : '';
            const inner = line.parts.map(p => {
                const style = styleOf(p.kinds, line.error);
                const main = p.kinds.find(k => !squiggles.has(k)) || p.kinds[0];
                if (!p.kinds.length && !line.error) return escape(p.text);
                return '<span' + (main ? ' data-k="' + main + '" title="' + main + '"' : '') + (p.kinds.length ? ' data-all="' + p.kinds.join('|') + '"' : '') + (style ? ' style="' + style + '"' : '') + '>' + escape(p.text) + '</span>';
            }).join('');
            return '<span class="line"' + (line.error ? ' data-error="1"' : '') + bg + '>' + (inner || ' ') + '</span>';
        }).join('');
        mark(marked);
    }

    function mark(name) {
        marked = name || '';
        for (const el of document.querySelectorAll('#preview .mark, #preview .mark-line')) el.classList.remove('mark', 'mark-line');
        if (!marked) return;

        if (marked === 'error lines') {
            for (const line of document.querySelectorAll('#preview .line[data-error]')) line.classList.add('mark-line');
            return;
        }

        for (const span of document.querySelectorAll('#preview span[data-all]')) {
            if (span.dataset.all.split('|').includes(marked)) span.classList.add('mark');
        }
    }

    function renderJson() {
        $('json').value = JSON.stringify(ordered(), null, 4);
        $('jsonNote').textContent = 'Paste colors here to load them, or copy them into colors.json.';
        $('jsonNote').classList.remove('bad');
    }

    function refreshRow(name) {
        const row = document.querySelector('.row[data-name="' + CSS.escape(name) + '"]');
        if (!row) return;
        const e = entry(name);
        const swatch = row.querySelector('.swatch');
        const hex = row.querySelector('input.hex');
        const has = !!(e.color && hexOk(e.color));
        swatch.classList.toggle('empty', !has);
        swatch.style.background = has ? e.color : '';
        if (document.activeElement !== hex) hex.value = e.color || '';
        hex.placeholder = 'theme';
        for (const t of row.querySelectorAll('.toggle')) t.classList.toggle('on', !!e[t.dataset.style]);
        const bg = row.querySelector('input.bg');
        if (bg && document.activeElement !== bg) bg.value = e.background || '';
    }

    function changed(name, fromJson) {
        if (name) refreshRow(name); else names.forEach(refreshRow);
        renderPreview();
        if (!fromJson) renderJson();
        if ($('live').checked) {
            clearTimeout(liveTimer);
            liveTimer = setTimeout(() => vscode.postMessage({ type: 'preview', colors: ordered() }), 120);
        }
    }

    function buildRows() {
        const rows = $('rows');
        rows.innerHTML = '';

        for (const name of names) {
            const row = document.createElement('div');
            row.className = 'row';
            row.dataset.name = name;
            const styled = !squiggles.has(name);
            row.innerHTML =
                '<div><div class="name"></div><div class="what"></div></div>' +
                '<div class="controls">' +
                    '<input type="color" class="picker" hidden>' +
                    '<button class="swatch" title="Pick a color"></button>' +
                    '<input type="text" class="hex" spellcheck="false" maxlength="9">' +
                    (styled ? '<button class="toggle b" data-style="bold" title="Bold">B</button><button class="toggle i" data-style="italic" title="Italic">I</button><button class="toggle u" data-style="underline" title="Underline">U</button>' : '') +
                    (name === 'error lines' ? '<input type="text" class="bg" spellcheck="false" maxlength="9" placeholder="no bg" title="Background (#RRGGBB or #RRGGBBAA)">' : '') +
                    '<button class="clear" title="Use the color theme\\'s own color">✕</button>' +
                '</div>';
            row.querySelector('.name').textContent = name;
            row.querySelector('.what').textContent = descriptions[name] || '';
            const picker = row.querySelector('.picker');
            const hex = row.querySelector('.hex');

            row.querySelector('.swatch').addEventListener('click', () => {
                const e = entry(name);
                picker.value = e.color && hexOk(e.color) ? e.color.slice(0, 7) : '#ffffff';
                picker.click();
            });
            picker.addEventListener('input', () => {
                const e = entry(name);
                const alpha = e.color && e.color.length === 9 ? e.color.slice(7) : '';
                e.color = picker.value.toUpperCase() + alpha;
                store(name, e);
                changed(name);
            });
            hex.addEventListener('input', () => {
                let v = hex.value.trim();
                if (v && v[0] !== '#') v = '#' + v;
                if (v && !hexOk(v)) { hex.classList.add('bad'); return; }
                hex.classList.remove('bad');
                const e = entry(name);
                e.color = v.toUpperCase();
                store(name, e);
                changed(name);
            });
            for (const t of row.querySelectorAll('.toggle')) {
                t.addEventListener('click', () => {
                    const e = entry(name);
                    e[t.dataset.style] = !e[t.dataset.style];
                    store(name, e);
                    changed(name);
                });
            }
            const bg = row.querySelector('.bg');
            if (bg) bg.addEventListener('input', () => {
                let v = bg.value.trim();
                if (v && v[0] !== '#') v = '#' + v;
                if (v && !hexOk(v)) { bg.classList.add('bad'); return; }
                bg.classList.remove('bad');
                const e = entry(name);
                e.background = v.toUpperCase();
                store(name, e);
                changed(name);
            });
            row.querySelector('.clear').addEventListener('click', () => {
                store(name, {});
                changed(name);
            });
            row.addEventListener('mouseenter', () => mark(name));
            row.addEventListener('mouseleave', () => { if (!row.contains(document.activeElement)) mark(''); });
            row.addEventListener('focusin', () => mark(name));
            row.addEventListener('focusout', ev => { if (!row.contains(ev.relatedTarget)) mark(''); });
            rows.appendChild(row);
        }
    }

    function setThemes(themes, selected) {
        const select = $('theme');
        const keep = selected === undefined ? select.value : selected;
        select.innerHTML = '';
        for (const t of themes) {
            const o = document.createElement('option');
            o.value = t.theme;
            o.textContent = t.label;
            select.appendChild(o);
        }
        select.value = keep;
    }

    $('preview').addEventListener('mouseover', ev => {
        const span = ev.target.closest('span[data-k]');
        mark(span ? span.dataset.k : '');
    });
    $('preview').addEventListener('mouseleave', () => mark(''));

    $('preview').addEventListener('click', ev => {
        const span = ev.target.closest('span[data-k]');
        if (!span) return;
        const row = document.querySelector('.row[data-name="' + CSS.escape(span.dataset.k) + '"]');
        if (!row) return;
        row.scrollIntoView({ block: 'center', behavior: 'smooth' });
        row.classList.add('lit');
        setTimeout(() => row.classList.remove('lit'), 1200);
        row.querySelector('.hex').focus();
    });

    $('json').addEventListener('input', () => {
        try {
            const data = JSON.parse($('json').value);
            if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('it should be one { ... } object');
            colors = {};
            for (const [k, v] of Object.entries(data)) if (k !== 'name' && k !== 'extends' && k !== '$schema') colors[k] = v;
            $('jsonNote').textContent = 'Loaded.';
            $('jsonNote').classList.remove('bad');
            changed(null, true);
        } catch (e) {
            $('jsonNote').textContent = 'Not valid JSON yet: ' + e.message;
            $('jsonNote').classList.add('bad');
        }
    });

    $('theme').addEventListener('change', () => vscode.postMessage({ type: 'load', theme: $('theme').value }));
    $('live').addEventListener('change', () => {
        if ($('live').checked) vscode.postMessage({ type: 'preview', colors: ordered() });
        else vscode.postMessage({ type: 'stopPreview' });
    });
    $('copy').addEventListener('click', () => vscode.postMessage({ type: 'copy', json: JSON.stringify(ordered(), null, 4) }));
    $('save').addEventListener('click', () => vscode.postMessage({ type: 'save', colors: ordered() }));
    $('saveTheme').addEventListener('click', () => vscode.postMessage({ type: 'saveTheme', colors: ordered() }));

    window.addEventListener('message', ev => {
        const m = ev.data;
        if (m.type === 'setup') {
            names = m.names;
            descriptions = m.descriptions;
            lines = parseSample(m.sample);
            buildRows();
        } else if (m.type === 'colors') {
            colors = {};
            for (const [k, v] of Object.entries(m.colors || {})) colors[k] = v;
            setThemes(m.themes, m.theme);
            changed(null);
        } else if (m.type === 'themes') {
            setThemes(m.themes);
        }
    });

    vscode.postMessage({ type: 'ready' });
})();
</script>
</body>
</html>`;
}

module.exports = { open, sample, descriptions };
