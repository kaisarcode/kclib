# emb.c - Vector Embedding Library

`emb.c` is a portable C library and CLI for generating vector embeddings from text using a GGML-based model. It is designed as a composable primitive.

---

## CLI

Generate vector embeddings from command-line arguments or standard input.

### Examples

Single sentence embedding:

```bash
./bin/x86_64/linux/emb "The quick brown fox"
```

Batch processing via standard input:

```bash
echo "The quick brown fox" | ./bin/x86_64/linux/emb
cat sentences.txt | ./bin/x86_64/linux/emb
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

### Output

Results are printed as space-separated floats, one line per input text:

```
0.123456 0.234567 ... 0.345678
```

---

## Public API

### Types and Status

```c
typedef struct kc_emb kc_emb_t;

#define KC_EMB_OK     0
#define KC_EMB_ERROR -1
```

### Functions

| Function | Returns | Description |
| :------- | :------ | :---------- |
| `kc_emb_version(void)` | `uint64_t` | Return the build version timestamp. |
| `kc_emb_open(kc_emb_t **out)` | `int` | Allocate and initialize embedding context. Heavyweight operation; embeds one local model (`lib/model.gguf`). Returns `KC_EMB_OK` on success, `KC_EMB_ERROR` on failure. |
| `kc_emb_close(kc_emb_t *ctx)` | `void` | Release a context and all associated resources. |
| `kc_emb_dim(const kc_emb_t *ctx)` | `size_t` | Return embedding dimension derived from the embedded model; 0 if `ctx` is NULL. Not a hardcoded constant. |
| `kc_emb_exec(kc_emb_t *ctx, const char *input, float **vec, size_t *count)` | `int` | Generate embedding for `input`; on success sets `*vec` to caller-owned float array of `*count` floats, to be freed with `kc_emb_free`. Input is borrowed only during the call; empty string is valid. Blocking and serialized; multiple calls on one context are serialized. Returns `KC_EMB_OK` or `KC_EMB_ERROR`. Example: `kc_emb_exec(ctx, "hello", &vec, &count)`. |
| `kc_emb_free(void *ptr)` | `void` | Free array returned by `kc_emb_exec`. Safe with NULL. |
| `kc_emb_get_error(const kc_emb_t *ctx)` | `const char *` | Borrowed contextual error string; valid until next mutating operation on `ctx` or `kc_emb_close`. Empty string on fresh context with no error; NULL if `ctx` is NULL. |

### Example

```c
#include "libemb.h"

kc_emb_t *ctx = NULL;
if (kc_emb_open(&ctx) == KC_EMB_OK) {
    float *vec = NULL;
    size_t count = 0;
    if (kc_emb_exec(ctx, "The quick brown fox", &vec, &count) == KC_EMB_OK) {
        // use vec[0..count-1]
    }
    kc_emb_free(vec);
    kc_emb_close(ctx);
}
```

Error details:

```c
if (kc_emb_exec(ctx, "hello", &vec, &count) != KC_EMB_OK) {
    const char *msg = kc_emb_get_error(ctx);
    // msg borrowed, valid until next mutating op or close
}
```

---

## Lifecycle

- `kc_emb_open(&ctx)` - heavyweight allocation; parses the one embedded local model (`lib/model.gguf`), prepares GGML backend, tokenizer, and worker state. One embedded model only.
- `kc_emb_dim(ctx)` - model-derived dimension; `size_t`, 0 for NULL.
- `kc_emb_exec(ctx, input, &vec, &count)` - blocking, serialized execution; multiple calls on the same context are serialized. Input is borrowed only during the call; empty input remains valid. Returns owned `float` array via `*vec` with length `*count`; caller owns the array and must release it with `kc_emb_free`.
- `kc_emb_get_error(ctx)` - borrowed contextual error string; valid until next mutating operation on the same context or `kc_emb_close`; empty string if no error; NULL for NULL `ctx`.
- `kc_emb_close(ctx)` - releases the context and all associated resources.

Notes:

- One embedded local model (`lib/model.gguf`) linked into the artifact. No model downloads, no network dependency, no remote fallback.
- No hidden vector allocations beyond the explicit owned array returned via `kc_emb_exec` and freed with `kc_emb_free`.
- Input string lifetime is caller-owned before and after the call; only borrowed during `kc_emb_exec`.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile the test executable, link dynamically against the generated shared library, and run directly.

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

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable embedding library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/emb.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_emb_*` API with its existing signatures, ownership, lifecycle, status codes, and vector contract. It contains the reusable embedding capability, not the `emb` CLI: `src/emb.c` is not compiled into the module.
- The module embeds `lib/model.gguf` and the vendored GGML runtime (with the generic WebAssembly CPU kernels). The Emscripten build runs single-threaded: execution is performed inline in the caller while preserving the serialized, blocking contract of `kc_emb_exec()`.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/emb.wasm` and reports how to build it when it is absent.

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
make wasm32/wasm
```

---

## Dependencies

| Path | Description |
|------|-------------|
| `lib/ggml/` | Tensor computation library for machine learning |
| `lib/model.gguf` | Embedded model weights |

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
- `libdl`
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
- Emscripten SDK for the `wasm32/wasm` target.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
