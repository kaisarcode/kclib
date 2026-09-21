# ngram.c - Sliding-window n-gram traversal

`ngram.c` is a minimalist C library and CLI for descending sliding-window n-gram traversal of text. It enables semantic analysis by emitting token spans and executing commands for each chunk, designed as a composable native primitive for the KaisarCode ecosystem.

---

## CLI

Traverse text and emit n-gram chunks based on token window constraints.

### Examples

Basic n-gram extraction (default 1-10 tokens):

```bash
./bin/x86_64/linux/ngram "The quick brown fox"
```

Extraction with custom window size and separators:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --max 3 --min 2 --sep " ,"
```

Execute a command for each chunk and close span on stdout:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --cmd "grep fox"
```

Standard input processing:

```bash
echo "The quick brown fox" | ./bin/x86_64/linux/ngram
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `--max, -max <n>` | Maximum tokens per block |
| `--min, -min <n>` | Minimum tokens per block |
| `--sep, -sep <s>` | Custom separator characters |
| `--cmd, -cmd <cmd>` | Execute command for each chunk |
| `--help, -h` | Show help and usage |
| `--version, -v` | Show version |

---

### Output

Chunks are printed to stdout, one per line:

```
The quick brown fox
The quick brown
quick brown fox
The quick
quick brown
brown fox
```

## Public API

The library is stateless and reusable. Input and separator strings are caller-owned and borrowed; chunks reference byte offsets in the original input during the synchronous traversal only.

```c
#include "libngram.h"

int my_visitor(const kc_ngram_chunk_t *chunk, void *userdata) {
    printf("%.*s\n", (int)(chunk->byte_end - chunk->byte_start), chunk->input + chunk->byte_start);
    return 0; // 0 continue, 1 close this span, negative abort
}

kc_ngram_options_t options = kc_ngram_options_default();
options.max_tokens = 3;

size_t out_count;
int rc = kc_ngram_execute("The quick brown fox", &options, my_visitor, NULL, &out_count);
if (rc != KC_NGRAM_OK) {
    // handle error (KC_NGRAM_ERROR) or visitor abort (KC_NGRAM_EABORT)
}
```

### `kc_ngram_options_default()`

Returns a `kc_ngram_options_t` by value with the built-in defaults: `max_tokens = 10`, `min_tokens = 1`, and `separators = " \t\r\n"` (borrowed, byte-oriented). Configure fields before calling `kc_ngram_execute`.

### `kc_ngram_execute(input, options, visit, userdata, out_count)`

```c
int kc_ngram_execute(const char *input, const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit, void *userdata, size_t *out_count);
```

Descending sliding-window traversal over byte-delimited tokens. Emitted chunks borrow `input` and reference its byte offsets: `byte_start`/`byte_end` delimit the chunk in the input, `start`/`end` are the 0-based inclusive token indexes, and `size` is the number of tokens. Separators are byte-oriented.

- Returns `KC_NGRAM_OK`, or `KC_NGRAM_ERROR`, or `KC_NGRAM_EABORT` when a visitor aborts traversal (the aborting chunk is not counted).
- Stores the number of emitted chunks in `*out_count`.
- Empty input returns `KC_NGRAM_OK` with `*out_count == 0`; the visitor is not called.
- `max_tokens == 0` uses all available tokens.
- `min_tokens == 0` is not a valid configuration; use `kc_ngram_options_default()`.

### Visitor return values

- `0` - keep traversal open.
- `1` - close this span; contained (shorter) windows are skipped. The closing chunk counts as emitted.
- negative - abort traversal with `KC_NGRAM_EABORT`; the aborting chunk is not counted.

Status codes are `KC_NGRAM_OK` (0), `KC_NGRAM_ERROR` (-1), and `KC_NGRAM_EABORT` (-2).

`kc_ngram_version()` returns the library version as an unsigned 64-bit integer.

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

The `wasm32/wasm` target builds the reusable traversal library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/ngram.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the stateless reusable API (`kc_ngram_options_default`, `kc_ngram_execute`, `kc_ngram_version`) with status codes `KC_NGRAM_OK`, `KC_NGRAM_ERROR`, and `KC_NGRAM_EABORT`. Input and separator strings are caller-owned. It contains the reusable library capability, not the `ngram` CLI: `src/ngram.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/ngram.wasm` and reports how to build it when it is absent.

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
- No additional system libraries required.

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
