const vscode = require('vscode');
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const tokenTypes = ['namespace', 'class', 'struct', 'enum', 'type', 'typeParameter', 'concept', 'function', 'method', 'variable', 'parameter', 'property', 'enumMember', 'operator', 'unresolved'];
const tokenModifiers = ['declaration', 'overloaded', 'templateQualifier'];
const legend = new vscode.SemanticTokensLegend(tokenTypes, tokenModifiers);

const categories = {
    'declarations': { tm: ['meta.declaration.variable.name.walnut'], sem: ['variable.declaration', 'parameter.declaration', 'property.declaration'] },
    'variables': { tm: ['variable.other.readwrite.walnut'], sem: ['variable', 'parameter', 'property'] },
    'unresolved': { sem: ['unresolved'] },
    'self': { tm: ['variable.language.self.walnut'] },
    'cast expressions': { tm: ['keyword.operator.cast.walnut'] },
    'this': { tm: ['variable.language.this.walnut'] },
    'built-in operators': { tm: ['keyword.operator.walnut', 'keyword.operator.arrow.walnut', 'keyword.operator.word.walnut'] },
    'overloaded operators': { tm: ['entity.name.function.operator.walnut'], sem: ['operator.overloaded'] },
    'functions': { tm: ['entity.name.function.walnut', 'entity.name.function.call.walnut'], sem: ['function', 'method'] },
    'template scopes': { tm: ['entity.name.function.template.walnut'], sem: ['*.templateQualifier'] },
    'types': { tm: ['entity.name.type.walnut'], sem: ['class', 'struct', 'enum', 'type', 'typeParameter', 'concept'] },
    'built-in types': { tm: ['storage.type.primitive.walnut'] },
    'namespaces': { tm: ['entity.name.namespace.walnut'], sem: ['namespace'] },
    'enum members': { sem: ['enumMember'] },
    'keywords': { tm: ['storage.type.walnut', 'storage.type.function.walnut', 'storage.type.namespace.walnut', 'storage.type.operator.walnut', 'storage.modifier.walnut'] },
    'control keywords': { tm: ['keyword.control.walnut', 'keyword.control.end.walnut'] },
    'constants': { tm: ['constant.language.walnut'] },
    'strings': { tm: ['string.quoted.double.walnut', 'string.quoted.single.walnut', 'string.interpolated.walnut'] },
    'escapes': { tm: ['constant.character.escape.walnut'] },
    'numbers': { tm: ['constant.numeric.decimal.walnut', 'constant.numeric.hex.walnut', 'constant.numeric.octal.walnut', 'constant.numeric.binary.walnut'] },
    'comments': { tm: ['comment.line.double-slash.walnut', 'comment.block.walnut'] },
    'preprocessor': { tm: ['meta.preprocessor.walnut', 'keyword.control.directive.walnut'] }
};

const squiggleColors = {
    'error underlines': 'editorError.foreground',
    'warning underlines': 'editorWarning.foreground'
};

const editorStyles = ['error lines'];

const keywordWords = new Set([
    'self', 'this',
    'fallthrough', 'co_return', 'co_yield', 'continue', 'default', 'repeat', 'return', 'switch', 'await', 'break', 'catch', 'label', 'throw', 'while', 'case', 'down', 'else', 'goto', 'then', 'end', 'for', 'try', 'as', 'do', 'if', 'in', 'is', 'invalidate',
    'constructor', 'destructor', 'namespace', 'function', 'inherits', 'operator', 'requires', 'template', 'typename', 'concept', 'extends', 'include', 'inherit', 'typedef', 'define', 'export', 'import', 'module', 'struct', 'class', 'union', 'using', 'enum', 'def', 'let',
    'thread_local', 'threadlocal', 'no_discard', 'threadsafe', 'automatic', 'consteval', 'constexpr', 'constinit', 'nodiscard', 'protected', 'abstract', 'constant', 'explicit', 'implicit', 'noexcept', 'override', 'volatile', 'hoisted', 'mutable', 'private', 'virtual', 'extern', 'friend', 'global', 'hidden', 'inline', 'public', 'static', 'async', 'const', 'hoist', 'local', 'lazy',
    'character', 'unsigned', 'variable', 'boolean', 'decimal', 'dynamic', 'floater', 'integer', 'special', 'variant', 'double', 'string', 'array', 'float', 'short', 'auto', 'bool', 'char', 'long', 'text', 'void', 'any', 'arr', 'int', 'str', 'txt', 'var',
    'indeterminate', 'undefined', 'infinity', 'nullptr', 'FALSE', 'False', 'false', 'TRUE', 'True', 'true', 'inf', 'nan',
    'reinterpret_cast', 'dynamic_cast', 'static_cast', 'const_cast', 'bit_cast', 'bitcast',
    'discard_no_discard', 'discard_nodiscard', 'discard_const', 'static_assert', 'align_as', 'align_of', 'count_of', 'decltype', 'alignas', 'alignof', 'countof', 'size_of', 'type_id', 'type_of', 'delete', 'sizeof', 'typeid', 'typeof', 'new'
]);

