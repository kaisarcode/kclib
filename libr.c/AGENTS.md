# AGENTS.md

## Project Context

`libr.c` is the reference blueprint for small, independent C libraries and CLI
tools in kclib.

It provides a concrete starting structure:

- a reusable C library;
- a public header;
- a thin CLI over the public API;
- explicit options and context ownership;
- optional resident stdin processing with byte delimiters;
- static and shared library artifacts;
- portable public-contract tests;
- native and cross-platform build targets.

The blueprint is meant to be copied and adapted into one-purpose projects. It
is not a common runtime that every kclib must depend on.

Read `README.md` for the effective interface before modifying the project.

## Required Mindset

Treat the blueprint as a compact example of the common kclib form.

Do not optimize it toward:

- enterprise application development;
- framework adoption;
- managed services;
- cloud deployment;
- hyperscale operation;
- organization-wide standardization;
- generic extensibility;
- centralized administration;
- runtime orchestration;
- ecosystem growth.

Do not assume that a blueprint becomes more useful by accumulating common
features. Every added facility increases the code inherited by future
projects.

Prefer deletion, direct adaptation, and project-local code over expanding
`libr.c` into a universal base layer.

## Core Invariants

Preserve these properties unless the project owner explicitly instructs
otherwise:

- the reusable library owns core behavior;
- the CLI only adapts arguments, stdin, stdout, stderr, and process exit status;
- the public header is the library contract;
- each opened context is independently caller-owned;
- options and context allocations have explicit cleanup;
- built-in defaults are overridden by environment values and then CLI flags;
- stdin residency uses an explicit byte delimiter;
- stdout responses use the same explicit delimiter;
- diagnostics go to stderr;
- the CLI, static library, and shared library remain normal build artifacts;
- tests exercise the public library and CLI contracts;
- the blueprint remains portable C11;
- network access and external services are not required;
- the implementation remains small enough for one person to inspect.

The placeholder verbs, parameter, and empty core operation are examples to be
replaced by a concrete project. They are not an invitation to build generic
storage, dispatch, command routing, or service infrastructure into the
blueprint.

## Blueprint Boundaries

`libr.c` demonstrates structure, lifecycle, and process composition.

It must not become:

- a general-purpose application framework;
- a common kclib ABI;
- a mandatory dependency of other kclib repositories;
- a plugin host;
- a dependency injection container;
- a service locator;
- an RPC framework;
- a daemon supervisor;
- a job scheduler;
- a workflow engine;
- a configuration framework;
- a logging framework;
- a telemetry runtime;
- a network protocol;
- a database abstraction;
- a package manager;
- a hosted control plane.

When a new kclib needs project-specific behavior, implement that behavior in
the new repository. Do not first generalize `libr.c` unless the behavior is
part of the blueprint itself and has an existing need across its basic form.

## Resident Processing

Resident mode is a Unix composition mechanism for avoiding repeated process
startup when useful.

Its contract is intentionally small:

- requests arrive through stdin;
- one configured byte separates requests;
- responses are separated by the same byte on stdout;
- stdout is flushed after each response;
- EOF terminates processing;
- no broker, registry, socket service, or remote endpoint is involved.

Do not reinterpret resident mode as the beginning of a process platform.

Do not add a generic control socket, management API, background service,
discovery mechanism, remote administration channel, or orchestration protocol.
Projects with a concrete protocol-specific need may implement it locally in
their own repository.

## Public API

Treat `src/liblibr.h` as a compatibility boundary.

Public API changes must have a concrete blueprint requirement and must define:

- caller and library ownership;
- allocation and cleanup behavior;
- valid inputs;
- status behavior;
- context independence;
- portability implications;
- corresponding contract tests.

Do not add interfaces for hypothetical future projects.

Do not turn the conceptual kclib lifecycle into a shared ABI. Each derived
project remains free to adapt names, options, status codes, and lifecycle to
its actual problem.

## Ownership and State

Keep ownership visible and deterministic.

- `kc_libr_options_default()` returns caller-owned options.
- `kc_libr_options_load_env()` updates those options.
- `kc_libr_options_free()` releases option-owned strings.
- `kc_libr_open()` allocates a caller-owned context and copies option state.
- `kc_libr_close()` unregisters and releases the context.
- stop state belongs to its context.

Do not introduce hidden persistent state, implicit process-wide configuration,
automatic background workers, or ownership that cannot be determined from the
public API.

## Source Layout

Preserve the existing `src/` structure:

