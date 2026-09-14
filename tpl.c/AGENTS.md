# AGENTS.md

## Project Context

`tpl.c` is a small C library and stdin/stdout CLI for rendering string-based
templates.

It provides escaped and raw interpolation, local includes, scoped variables,
named blocks, conditionals, and bounded list iteration. It is intended for
local composition and embedding in small native programs.

It is not a web framework, application runtime, scripting language, or content
management system.

Read `README.md` for effective syntax before modifying the project.

## Required Mindset

Keep the renderer direct, deterministic, and understandable as one small
component.

Do not optimize it toward:

- enterprise web application development;
- general-purpose programming;
- framework compatibility;
- hosted rendering;
- collaborative content systems;
- plugin ecosystems;
- theme marketplaces;
- remote template storage;
- runtime orchestration;
- universal data binding.

Additional syntax is a permanent language commitment. Add it only for a
concrete template that cannot be expressed clearly with the existing model.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- the library owns rendering behavior;
- the CLI only adapts options, stdin, stdout, stderr, and exit status;
- variables are strings;
- missing variables resolve to empty strings;
- normal interpolation HTML-escapes values;
- triple-brace interpolation emits raw values deliberately;
- includes read local files relative to one configured root;
- scopes form an explicit parent chain;
- block properties and foreach aliases use child scopes;
- named blocks and list expansion remain bounded;
- output uses a fixed maximum buffer and fails when exceeded;
- rendered output is returned as a caller-owned allocation;
- context-owned errors describe the latest failure;
- no network or persistent service is required;
- the implementation remains portable C11 and inspectable by one person.

## Language Boundary

The template language supports only the documented constructs:

- escaped `{{ expr }}` interpolation;
- raw `{{{ expr }}}` interpolation;
- dot notation mapped to flat underscore keys;
- template comments;
- local includes;
- variable assignment;
- named block definition and rendering;
- inline block properties;
- conditionals with one optional else branch;
- conditionals may combine `!`, `==`, `!=`, `&&`, and `||` operands;
- foreach over comma-separated or bracketed string lists.

Do not add arbitrary expressions, function calls, user-defined
functions, reflection, object models, bytecode, embedded Lua or JavaScript, or
shell execution.

Truthiness remains string-based: empty, `0`, `false`, and `null` are false.

## Escaping and Trust Boundary

Double-brace output escapes `&`, `<`, `>`, and double quotes. Triple-brace
output is intentionally unescaped.

The renderer is not an HTML sanitizer. Raw output transfers trust decisions to
the template author and caller.

Do not silently sanitize raw output or silently make escaped output raw. Either
change would break the language contract.

Templates inside HTML comments are left unevaluated. Preserve this behavior so
commented directives remain literal source.

## Include Boundary

Includes concatenate the configured root and evaluated include path, then read
one local file into a bounded buffer.

The root is a resolution base, not a security sandbox. Current behavior does
not promise path canonicalization or traversal confinement.

Do not claim sandboxing that the implementation does not provide. If a caller
needs untrusted-template isolation, that belongs at the application or process
boundary unless an explicit project requirement changes this contract.

Do not add network includes, package resolution, template registries, remote
fetching, automatic caching, file watching, or distributed template stores.

## Scope and Data Model

Variables are key/string-value pairs. Dot notation resolves flat keys such as
`object_field`, including foreach aliases that point to a flat prefix.

This is not a JSON, YAML, reflection, or object traversal system.

Child scopes are temporary and reference a parent scope. Values assigned to a
child are released when that block or iteration ends. Context-level variables
remain until replaced or the context closes.

Do not introduce hidden global variables or shared process-wide template state.

## Resource Limits

Existing fixed limits are part of the inspectable resource model:

- output buffer: 262144 bytes including termination space;
- evaluated value buffer: 2048 bytes;
- root storage: 4096 bytes;
- include content: at most 8191 bytes;
- blocks per scope: 64;
- block body storage: 8192 bytes;
- block and variable names: at most 63 bytes;
- foreach items: at most 64;
- one foreach item: at most 127 bytes.

Do not replace these with unbounded structures by default. If a concrete use
case requires a limit change, make the behavior explicit and test boundaries.

Nested rendering and includes must not become an uncontrolled recursion or
resource-exhaustion mechanism. Any new nesting behavior requires defined depth
and failure handling.

## Public API and Ownership

Treat `src/libtpl.h` as a compatibility boundary.

- options own their root allocation;
- an opened context copies option-owned root state;
- `kc_tpl_set_var()` copies keys and values into context ownership;
- `kc_tpl_render_string()` returns a caller-owned output allocation;
- `kc_tpl_strerror()` returns static or context-owned text;
- `kc_tpl_close()` releases context variables and state.

Public changes must document ownership, limits, malformed input behavior,
context independence, and tests.

Do not expose internal scopes or add generic extension callbacks without an
existing caller.

## Source Layout

Preserve the existing `src/` structure:

