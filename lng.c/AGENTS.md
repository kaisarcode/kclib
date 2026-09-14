# AGENTS.md

## Project Context

`lng.c` is a small local language detector built from internal seed profiles and
UTF-8 trigram frequency matching.

It returns stable language codes and ranked heuristic scores through a C library
and byte-framed CLI. It is not machine learning infrastructure, translation,
or a hosted language service.

Read `README.md` before modifying the project.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- language profiles are compiled into the library;
- profiles are built exactly once with platform once-control;
- initialized profiles become read-only and support concurrent detection;
- normalization is local, deterministic, and allocation-based;
- matching uses three-character UTF-8 n-grams;
- each language profile has at most 2048 grams;
- at most 32 language slots exist;
- results are sorted by descending score;
- thresholds filter results and limits bound output count;
- language codes point to static storage;
- scores are heuristic matches, not calibrated probabilities;
- context detection observes cooperative stop state;
- no network, external model, training service, or persistent state is required;
- the implementation remains portable C11 and inspectable.

## Detection Boundary

The detector compares normalized text with fixed profiles. It does not identify
dialects, authors, locale, script variants, translation quality, sentiment,
topic, toxicity, or user identity.

Short or mixed-language text may be ambiguous. Returning no result above the
threshold is valid behavior.

Do not present scores as confidence percentages or guarantees. Do not add
automatic network fallback, external APIs, model downloads, or hidden profile
updates.

## Architecture

Language profiles are compiled into the library as static seed strings.
Initialization trains each profile into a fixed-capacity trigram frequency
table. Detection normalizes input once, scores it against every trained profile,
sorts the results descending, and writes the best matches above the threshold.

The CLI is a thin adapter over these public functions. It builds no intermediate
JSON and calls the C API directly. Programmatic callers can dlopen the shared
library and use the same functions without a runner or subprocess.

## Normalization and Profiles

Normalization copies input into an allocation no larger than the original byte
length. It lowercases ASCII and a limited set of Cyrillic and Greek uppercase
sequences, collapses ASCII punctuation and whitespace to one space, and
otherwise preserves UTF-8 byte sequences.

This is intentionally basic normalization. It does not validate complete UTF-8,
apply Unicode normalization forms, or perform full case folding. Do not silently
replace it with locale-dependent behavior.

Profile changes alter classification behavior globally. Each seed addition or
change requires concrete representative and confusion tests, especially
between related languages.

Do not add runtime training, user profile uploads, databases, or generic model
formats by default.

## Initialization

POSIX uses `pthread_once`; Windows uses `InitOnceExecuteOnce`. `kc_lng_init()`
is idempotent and thread-safe, and detection functions call it automatically.

The once boundary permits concurrent detection after profiles are constructed
without locks in the scoring path.

## Trigrams and Scoring

Profiles and inputs are traversed in units inferred from UTF-8 lead bytes. Every
three-character sequence is encoded into a fixed 12-byte gram. Each language
profile stores at most 2048 distinct grams.

Each language scores a smoothed log sum of matching gram counts. Inputs with no
trigrams or fewer than one percent matching trigrams score zero. The average log
value is mapped through a logistic function into `[0, 1]`.

The output is a ranking score shaped for practical thresholding. It is not a
probability or statistically calibrated confidence.

## Ranking

`kc_lng_detect_top()` scores all compiled languages, sorts descending, and writes
up to the caller's requested count whose scores meet the threshold.

`kc_lng_detect()` requests one result at threshold `0.001` and returns its static
code or NULL.

`kc_lng_detect_ctx()` ranks the same way but checks context stop state between
scores and returns no results after cancellation.

## Context and CLI

Contexts copy scalar options and own stop state. The context limit is normalized
into `[1, 32]`.

The CLI accepts one positional text or reads all stdin. Limit one prints only
the code; larger limits print `code: score` with four decimal places. Empty
input produces no output.

Defaults are threshold `0.001` and limit `1`. The CLI resolves these from
defaults and flags only. Library callers may apply `KC_LNG_*` environment
overrides through `kc_lng_options_load_env()`.

## Public API and Ownership

Treat `src/liblng.h` as a compatibility boundary.

Caller-provided result arrays remain caller-owned. Result code strings are
static and require no release. Contexts copy scalar options and own stop state.
Detection does not retain input.

`kc_lng_init()` is idempotent and thread-safe. Once initialized, profile state
must remain read-only. Do not introduce mutable global detection state.

## Source Layout

Preserve the existing `src/` structure:

- `src/lng.c` contains the CLI;
- `src/liblng.c` contains profiles and detection;
- `src/liblng.h` contains the public contract;
- `src/test.c` contains all tests.

The CLI calls the public C API directly. The public header exposes no runner or
JSON type.

Do not create additional source, header, profile, generated, or test files.
Keep new profiles in `liblng.c` and every test in `test.c`. Do not create files
such as `profiles.c`, `test_accuracy.c`, or per-language data files.

## One-Shot Processing

The CLI accepts one positional text or reads all available stdin at once.

Do not add a daemon, language server, HTTP API, batch platform, or generic
control socket.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- neural models or embeddings;
- external language-detection libraries;
- model downloads or update services;
- runtime training or adaptive profiles;
- user-supplied profile databases;
- translation or transliteration;
- locale negotiation;
- dialect, demographic, or identity inference;
- corpus collection;
- telemetry or analytics;
- remote APIs, SaaS, or cloud inference;
- plugin systems;
- distributed or GPU processing;
- a generic control socket.

Do not justify changes through AI trends, benchmark competition, enterprise
scale, or hypothetical corpus growth.

## Change Evaluation

Before changing detection, identify exact input texts, expected language codes,
rank order, threshold, and score relationships. Check related-language
confusion, short text, mixed scripts, normalization effects, profile capacity,
initialization safety, and platform determinism.

Reject speculative profile expansion and generic model abstractions.

## Resource Model

Compiled profiles have fixed capacities. Initialization trains them once from
embedded seeds. Detection allocates normalized text while scoring each language
and uses a fixed rank array.

The CLI buffers all available stdin and caps printed results at 32. There is no
network, filesystem state, cache service, or background worker.

The implementation is portable C11 and links native once-control and math
facilities. Classification semantics must remain platform-independent.

## Stop State and Concurrency

Read-only initialized profiles are safe for concurrent detection. Context stop
state does not imply that one context may be mutated concurrently without
coordination.

## Testing

Behavioral changes require tests for every affected language, related-language
confusion, ASCII and non-ASCII case handling, punctuation normalization, short
and empty text, threshold filtering, deterministic ranking, result limits,
concurrent initialization, context cancellation, and CLI formatting.

All tests stay in `src/test.c`.

## Build and Completion

For documentation-only changes, run `kcs AGENTS.md`. For source
changes, use `README.md` build and test commands. Do not run `make clean`
without authorization.

A change is complete when concrete detection behavior improves without
misrepresenting scores, initialization remains safe, profiles remain local and
fixed, tests cover regressions, and no external AI platform was introduced.

The goal is a small useful heuristic detector, not universal language
understanding.
