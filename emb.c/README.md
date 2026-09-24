# emb.c - Vector Embedding Library

`emb.c` is a portable C library and CLI for generating text embeddings with one fixed local model: [BAAI/bge-small-en-v1.5](https://huggingface.co/BAAI/bge-small-en-v1.5). The model is embedded into the built artifact and inference runs locally through vendored GGML.

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

Read the fixed model dimension:

```bash
./bin/x86_64/linux/emb --dim
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `-d`, `--dim` | Print the embedded model vector dimension |
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

`emb.c` exposes one embedding operation because it ships one predefined model and one input type: text.

```c
#include "libemb.h"

#define KC_EMB_OK 0
#define KC_EMB_ERROR -1

size_t kc_emb_dimension(void);

int kc_emb_embed(
    const char *input,
    float **out_data,
    size_t *out_count
);

void kc_emb_free(void *ptr);
uint64_t kc_emb_version(void);
```

- `kc_emb_dimension` returns the fixed vector dimension declared by the embedded model. It is a model property, not a caller option.
- `kc_emb_embed` generates an embedding for one null-terminated input string. Empty input is valid.
- `input` is borrowed only for the duration of the call and is never retained.
- On success, `*out_data` is a caller-owned float array and `*out_count` is its element count. The embedded BGE small v1.5 model currently returns 384 floats.
- Release vectors with `kc_emb_free()`; `kc_emb_free(NULL)` is safe.
- Invalid arguments or initialization/inference/allocation failures return `KC_EMB_ERROR` and reset available outputs to `NULL` / `0`.
- `kc_emb_version` returns the generated build timestamp.
- Model loading, GGML compute state, and serialization are internal. Callers do not open, configure, query, or close a model.

### Example

```c
#include "libemb.h"

float *vec = NULL;
size_t count = 0;
size_t dimension = kc_emb_dimension();

if (kc_emb_embed("The quick brown fox", &vec, &count) == KC_EMB_OK) {
    /* use vec[0..count-1] */
}

kc_emb_free(vec);
```

A natural scripting binding is therefore mechanical:

```js
const dimension = emb.dimension();
const vector = emb.embed("The quick brown fox");
```

The dimension is fixed by the embedded model. Callers do not choose it. This lets
a vector index such as `hnsw.c` use `emb.dimension()` directly when creating
an index, without hardcoding the model's current 384-element output.

No lifecycle object or semantic wrapper is required.

### Runtime model

The embedded model is **BAAI/bge-small-en-v1.5**, published by the Beijing Academy of Artificial Intelligence (BAAI) as part of FlagEmbedding. The upstream model is distributed under the **MIT License**:

- Model: https://huggingface.co/BAAI/bge-small-en-v1.5
- FlagEmbedding: https://github.com/FlagOpen/FlagEmbedding
- Upstream license: MIT

`lib/model.gguf` contains the local GGUF representation used by this project. The model is linked into the produced artifacts; `emb.c` performs no downloads and has no network or hosted-service dependency.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Native and Wine runs execute five reusable public-API cases plus one grouped `kc_emb_cli` case covering argument input, stdin input, vector output, dimension, help,
version, stdout/stderr, and exit status.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. WASM runs the five reusable API cases and excludes the host CLI process harness. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable embedding library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/emb.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports `kc_emb_dimension`, `kc_emb_embed`, `kc_emb_free`, and `kc_emb_version`. It contains the reusable embedding capability, not the `emb` CLI: `src/emb.c` is not compiled into the module.
- The module embeds `lib/model.gguf` and the vendored GGML runtime (with the generic WebAssembly CPU kernels). Calls remain blocking and reuse the same fixed-model inference capability.

`make test wasm` compiles `src/test.c` with Emscripten and runs the five reusable public-API contract cases under Node.js. It requires `bin/wasm32/wasm/emb.wasm` and reports how to build it when it is absent.

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
| `lib/model.gguf` | Embedded GGUF representation of BAAI/bge-small-en-v1.5 |

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

The `emb.c` source code is distributed under the **GNU General Public License version 3 (GPLv3)**. The embedded `BAAI/bge-small-en-v1.5` model is an upstream BAAI/FlagEmbedding work distributed under the **MIT License**; its attribution and source are documented above.
