# ngram.c - Sliding-window n-gram traversal

`ngram.c` is a minimalist C library and CLI for descending sliding-window n-gram traversal of text. It enables semantic analysis by emitting token spans and executing commands for each chunk, designed as a composable native primitive for the KaisarCode ecosystem.

---

## CLI

Traverse text and emit n-gram chunks based on token window constraints.

### Examples

Basic n-gram extraction (default 1-5 tokens):

```bash
./bin/x86_64/linux/ngram "The quick brown fox"
```

Extraction with custom window size and separators:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --max 3 --min 2 --sep " ,"
```

Execute a command for each chunk and close span on stdout:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --cmd "grep -q fox"
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

```c
#include "libngram.h"

int my_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    printf("%.*s\n", (int)(chunk->byte_end - chunk->byte_start), chunk->input + chunk->byte_start);
    return 0; // 1 to close span, -1 to abort
}

kc_ngram_options_t options;
kc_ngram_options_default(&options);
options.max_tokens = 3;

kc_ngram_execute("The quick brown fox", &options, my_visitor, NULL);
```

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

The `wasm32/wasm` target builds the reusable traversal library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/ngram.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_ngram_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `ngram` CLI: `src/ngram.c` is not compiled into the module.

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
