# min.c - Asset Minifier

`min.c` provides conservative CSS, JavaScript, HTML, and generic text minification as a small C library and stdin/stdout CLI. It reduces payload size while preserving valid runtime behavior for common web asset pipelines.

---

## CLI

### Examples

Minify CSS:

```bash
echo 'body { color: red; }' | ./bin/x86_64/linux/min --css
```

Minify JavaScript:

```bash
echo 'const x = 1; // comment' | ./bin/x86_64/linux/min --js
```

Minify HTML:

```bash
echo '<div>  hello  </div>' | ./bin/x86_64/linux/min --html
```

Minify generic text:

```bash
echo '  hello   world  ' | ./bin/x86_64/linux/min --txt
```

Pass a value directly instead of stdin:

```bash
./bin/x86_64/linux/min -txt "Hello                      World  !"
```

---

### Parameters

| Parameter | Description |
| :--- | :--- |
| `-css`, `--css` | Minify CSS input |
| `-js`, `--js` | Minify JavaScript input |
| `-html`, `--html` | Minify HTML input |
| `-txt`, `--txt` | Minify generic text input |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

When a value argument is omitted, the CLI reads the source from stdin. When a
value is present after the mode flag, that value is minified directly.

---

## Minification Behavior

**CSS** - removes block comments, collapses whitespace, removes trailing semicolons before `}`, and strips unit suffixes from zero values. Preserves quoted strings and `calc()` spacing.

**JS** - removes line and block comments, collapses whitespace between tokens. Preserves quoted strings, template literals, and regex literals. Regex detection is context-aware using the preceding significant character.

**HTML** - removes HTML comments, collapses whitespace between tokens. Preserves content inside `<pre>` and `<textarea>` verbatim. Preserves spaces adjacent to inline elements.

**Text** - collapses whitespace runs to a single space and trims leading and trailing whitespace. It does not interpret comments, markup, quotes, or other syntax.

---

## Public API

The reusable API is stateless and exposes each minification capability directly.

```c
#include "libmin.h"

char *css = kc_min_css("body { color: red; }");
char *js = kc_min_js("const x = 1; // comment");
char *html = kc_min_html("<div>  hello  </div>");
char *txt = kc_min_txt("  hello   world  ");

kc_min_free(css);
kc_min_free(js);
kc_min_free(html);
kc_min_free(txt);
```

The public operations are:

- `kc_min_css()` for CSS.
- `kc_min_js()` for JavaScript.
- `kc_min_html()` for HTML.
- `kc_min_txt()` for generic text whitespace minification.
- `kc_min_free()` releases any successful returned allocation and accepts `NULL`.
- `kc_min_version()` returns the generated build version.

Each minifier accepts one borrowed null-terminated source string. On success it
returns an owned null-terminated string, including an allocated empty string for
empty input. A `NULL` input or allocation failure returns `NULL`.

A natural scripting binding can expose the same capability directly:

```lua
local css = min.css(source)
local js = min.js(source)
local html = min.html(source)
local txt = min.txt(source)
```

There is no public context, mode enum, dispatcher, or lifecycle state.


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

The `wasm32/wasm` target builds the reusable asset-minifier library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/min.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports `kc_min_css`, `kc_min_js`, `kc_min_html`, `kc_min_txt`, `kc_min_free`, and `kc_min_version`. It contains the reusable stateless library capability, not the `min` CLI: `src/min.c` is not compiled into the module.

`make test wasm` compiles the six reusable public-API contract cases in `src/test.c` with Emscripten and runs them under Node.js. Native and Wine tests additionally run one grouped `kc_min_cli` case; host process-spawning code is excluded from the WASM build. It requires `bin/wasm32/wasm/min.wasm` and reports how to build it when it is absent.

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
