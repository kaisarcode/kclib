# AGENTS.md

## Project Context

`ngram.c` is a small C library and CLI for descending sliding-window traversal
over byte-delimited text tokens.

It emits spans from larger windows to smaller windows. A synchronous visitor
may close one span, causing contained windows to be skipped, or abort traversal.

The CLI can print chunks or run one operator-supplied command for each chunk.
It is not an NLP framework, search platform, or distributed job system.

Read `README.md` before modifying the project.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- tokenization uses a caller-provided set of separator bytes;
- chunks reference byte offsets in the original input;
- traversal proceeds from maximum window size down to minimum size;
- windows of equal size proceed from left to right;
- visitor callbacks run synchronously in the calling thread;
- callback `0` keeps traversal open;
- callback `1` closes that span and suppresses contained spans;
- callback `-1` aborts with error;
- `KC_NGRAM_ESTOP` propagates cooperative cancellation;
- closed-span storage remains local to one execution;
- the library does not execute commands;
- the CLI prints each chunk before evaluating `--cmd`;
- command stdout presence, not content or exit code, closes the span;
- command execution does not invoke a shell;
- no network, model, database, or persistent state is required;
- the implementation remains portable C11 and inspectable.

## Traversal Boundary

`ngram.c` enumerates spans. It does not decide semantic similarity, relevance,
language boundaries, sentence structure, Unicode words, stemming, ranking, or
classification.

Separators are bytes, not Unicode character classes. A chunk includes the
original bytes from its first token start through its last token end, including
separator bytes between those tokens.

Do not silently replace byte tokenization with locale, regex, Unicode, or model
tokenization.

## Span Closure

A closed span suppresses only candidate windows fully contained within it.
Closed spans are kept sorted and redundant contained entries are pruned.

Do not reinterpret closure as deduplication, global filtering, overlap removal,
ranking, or early termination of unrelated spans.

Changes to traversal order or closure semantics are public behavioral changes
and require exact sequence tests.

## Command Boundary

`--cmd` belongs to the CLI, not the library. The CLI parses a command string
into argv, starts one process per emitted chunk, writes the chunk and newline to
its stdin, drains stdout, and closes the span when any stdout was produced.

No shell is invoked. Shell expansion, pipelines, redirection, environment
management, retries, concurrency, scheduling, and command persistence are not
part of the contract.

Do not move process execution into `libngram` or add a worker pool, queue,
daemon, remote executor, or generic command framework.

## Public API and Ownership

Treat `src/libngram.h` as a compatibility boundary.

The input and separator strings are caller-owned. Chunk pointers and offsets
are valid only during synchronous traversal of that input. The visitor must not
assume the chunk owns or copies bytes.

`kc_ngram_execute()` is reentrant because traversal state is per call.
`kc_ngram_configure()` attaches a mutable caller-owned options pointer to a
context; it does not transfer ownership. Keep this borrowing explicit.

Do not add hidden chunk allocations or callback lifetimes beyond the call.

## Source Layout

Preserve the existing `src/` structure:

- `src/ngram.c` contains the CLI and command execution;
- `src/libngram.c` contains traversal behavior;
- `src/libngram.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Do not split command
code, traversal, stress tests, platform tests, or span tests into new files.
Extend only the existing four files.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- NLP or model tokenizers;
- Unicode segmentation frameworks;
- stemming, ranking, embeddings, or semantic scoring;
- indexes or databases;
- distributed traversal;
- worker pools or parallel callbacks;
- job queues, retries, or command supervision;
- shell evaluation;
- command plugins;
- persistent result storage;
- network APIs or hosted processing;
- telemetry or analytics;
- a generic control socket.

Do not justify changes through enterprise scale, corpus size, AI trends,
framework parity, or hypothetical adoption.

## Change Evaluation

Before changing behavior, determine the exact input, separators, window bounds,
expected chunk order, visitor decisions, and resulting closed spans.

Check whether the change alters byte offsets, original-input borrowing,
callback synchrony, closure containment, command semantics, memory growth, or
cross-platform argv handling.

Reject speculative abstractions. Prefer explicit traversal and process code.

## Resource Model

One execution allocates token offsets proportional to token count and closed
spans proportional to visitor closure decisions. Traversal may emit
quadratically many windows when bounds cover the complete token sequence.

The CLI starts one command process per visited chunk when `--cmd` is set. This
cost is explicit and sequential.

Do not hide these costs behind unbounded concurrency. If limits are needed,
define them as explicit traversal options and public behavior.

## Stop State and Concurrency

Context stop flags are local. Applications own signal handling and may request
a context stop through `kc_ngram_stop()`.

Traversal calls are reentrant with independent per-call state. This does not
promise concurrent mutation of one context or borrowed options.

## Testing

Behavioral changes require exact tests for token offsets, separator sets,
descending order, min and max bounds, max zero behavior, empty input, closure
containment, overlapping closed spans, abort and stop returns, borrowed option
lifetime, command quoting, shell metacharacters as literal argv, stdout-based
closure, and POSIX/Windows behavior where affected.

All tests remain in `src/test.c`.

## Build and Completion

For documentation-only changes, run `kcs .`. For source
changes, use the build and test entry points in `README.md`. Never run
`make clean` without authorization.

A change is complete when traversal order and closure semantics are exact,
ownership remains explicit, relevant tests pass, documentation matches actual
behavior, and no unrelated analysis or execution platform was introduced.

The goal is a sharp traversal primitive, not a universal text-analysis system.
