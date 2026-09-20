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

kc_mdp_t *ctx = NULL;

if (kc_mdp_open(&ctx) == KC_MDP_OK) {
    kc_mdp_set_mode(ctx, KC_MDP_MODE_HTML);

    unsigned char *out = NULL;
    size_t out_len = 0;

    if (kc_mdp_exec(ctx, "# Hello", &out, &out_len) == KC_MDP_OK) {
        /* Use out and out_len. */
        kc_mdp_free(out);
    }
    kc_mdp_close(ctx);
}
```

New contexts default to `KC_MDP_MODE_HTML`; call `kc_mdp_set_mode()` to select
body or metadata output. `kc_mdp_exec()` allocates the output buffer. The
caller owns that returned buffer and must release it with `kc_mdp_free()`,
never raw `free()`.

---

## Lifecycle

- `kc_mdp_open()` - creates a context with `KC_MDP_MODE_HTML` selected by default.
- `kc_mdp_set_mode()` - optionally selects HTML, body, or metadata output for that context.
- `kc_mdp_mode()` - converts a CLI mode name or flag to an API mode constant.
- `kc_mdp_exec()` - parses a null-terminated Markdown string and returns a NUL-terminated output buffer allocated by `mdp.c`.
- `kc_mdp_free()` - releases an output buffer returned by `kc_mdp_exec()`; never use raw `free()` for that buffer.
- `kc_mdp_close()` - releases the context after all returned output has been freed.

The lifecycle is: open a context, optionally set its mode, execute, free every
returned output buffer with `kc_mdp_free()`, then close the context.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
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
- The module exports the public `kc_mdp_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `mdp` CLI: `src/mdp.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/mdp.wasm` and reports how to build it when it is absent.

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