const wholeLineWords = new Set(['import', 'include']);
const labelWords = new Set(['goto', 'label']);

const rulePrefix = 'Walnut: ';
const semanticSuffix = ':walnut';
const squiggleStateKey = 'walnut.squiggleColors';

let extensionContext = null;
let errorLineDecoration = null;
const errorLines = new Map();
let extensionPath = '';
let output = null;
let diagnostics = null;
let warnedMissingCompiler = false;
let lastColorsError = '';
let watchedColorsFile = '';
let applyTimer = null;
const lastResults = new Map();
const running = new Map();
const tokensChanged = new vscode.EventEmitter();

function settings() {
    return vscode.workspace.getConfiguration('walnut');
}

function log(text) {
    if (output) output.appendLine(text);
}

function findOnPath(name) {
    if (path.isAbsolute(name) || name.includes('/') || name.includes('\\')) return fs.existsSync(name) ? path.resolve(name) : '';
    const exts = process.platform === 'win32' ? ['', '.exe', '.cmd', '.bat'] : [''];

    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
        if (!dir) continue;

        for (const ext of exts) {
            const candidate = path.join(dir, name + ext);

            try {
                if (fs.statSync(candidate).isFile()) return candidate;
            } catch (e) {
            }
        }
    }

    return '';
}

function compilerPath() {
    const configured = (settings().get('compilerPath') || '').trim();
    return configured || 'walnut';
}

function repoColorsFile() {
    const compiler = findOnPath(compilerPath());
    if (!compiler) return '';
    let dir;

    try {
        dir = path.dirname(fs.realpathSync(compiler));
    } catch (e) {
        return '';
    }

    for (let i = 0; i < 6; ++i) {
        const candidate = path.join(dir, 'Tools', 'vscode-walnut', 'colors.json');
        if (fs.existsSync(candidate)) return candidate;
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }

    return '';
}

function colorsFile() {
    const configured = (settings().get('colorsFile') || '').trim();
    if (configured) return configured;
    return repoColorsFile() || path.join(extensionPath, 'colors.json');
}

function isBundledColorsFile(file) {
    return path.resolve(file) === path.resolve(path.join(extensionPath, 'colors.json'));
}

function styleOf(value) {
    if (typeof value === 'string') return value.trim() ? { foreground: value.trim() } : null;
    if (!value || typeof value !== 'object') return null;
    const style = {};
    if (typeof value.color === 'string' && value.color.trim()) style.foreground = value.color.trim();
    const font = [];
    if (value.bold) font.push('bold');
    if (value.italic) font.push('italic');
    if (value.underline) font.push('underline');
    if (font.length || value.bold === false || value.italic === false || value.underline === false) style.fontStyle = font.join(' ');
    return Object.keys(style).length ? style : null;
}

function semanticStyleOf(style) {
    const out = {};
    if (style.foreground) out.foreground = style.foreground;
    if (style.fontStyle !== undefined) {
        out.bold = style.fontStyle.includes('bold');
        out.italic = style.fontStyle.includes('italic');
        out.underline = style.fontStyle.includes('underline');
    }
    return out;
}

function readColors(file) {
    const text = fs.readFileSync(file, 'utf8').replace(/^﻿/, '');
    const data = JSON.parse(text);
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('the file should hold one { ... } object of "name": "#RRGGBB" pairs');
    return data;
}

