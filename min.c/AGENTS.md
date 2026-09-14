# AGENTS.md

## Project Context

`min.c` is a small C library and stdin/stdout CLI for conservative CSS,
JavaScript, and HTML minification.

It removes comments and unnecessary whitespace while preserving common runtime
behavior. It is a local asset filter, not a web build platform.

Read `README.md` for effective behavior before modifying the project.

## Required Mindset

Correctness is more important than maximum byte reduction.

Do not optimize the project toward bundling, transpilation, semantic rewriting,
framework integration, enterprise asset pipelines, hosted builds, or exhaustive
language-standard coverage.

When uncertain whether a byte can be removed safely, preserve it.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- CSS, JavaScript, and HTML remain separate explicit modes;
- the library owns minification behavior;
- the CLI only adapts mode, framing, streams, diagnostics, and exit status;
- input is a null-terminated string;
- output is a caller-owned allocation released with `kc_min_free()`;
- quoted strings are preserved;
- JavaScript template literals and regex literals are preserved;
- CSS comments and safe whitespace are removed conservatively;
- HTML comments are removed outside preserved regions;
- `<pre>` and `<textarea>` content remains verbatim;
- spaces with possible token or inline-layout significance remain;
- empty input produces an owned empty string;
- no filesystem, network, cache, or external service is required;
- the implementation remains portable C11 and directly inspectable.

## Architecture

`kc_min_exec()` selects one scanner from the context mode. Each scanner reads a
null-terminated source string and grows a separate output allocation.

The scanners maintain only lexical state needed by their mode. They do not
build syntax trees, resolve dependencies, or execute source.

The CLI selects a mode, reads all available stdin, calls the public API once,
writes the owned result to stdout, then releases both input and output.

## Lifecycle and Ownership

Default options select CSS mode. Environment values may override mode; the CLI
requires an explicit named mode.

`kc_min_open()` allocates a context and copies scalar options. `kc_min_exec()`
returns a caller-owned string, including for empty output. `kc_min_free()` is
the matching release operation. The context retains neither input nor output.

## Mode Boundaries

Each mode is a dedicated scanner with small explicit state. Do not merge the
three languages into a generic token framework.

### CSS Scanner

CSS mode tracks quote and escape state. Outside strings it:

- removes `/* */` comments;
- collapses whitespace;
- removes spaces around selected punctuation;
- removes a trailing semicolon before a closing brace;
- removes alphabetic or percent units from syntactically isolated zero values;
- preserves spacing where punctuation and calculations may require it.

It does not parse selectors, declarations, values, custom properties, URLs, or
the CSS grammar semantically.

### JavaScript Scanner

JavaScript mode tracks quotes, escapes, regex literals, regex character
classes, pending whitespace, and the preceding significant byte.

It removes line and block comments outside protected regions. Single quotes,
double quotes, and backtick literals are copied byte-for-byte.

A slash begins a regex when the preceding significant byte is absent or belongs
to a small expression-leading set. Regex classes and alphabetic flags are then
preserved. Otherwise the slash remains ordinary source, including division.

This is a deliberate lexical heuristic, not a complete ECMAScript parser.

### HTML Scanner

HTML mode removes `<!-- -->` comments outside preserved regions, collapses
whitespace, and copies tags while respecting quoted attributes.

Entering `<pre` or `<textarea` switches to verbatim copying until the matching
closing tag begins. Spaces near tags and text are retained conservatively to
avoid changing inline layout.

The scanner does not build a DOM, validate nesting, decode entities, or apply
HTML optional-tag rules.

## Correctness Boundary

Do not add transformations that rename identifiers, fold expressions, reorder
rules, rewrite selectors, normalize URLs, merge declarations, remove optional
tags, change quote styles, or infer semantic equivalence.

`min.c` does not validate source. Malformed or ambiguous input is processed by
the current scanner rules. Do not claim parser-level correctness or silently
turn the tool into a validator.

The scanners are best-effort lexical transforms. Unterminated comments,
strings, regexes, or tags follow scanner state until input ends.

Every byte-removal rule needs adversarial tests for strings, escapes, comments,
operators, regexes, tags, and whitespace boundaries.

