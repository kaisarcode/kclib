# mdp.c - Markdown Parser

`mdp.c` parses Markdown text, extracts optional YAML frontmatter, and renders the body as an HTML fragment through a small C library and stdin/stdout CLI.

---

## CLI

### Examples

Send a Markdown document through the CLI:

```bash
echo '# Hello' | ./bin/x86_64/linux/mdp
```

Extract frontmatter only:

```bash
echo $'---\ntitle: Home\n---\n# Hello' | ./bin/x86_64/linux/mdp --meta
```

Extract body without frontmatter:

```bash
echo $'---\ntitle: Home\n---\n# Hello' | ./bin/x86_64/linux/mdp --body
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

## Frontmatter

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

## Supported Markdown

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

## Public API

```c
#include "libmdp.h"

char *html = kc_mdp_html("# Hello");

if (html != NULL) {
    /* Use html. */
    kc_mdp_free(html);
}
```

The public API is stateless:

- `kc_mdp_html()` renders the Markdown body as an HTML fragment.
- `kc_mdp_body()` returns the body after recognized frontmatter.
- `kc_mdp_meta()` returns recognized raw frontmatter content.
- `kc_mdp_free()` releases any successful returned allocation and accepts `NULL`.
- `kc_mdp_version()` returns the generated build version.

All processing functions accept one null-terminated document. On success they
return an owned NUL-terminated string, including an allocated empty string for
an empty result. The caller must release successful results with
`kc_mdp_free()`, never raw `free()`. A `NULL` return means invalid input or
allocation/processing failure.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test`, `make test wine`, or `make test wasm`.

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable Markdown-processing library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/mdp.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the stateless public API: `kc_mdp_html`, `kc_mdp_body`, `kc_mdp_meta`, `kc_mdp_free`, and `kc_mdp_version`. It contains the reusable library capability, not the `mdp` CLI: `src/mdp.c` is not compiled into the module.

`make test wasm` compiles the reusable public-contract cases in `src/test.c` with Emscripten and runs them under Node.js. Native and Wine test runs additionally execute one grouped `kc_mdp_cli` case against the shipped CLI. It requires `bin/wasm32/wasm/mdp.wasm` and reports how to build it when it is absent.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
- Emscripten SDK for `wasm32/wasm` builds and `make test wasm`.
