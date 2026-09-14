# AGENTS.md

## Project Context

`tpm.c` is a small C library and one-shot CLI for scoring input text against one
reference text profile.

It builds an in-memory byte n-gram profile from representative text and returns
a heuristic score from 0 to 1. It is not machine learning infrastructure,
search, classification orchestration, or a model service.

Read `README.md` before modifying the project.

## Purpose

`tpm.c` builds one reference n-gram profile from representative text and scores
another text against it. The operator supplies the category meaning through the
map text. The library only computes similarity; it does not manage labels or
decisions.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- one context owns one built reference profile;
- map input is ordinary representative text;
- n-gram size is explicitly 1 through 8;
- normalization lowercases ASCII and collapses whitespace only;
- n-grams are contiguous byte windows, not Unicode characters or tokens;
- raw grams are sorted and counted before edge spaces are stripped;
- separately counted raw grams may remain duplicate stripped profile entries;
- profile capacity is 8192 entries;
- raw window capacity is 16384;
- scoring uses the built profile and returns a bounded heuristic value;
- a score is not a probability or guarantee;
- the CLI reads one map file and complete stdin, then prints one score;
- no network, external model, database, or persistent runtime state is required;
- the implementation remains portable C11 and inspectable.

## Matching Boundary

`tpm.c` measures similarity to one supplied text profile. It does not assign
labels, compare many profiles, choose thresholds, explain matches, detect
language, parse syntax, or establish semantic meaning.

Callers may run multiple independent contexts or CLI invocations when comparing
profiles. Do not absorb catalog, ranking, index, or classifier responsibilities
into this library by default.

Scores are shaped by smoothed n-gram likelihood and a logistic mapping. They are
useful for relative comparisons under similar conditions, not calibrated
confidence percentages.

## Profile Compatibility

The raw-sort-then-trim behavior intentionally mirrors the historical text
pipeline. Leading and trailing spaces are removed after raw grams are grouped,
so distinct raw grams can produce duplicate stripped names with separate
counts.

Do not deduplicate or reinterpret these entries as cleanup without exact score
compatibility tests. Changing normalization, n-gram extraction, smoothing, or
the logistic constants changes all scores.

## Normalization

Input is copied, ASCII `A-Z` is lowercased, whitespace runs collapse to one
space, and leading or trailing spaces are removed.

Other bytes are preserved. There is no punctuation removal, Unicode case
folding, normalization form, locale, or tokenization.

N-grams are byte windows over this normalized string.

## Profile Construction

`kc_tpm_build()` accepts n-gram sizes 1 through 8 and resets the context profile.

All raw windows are copied into a fixed 16384-entry workspace, sorted by their
raw bytes, and grouped into counts. Leading and trailing spaces are then removed
from each grouped gram before storing up to 8192 profile entries.

Because grouping precedes stripping, different raw windows can become equal
stored strings with separate counts. This preserves historical pipeline behavior
and is part of score compatibility.

The context stores profile entries, total raw windows, and selected n-gram size.

## Scoring

Input text is normalized and profiled with the same process. Each input gram
looks up the corresponding reference count. Add-one smoothing produces a log
likelihood contribution weighted by the input count.

The average log value is mapped with a fixed logistic function and clamped to
`[0, 1]`.

The result is a heuristic similarity score. It is not a probability and scores
from different map construction methods or n-gram sizes are not inherently
calibrated against each other.

Invalid, unbuilt, empty, or over-capacity scoring returns `0.0`.

## Public API and Ownership

Treat `src/libtpm.h` as a compatibility boundary.

Contexts own fixed profile storage, scalar options, and stop state.
Map and input strings are borrowed only during calls. `kc_tpm_build()` replaces
the current profile. `kc_tpm_score()` returns zero for invalid, unbuilt, empty,
or overflow cases rather than allocating an error object.

The public header exposes only the pure C API. It does not declare
`kc_tpm_run` or any JSON types. The in-process runner contract is provided
generically by `kcrun` (see Runner).

Do not add hidden persistent profiles or global matching state.

## Source Layout

Preserve the existing `src/` structure:

