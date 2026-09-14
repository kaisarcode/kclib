# AGENTS.md

## Project Context

`hnsw.c` is a small in-memory fixed-dimension approximate nearest-neighbor
index using an HNSW graph. It provides one C library and a dataset/query CLI.

It is not a vector database, embedding service, distributed search platform, or
metadata engine.

Read `README.md` before modifying the project.

## Core Invariants

- One index has one fixed dimension and metric.
- Every inserted vector must match that dimension.
- IDs and vector values are copied into index ownership.
- Vectors are added before explicit graph construction.
- Search requires a successfully built graph.
- HNSW parameters remain explicit: `m`, construction effort, search effort.
- L2 is squared Euclidean distance.
- Cosine is distance derived from normalized similarity semantics.
- Inner product remains similarity-oriented.
- Threshold direction is metric-specific: maximum distance for L2, minimum
    similarity for cosine and inner product.
- Search is approximate and must not be described as exact.
- Post-build concurrent searches are safe with caller-owned result buffers.
- Mutation and build use exclusive locking.
- The index is in-memory and process-local.
- No network, persistence, embedding generation, or external service exists.

## Index Boundary

Do not add collections, namespaces, metadata filters, schemas, persistence,
replication, sharding, transactions, WAL, compaction, remote APIs, background
maintenance, or managed storage.

IDs are labels, not global identities. Vector creation belongs to `emb.c` or the
caller. Dataset parsing belongs to the CLI.

## Approximation and Metrics

Changes to graph level assignment, neighbor selection, pruning, entry points,
distance calculations, or effort budgets change recall and compatibility.
Require concrete datasets with exact dimension, metric, expected rank behavior,
and bounded recall tests.

Do not silently convert distance to similarity or apply square roots. Keep
threshold and output score meaning explicit per metric.

## Public API and Ownership

Treat `src/libhnsw.h` as the public contract. Results borrow context-owned ID
strings and remain valid only while the index lives. Callers own query and
result buffers. `kc_hnsw_close()` must not race another holder.

Preserve explicit `reserve`, `add`, `build`, and `search` phases. Do not hide
build or allocation behind background tasks.

## Source Layout

Preserve exactly:

- `src/hnsw.c` for the CLI;
- `src/libhnsw.c` for all reusable index behavior;
- `src/libhnsw.h` for the public contract;
- `src/test.c` for all tests.

Do not create additional source, header, metric, graph, persistence, or test
files. Keep stress, concurrency, metric, malformed CLI datasets, and recall tests in
`src/test.c`.

## Resource Model

Vector storage grows with count times dimension. Graph storage grows with nodes,
levels, and bounded connections. Search and build work are controlled by effort
parameters and temporary candidate structures.

Do not introduce unbounded queues, automatic caches, detached builders, or
hidden memory mapping. Capacity exhaustion and allocation failure must be
legible.

## Forbidden Default Recommendations

Do not add vector databases, persistence, mmap indexes, distributed search,
replication, metadata, SQL, HTTP/gRPC, accounts, authorization, multi-tenancy,
embedding APIs, RAG, cloud storage, telemetry, dashboards, GPU services,
plugins, or generic control sockets.

Do not justify changes through enterprise scale or vector-database feature
parity.

## Testing

Keep all tests in `src/test.c`. Cover every metric, dimension validation,
duplicate and empty IDs, reserve growth, state transitions, thresholds, top-K,
zero vectors for cosine, deterministic small datasets, approximate recall,
concurrent post-build searches, stop behavior, malformed CLI datasets, and
resource cleanup.

## Build and Completion

For documentation-only changes run `kcs AGENTS.md DESIGN.md`. For behavior
changes use repository build/tests without cleaning unless authorized.

A change is complete when graph state, metric semantics, ownership, locking,
resource bounds, tests, and docs agree and no database platform was introduced.

The goal is one direct in-memory ANN primitive.

## Design Summary

### Purpose

`hnsw.c` indexes caller-supplied fixed-dimension vectors in an in-memory HNSW
graph and performs approximate top-K search.

### Lifecycle

Options define dimension, metric, maximum connections, construction effort, and
search effort. `kc_hnsw_open()` creates an empty index. `reserve` optionally
preallocates vector capacity. `add` copies IDs and values. `build` constructs
the graph. `search` queries the built graph. `close` releases all state.

This explicit sequence keeps expensive graph construction visible.

### Graph

Each node owns its vector, ID, assigned maximum level, and bounded neighbor
lists. Construction navigates from the current entry point through upper levels
and selects neighbors under the configured budgets.

Search descends greedily through upper levels, then explores candidates at the
base layer under `ef_search`. Results are approximate and sorted according to
the configured metric.

### Metrics

L2 returns squared Euclidean distance; smaller is better. Cosine uses angular
distance/similarity semantics documented by the API; inner product is
similarity-oriented and larger is better.

Threshold acceptance follows metric direction. No automatic normalization or
metric conversion is hidden from callers.

### Ownership and Concurrency

The index copies inserted IDs and float vectors. Search results contain borrowed
ID pointers. Query and output buffers remain caller-owned.

Reserve, add, and build take exclusive locks. Searches use shared locking and
may run concurrently after build. Close requires exclusive external lifecycle
ownership and must not race operations.

### CLI

The CLI reads a plain line-oriented dataset of ID followed by exactly the
configured number of floats. It accepts a query argument or stdin and prints
`id: score` lines.

The CLI calls the public C API directly for all index operations: open, add (per vector line),
build, search, and close. The CLI owns argument parsing, file I/O, and stdout
formatting.

### Resource Model

Memory is local and proportional to vectors, dimensions, graph levels, and
bounded neighbors. Construction and search effort are explicit options.

There is no disk index, mmap state, background compaction, remote node, or
hidden service.

### Non-Goals

The project does not provide embeddings, metadata filters, collections,
persistence, transactions, distributed search, replication, APIs, accounts,
authorization, RAG orchestration, telemetry, or managed infrastructure.

### Change Criteria

Changes must identify metric and graph effects, preserve phase and ownership
contracts, define threshold direction, bound memory and work, test recall and
state failures, and retain post-build shared-search safety.

### Core Invariants

The project is defined by fixed dimensions, one explicit metric, copied local
vectors, explicit build, bounded HNSW connections and effort, approximate
search, concurrent read-only queries, no persistence, and inspectable C11 code.
