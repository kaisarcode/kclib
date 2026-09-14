# lng.c - Language Detection Library

`lng.c` is a minimalist C library and CLI for detecting the language of the given text using internal language profiles. It is designed as a composable native primitive for the KaisarCode ecosystem.

---

## CLI

Detect the language of text provided as an argument or via standard input.

### Examples

Single language detection:

```bash
./bin/x86_64/linux/lng "Hello world"
```

Ranked detection with threshold and limit:

```bash
./bin/x86_64/linux/lng "Hello world" -l 3 -t 0.1
```

Standard input processing (one-shot):

```bash
printf 'hola mundo' | ./bin/x86_64/linux/lng
```

EOF terminates the one-shot request and the process exits.

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `-t`, `--threshold <n>` | Minimum score threshold |
| `-l`, `--limit <n>` | Maximum number of results |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

### Output

Results are printed as the language code (for single match) or code and score (for ranked results):

```
en
```

```
en: 0.9500
es: 0.0400
```

## Public API

```c
#include "liblng.h"

kc_lng_init();

const char *code = kc_lng_detect("Hello world");

kc_lng_result_t results[3];
int count = kc_lng_detect_top("Hello world", results, 3, 0.1);
```

## Lifecycle

- `kc_lng_init()` - initializes internal language profiles. This call is idempotent and thread-safe.
- `kc_lng_detect()` and `kc_lng_detect_top()` - perform language detection. Both call `kc_lng_init()` internally if not yet initialized. Once initialized, all state is read-only and safe for concurrent access.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

Run the portable C contract tests after building:

```bash
make test
```

When Windows artifacts are available and Wine is installed:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable language-detection library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/lng.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_lng_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `lng` CLI: `src/lng.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/lng.wasm` and reports how to build it when it is absent.

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