function sameJson(a, b) {
    return JSON.stringify(a) === JSON.stringify(b);
}

async function applyColors(showErrors) {
    const config = vscode.workspace.getConfiguration('editor');
    const enabled = settings().get('colors.enabled', true);
    let colors = {};

    if (enabled) {
        const file = colorsFile();

        try {
            colors = readColors(file);
            lastColorsError = '';
        } catch (e) {
            const message = `Walnut couldn't read the colors file ${file}: ${e.message}`;
            log(message);
            if (showErrors || message !== lastColorsError) vscode.window.showWarningMessage(message);
            lastColorsError = message;
            return;
        }

        const known = Object.keys(categories).concat(Object.keys(squiggleColors), editorStyles);

        for (const name of Object.keys(colors)) {
            if (!known.includes(name)) log(`Walnut colors: unknown name "${name}" in ${file}; the names are: ${known.join(', ')}`);
        }
    }

    setErrorLineStyle(colors['error lines']);

    const textMateRules = [];
    const semanticRules = {};

    for (const [name, category] of Object.entries(categories)) {
        const style = styleOf(colors[name]);
        if (!style) continue;
        if (category.tm) textMateRules.push({ name: rulePrefix + name, scope: category.tm, settings: style });
        if (category.sem) for (const selector of category.sem) semanticRules[selector + semanticSuffix] = semanticStyleOf(style);
    }

    const tokenColors = Object.assign({}, config.inspect('tokenColorCustomizations').globalValue || {});
    const keptRules = (tokenColors.textMateRules || []).filter(r => !(r && typeof r.name === 'string' && r.name.startsWith(rulePrefix)));
    const newTokenColors = Object.assign({}, tokenColors, { textMateRules: keptRules.concat(textMateRules) });
    if (!newTokenColors.textMateRules.length) delete newTokenColors.textMateRules;

    const semanticColors = Object.assign({}, config.inspect('semanticTokenColorCustomizations').globalValue || {});
    const keptSemantic = {};
    for (const [key, value] of Object.entries(semanticColors.rules || {})) if (!key.endsWith(semanticSuffix)) keptSemantic[key] = value;
    const newSemanticColors = Object.assign({}, semanticColors, { rules: Object.assign(keptSemantic, semanticRules) });
    if (!Object.keys(newSemanticColors.rules).length) delete newSemanticColors.rules;

    try {
        if (!sameJson(newTokenColors, tokenColors)) await config.update('tokenColorCustomizations', Object.keys(newTokenColors).length ? newTokenColors : undefined, vscode.ConfigurationTarget.Global);
        if (!sameJson(newSemanticColors, semanticColors)) await config.update('semanticTokenColorCustomizations', Object.keys(newSemanticColors).length ? newSemanticColors : undefined, vscode.ConfigurationTarget.Global);
        await applySquiggleColors(colors);
    } catch (e) {
        log(`Walnut couldn't save the colors to your settings: ${e.message}`);
    }
}

function colorOf(value) {
    if (typeof value === 'string') return value.trim();
    if (value && typeof value === 'object' && typeof value.color === 'string') return value.color.trim();
    return '';
}

async function applySquiggleColors(colors) {
    const config = vscode.workspace.getConfiguration('workbench');
    const current = Object.assign({}, config.inspect('colorCustomizations').globalValue || {});
    const ours = Object.assign({}, extensionContext.globalState.get(squiggleStateKey, {}));
    const updated = Object.assign({}, current);

    for (const [name, key] of Object.entries(squiggleColors)) {
        const wanted = colorOf(colors[name]);

        if (wanted) {
            updated[key] = wanted;
            ours[key] = wanted;
        } else if (ours[key] !== undefined) {
            if (updated[key] === ours[key]) delete updated[key];
            delete ours[key];
        }
    }

    await extensionContext.globalState.update(squiggleStateKey, ours);
    if (!sameJson(updated, current)) await config.update('colorCustomizations', Object.keys(updated).length ? updated : undefined, vscode.ConfigurationTarget.Global);
}

