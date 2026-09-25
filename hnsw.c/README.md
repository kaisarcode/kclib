# hnsw.c - HNSW Vector Search

A minimalist C library and CLI for fixed-dimension vector indexing with Approximate Nearest Neighbor search using a Hierarchical Navigable Small World (HNSW) graph.

---

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
hnsw --dim 3 --input vectors.txt --query "1 0 0"
```

Using a different metric and limiting results:

```bash
hnsw --dim 3 --input vectors.txt --query "1 0 0" --metric cosine --top 5
```

Applying a threshold:

```bash
hnsw --dim 3 --input vectors.txt --query "1 0 0" --threshold 0.8
```

Pipe query vector through standard input:

```bash
echo "1 0 0" | hnsw --dim 3 --input vectors.txt
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
| `--max-conn` | Maximum graph connections per level |
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

---

## Public API

The index is a persistent in-memory object. The only required configuration is the vector dimension. Metric and tuning values have library defaults.

```c
#include "libhnsw.h"

kc_hnsw_options_t options = {0};
kc_hnsw_t *index = NULL;
kc_hnsw_result_t *results = NULL;
size_t result_count = 0;

options.dimension = 384;

if (kc_hnsw_open(&index, &options) == KC_HNSW_OK) {
    kc_hnsw_add(index, "id_1", values);
    kc_hnsw_build(index);
    kc_hnsw_search(
        index,
        query,
        5,
        -1.0,
        &results,
        &result_count
    );

kc_hnsw_free(results);
    kc_hnsw_close(index);
}
```

### Options and defaults

`kc_hnsw_options_t` represents the required dimension plus independently optional configuration:

| Option | Meaning | Internal default |
| :--- | :--- | :--- |
| `dimension` | Number of values in every vector | Required |
| `metric` | How vectors are compared | `KC_HNSW_METRIC_COSINE` |
| `max_connections` | Maximum graph connections per level | `16` |
| `build_effort` | Work spent building a higher-quality search graph | `64` |
| `search_effort` | Work spent finding better matches during a search | `64` |

Optional scalar fields are nullable pointers. A NULL pointer means the option is omitted and the library applies its internal default. A non-NULL pointer means the pointed value is explicit and is interpreted literally.

For example, to override only search effort:

```c
kc_hnsw_options_t options = {0};
int search_effort = 128;

options.dimension = 384;
options.search_effort = &search_effort;
```

### Lifecycle and ownership

- `kc_hnsw_open()` creates one reusable in-memory index. `dimension` must be greater than zero.
- `kc_hnsw_add()` copies both the identifier and vector values into the index.
- Adding a vector after a build invalidates the graph; call `kc_hnsw_build()` again before searching.
- `kc_hnsw_build()` explicitly constructs the approximate-neighbor graph.
- `kc_hnsw_search()` queries a built graph. Searches may run concurrently after build.
- Search result arrays are caller-owned and must be released with `kc_hnsw_free()`.
- Each result `id` borrows index storage and remains valid only while the index remains alive and unmodified.
- `kc_hnsw_dimension()`, `kc_hnsw_metric()`, and `kc_hnsw_count()` expose stable index properties.
- `kc_hnsw_close()` releases the index. Do not mutate or close it while searches are running.

Cosine and inner-product scores are similarities, so thresholds are minimum accepted scores. L2 scores are squared distances, so thresholds are maximum accepted distances.

The library performs approximate nearest-neighbor search. It is in-memory only and provides no persistence, embedding generation, network service, or database layer.

---

### Metrics

Available metrics:

- `l2`: squared Euclidean distance
- `cosine`: cosine similarity
- `inner` (or `inner_product`): inner product similarity

`l2` uses squared Euclidean distance:

```
d(a, b) = sum((a[i] - b[i])^2)
```

Note: no square root is applied. Rankings are identical to Euclidean distance.

---

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

- Artifact: `bin/wasm32/wasm/hnsw.wasm`
- Exports: `kc_hnsw_open`, `kc_hnsw_build`, `kc_hnsw_add`, `kc_hnsw_free`, `kc_hnsw_search`, `kc_hnsw_dimension`, `kc_hnsw_metric`, `kc_hnsw_count`, `kc_hnsw_close`, `kc_hnsw_strerror`, `kc_hnsw_version`
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
