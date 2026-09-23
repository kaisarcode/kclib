# min.c - Asset Minifier

`min.c` provides conservative CSS, JavaScript, and HTML asset minification as a small C library and stdin/stdout CLI. It reduces payload size while preserving valid runtime behavior for common web asset pipelines.

---

## CLI

### Examples

Minify CSS:

```bash
echo 'body { color: red; }' | ./bin/x86_64/linux/min css
```

Minify JavaScript:

```bash
echo 'const x = 1; // comment' | ./bin/x86_64/linux/min js
```

Minify HTML:

```bash
echo '<div>  hello  </div>' | ./bin/x86_64/linux/min html
```

---

### Parameters

| Parameter | Description |
| :--- | :--- |
| `css` | Minify CSS input |
| `js` | Minify JavaScript input |
| `html` | Minify HTML input |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Minification Behavior

**CSS** - removes block comments, collapses whitespace, removes trailing semicolons before `}`, and strips unit suffixes from zero values. Preserves quoted strings and `calc()` spacing.

**JS** - removes line and block comments, collapses whitespace between tokens. Preserves quoted strings, template literals, and regex literals. Regex detection is context-aware using the preceding significant character.

**HTML** - removes HTML comments, collapses whitespace between tokens. Preserves content inside `<pre>` and `<textarea>` verbatim. Preserves spaces adjacent to inline elements.

---

## Public API

```c
#include "libmin.h"

char *output = kc_min_minify(KC_MIN_MODE_CSS, "body { color: red; }");

if (output) {
    /* use output */
    kc_min_free(output);
}
```

The reusable API is stateless. `kc_min_minify()` accepts one of
`KC_MIN_MODE_CSS`, `KC_MIN_MODE_JS`, or `KC_MIN_MODE_HTML` plus a borrowed
null-terminated input string. On success it returns an owned, null-terminated
string, including an allocated empty string for empty input. It returns `NULL`
for a null input, an invalid mode, or an allocation failure.

`kc_min_free()` releases memory returned by the library and accepts `NULL`.
There is no public context or lifecycle state.

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

The `wasm32/wasm` target builds the reusable asset-minifier library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/min.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports `kc_min_minify`, `kc_min_free`, and `kc_min_version`. It contains the reusable stateless library capability, not the `min` CLI: `src/min.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the reusable public-API contract tests under Node.js. Native and Wine tests additionally run one grouped `kc_min_cli` case; host process-spawning code is excluded from the WASM build. It requires `bin/wasm32/wasm/min.wasm` and reports how to build it when it is absent.

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