function setErrorLineStyle(value) {
    if (errorLineDecoration) errorLineDecoration.dispose();
    errorLineDecoration = null;
    const options = {};

    if (typeof value === 'string') {
        if (value.trim()) options.color = value.trim();
    } else if (value && typeof value === 'object') {
        if (typeof value.color === 'string' && value.color.trim()) options.color = value.color.trim();
        if (typeof value.background === 'string' && value.background.trim()) options.backgroundColor = value.background.trim();
        if (value.bold) options.fontWeight = 'bold';
        if (value.italic) options.fontStyle = 'italic';
        if (value.underline) options.textDecoration = 'underline';
    }

    if (Object.keys(options).length) {
        options.isWholeLine = true;
        options.rangeBehavior = vscode.DecorationRangeBehavior.ClosedClosed;
        errorLineDecoration = vscode.window.createTextEditorDecorationType(options);
    }

    showErrorLines();
}

function showErrorLines() {
    if (!errorLineDecoration) return;

    for (const editor of vscode.window.visibleTextEditors) {
        const lines = errorLines.get(editor.document.uri.toString()) || [];
        const ranges = [];

        for (const line of lines) {
            if (line >= editor.document.lineCount) continue;
            ranges.push(editor.document.lineAt(line).range);
        }

        editor.setDecorations(errorLineDecoration, ranges);
    }
}

function setErrorLines(document, lines) {
    const key = document.uri.toString();
    if (lines.length) errorLines.set(key, lines); else errorLines.delete(key);
    showErrorLines();
}

function scheduleApply() {
    if (applyTimer) clearTimeout(applyTimer);
    applyTimer = setTimeout(() => { applyTimer = null; applyColors(false); }, 150);
}

function watchColors() {
    const file = settings().get('colors.enabled', true) ? colorsFile() : '';
    if (file === watchedColorsFile) return;
    if (watchedColorsFile) fs.unwatchFile(watchedColorsFile);
    watchedColorsFile = file;
    if (file) fs.watchFile(file, { interval: 1000 }, scheduleApply);
}

async function removeColors() {
    await vscode.workspace.getConfiguration('walnut').update('colors.enabled', false, vscode.ConfigurationTarget.Global);
    await applyColors(false);
    watchColors();
    vscode.window.showInformationMessage('Walnut colors were removed from your settings. Run "Walnut: Edit Colors" to turn them back on.');
}

async function chooseColorsFile() {
    const picked = await vscode.window.showOpenDialog({ canSelectMany: false, openLabel: 'Use these colors', filters: { 'Walnut colors': ['json'] } });
    if (!picked || !picked.length) return;
    await settings().update('colorsFile', picked[0].fsPath, vscode.ConfigurationTarget.Global);
}

async function editColors() {
    if (!settings().get('colors.enabled', true)) await settings().update('colors.enabled', true, vscode.ConfigurationTarget.Global);
    const file = colorsFile();

    if (isBundledColorsFile(file)) {
        const choice = await vscode.window.showInformationMessage(
            'These are the colors that came with the extension, and changes to them are lost when it is reinstalled. To keep your own colors, pick the colors.json in your Walnut folder (Walnut/Tools/vscode-walnut/colors.json).',
            'Choose File...', 'Edit Anyway');
        if (choice === 'Choose File...') { await chooseColorsFile(); return editColors(); }
        if (choice !== 'Edit Anyway') return;
    }

    const doc = await vscode.workspace.openTextDocument(file);
    await vscode.window.showTextDocument(doc);
}

async function chooseCompiler() {
    const picked = await vscode.window.showOpenDialog({ canSelectMany: false, openLabel: 'Use this compiler' });
    if (!picked || !picked.length) return;
    await settings().update('compilerPath', picked[0].fsPath, vscode.ConfigurationTarget.Global);
    warnedMissingCompiler = false;
    tokensChanged.fire();
}

function documentPath(document) {
    if (document.uri.scheme === 'file') return document.uri.fsPath;
    const base = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length ? vscode.workspace.workspaceFolders[0].uri.fsPath : os.tmpdir();
    return path.join(base, 'untitled.wal');
}