- `src/tpm.c` contains the CLI;
- `src/libtpm.c` contains profile building and scoring;
- `src/libtpm.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, profile, or test files. Keep every new
test in `test.c`; do not add `test_accuracy.c`, `profile.c`, generated model
sources, or per-domain fixtures.

The per-project `run.h` runner was removed during runner unification. The
library no longer builds a hand-written JSON runner and links no vendored JSON
dependency for its own operation. The CLI calls the public C API directly.

## Runner

The library no longer ships its own runner. The in-process composition interface
is provided by `kcrun` as a generic runner. `kcrun_call` auto-discovers the
standard C API symbols (`kc_tpm_open`, `kc_tpm_build`, `kc_tpm_score`,
`kc_tpm_stop`, `kc_tpm_close`, `kc_tpm_version`) via `dlsym` and dispatches the
JSON commands `score`, `open`, `close`, `stop`, and `version` to them.

Bridges (wvw, jni/kcapk) load `libtpm.so` in solitude through the `kcrun` host
face (`kcr_load` + `kcr_call`) and forward JSON payloads verbatim. There is no
linked shared-library dependency between host and kclib.

- Request: `{ "cmd": "score", "args": { "map_text": "...", "input_text": "...", "ngram_size": 3 } }`
- Success: `{ "result": { "score": 0.123456 }, "handle": 0 }`
- Error: returns NULL, sets `*out_err` to a malloc'd message.

The runner is stateless; `handle` is always 0.

## CLI

The CLI reads one complete map file and complete stdin into dynamic buffers,
builds one profile, scores stdin, and prints exactly six decimal places.

The default n-gram size is 3. `-n` accepts 1 through 8. Empty stdin prints
`0.000000`. The CLI is one-shot and has no resident request protocol. It calls
the public C API directly for all operations: `kc_tpm_options_default`,
`kc_tpm_open`, `kc_tpm_build`, `kc_tpm_score`, `kc_tpm_close`, and
`kc_tpm_options_free`.

## Resource Model

Profiles and raw gram workspaces have fixed capacities. Inputs that generate
more than 16384 raw windows or 8192 profile entries fail profile construction;
scoring overflow returns zero.

The context profile is fixed at 8192 entries. Profile construction and scoring
use fixed raw and profile workspaces, while normalization allocates proportional
to input byte length.

The CLI buffers the complete map file and stdin. There is no streaming score,
hidden truncation, cache, database, network, or background work.

Do not replace visible limits with unbounded allocations by default.

## Portability

The implementation is portable C11 and uses libm for logarithmic and logistic
scoring. Scores should remain materially stable across supported platforms,
subject to normal floating-point formatting differences.

## Concurrency

Profile build and scoring do not promise concurrent mutation of one context.

Do not expand lifecycle support into process supervision.

## Non-Goals

`tpm.c` does not provide multi-label classification, profile catalogs, search,
embeddings, machine learning, Unicode linguistic processing, persisted binary
models, automatic thresholds, distributed scoring, remote APIs, telemetry, or
a control plane.

## Change Evaluation

Every scoring change must name map text, input text, n-gram size, old score, and
expected score relationship. Check empty and short text, whitespace,
case-folding, capacity boundaries, duplicate stripped grams, unrelated input,
and repeated builds on one context.

Reject speculative model abstractions. Prefer explicit fixed behavior.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- machine-learning models or embeddings;
- vector databases or search indexes;
- multi-profile catalogs or classifiers;
- training pipelines or corpus collection;
- persisted binary model formats;
- automatic threshold selection;
- Unicode tokenization frameworks;
- explainability or feature dashboards;
- batch services, queues, or parallel workers;
- remote APIs, SaaS, or cloud inference;
- telemetry or analytics;
- plugins or generic metrics frameworks;
- a resident control socket.

Do not justify changes through AI trends, benchmark competition, enterprise
scale, or hypothetical datasets.

## Testing

Behavioral changes require exact or bounded score tests for identical,
representative, unrelated, empty, short, mixed-case, and whitespace-varied
texts; all n-gram sizes; capacity overflow; rebuilding; invalid contexts; stop
state; and CLI output formatting.

All tests remain in `src/test.c`. The test suite follows the common
`hnsw.c` layout: one case function per public API function
(`case_kc_tpm_<function>`), a `case_result(fail, name, detail)` line, and an
`all` target plus per-function dispatch in `main`.

## Build and Completion

For documentation-only changes, run `kcs .`. For source changes, use `README.md`
build and test commands. Do not run `make clean` without authorization.

A change is complete when score behavior is explicit and tested, fixed limits
and ownership remain clear, documentation matches implementation, and no
unrelated AI or classification platform was introduced.

The goal is one small text-profile comparison primitive.
