# libmdp.c - Markdown Parser

`libmdp.c` parses Markdown text, extracts optional YAML frontmatter, and renders the same parsed Markdown semantics through HTML or ANSI terminal backends.

---
## Public API

The reusable API models one parsed Markdown document. Frontmatter is split from
the body once when the document is opened. HTML is the default presentation
backend, while ANSI provides terminal output from the same Markdown parser.

```c
#include "libmdp.h"

kc_mdp_t *mark = NULL;

if (kc_mdp_open(&mark, source) == KC_MDP_OK) {
    const char *body = kc_mdp_body(mark);
    const char *meta = kc_mdp_meta(mark);
    const char *html = kc_mdp_html(mark);
    const char *ansi = kc_mdp_ansi(mark);

    /* Use body, meta, html, and ansi while mark is alive. */

    kc_mdp_close(mark);
}
```

The public lifecycle is:

- `kc_mdp_open()` creates one persistent document and performs the frontmatter/body split once.
- `kc_mdp_body()` returns the stored body view without reparsing.
- `kc_mdp_meta()` returns the stored raw frontmatter view without reparsing.
- `kc_mdp_html()` renders through the HTML backend on first use and caches the result.
- `kc_mdp_ansi()` renders through the ANSI terminal backend on first use and caches the result.
- `kc_mdp_close()` releases the document and both cached renderings.
- `kc_mdp_version()` returns the library build version.

Empty body, metadata, HTML, or ANSI results are returned as empty strings.

Internally, Markdown syntax is interpreted once by the shared parser. Render
backends receive semantic events such as headings, emphasis, lists, tables,
quotes, links, and code blocks. This keeps HTML and ANSI support aligned without
maintaining two Markdown interpreters.

---

### Frontmatter

An optional metadata block delimited by `---` at the top of the document is supported. The block content is raw text and is excluded from body rendering.

```text
---
title: My Page
date: 2026-05-07
---

# My Page
```

Both LF and CRLF line endings are supported in the frontmatter delimiter.

---

### Supported Markdown

| Feature | Syntax |
| :--- | :--- |
| Headings | `# H1` through `###### H6` |
| Paragraph | Plain text lines |
| Bold | `**text**` |
| Italic | `*text*` |
| Strikethrough | `~~text~~` |
| Inline code | `` `code` `` |
| Link | `[label](url)` |
| Image | `![alt](url)` |
| Linked image | `[![alt](img)](url)` |
| Unordered list | `- item` or `* item` |
| Ordered list | `1. item` |
| Task list | `- [ ] item` or `- [x] item` |
| Blockquote | `> text` |
| Fenced code block | ```` ``` ... ``` ```` |
| Fenced code language | ```` ```c ... ``` ```` |
| Table | Pipe rows with a separator row such as `| --- | --- |` |
| Horizontal rule | `---` or `***` |
| Raw HTML | `<tag>` outside code blocks |

Both renderers consume the same recognized Markdown features. The HTML backend
emits structural HTML. The ANSI backend removes Markdown syntax and renders the
same semantics using terminal styling and text conventions: headings use ANSI
emphasis, inline and fenced code use dim styling, links use OSC 8 hyperlinks,
lists use terminal markers, task lists use checkbox glyphs, and tables use
aligned monospace borders.

Code spans may use longer backtick delimiters to include literal backticks in
their content. For example, `` `code` `` displays the inline-code Markdown
syntax itself, while ```` ```c ... ``` ```` displays fenced-code syntax.

Fenced code language identifiers are emitted as a `language-NAME` class by the
HTML backend. The ANSI backend consumes the language metadata but does not print
it as visible Markdown syntax.

Raw HTML is passed through by the HTML backend. The ANSI backend omits HTML tags
and emits visible raw-block text where possible.

---
## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/libmdp.wasm`
- Exports: `kc_mdp_open`, `kc_mdp_close`, `kc_mdp_html`, `kc_mdp_ansi`, `kc_mdp_body`, `kc_mdp_meta`, `kc_mdp_version`
- `wasm32/wasm` is included in `make all`.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains are required only by enabled targets.

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