function runCompiler(document, cancel) {
    return new Promise((resolve, reject) => {
        const file = documentPath(document);
        const args = ['highlight', file, '--stdin'].concat(settings().get('extraArguments') || []);
        let child;

        try {
            child = childProcess.spawn(compilerPath(), args, { cwd: path.dirname(file), windowsHide: true });
        } catch (e) {
            reject(e);
            return;
        }

        const key = document.uri.toString();
        const previous = running.get(key);
        if (previous) previous.kill();
        running.set(key, child);

        let stdout = '';
        let stderr = '';
        let done = false;
        const finish = (fn) => { if (done) return; done = true; if (running.get(key) === child) running.delete(key); fn(); };
        const stop = cancel.onCancellationRequested(() => { child.kill(); finish(() => reject(new vscode.CancellationError())); });

        child.stdout.setEncoding('utf8');
        child.stderr.setEncoding('utf8');
        child.stdout.on('data', d => { stdout += d; });
        child.stderr.on('data', d => { stderr += d; });
        child.on('error', e => { stop.dispose(); finish(() => reject(e)); });
        child.on('close', code => {
            stop.dispose();
            finish(() => {
                const start = stdout.indexOf('{');
                if (start < 0) { reject(new Error(`the compiler printed no result (exit code ${code})${stderr ? ': ' + stderr.trim() : ''}`)); return; }

                try {
                    resolve(JSON.parse(stdout.slice(start)));
                } catch (e) {
                    reject(new Error(`the compiler's result couldn't be read: ${e.message}`));
                }
            });
        });

        child.stdin.on('error', () => {});
        child.stdin.end(document.getText(), 'utf8');
    });
}

function cleanMessage(message) {
    return message.replace(/ on line \d+:/, ':');
}

function publishDiagnostics(document, result) {
    if (!settings().get('diagnostics', true)) { diagnostics.delete(document.uri); setErrorLines(document, []); return; }
    const list = [];
    const lines = new Set();

    for (const d of result.diagnostics || []) {
        const line = Math.min(Math.max(d.line || 0, 0), document.lineCount - 1);
        const text = document.lineAt(line);
        let range;

        if (d.len > 0) {
            range = new vscode.Range(line, d.col, line, Math.min(d.col + d.len, text.range.end.character));
        } else {
            range = new vscode.Range(line, text.firstNonWhitespaceCharacterIndex, line, text.range.end.character);
        }

        const severity = d.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
        const diagnostic = new vscode.Diagnostic(range, cleanMessage(d.message || ''), severity);
        diagnostic.source = 'walnut';
        list.push(diagnostic);
        if (severity === vscode.DiagnosticSeverity.Error) lines.add(line);
    }

    diagnostics.set(document.uri, list);
    setErrorLines(document, Array.from(lines));
}

const hoverResults = new Map();
const maxHoverText = 400;

function storeHovers(document, result) {
    const key = document.uri.toString();
    if (!Array.isArray(result.hovers)) return;
    if (!result.semantic && hoverResults.has(key)) return;
    const byLine = new Map();

    for (const h of result.hovers) {
        if (!h || typeof h.line !== 'number' || h.line >= document.lineCount || !(h.len > 0)) continue;
        const range = new vscode.Range(h.line, h.col, h.line, h.col + h.len);
        const entry = { range, text: document.getText(range), hover: h };
        if (!byLine.has(h.line)) byLine.set(h.line, []);
        byLine.get(h.line).push(entry);
    }

    hoverResults.set(key, byLine);
}

function shorten(text) {
    return text.length > maxHoverText ? text.slice(0, maxHoverText) + '…' : text;
}

