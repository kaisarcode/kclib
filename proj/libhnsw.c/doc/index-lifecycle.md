# HNSW index lifecycle

`libhnsw.c` keeps vectors and graph state in one in-memory index. This page
describes the lifecycle and score semantics that are important when integrating
the index.

## Open an index

The vector dimension is required and must be greater than zero.

Other tuning values are optional. The current defaults are:

| Option | Default | Purpose |
| --- | --- | --- |
| `metric` | `KC_HNSW_METRIC_COSINE` | Vector comparison metric |
| `max_connections` | `16` | Maximum graph connections per level |
| `build_effort` | `64` | Work spent constructing the graph |
| `search_effort` | `64` | Work spent during a search |

The index is in memory only. It does not persist vectors or generate embeddings.

## Add and build

`kc_hnsw_add()` copies both the identifier and vector values into the index.

`kc_hnsw_build()` constructs the approximate-neighbor graph. A search requires
a built graph.

Adding a vector after a successful build invalidates that graph. Call
`kc_hnsw_build()` again before searching.

## Search

`kc_hnsw_search()` returns allocated result storage. Release the result array
with `kc_hnsw_free()`.

Result identifiers borrow storage owned by the index. They remain valid only
while the index remains alive and unmodified.

After a build, searches may run concurrently. Do not mutate or close the index
while searches are active.

## Metric semantics

`KC_HNSW_METRIC_COSINE` and inner product produce similarity scores. A search
threshold is therefore a minimum accepted score.

L2 produces squared Euclidean distance:

```text
d(a, b) = sum((a[i] - b[i])^2)
```

No square root is applied. A threshold for L2 is therefore a maximum accepted
distance.

## Index properties

`kc_hnsw_dimension()`, `kc_hnsw_metric()`, and `kc_hnsw_count()` expose stable
properties of the current index.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libhnsw.h) for the complete C interface.
