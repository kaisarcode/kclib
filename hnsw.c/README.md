# hnsw.c - HNSW Vector Search

A minimalist C library and CLI for fixed-dimension vector indexing with Approximate Nearest Neighbor search using a Hierarchical Navigable Small World (HNSW) graph.

---

## CLI

Run a nearest neighbor search over a vector dataset.

### Dataset format

Each line in the input file must contain:

```
<id> <v1> <v2> ... <vN>
```

Example (3D vectors):

```
item_1 1.0 0.0 0.0
item_2 0.0 1.0 0.0
item_3 0.0 0.0 1.0
```

---

### Examples

Basic search:

```bash
./bin/x86_64/linux/hnsw --dim 3 --input vectors.txt --query "1 0 0"
```

Using a different metric and limiting results:

```bash
./bin/x86_64/linux/hnsw --dim 3 --input vectors.txt --query "1 0 0" --metric cosine --top 5
```

Applying a threshold:

```bash
./bin/x86_64/linux/hnsw --dim 3 --input vectors.txt --query "1 0 0" --threshold 0.8
```

Pipe query vector through standard input:

```bash
echo "1 0 0" | ./bin/x86_64/linux/hnsw --dim 3 --input vectors.txt
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `--dim`, `-d` | Vector dimension |
| `--input`, `-i` | Input dataset file |
| `--query`, `-q` | Query vector |
| `--metric`, `-m` | Metric (`l2`, `cosine`, `inner`, `inner_product`) |
| `--top`, `-k` | Number of results |
| `--threshold`, `-t` | Threshold filter |
| `--max-conn` | Maximum graph connections per level (HNSW M) |
| `--build-effort` | Index quality vs. build speed (higher improves recall but slows index construction) |
| `--search-effort` | Search accuracy vs. query speed (higher improves recall but slows queries) |
| `--help`, `-h` | Show help |
| `--version`, `-v` | Show version |

---

### Output

Results are printed as:

```
<id>: <score>
```

---

## Metrics

Available metrics:

- `l2`: squared Euclidean distance
- `cosine`: cosine distance
- `inner` (or `inner_product`): inner product similarity

`l2` uses squared Euclidean distance:

```
d(a, b) = sum((a[i] - b[i])^2)
```

Note: no square root is applied. Rankings are identical to Euclidean distance.

---

## Public API

```c
#include "libhnsw.h"

kc_hnsw_options_t opts = kc_hnsw_options_default();
opts.dimension = dimension;
opts.metric = KC_HNSW_METRIC_COSINE;

kc_hnsw_t *hnsw = kc_hnsw_open(&opts);
kc_hnsw_add(hnsw, "id_1", values);
kc_hnsw_build(hnsw);
kc_hnsw_search(hnsw, query, limit, threshold, results);
kc_hnsw_close(hnsw);
```

## Lifecycle

- `kc_hnsw_options_default()` returns default index options.
- `kc_hnsw_options_load_env()` applies `KC_HNSW_*` environment overrides.
- `kc_hnsw_options_free()` releases option resources.
- `kc_hnsw_open()` allocates a new index from options.
- `kc_hnsw_add()` inserts vectors.
- `kc_hnsw_build()` constructs the HNSW graph.
- `kc_hnsw_search()` queries the index.
- `kc_hnsw_close()` releases all resources.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
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

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable index library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/hnsw.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_hnsw_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable index capability, not the `hnsw` CLI: `src/hnsw.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/hnsw.wasm` and reports how to build it when it is absent.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