function inlineCode(text) {
    text = shorten(text);
    const runs = text.match(/`+/g) || [];
    const fence = '`'.repeat(runs.reduce((most, r) => Math.max(most, r.length), 0) + 1);
    const pad = text.startsWith('`') || text.endsWith('`') ? ' ' : '';
    return fence + pad + text + pad + fence;
}

function hoverMarkdown(h) {
    const md = new vscode.MarkdownString();
    if (h.code) md.appendCodeblock(shorten(h.code), 'walnut');
    const lines = [];

    const add = (one, many, items) => {
        if (!Array.isArray(items) || !items.length) return;
        lines.push(`**${items.length > 1 ? many : one}:** ` + items.map(inlineCode).join(', '));
    };

    add('Type', 'Types', h.types);
    add('Value', 'Values', h.values);
    add('Result', 'Results', h.results);
    if (lines.length) md.appendMarkdown(lines.join('  \n'));
    return md;
}

const hoverProvider = {
    provideHover(document, position) {
        if (!settings().get('hover', true)) return null;
        const byLine = hoverResults.get(document.uri.toString());
        const entries = byLine && byLine.get(position.line);
        if (!entries) return null;

        for (const e of entries) {
            if (position.character < e.range.start.character || position.character >= e.range.end.character) continue;
            if (document.getText(e.range) !== e.text) continue;
            return new vscode.Hover(hoverMarkdown(e.hover), e.range);
        }

        return null;
    }
};

function scanNames(text) {
    const names = [];
    const n = text.length;
    let i = 0;
    let line = 0;
    let lineStart = 0;
    let lineHasCode = false;
    let previous = '';
    let beforePrevious = '';
    const newline = () => { ++line; lineStart = i + 1; lineHasCode = false; previous = ''; beforePrevious = ''; };
    const isStart = ch => (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch === '_';
    const isDigit = ch => ch >= '0' && ch <= '9';
    const isPart = ch => isStart(ch) || isDigit(ch);
    const toLineEnd = () => { while (i < n && text[i] !== '\n') ++i; };

    while (i < n) {
        const c = text[i];

        if (c === '\n') { newline(); ++i; continue; }

        if (c === '/' && text[i + 1] === '/') { toLineEnd(); continue; }

        if (c === '/' && text[i + 1] === '*') {
            let depth = 0;

            while (i < n) {
                if (text[i] === '/' && text[i + 1] === '*') { ++depth; i += 2; continue; }
                if (text[i] === '*' && text[i + 1] === '/') { --depth; i += 2; if (depth === 0) break; continue; }
                if (text[i] === '\n') newline();
                ++i;
            }

            continue;
        }

        if (c === '"' || c === '\'' || c === '`') {
            ++i;

            while (i < n) {
                const d = text[i];
                if (d === c) { ++i; break; }
                if (d === '\\') { i += 2; continue; }
                if (d === '\n') { if (c !== '`') break; newline(); }
                ++i;
            }

            lineHasCode = true;
            previous = '';
            beforePrevious = '';
            continue;
        }

        if (c === '#' && !lineHasCode) { toLineEnd(); continue; }

        if (isDigit(c)) {
            while (i < n && (isPart(text[i]) || text[i] === '.' || text[i] === '\'')) ++i;
            lineHasCode = true;
            previous = '';
            beforePrevious = '';
            continue;
        }

        if (isStart(c)) {
            const start = i;
            while (i < n && isPart(text[i])) ++i;
            const word = text.slice(start, i);

            if (wholeLineWords.has(word)) { toLineEnd(); continue; }

            const named = beforePrevious === 'end' || previous === 'end' || labelWords.has(previous);
            if (!keywordWords.has(word) && !named) names.push([line, start - lineStart, word.length]);

            beforePrevious = previous;
            previous = word;
            lineHasCode = true;
            continue;
        }

        if (c !== ' ' && c !== '\t' && c !== '\r') {
            lineHasCode = true;
            previous = '';
            beforePrevious = '';
        }

        ++i;
    }

    return names;
}

function addUnresolved(document, result) {
    if (!result.semantic || !settings().get('markUnresolved', true)) return;
    const tokens = result.tokens || [];
    if (tokens.some(t => t[3] === 'unresolved')) return;
    const covered = new Map();

    for (const [line, col, len] of tokens) {
        if (!covered.has(line)) covered.set(line, []);
        covered.get(line).push([col, col + len]);
    }

    for (const [line, col, len] of scanNames(document.getText())) {
        const spans = covered.get(line) || [];
        if (spans.some(([from, to]) => col < to && col + len > from)) continue;
        tokens.push([line, col, len, 'unresolved', []]);
    }

    tokens.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    result.tokens = tokens;
}

function buildTokens(result) {
    const builder = new vscode.SemanticTokensBuilder(legend);

    for (const entry of result.tokens || []) {
        const [line, col, len, type, mods] = entry;
        const typeIndex = tokenTypes.indexOf(type);
        if (typeIndex < 0 || len <= 0) continue;
        let bits = 0;
        for (const m of mods || []) { const i = tokenModifiers.indexOf(m); if (i >= 0) bits |= 1 << i; }
        builder.push(line, col, len, typeIndex, bits);
    }

    return builder.build();
}

function delay(ms, cancel) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { stop.dispose(); resolve(); }, ms);
        const stop = cancel.onCancellationRequested(() => { clearTimeout(timer); stop.dispose(); reject(new vscode.CancellationError()); });
    });
}

