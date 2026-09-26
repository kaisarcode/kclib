# AGENTS.md

## Kclib

This repository is the public development monorepo for the kclib family.

Read the workspace-level `AGENTS.md` first. These rules add kclib-specific conventions.

Individual kclibs live as project directories under `proj/`, using the
`proj/NAME.c/` form. They are not separate Git repositories.

Repository-wide source-control files such as `.gitignore` and `.kcsignore`
belong at the monorepo root rather than inside each kclib project.

Repository-wide tooling lives under `scripts/`. Generated distribution artifacts
live under `dist/`; that directory is build output and is not versioned.

A kclib is a small, independent native library built around one concrete capability, usually with a thin CLI.

## Project form

Prefer:

* reusable behavior in the library;
* a thin CLI over the public API;
* a compact C-friendly public header;
* explicit ownership, lifetime, errors, and cleanup;
* ordinary native build artifacts;
* independent local use without hosted services or shared runtimes.

Use the current kclib blueprint as the default for naming, layout, lifecycle, CLI style, configuration, build targets, tests, and README structure.

Do not copy blueprint mechanics when the capability is materially different.

Rule:

```text
same concept -> same convention
different concept -> capability-specific design
```

## Consumer-first public ABI

Design the public ABI for kclibs and their supported consumption paths, not for
arbitrary C programs or maximal general-purpose C expressiveness.

Before closing a public API, ask:

```text
If this capability had been designed directly for a JavaScript programmer,
what would the natural API look like?
```

Use that JavaScript shape as the primary consumer-design test. Lua should
normally project the same capability model idiomatically.

Design from that consumer experience inward:

```text
kclib capability
    ↓
natural JavaScript/Lua consumer model
    ↓
minimal public C ABI expressing that same model
    ↓
public header
    ↓
generated cdef / JNI / WASM mechanical bridge
```

The C ABI remains canonical, but it is the stable native bridge surface for the
kclib ecosystem. It is not an attempt to design a universal C API for every
program or embedding scenario.

Bindings must remain mechanical adapters. They may translate calling mechanics,
ownership, tables or objects into public value structures, and platform
transport details. They should not need semantic glue to make the API pleasant.

Prefer opaque handles only when persistent identity or state is a real
capability-level concept. Prefer simple public value structs, explicit arrays,
plain scalar types, and clear ownership. Avoid public ABI complexity that does
not serve an actual kclib consumer.

## Represent intent, not mechanism

The public API must represent the user's intent and the capability being
offered, not the low-level mechanism used to implement it.

This rule applies to the public surface only: public headers, exported symbols,
and the semantics directly exposed to consumers. It does not restrict internal
implementation design.

Inside a kclib, use any private mechanisms required to implement the capability
correctly and efficiently, including private helpers, state machines, threads,
poll loops, queues, buffers, retries, native handles, platform-specific code,
and direct operating-system APIs.

Design public operations in terms of what a consumer wants to do:

```text
listen
receive
respond
save
build
stop
render
search
```

Do not export lower-level machinery merely because the implementation uses it:

```text
poll loops
socket readiness
send/sendto distinctions
partial-write retry state
native handles
temporary synchronization phases
internal queues
platform-specific transport details
```

A technically correct native primitive is not automatically an appropriate
public kclib operation. If the library can absorb the mechanism while preserving
the caller's meaningful control, keep that mechanism private.

Do not over-simplify by removing real user control. Operations such as explicit
save, build, configuration mutation, or cooperative stop remain public when the
caller intentionally decides when or whether they happen.

Use this test for every public symbol:

```text
Does this describe what the user wants to do,
or how the implementation happens to do it?
```

If it primarily describes the implementation, redesign the public surface at
the capability level. The private implementation remains free to use whatever
mechanisms are necessary.

## Compatibility

Treat the public header and CLI behavior as compatibility contracts.

Preserve existing public symbols, ownership semantics, return codes, CLI behavior, parseable output, configuration precedence, and lifecycle behavior unless the task explicitly requires a change.

Do not rename or redesign stable APIs solely for family consistency.

## Library and CLI

Keep reusable capability logic in the library.

The CLI may parse configuration and arguments, call the public API, print results or diagnostics, select exit status, and release resources.

Do not implement the capability twice.

Do not add public API solely to expose an internal CLI helper.

## Configuration

Where defaults, environment, and CLI configuration all exist, use:

1. built-in defaults;
2. documented environment values;
3. CLI arguments.

Use established `KC_*` environment naming.

Do not introduce generic configuration infrastructure.

## State and lifecycle

Use contexts only when persistent state is required.

Keep independent contexts independent.

Use established kclib lifecycle terminology for equivalent concepts, but do not invent `context`, `run`, `exec`, `stop`, or other lifecycle operations merely for symmetry.

Keep unavoidable process-global state narrow and explicit.

## Errors and I/O

Prefer explicit failures and established family return-code conventions where applicable.

Do not silently ignore malformed input, unsupported behavior, or native failures.

Use the simplest interface appropriate to the capability. No transport or protocol is mandatory across kclibs.

Preserve machine-parseable output where applicable.

## Structure and dependencies

Keep public headers, library source, CLI source, tests, vendored code, and platform-specific code easy to identify.

Do not restructure stable code solely to match another kclib.

Do not add shared runtimes, frameworks, registries, daemons, service layers, or common infrastructure merely to remove small duplication.

Small project-local or platform-specific duplication is acceptable when it keeps behavior easier to inspect.

## Platform code

Do not force materially different native platforms through a common abstraction solely for symmetry.

Share code only when it removes meaningful repeated complexity or inconsistent behavior.

## Tests and documentation

Use the project's existing test model and test shipped behavior.

Do not expose private internals or redesign production code solely for tests.

Keep README organization recognizably consistent with other kclibs, but document only actual project behavior.

Treat each kclib README as user-facing product documentation, not as a C
tutorial, implementation diary, design rationale, or ABI commentary.

A README should focus on:

* what the kclib does;
* how to use its CLI;
* practical public API examples;
* user-visible options and behavior;
* supported platforms and dependencies;
* build and test commands.

Do not fill README files with low-level C mechanics that a user does not need
in order to use the library. In particular, avoid explaining pointer ownership,
borrowed versus owned memory, pointer lifetimes, allocation layout, NULL-safety,
nullable scalar pointers, retained userdata, callback storage duration, internal
threading, private event loops, backend plumbing, dispatch mechanics, or other
implementation details. Those belong in the public header, source, tests, or
developer documentation when relevant.

Do not narrate development decisions or justify why an implementation was
designed a certain way. A reader should not need context from the development
process to understand the README.

Treat every kclib README as independent and self-contained. Do not compare one
kclib with another, explain a capability by contrasting it with another project,
or reference what other kclibs do or support. The reader may know nothing about
the rest of the monorepo and should never need that context.

Do not explain why a kclib does not support a platform or build target. Document
supported targets only. If WebAssembly is supported, document how to build and
test it. If it is not supported, omit WebAssembly entirely.

README examples should show installed CLI names such as `trust`, `tray`, or
`wvw`, not repository build paths such as
`./bin/x86_64/linux/<command>`. Artifact paths may still be documented where
the artifact location itself is relevant.

Update documentation when public or operational behavior changes.

## Existing projects

Do not migrate an existing kclib to the current blueprint merely because it differs.

When modifying existing code:

* preserve compatibility;
* avoid introducing new inconsistencies;
* adopt current family conventions when relevant and low-risk;
* leave harmless historical variation alone.

## Final rule

Keep kclibs consistent where they express the same concepts.

Let the concrete capability determine everything else.
