# tpl.c - Template Renderer

`tpl.c` is a simple template renderer with includes, scoped variables, blocks, and basic control directives. It provides reusable template instances through `libtpl` and a one-shot `tpl` CLI.

---

## CLI

The CLI receives all render configuration in one invocation. The template
source can be passed directly or read from stdin when omitted.

### Examples

Render a direct template value:

```bash
./bin/x86_64/linux/tpl --var title=Home '<h1>{{ title }}</h1>'
```

Arguments may appear before or after the source:

```bash
./bin/x86_64/linux/tpl '<h1>{{ title }}</h1>' -var title=Home
```

Render from stdin:

```bash
echo '<h1>{{ title }}</h1>' | ./bin/x86_64/linux/tpl --var title=Home
```

Render with includes:

```bash
./bin/x86_64/linux/tpl --root ./views --var page=Home < views/page.html
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `-root <dir>`, `--root <dir>` | Base directory for include resolution (default: `.`) |
| `-var <key=value>`, `--var <key=value>` | Render variable; repeatable |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

## Template Syntax

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

Variables are string-based. Lists are passed as CSV or `[a,b,c]`. Truthy values are non-empty strings except `0`, `false`, and `null`. Conditions may use `!`, `==`, `!=`, `&&`, and `||`; operands are evaluated as strings, and a single operand follows the truthiness rule. A block body only sees the variables passed as its inline props; parent and context variables are not visible inside it. The block props syntax is similar to JSON but is not JSON: each `value` is a single scalar expression (quoted string, `true`/`false`/`null`, or a variable/dot lookup). Nested arrays or objects cannot be inlined in a template; structured data is passed through context variables and traversed with flat dot notation. This is intentional: templates describe presentation, not application logic. Directives inside HTML comments (`<!-- -->`) are not evaluated.

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
- `kc_tpl_free()` releases rendered output and accepts `NULL`.
- `kc_tpl_close()` releases the template instance and accepts `NULL`.
- `kc_tpl_version()` returns the generated build version.

`kc_tpl_open()` uses `"."` as the include root when options are `NULL` or
when `options->root` is `NULL`. A non-NULL `root` is explicit and must be a
non-empty string. The source is borrowed only for the duration of
`kc_tpl_open()`; the instance owns its copy.

Variables are borrowed only for the duration of `kc_tpl_render()`. Each render
creates a fresh root scope, so values from one render do not persist into the
next. The returned output is caller-owned and must be released with
`kc_tpl_free()`.

A scripting binding can map objects/tables mechanically into `kc_tpl_var_t`
entries:

```js
const page = tpl(source, { root: "./views" });

const html = page.render({
    title: "Home",
    user_name: "John"
});
```

```lua
local page = tpl(source, {
    root = "./views"
})

local html = page:render({
    title = "Home",
    user_name = "John"
})
```

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

Run the portable native test suite after building the artifacts:

```bash
make test
```

Run the Windows test suite through Wine after building `x86_64/windows` artifacts:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable template-rendering library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/tpl.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports `kc_tpl_open`, `kc_tpl_render`, `kc_tpl_error`, `kc_tpl_free`, `kc_tpl_close`, and `kc_tpl_version`. It contains the reusable library capability, not the `tpl` CLI: `src/tpl.c` is not compiled into the module.

`make test wasm` compiles the six reusable public-API contract cases in `src/test.c` with Emscripten and runs them under Node.js. Native and Wine tests additionally run one grouped `kc_tpl_cli` case. It requires `bin/wasm32/wasm/tpl.wasm` and reports how to build it when it is absent.

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
- Emscripten SDK (`emcmake`, `emcc`, Node.js) for the `wasm32/wasm` target.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