const provider = {
    onDidChangeSemanticTokens: tokensChanged.event,

    async provideDocumentSemanticTokens(document, cancel) {
        const key = document.uri.toString();
        if (!settings().get('semanticHighlighting', true)) { diagnostics.delete(document.uri); setErrorLines(document, []); return new vscode.SemanticTokensBuilder(legend).build(); }
        await delay(Math.max(0, settings().get('highlightDelay', 300)), cancel);
        const version = document.version;
        let result;

        try {
            result = await runCompiler(document, cancel);
        } catch (e) {
            if (e instanceof vscode.CancellationError) throw e;

            if (e && e.code === 'ENOENT') {
                if (!warnedMissingCompiler) {
                    warnedMissingCompiler = true;
                    vscode.window.showWarningMessage(
                        `Walnut can't find the compiler "${compilerPath()}", so it can only do basic coloring. Choose your walnut program (in Walnut's build folder) to turn on full coloring and error underlines.`,
                        'Choose Compiler...').then(choice => { if (choice) chooseCompiler(); });
                }
            } else {
                log(`Walnut highlighting failed: ${e && e.message ? e.message : e}`);
            }

            return new vscode.SemanticTokensBuilder(legend).build();
        }

        if (cancel.isCancellationRequested || document.version !== version) throw new vscode.CancellationError();
        publishDiagnostics(document, result);
        storeHovers(document, result);

        if (!result.semantic && lastResults.has(key)) throw new vscode.CancellationError();
        addUnresolved(document, result);
        const tokens = buildTokens(result);
        lastResults.set(key, true);
        return tokens;
    }
};

function activate(context) {
    extensionContext = context;
    extensionPath = context.extensionPath;
    output = vscode.window.createOutputChannel('Walnut');
    diagnostics = vscode.languages.createDiagnosticCollection('walnut');

    context.subscriptions.push(
        output,
        diagnostics,
        tokensChanged,
        vscode.languages.registerDocumentSemanticTokensProvider({ language: 'walnut' }, provider, legend),
        vscode.languages.registerHoverProvider({ language: 'walnut' }, hoverProvider),
        vscode.workspace.onDidCloseTextDocument(doc => { diagnostics.delete(doc.uri); lastResults.delete(doc.uri.toString()); errorLines.delete(doc.uri.toString()); hoverResults.delete(doc.uri.toString()); }),
        vscode.window.onDidChangeVisibleTextEditors(showErrorLines),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (watchedColorsFile && doc.uri.scheme === 'file' && path.resolve(doc.uri.fsPath) === path.resolve(watchedColorsFile)) applyColors(true);
        }),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('walnut.colorsFile') || e.affectsConfiguration('walnut.colors.enabled') || e.affectsConfiguration('walnut.compilerPath')) { watchColors(); applyColors(true); }
            if (e.affectsConfiguration('walnut')) tokensChanged.fire();
        }),
        vscode.commands.registerCommand('walnut.editColors', editColors),
        vscode.commands.registerCommand('walnut.chooseColorsFile', chooseColorsFile),
        vscode.commands.registerCommand('walnut.removeColors', removeColors),
        vscode.commands.registerCommand('walnut.chooseCompiler', chooseCompiler),
        vscode.commands.registerCommand('walnut.refresh', () => { warnedMissingCompiler = false; tokensChanged.fire(); }),
        { dispose: () => { if (watchedColorsFile) fs.unwatchFile(watchedColorsFile); for (const child of running.values()) child.kill(); if (errorLineDecoration) errorLineDecoration.dispose(); } }
    );

    watchColors();
    applyColors(false);
}

function deactivate() {
}

module.exports = { activate, deactivate };