- `src/tpl.c` contains the CLI;
- `src/libtpl.c` contains all reusable implementation;
- `src/libtpl.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Add new CLI behavior to
`tpl.c`, reusable behavior to `libtpl.c`, public declarations to `libtpl.h`,
and all new test cases and fixtures to `test.c`.

Do not split tests into files such as `test_stress.c`, `test_parser.c`, or
platform-specific test sources. The complete project source layout must remain
visible in these four files.

## Forbidden Default Recommendations

Do not recommend or implement these without explicit instruction:

- a general expression language;
- embedded scripting runtimes;
- shell command execution;
- plugin or filter registries;
- inheritance frameworks;
- JSON, YAML, or database object mapping;
- network or package includes;
- template repositories or marketplaces;
- automatic file watching or hot reload;
- HTTP serving;
- session, account, or permission systems;
- CMS behavior;
- theme systems;
- telemetry, analytics, tracing, or metrics;
- distributed caches;
- cloud storage or hosted rendering;
- orchestration or deployment integration;
- a generic control socket.

Do not justify changes through enterprise readiness, framework expectations,
ecosystem adoption, or hypothetical future scale.

## Change Evaluation

Before changing behavior, determine:

- the concrete template that requires the change;
- whether composition outside the renderer is simpler;
- whether syntax or output compatibility changes;
- whether escaping or raw-output trust changes;
- whether scope lookup and lifetime remain explicit;
- whether include behavior or filesystem exposure changes;
- which fixed resource limits are affected;
- how malformed or unterminated directives fail;
- whether recursion remains controlled;
- whether public ownership or CLI behavior changes;
- whether one person can still inspect the complete rendering path.

Reject speculative syntax and extension points.

## Implementation Preferences

Prefer:

- direct C11 parsing;
- explicit directive branches;
- bounded fixed storage where already used;
- checked dynamic allocation for variables and returned output;
- context-owned error detail;
- explicit parent scopes;
- deterministic child-scope cleanup;
- escaped interpolation by default;
- local file includes only;
- public-contract tests.

Avoid:

- generic language runtimes;
- opaque AST frameworks;
- hidden global state;
- unbounded queues, blocks, or output;
- implicit ownership;
- callbacks without a current implementation need;
- platform-specific syntax behavior;
- silent truncation where failure is required;
- dependencies added only for convenience.

## Signals and Concurrency

Stop state belongs to one context. The library does not install or dispatch
operating-system signals.

The library does not promise thread-safe concurrent use. Do not add
synchronization without a concrete requirement and defined resulting contract.

## Testing

Behavioral changes should test:

- escaped and raw interpolation;
- missing variables and string truthiness;
- variable replacement and scope shadowing;
- flat dot notation and foreach aliases;
- nested conditionals and loops;
- block definition, rendering, props, and limits;
- local includes and include failures;
- comments and directives inside HTML comments;
- malformed and unterminated tags;
- output, include, list, name, and block boundaries;
- context error details;
- caller ownership of rendered output;
- CLI behavior and precedence.

Do not weaken exact output assertions to accommodate a language change.

## Build and Documentation

For documentation-only changes, run:

```bash
kcs AGENTS.md
```

For source changes, use the build and test commands documented in `README.md`.
Do not run `make clean` or delete build artifacts without authorization.

Use `README.md` for effective syntax and interfaces, and `AGENTS.md` for
implementation constraints.

## Completion Standard

A change is complete when the concrete template works, existing syntax remains
compatible unless explicitly revised, ownership and cleanup are correct,
limits and errors are explicit, relevant behavioral tests pass, documentation
matches implementation, and no unrelated application platform was added.

The goal is not a universal template language.

The goal is a small renderer with clear local composition and predictable
limits.

## Pure C API Architecture

The library exposes a pure C API through `src/libtpl.h`. There is no JSON runner,
no `kc_tpl_run` function, and no embedded JSON parser. The runner functionality
has been migrated to the shared `kcrun` library, which provides a generic
`kcr_call` host face for bridges (wvw, jni) to load and invoke kclibs via
`dlopen`/`dlsym`.

The public API consists of:
- Options lifecycle: `kc_tpl_options_default`, `kc_tpl_options_load_env`, `kc_tpl_options_free`
- Context lifecycle: `kc_tpl_open`, `kc_tpl_close`, `kc_tpl_stop`
- Configuration: `kc_tpl_set_root`, `kc_tpl_set_var`
- Core operation: `kc_tpl_render_string`
- Introspection: `kc_tpl_version`, `kc_tpl_strerror`

The CLI (`src/tpl.c`) calls the C API directly. It does not use the JSON runner.
Tests exercise the public C API only. The vendored `parson` JSON parser has been
removed.

Bridges (wvw, jni) use the kcrun host face (`kcr_load`, `kcr_call`,
`kcr_whitelist`) to load `libtpl.so` and invoke its standard C API symbols.
