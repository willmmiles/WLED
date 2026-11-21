# `cdata.json` usermod manifest reference

A usermod's `cdata.json` tells `tools/cdata.js` what web-UI assets to build for it.
The machine-readable contract is [`cdata.schema.json`](cdata.schema.json); this is the
human summary. A manifest must define at least one of `header` or `ui`. All file paths
are resolved relative to the manifest and may not escape the usermod folder.

Every key set below is closed: a key that is not listed fails the build, naming it — at
the top level, inside a `header` or `ui` entry, and inside their `specs`/`sources`.

## Top level

| Key | Type | Default | Meaning |
|---|---|---|---|
| `header` | array | — | Writes one `.h` into the usermod folder. Non-empty if present. |
| `ui` | array | — | Contributions folded into the core WLED UI bundle. Non-empty if present; ≤1 entry per `target`, so one usermod's load order stays visible in one list. |
| `schemaVersion` | int | `1` | Manifest version. Only `1` (or absent) is supported. |
| `$schema` | string | — | Optional editor hint, ignored by the build. |

## `header[]` — private generated headers

Compiles the usermod's own pages into a PROGMEM header for serving.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `output` | string | *required* | Generated header filename, written into the usermod folder (e.g. `example_ui.h`). Must be unique across header ops. |
| `srcDir` | string | `data` | Source folder, relative to the manifest. |
| `specs` | array | *required* | One entry per embedded file/symbol. Non-empty. |

### `header[].specs[]`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `file` | string | *required* | Source filename under `srcDir`. |
| `name` | string | *required* | C symbol to generate — a valid C identifier. Yields `const char <name>[] PROGMEM` plus `<name>_length`. |
| `method` | enum | `gzip` | Encoding: `gzip` (compressed), `plaintext` (verbatim text), `binary` (raw byte array). |
| `filter` | enum | by extension | Pre-processing: `plain` (none), `css-minify`, `js-minify`, `html-minify`. Defaults from the extension (`.htm`/`.html`→`html-minify`, `.css`→`css-minify`, `.js`→`js-minify`, else `plain`); set explicitly to override (e.g. `plain` for already-minified JS). |
| `prepend` | string | `=====(` | Raw-string delimiter prefix; only meaningful with `plaintext`. |
| `append` | string | `)=====` | Raw-string delimiter suffix; only meaningful with `plaintext`. |

So the common case — serve one page, compressed and minified — needs only `file` and `name`.
`prepend`/`append` must be supplied as a pair or not at all.

## `ui[]` — contributions to a core UI bundle

Splices `.css`/`.htm`/`.js` into a WLED page **before** minification, so the assets ship
in that page's single gzipped payload — no extra endpoint, no JS in C++ literals.
CSS is inlined into `<head>`; markup and scripts into `<body>` (markup first, then
scripts). A script registers itself by calling `WLED.module("<id>", function(WLED) { … })`
in its own source; that call hands it the namespaced UI facade defined in
`wled00/data/um.js` in the WLED repo (bundled only when something contributes; the
module author's reference is the runtime API link at the end of this section).
**Markup is appended exactly as written** — the build adds no wrapper and rewrites
nothing, so a `<template>`'s id is whatever your file says it is.

Nothing is sorted: usermods land in the order their manifests reach the build, and a
usermod's own files in `sources` order. Note that `sources` order is honoured *within
each type*, not across types — the three types are spliced at different points in the
page, so a manifest listing `.js` before `.htm` still gets its markup first.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `target` | enum | *required* | Bundle to extend. Only `index` (the main UI page) is accepted today. |
| `sources` | array | *required* | Files to contribute, in load order. Non-empty. |
| `api` | int | `1` | UI extension API version the code targets. Only `1` is supported. |
| `srcDir` | string | `data` | Source folder, relative to the manifest. |

### `ui[].sources[]`

Either a bare filename string, or an object:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `file` | string | *required* | Source filename under `srcDir`. |
| `type` | enum | inferred from extension | How the file is spliced: `css`, `html`, or `script`. Required when the extension isn't `.css`/`.htm`/`.html`/`.js`. |

Extension → type: `.css`→`css`, `.htm`/`.html`→`html`, `.js`→`script`.

| Type | Where it goes | What the build adds |
|---|---|---|
| `css` | before `</head>` | wrapped in `<style>…</style>` |
| `html` | before `</body>` | **nothing** — appended verbatim |
| `script` | before `</body>`, after the markup | wrapped in `<script>…</script>` |

## Writing a module

The build supplies the `<script>` tag and nothing else. A contributed script is an
ordinary script that registers itself:

```js
// ui/example.js
WLED.module("example", function(WLED) {
	// `WLED` here is this module's own view of the API -- slots, templates, events.
	WLED.slot('info').appendChild(WLED.row('Hello', 'world'));
});
```

The id you pass is what namespaces this module's slots, and what `WLED.tpl()` prepends
when looking a template up (`um-example-panel` for `WLED.tpl('panel')`). It lives in
this one place; the manifest does not know or care about it.

Nothing checks that a contributed script registers at all. One that never calls
`WLED.module()` still parses, ships, and runs against the bare global `WLED` — which
carries only `module` — so it dies on its first `WLED.slot(...)`. The failure is confined
to that one `<script>`; everything else on the page carries on, so the symptom is one
usermod quietly missing.

Two scripts registering under the same id each get their own facade but share that id's
slots and templates, which is how a module can span several files. Two different usermods
sharing an id share them the same way — so pick one unlikely to collide, and note this is
visible in the two source files rather than something the build arbitrates.

Note that `WLED` inside the factory is a *parameter*, shadowing the global of the same
name. Omit it from the signature and you get the global instead, which does not carry
`slot`/`tpl`/`on`.

## Writing markup

An `html` source is copied into the page as-is, so one file can hold as many
`<template>` elements as you like — and anything else you want in the page:

```html
<!-- ui/templates.htm -->
<template id="um-example-panel">
  <p class="labels hd">Example usermod</p>
  <button class="btn btn-s ex-refresh">Refresh</button>
</template>

<template id="um-example-segment">
  <div class="ex-seg">Example: <span class="ex-len">?</span> LEDs</div>
</template>
```

`WLED.tpl('panel')` in your script looks up the element id `um-<id>-panel`, where `<id>`
is the one you passed to `WLED.module()` — so the two ends of the wiring are both things
you can grep for. That prefix is not etiquette —
it is what `tpl()` searches for, and a template without it cannot be found. It also keeps
you clear of the core's ids and other usermods', which share one global namespace. The
element found must be a `<template>`; `tpl()` clones its content, warns to the browser
console and returns an empty fragment if the name does not resolve.

The runtime side of this — `tpl()`, slots, events, helpers — is documented at
[kno.wled.ge/advanced/custom-features](https://kno.wled.ge/advanced/custom-features/#extending-the-main-web-ui).

Because nothing is rewritten, nothing is checked here either. Close your tags: an unclosed
element is not an error, it swallows what follows. The minifier closes it at the end of the
document, and since the module's `<script>` is appended *after* the markup, an unclosed
`<template>` captures that script and it silently never runs.

A `css` or `script` source *is* wrapped in a tag this build supplies, so those two may not
contain `</style>` / `</script>`; `html` has no such limit.