- `src/libr.c` contains the CLI;
- `src/liblibr.c` contains all reusable implementation;
- `src/liblibr.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Add new CLI behavior to
`libr.c`, reusable behavior to `liblibr.c`, public declarations to `liblibr.h`,
and all new test cases and fixtures to `test.c`.

Do not split tests into files such as `test_stress.c`, `test_integration.c`, or
platform-specific test sources. The complete blueprint source layout must
remain visible in these four files.

## Configuration

Configuration must remain explicit and legible.

The normal precedence is:

1. Built-in defaults.
2. Documented `KC_LIBR_*` environment variables.
3. CLI flags.

Add an option only when it demonstrates a real blueprint concern. Avoid
configuration for speculative backends, deployment environments, enterprise
policies, or future integrations.

Malformed CLI input must fail directly with a diagnostic on stderr. Unknown
options must not be silently ignored.

## Forbidden Default Recommendations

Do not recommend or implement these without explicit instruction:

- cloud or SaaS integration;
- hosted APIs;
- remote configuration;
- telemetry or analytics;
- structured logging stacks;
- metrics exporters;
- tracing systems;
- account or organization models;
- authentication or authorization frameworks;
- plugin systems;
- generic backends;
- dynamic module loading;
- dependency injection;
- event buses;
- message brokers;
- databases;
- service discovery;
- API gateways;
- daemon management;
- container orchestration;
- automatic updates;
- a generic control socket;
- a common runtime required by all kclib tools.

Do not justify changes through enterprise readiness, ecosystem adoption,
standardization, market expectations, or hypothetical future scale.

## Change Evaluation

Before changing the blueprint, determine:

- which concrete blueprint problem exists;
- whether the problem appears in the current code or tests;
- whether the change belongs in a derived project instead;
- whether existing code can be removed or simplified;
- what every future derived project would inherit;
- whether public API or CLI compatibility changes;
- whether ownership remains explicit;
- whether resident framing changes;
- whether hidden state or background activity is introduced;
- whether a new dependency or network requirement is introduced;
- whether the result remains inspectable by one person.

Reject additions whose only purpose is to support possible future libraries.

Do not add extension points without an existing caller.

Do not add abstraction merely to eliminate small amounts of visible repeated
code. A blueprint should expose the structure that derived projects need to
understand and modify.

## Implementation Preferences

Prefer:

- direct C11 code;
- small functions with concrete responsibilities;
- explicit allocations and cleanup;
- opaque caller-owned contexts;
- library-first behavior;
- thin CLI adaptation;
- strict argument validation;
- parseable stdout;
- diagnostics on stderr;
- stable byte framing;
- portable system boundaries;
- public-contract tests;
- removal of obsolete example facilities.

Avoid:

- framework-style architecture;
- generic registries;
- unbounded background work;
- hidden allocation;
- implicit ownership;
- recursive dispatch;
- macro metaprogramming for hypothetical variants;
- generated configuration layers;
- callbacks or interfaces with no concrete use;
- platform abstractions that hide materially different behavior;
- dependencies added only for convenience.

## Stop Behavior

Stop state is context-local. `kc_libr_stop()` marks one context for
cooperative termination.

Changes in this area must preserve:

- independent context stop state;
- deterministic cleanup.

The blueprint does not promise thread-safe concurrent access. A derived project
must define and test concurrency only when its concrete operation requires it.
Do not add locks, worker pools, or synchronization frameworks preemptively.

## Testing

Every behavioral change must update or add tests.

Tests should cover the shipped contract, including:

- default options;
- environment loading;
- option cleanup;
- context allocation and release;
- independent contexts;
- stop behavior;
- build version reporting;
- core operation validation;
- malformed CLI input;
- environment and CLI precedence;
- multiple stdin requests;
- custom delimiter framing;
- exact stdout and stderr behavior.

Tests should link against the generated shared library when validating the
distributed public API. Do not weaken tests to accommodate an implementation
change.

## Build and Validation

Use the repository build system.

Typical validation is:

```bash
kcs .
make
make test
```

When Windows behavior is affected:

```bash
make x86_64/windows
make test wine
```

Treat compiler warnings as failures.

Do not run `make clean` or delete build artifacts without authorization.

Cross-compilation verifies compilation only. Do not claim runtime portability
for a target that was not run.

## Documentation

Keep documentation operational and truthful.

Use:

- `README.md` for the effective CLI, API, build, test, requirements, status,
    and license contract;
- `AGENTS.md` for implementation constraints and agent behavior.

Do not turn documentation into a product roadmap.

Do not describe the absence of enterprise facilities as incompleteness.

## Completion Standard

A change is complete when:

- the concrete blueprint requirement is met;
- the library and CLI boundary remains clear;
- ownership and cleanup remain explicit;
- resident framing remains deterministic;
- public and CLI contracts are tested;
- relevant builds and tests pass;
- documentation matches actual behavior;
- no unrelated platform or enterprise machinery was introduced;
- the blueprint remains small, direct, portable, and easy to adapt.

The goal is not to make `libr.c` capable of every future project.

The goal is to preserve a sharp starting point from which unnecessary example
parts can be removed and one concrete library can be built.

## Design Summary (from DESIGN.md)

### Purpose

`libr.c` is the reference blueprint for new kclib libraries and command-line
tools. It demonstrates a common project form rather than a common runtime:

- reusable behavior in a C library;
- a public C header as the contract;
- a thin CLI built over that public API;
- explicit options, context ownership, and cleanup;
- optional multi-request processing through stdin and stdout;
- portable builds for an executable, static library, and shared library;
- tests against public artifacts.

A derived project copies and adapts this structure for one concrete purpose.
It does not need to retain example fields or interfaces that do not fit that
purpose.

### Operating Model

The intended operator builds and runs the tool locally or links the library
directly into another local program. The project requires no account,
subscription, hosted API, remote control plane, package registry, or permanent
service infrastructure.

### Repository Form

The blueprint separates responsibilities into four source files:

- `src/liblibr.h` defines the public API and ownership contract.
- `src/liblibr.c` implements reusable lifecycle and core behavior.
- `src/libr.c` adapts process arguments and streams to the public API.
- `src/test.c` validates the portable public and CLI contracts.

`CMakeLists.txt` defines the build graph. `Makefile` provides the operator-facing
native and cross-compilation commands.

This separation is architectural. Reusable behavior belongs in the library;
the CLI should not become an alternative implementation.

### Library Lifecycle

The public lifecycle is explicit:

1. `kc_libr_options_default()` creates caller-owned default options.
2. `kc_libr_options_load_env()` applies documented environment overrides.
3. `kc_libr_open()` allocates a context and copies its option state.
4. `kc_libr_exec()` performs the project-specific core operation.
5. `kc_libr_close()` unregisters and releases the context.
6. `kc_libr_options_free()` releases allocations owned by the caller's options.

The options and the opened context own separate copies of string values. The
caller may release its options after opening without transferring their
storage to the context.

The opaque context keeps implementation state private while leaving allocation
and release visible at the API boundary.

### Core Operation

`kc_libr_exec()` is the intentional adaptation point for a derived project.
The blueprint implementation validates the context and input and otherwise
performs no operation. This emptiness is deliberate: the blueprint does not
pretend to provide a generic domain operation.

A derived project replaces this function and its example API surface with the
smallest interface required by its actual purpose.

The placeholder `set` and `get` CLI verbs and `param` option illustrate where
project-specific command and option handling may exist. They do not define a
storage model or generic command dispatcher.

### Configuration

Configuration has three explicit layers:

1. Built-in defaults initialize the options structure.
2. Documented environment variables override defaults.
3. CLI flags override environment values.

String options are owned allocations and are released through
`kc_libr_options_free()`.

### CLI Boundary

The CLI performs process-specific adaptation:
- parses verbs, positional input, and flags;
- loads environment configuration;
- opens and closes the library context;
- reads requests from stdin;
- invokes the public core operation;
- writes response delimiters to stdout;
- writes diagnostics to stderr;
- converts library status into process exit status.

Core reusable behavior must not be implemented only in the CLI. Library users
must not need to emulate CLI parsing to obtain the operation.

Unknown or malformed options fail directly. The CLI does not silently select
fallback behavior.

### Stream Framing

The default request delimiter is EOT, byte value 4. `--until N` selects another
byte from 0 through 255.

In stdin mode, the CLI:
1. reads bytes until the configured delimiter or EOF;
2. passes each non-empty request to `kc_libr_exec()`;
3. writes the same delimiter after each successful response;
4. flushes stdout;
5. continues until EOF.

When stdout is a terminal, the CLI adds a newline after the response delimiter
for operator readability. Redirected and piped stdout remains byte-exact.

Empty framed requests are ignored. A final non-empty request ending at EOF is
processed before termination.

This is a local byte-stream protocol. It is intentionally not JSON-RPC, HTTP,
a message broker protocol, or a remote service interface.

### Resident Execution

Resident execution exists to process multiple requests without repeated
process startup. The process remains an ordinary foreground Unix-style filter:
- stdin is the request channel;
- stdout is the result channel;
- stderr is the diagnostic channel;
- EOF ends the process;
- signals request process-level interruption.

### Artifacts

The build produces three normal artifacts from the same library source:
- `libr`, the CLI executable;
- `liblibr.a`, the static library;
- the platform shared library, such as `liblibr.so` or `libr.dll`.

The executable embeds the library implementation for a direct standalone
binary. Static and shared artifacts allow ordinary C linkage.

The blueprint does not define a package service, runtime loader, plugin ABI, or
automatic distribution mechanism.

### Tests

The portable contract tests cover:
- option defaults, environment loading, and cleanup;
- build version reporting;
- context allocation, independence, and release;
- core operation input validation;
- context-local stop state;
- CLI help, version, validation, and precedence;
- default and custom multi-request framing.

The normal test build imports the generated shared library and invokes the
generated CLI. This checks distributable artifacts instead of compiling the
implementation privately into the contract test.

Windows-through-Wine tests use the corresponding generated Windows artifacts.
Skipped runtime cases are not evidence that the skipped behavior works on that
platform.

### Portability

The project targets portable C11 and uses explicit platform branches where
process or terminal APIs differ.

The Makefile exposes named native and cross-compilation targets. Artifacts are
placed under `bin/{arch}/{platform}/` so target identity remains visible.

Portability supports local use on heterogeneous hardware. It is not a promise
of equal runtime validation on every listed target.

Platform differences must not silently change the public ownership model,
configuration precedence, or stream framing.

### Dependency Model

The blueprint uses the C standard library and small native system interfaces.
Its build may link normal platform libraries such as pthread and libm where the
target requires them.

There is no runtime network dependency and no mandatory external service.

Dependencies added to the blueprint propagate into every project derived from
it. New dependencies therefore require a concrete structural need that cannot
be met clearly with the existing platform facilities.

### Composition

The CLI composes through:
- positional arguments;
- stdin and stdout;
- explicit delimiter bytes;
- stderr diagnostics;
- process exit status.

The library composes through ordinary C calls and explicit context ownership.

These boundaries are sufficient for the blueprint. Higher-level composition
belongs in shell scripts, dedicated kclib tools, or the application embedding
the library.

### Resource Model

The blueprint keeps state local to options, contexts, and request buffers.
One stdin request is buffered in memory before execution. Derived projects that
accept large or adversarial input must define and enforce their own appropriate
limits rather than assuming unbounded input is harmless.

Context allocations and option strings have explicit release functions.
Background queues, worker pools, caches, persistent databases, and remote state
are absent.

The blueprint does not define thread-safe concurrent use of contexts. A derived
project must establish its own concurrency contract only when the concrete
operation needs one.

### Inspectability

One person should be able to follow the complete path:
1. CLI configuration is initialized and overridden.
2. A context copies the effective options.
3. One argument or framed stdin request reaches `kc_libr_exec()`.
4. The response boundary is emitted.
5. Context and option storage are released.

New architecture must not obscure this path behind generic dispatch,
reflection, generated bindings, dependency injection, or runtime discovery.

### Non-Goals

`libr.c` is not intended to provide:
- a common runtime dependency for all kclib projects;
- a general application framework;
- a plugin ecosystem;
- remote procedure calls;
- network transport;
- service discovery;
- daemon supervision;
- workflow orchestration;
- persistent application state;
- database access;
- generic authentication or authorization;
- account or organization management;
- remote configuration;
- telemetry, analytics, tracing, or metrics;
- hosted infrastructure;
- cloud deployment abstractions;
- automatic updates;
- enterprise administration;
- universal configuration or logging systems.

These omissions keep inherited code small and adaptable. They are not an
unfinished product roadmap.

### Change Criteria

A proposed blueprint change should answer:
1. What concrete problem exists in the blueprint itself?
2. Does the change belong in one derived project instead?
3. What code would every future derived project inherit?
4. Can existing blueprint code be simplified or removed instead?
5. Does the public API still state ownership clearly?
6. Does the CLI remain a thin adapter?
7. Does stdin and stdout framing remain explicit and deterministic?
8. Does the change introduce hidden state, background work, or networking?
9. Does it add a dependency or operational requirement?
10. Can one person still inspect the complete execution path?

Changes justified mainly by enterprise readiness, generic extensibility,
ecosystem expectations, managed operation, or hypothetical future scale should
be rejected.

### Core Invariants

The following properties define the blueprint:
- library-first reusable behavior;
- a thin CLI over the public API;
- explicit options and context ownership;
- deterministic cleanup;
- defaults overridden by environment and then CLI;
- explicit stdin and stdout byte framing;
- local foreground execution;
- CLI, static library, and shared library artifacts;
- public-contract tests against built artifacts;
- portable native C;
- no mandatory network or hosted service;
- no common kclib runtime or framework;
- project-local adaptation over speculative generalization;
- code small enough for one operator to understand and change.

These constraints make `libr.c` useful as a blueprint. Expanding it into a
platform would destroy the property it exists to demonstrate.