# tpl.c - Template Renderer

`tpl.c` is a simple template renderer with includes, scoped variables, blocks, and basic control directives. It provides reusable template instances through `libtpl` and a one-shot `tpl` CLI.

---
## CLI

The CLI receives all render configuration in one invocation. The template
source can be passed directly or read from stdin when omitted.

### Examples

Render a direct template value:

```bash
tpl --var title=Home '<h1>{{ title }}</h1>'
```

Arguments may appear before or after the source:

```bash
tpl '<h1>{{ title }}</h1>' -var title=Home
```

Render from stdin:

```bash
echo '<h1>{{ title }}</h1>' | tpl --var title=Home
```

Render with includes:

```bash
tpl --root ./views --var page=Home < views/page.html
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `-root <dir>`, `--root <dir>` | Base directory for include resolution (default: `.`) |
| `-var <key=value>`, `--var <key=value>` | Render variable; repeatable |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Public API

A `kc_tpl_t` represents one reusable template source. The source and root are
fixed when the instance is opened; each render receives an isolated variable
set.

```c
#include "libtpl.h"

kc_tpl_t *page = NULL;
kc_tpl_options_t options = { .root = "./views" };
kc_tpl_var_t vars[] = {
    { "title", "Home" },
    { "user_name", "John" }
};
char *html;

if (kc_tpl_open(
        &page,
        "<h1>{{ title }}</h1><p>{{ user_name }}</p>",
        &options
    ) == KC_TPL_OK) {
    html = kc_tpl_render(page, vars, 2U);

if (html != NULL) {
        /* use html */
        kc_tpl_free(html);
    } else {
        /* inspect kc_tpl_error(page) */
    }

kc_tpl_close(page);
}
```

The public API is:

- `kc_tpl_open()` creates a reusable template and copies its source.
- `kc_tpl_render()` renders that stored source with one isolated variable array.
- `kc_tpl_error()` returns the latest error for the template instance.
- `kc_tpl_free()` releases rendered output.
- `kc_tpl_close()` closes the template instance.
- `kc_tpl_version()` returns the library build version.

`kc_tpl_open()` uses `"."` as the include root unless another non-empty root is configured.

Each render creates a fresh root scope, so values from one render do not persist into the next.

### Template Syntax

| Directive | Description |
| :--- | :--- |
| `{{ expr }}` | HTML-escaped output |
| `{{{ expr }}}` | Raw (unescaped) output |
| `{{ object.field }}` | Dot notation over flat `object_field` keys and foreach aliases |
| `{{/* comment */}}` | Template comment, stripped from output |
| `{{@include "path"}}` | Include a file relative to `--root` |
| `{{@var name expr}}` | Set a variable in the current scope |
| `{{@setblock name}} ... {{@endsetblock}}` | Define a named block |
| `{{@block name}}` | Render a named block |
| `{{@block name [ key: val ]}}` | Render a block with inline props |
| `{{@if expr}} ... {{@else}} ... {{@endif}}` | Conditional rendering with `!`, `==`, `!=`, `&&`, `||` |
| `{{@foreach item in list}} ... {{@endforeach}}` | Iterate over a comma-separated list or `[a,b,c]` |

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

- Artifact: `bin/wasm32/wasm/tpl.wasm`
- Exports: `kc_tpl_open`, `kc_tpl_render`, `kc_tpl_error`, `kc_tpl_free`, `kc_tpl_close`, `kc_tpl_version`
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
