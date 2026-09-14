# AGENTS.md

## Project Context

`emb.c` is a portable local embedding library and CLI backed by vendored GGML
and one model embedded into the built artifact.

It converts text to one fixed-dimension float vector. It is not a model server,
vector database, semantic search system, or hosted embedding API.

Read `README.md` before modifying the project.

## Core Invariants

- The model weights are local and embedded from `lib/model.gguf`.
- GGML is vendored under `lib/ggml/`.
- No model download, network service, or account is required.
- Model architecture and tensor names are validated explicitly at open.
- `kc_emb_dim()` reports the required caller buffer dimension.
- `kc_emb_exec()` writes into caller-owned float storage.
- Input is borrowed only for the blocking call.
- Each context owns prepared model, compute, tokenizer, and worker state.
- Multiple calls on one context are serialized.
- Different contexts remain independently owned.
- Stop makes future work terminate with explicit status.
- Output format is one space-separated vector per input line.
- The implementation remains bounded by model/context dimensions and
    inspectable by one operator.

## Model Boundary

This project deliberately ships one embedding model. Do not add runtime model
selection, model registries, downloads, remote fallback, arbitrary architecture
loading, conversion pipelines, or model marketplaces.

A model replacement is a compatibility change: dimensions, tokenization,
normalization, pooling, output values, test tolerances, artifact size, and
supported targets must all be reviewed.

Do not claim semantic quality, universality, fairness, or calibrated similarity
beyond measured behavior of the embedded model.

## Public API and Ownership

Treat `src/libemb.h` as the contract. Contexts own inference resources; callers
own input and output buffers. `kc_emb_close()` must never race an active
`kc_emb_exec()`.

Do not return hidden allocations for vectors or expose GGML internals. Preserve
explicit dimension discovery and caller allocation.

## Source Layout

Preserve exactly:

- `src/emb.c` for the CLI;
- `src/libemb.c` for all reusable inference behavior;
- `src/libemb.h` for the public contract;
- `src/test.c` for all tests.

Do not create additional source, header, tokenizer, model, worker, backend, or
test files. Vendored GGML remains under `lib/`; all project logic and tests stay
in the existing four source files.

## Forbidden Default Recommendations

Do not add hosted embedding APIs, OpenAI compatibility, model downloads,
multiple model management, vector storage, HNSW search, RAG, document chunking,
batch schedulers, distributed workers, GPU services, telemetry, analytics,
accounts, API keys, plugins, or cloud deployment.

Composition with `hnsw`, files, and other tools is preferred over absorbing
their responsibilities.

## Resource and Concurrency Model

Each context allocates model metadata, tensors, compute buffers, token buffers,
and worker synchronization. CPU threads are bounded from available hardware.
Requests on one context are serialized and blocking.

Do not add unbounded queues, implicit batching, detached workers, shared global
model pools, or background services. Keep shutdown deterministic.

## Change Evaluation

For every change identify the exact model/input behavior, dimension, tokenizer
and normalization effects, output tolerance, memory impact, worker lifecycle,
cross-platform embedding consistency, and stop/close behavior.

Prefer direct model-specific code over generic inference abstractions.

## Testing

All tests remain in `src/test.c`. Cover open/close, dimension, deterministic
vectors, normalization and tokenization edges, empty and long input, caller
buffer contract, multiple contexts, serialized callers, stop, worker shutdown,
malformed embedded model handling where feasible, and CLI parseable output.

## Build and Completion

For documentation-only changes run `kcs AGENTS.md`. For behavior
changes use repository build/tests without cleaning unless authorized.

A change is complete when local model ownership, deterministic lifecycle,
vector contract, tests, and documentation agree and no unrelated embedding
platform was introduced.

The goal is one small local text-to-vector primitive.

## Design Summary

### Purpose

`emb.c` turns text into a fixed-size embedding vector using one embedded GGUF
model and vendored GGML runtime.

### Artifact and Model

`lib/model.gguf` is linked into the executable and libraries as binary data with
platform-specific linker symbols. The runtime opens GGUF metadata and tensors
directly from those bytes.

The implementation is model-specific: vocabulary, special token identifiers,
embedding dimensions, transformer layers, attention, feed-forward tensors,
pooling, and normalization are prepared explicitly.

### Context Lifecycle

`kc_emb_open()` allocates a context, parses the embedded model, prepares GGML
backend and graph allocation, creates token and compute storage, and starts its
worker resources.

`kc_emb_dim()` exposes model output dimension. `kc_emb_exec()` submits borrowed
input and caller output to the prepared worker and blocks for completion.
`kc_emb_close()` shuts workers down and releases every owned resource.

Close is not valid while execution is active.

### Execution

Input is normalized and tokenized with the model vocabulary, bounded by model
context size, then evaluated through the prepared transformer graph. The final
embedding is written directly to the caller's buffer.

No result allocation or persistent vector state is hidden in the API.

### Concurrency

One context serializes callers through mutexes and condition variables around
its prepared worker. Separate contexts own separate inference state.

The design avoids an unbounded request queue, process-wide model server, or
detached background service.

### CLI

The CLI accepts argument text or stdin lines and writes one line of
space-separated floats for each input. Its output is intended for pipes and
files, including composition with `hnsw.c`.

### Resource Model

Model size, context, vocabulary, layer count, embedding dimension, compute
buffer, token storage, and CPU threads determine resource use. These resources
are local to each context and model artifact.

There is no network, cache service, vector store, model registry, or remote
accelerator.

### Portability

GGML provides portable tensor execution. Project code handles platform binary
symbols and synchronization explicitly. Supported builds must preserve vector
dimension, lifecycle, parseable output, and materially stable inference.

### Non-Goals

The project does not provide runtime model selection, model conversion or
downloads, hosted APIs, vector indexing, similarity search, document chunking,
RAG, distributed inference, job queues, GPU serving, telemetry, or accounts.

### Change Criteria

A change must serve the embedded model directly, preserve caller-owned output
and serialized context behavior, state memory and dimension effects, test
vector behavior across relevant targets, and avoid hidden external services.

### Core Invariants

The project is defined by one embedded local model, vendored GGML, explicit
model-specific loading, caller-owned vectors, serialized prepared contexts,
parseable CLI output, no network dependency, and inspectable native code.
