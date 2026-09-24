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
| `-t`, `--threshold <n>` | Minimum score threshold (default `0.001`) |
| `-l`, `--limit <n>` | Maximum number of results (default `1`) |
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

Defaults are threshold `0.001` and limit `1`. Limit `1` prints only the code; larger limits print `code: score` with four decimal places. Empty input produces no output.

---

## Public API

Stateless detection over heap-allocated results. No handle or explicit initialization required - internal language profiles are initialized automatically exactly once with platform once-control and remain read-only thereafter, safe for concurrent detection.

Scores are heuristic ranking values in `[0, 1]` shaped for thresholding, not calibrated probabilities.

```c
#include "liblng.h"

#define KC_LNG_OK 0
#define KC_LNG_ERROR -1

typedef struct {
    const char *code;  // borrowed static storage, do not free or modify
    double score;      // heuristic ranking value, not a probability
} kc_lng_result_t;

int kc_lng_detect(const char *text, double threshold, size_t limit,
                  kc_lng_result_t **out_results, size_t *out_count);
void kc_lng_free(void *ptr);
uint64_t kc_lng_version(void);
```

- `kc_lng_detect` - stateless detection. Sanitizes/normalizes text internally, scores against all compiled profiles, sorts descending, filters below `threshold` (`[0, 1]`), bounds output by `limit`. `text == NULL` yields `KC_LNG_ERROR`; `text[0] == '\0'` yields zero results (`KC_LNG_OK` with `*out_count == 0`, `*out_results == NULL`, even at threshold `0.0`). On success `*out_results` points to a heap-allocated array owned by the caller; on zero results or error `*out_results` is set to `NULL`. Returns `KC_LNG_OK` on success (including zero matches), `KC_LNG_ERROR` on invalid arguments or allocation failure.
- Ownership - array returned via `out_results` is heap-allocated and caller-owned; release with `kc_lng_free()`. Each `code` string points to static library-owned storage; caller must not free or modify it. Detection does not retain input.
- `kc_lng_free` - release memory allocated by `kc_lng_detect`. `NULL` safe (no-op).
- `kc_lng_version` - returns build version as Unix timestamp.
- Threshold filters results; limit bounds output count. Zero matches is a successful result with count zero, not an error. No network, external model, or persistent state is required.

### Example

```c
#include "liblng.h"

kc_lng_result_t *results = NULL;
size_t count = 0;

if (kc_lng_detect(
    "Hello world",
    0.001,
    3,
    &results,
    &count
) == KC_LNG_OK) {
    for (size_t i = 0; i < count; i++) {
        // results[i].code is borrowed static storage; results[i].score is heuristic
    }
}

kc_lng_free(results);
```

A natural scripting binding can expose the same stateless capability directly:

```js
const results = lng.detect(text, 0.001, 3);
```

The binding only adapts C array ownership and strings mechanically. It does not
need a context, lifecycle object, setters, callbacks, or semantic wrapper.

## Lifecycle

No explicit initialization required. Internal profiles are initialized automatically on first detection, exactly once, in a thread-safe manner. Once initialized, profile state remains read-only and safe for concurrent detection without additional synchronization. Results are sorted by descending heuristic score. Scores remain heuristic ranking values, not probabilities or confidence percentages.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

Run the portable contract tests after building:

```bash
make test
```

Native tests execute five reusable public-API cases plus one grouped
`kc_lng_cli` case covering the shipped CLI contract: argument and stdin input,
default and ranked output, threshold and limit handling, diagnostics, help,
version, and exit status.

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

`make test wasm` compiles `src/test.c` with Emscripten and runs the five reusable
public-API contract cases under Node.js. Native and Wine runs additionally execute
the grouped `kc_lng_cli` case; host process-spawning code is excluded from the
WASM test build. It requires `bin/wasm32/wasm/lng.wasm` and reports how to build
it when it is absent.

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
