# tpl.c - Template Renderer

`tpl.c` is a simple template renderer with includes, scoped variables, blocks, and basic control directives. It provides `libtpl` for C callers and a `tpl` CLI that reads the template from standard input and writes the rendered output to standard output.

---

## CLI

### Examples

Render a template with variables:

```bash
echo '<h1>{{ title }}</h1>' | ./bin/x86_64/linux/tpl --var title=Home
```

Render with raw (unescaped) output:

```bash
echo '{{{ body }}}' | ./bin/x86_64/linux/tpl --var body='<b>bold</b>'
```

Render a file with includes:

```bash
./bin/x86_64/linux/tpl --root ./views --var page=Home < views/page.html
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `--root <dir>` | Base directory for `{{@include ...}}` path resolution (default: cwd) |
| `--var <key=value>` | Inject a template variable (repeatable) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

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

```c
#include "libtpl.h"

kc_tpl_t *ctx = kc_tpl_open();
char *output = NULL;

kc_tpl_set_root(ctx, ".");
kc_tpl_set_var(ctx, "title", "Home");
kc_tpl_render_string(ctx, "<h1>{{ title }}</h1>", &output);

free(output);
kc_tpl_close(ctx);
```

---

## Lifecycle

- `kc_tpl_open()` - allocates and returns a new renderer context owned by the caller.
- `kc_tpl_set_root()` - configures include path resolution for the context.
- `kc_tpl_set_var()` - stores string variables in the context scope.
- `kc_tpl_render_string()` - renders one template into a caller-owned output buffer.
- `kc_tpl_close()` - releases the context and all associated variable storage.

---

## Runner Interface

`tpl.c` exports a standard in-process entry point for bridge composition:

```c
char *kc_tpl_run(const char *payload_json, char **out_err);
```

This is the canonical implementation of the CLI's functionality. The CLI builds a JSON payload from argv, calls the runner, and formats the JSON result back to stdout.

### JSON Contract

**Request:**
```json
{ "cmd": "exec", "args": { "template": "...", "root": ".", "vars": { "key": "value" } } }
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `cmd` | string | yes | Subcommand: `"exec"` or `"version"` |
| `args.template` | string | yes (for exec) | Template string to render |
| `args.root` | string | no | Include root directory (default: cwd) |
| `args.vars` | object | no | Template variables as key-value pairs |

**Success (exec):**
```json
{ "result": { "output": "rendered template text" }, "handle": 0 }
```

**Success (version):**
```json
{ "result": { "version": 1234567890 }, "handle": 0 }
```

**Error:**
- Returns `NULL` and sets `*out_err` to a malloc'd error message.
- Caller must free both the result string and `*out_err` (when set).

The runner is stateless; `handle` is always `0`.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
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
- The module exports the public `kc_tpl_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `tpl` CLI: `src/tpl.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/tpl.wasm` and reports how to build it when it is absent.

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
