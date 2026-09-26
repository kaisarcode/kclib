# emb.c - Vector Embedding Library

`emb.c` is a portable C library and CLI for generating text embeddings with one fixed local model: [BAAI/bge-small-en-v1.5](https://huggingface.co/BAAI/bge-small-en-v1.5). The model is embedded into the built artifact and inference runs locally through vendored GGML.

---
## CLI

Generate vector embeddings from command-line arguments or standard input.

### Examples

Single sentence embedding:

```bash
emb "The quick brown fox"
```

Batch processing via standard input:

```bash
echo "The quick brown fox" | emb
cat sentences.txt | emb
```

Read the fixed model dimension:

```bash
emb --dim
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
- `kc_emb_embed` generates an embedding for one input string.
- The embedded BGE small v1.5 model currently returns 384 floats.
- Use `kc_emb_free()` to release returned vectors.

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

The dimension is fixed by the embedded model. Callers do not choose it. This lets
a vector index such as `hnsw.c` use `emb.dimension()` directly when creating
an index, without hardcoding the model's current 384-element output.

### Runtime model

The embedded model is **BAAI/bge-small-en-v1.5**, published by the Beijing Academy of Artificial Intelligence (BAAI) as part of FlagEmbedding. The upstream model is distributed under the **MIT License**:

- Model: https://huggingface.co/BAAI/bge-small-en-v1.5
- FlagEmbedding: https://github.com/FlagOpen/FlagEmbedding
- Upstream license: MIT

`lib/model.gguf` contains the local GGUF representation used by this project. The model is linked into the produced artifacts; `emb.c` performs no downloads and has no network or hosted-service dependency.

---
## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/emb.wasm`
- Exports: `kc_emb_dimension`, `kc_emb_free`, `kc_emb_embed`, `kc_emb_version`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains are required only by enabled targets.

### System Libraries

Linux:
- `libpthread`
- `libdl`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Dependencies

| Path | Description |
|------|-------------|
| `lib/ggml/` | Tensor computation library for machine learning |
| `lib/model.gguf` | Embedded GGUF representation of BAAI/bge-small-en-v1.5 |

---
## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