## Public API and Ownership

Treat `src/libmin.h` as a compatibility boundary.

- contexts are caller-owned after `kc_min_open()`;
- mode is context-local;
- `kc_min_exec()` allocates output for the caller;
- `kc_min_free()` releases library output;
- input is borrowed only for the call;
- contexts do not retain input or output.

Public changes must define valid modes, ownership, error behavior, and tests.
Do not expose scanner internals or generic token callbacks.

## Source Layout

Preserve exactly:

- `src/min.c` contains the CLI;
- `src/libmin.c` contains all reusable implementation;
- `src/libmin.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Add CLI behavior to
`min.c`, reusable behavior to `libmin.c`, public declarations to `libmin.h`, and
all test cases and fixtures to `test.c`.

Do not create files such as `test_stress.c`, `css.c`, `js.c`, or `html.c`. The
complete implementation and contract tests must remain visible in the existing
four-file form.

## One-Shot Processing

The CLI reads all available stdin, minifies once, writes the result, and exits.

Do not add a daemon, watcher, build server, remote API, job queue, or generic
control socket.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- asset bundling or dependency graphs;
- transpilation or polyfills;
- source maps;
- identifier mangling;
- semantic optimizers;
- full CSS, JavaScript, or HTML parser dependencies;
- framework-specific transforms;
- plugin systems;
- filesystem crawling or file watching;
- build manifests or project configuration;
- HTTP serving or CDN integration;
- remote build services;
- persistent caches;
- parallel worker farms;
- telemetry, analytics, tracing, or metrics;
- cloud storage or deployment;
- a generic control socket.

Do not justify changes through enterprise readiness, benchmark competition,
ecosystem parity, or hypothetical scale.

## Change Evaluation

Before changing a scanner, determine:

- the concrete source that is currently mishandled;
- whether preserving more input is safer than adding inference;
- which lexical state recognizes the case;
- whether strings, escapes, comments, regexes, or preserved HTML regions are
    affected;
- whether output remains runtime-equivalent for tested inputs;
- whether malformed input behavior changes;
- whether memory ownership or CLI behavior changes;
- whether a dependency or hidden operational state is introduced;
- whether one person can still follow the scanner.

Reject speculative optimizations and generic parser abstractions.

## Implementation Preferences

Prefer direct single-pass scanners, explicit state flags, checked dynamic
output growth, conservative whitespace retention, deterministic cleanup, and
exact output tests.

Avoid AST frameworks, recursive parsers, hidden global scanner state,
unbounded auxiliary structures, implicit ownership, language-server
dependencies, and platform-specific minification behavior.

## Resource Model

Output capacity starts at 4096 bytes and doubles as required. Empty output gets
a one-byte allocation.

Memory use grows with the complete input and output sizes. There is no hidden
truncation, fixed input limit, background work, persistent state, or external
resource use.

Do not add hidden truncation. A future explicit size limit must be documented
and tested as public behavior.

## Stop State and Concurrency

Stop state is context-local. Applications own signal handling and may request a
context stop through `kc_min_stop()`.

The library does not promise thread-safe concurrent access. Add synchronization
only for a concrete requirement.

## Composition

The library composes through direct C calls and explicit output ownership. The
CLI composes through stdin, stdout, stderr, and exit status. File discovery,
bundling, deployment, serving, caching, and orchestration belong in other
tools.

## Testing

Behavioral changes require exact tests for all affected modes. Cover comments,
quotes, escapes, template literals, regex classes and flags, division versus
regex ambiguity, CSS zero units and calculations, HTML attributes, inline text,
preserved elements, malformed source, empty output, allocation ownership,
and CLI validation.

Do not weaken expected output to make an aggressive transform pass.

## Build and Documentation

For documentation-only changes, run `kcs .`.

For source changes, use the build and tests documented in `README.md`. Do not
run `make clean` or delete build artifacts without authorization.

## Completion Standard

A change is complete when the concrete source is minified safely, existing
output remains compatible unless explicitly revised, ownership and cleanup are
correct, relevant exact tests pass, documentation matches implementation, and
no unrelated asset platform was introduced.

The goal is not the smallest output at any cost. The goal is a small,
conservative local minifier.