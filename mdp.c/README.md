# mdp.c - Markdown Parser

`mdp.c` parses Markdown text, extracts optional YAML frontmatter, and renders the body as an HTML fragment through a small C library and stdin/stdout CLI.

---

---

## CLI

### Examples

Send a Markdown document through the CLI:

```bash
echo '# Hello' | mdp
```

Extract frontmatter only:

```bash
echo $'---\ntitle: Home\n---\n# Hello' | mdp --meta
```

Extract body without frontmatter:

```bash
echo $'---\ntitle: Home\n---\n# Hello' | mdp --body
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `--html` | Render Markdown body as HTML fragment (default) |
| `--body` | Output raw body without frontmatter |
| `--meta` | Output raw frontmatter block |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

---

## Public API

The reusable API models one parsed Markdown document. Frontmatter is split from
the body once when the document is opened, then the same instance exposes its
body, metadata, and cached HTML representation.

```c
#include "libmdp.h"

kc_mdp_t *mark = NULL;

if (kc_mdp_open(&mark, source) == KC_MDP_OK) {
    const char *body = kc_mdp_body(mark);
    const char *meta = kc_mdp_meta(mark);
    const char *html = kc_mdp_html(mark);

/* Use body, meta, and html while mark is alive. */

kc_mdp_close(mark);
}
```

The public lifecycle is:

- `kc_mdp_open()` creates one persistent document and performs the frontmatter/body split once.
- `kc_mdp_body()` returns the stored body view without reparsing.
- `kc_mdp_meta()` returns the stored raw frontmatter view without reparsing.
- `kc_mdp_html()` renders the stored body on first use and caches the HTML for later calls.
- `kc_mdp_close()` releases the document, including cached HTML.
- `kc_mdp_version()` returns the generated build version.

The input string is borrowed only for the duration of `kc_mdp_open()`.
Successful open owns its split body and metadata independently of the original
input buffer.

Pointers returned by `kc_mdp_body()`, `kc_mdp_meta()`, and
`kc_mdp_html()` belong to the document. Callers must not free them. They
remain valid until `kc_mdp_close()`. Empty body, metadata, or HTML results are
represented by valid empty strings.

---

### Frontmatter

An optional metadata block delimited by `---` at the top of the document is supported. The block content is raw text accessible via `--meta` and is excluded from body rendering.

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
| Inline code | `` `code` `` |
| Link | `[label](url)` |
| Image | `![alt](url)` |
| Linked image | `[![alt](img)](url)` |
| Unordered list | `- item` or `* item` |
| Blockquote | `> text` |
| Fenced code block | ` ``` ` ... ` ``` ` |
| Horizontal rule | `---` or `***` |
| Raw HTML | `<tag>` pass-through outside code blocks |

Raw HTML tags, comments, and block-level elements are passed through to the
output when they appear outside a fenced code block. Inside a fenced code block
all HTML remains escaped as literal text. Content between a block-level opening
tag (`<div>`, `<details>`, `<table>`, `<iframe>`, and similar) and its matching
closing tag is emitted without Markdown processing. Text that only resembles
HTML, such as `2 < 3` or `<3`, stays escaped.

---

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

- Artifact: `bin/wasm32/wasm/mdp.wasm`
- Exports: `kc_mdp_open`, `kc_mdp_close`, `kc_mdp_html`, `kc_mdp_body`, `kc_mdp_meta`, `kc_mdp_version`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

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
