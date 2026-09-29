# AGENTS.md

## Kclib

This repository is the public development monorepo for the native primitive
libraries of the KaisarCode ecosystem.

Individual kclibs live as project directories under `proj/`, using the
`proj/libNAME.c/` form. They are not separate Git repositories.

Repository-wide source-control files such as `.gitignore` and `.kcsignore`
belong at the monorepo root rather than inside each kclib project.

Repository-wide tooling lives under `scripts/`. Generated distribution artifacts
live under `dist/`; that directory is build output and is not versioned.

A kclib is an independent native library built around one concrete capability.

No kclib depends on another kclib. Each library must remain independently
buildable, usable, and distributable. Composition between capabilities belongs
in consumers, applications, processes, or explicit higher-level tooling rather
than through kclib-to-kclib dependencies.

## Project form

Prefer:

*  reusable behavior in the library;
*  a compact C-friendly public header;
*  explicit ownership, lifetime, errors, and cleanup;
*  ordinary native build artifacts;
*  independent local use without hosted services or shared runtimes.

Use the current kclib blueprint as the default for naming, layout, lifecycle,
build targets, tests, and README structure.

Do not copy blueprint mechanics when the capability is materially different.

Rule:

```text
same concept -> same convention
different concept -> capability-specific design
```

## Public API philosophy

A kclib public API must express the user's intent and the capability being
offered, not the mechanism used internally to implement it.

Design the public ABI for kclibs and their supported consumption paths, not for
arbitrary C programs or maximal general-purpose C expressiveness.

The public surface should be easy to project mechanically into higher-level
languages such as Lua and JavaScript without inventing new semantics in the
binding layer.

Do not design the public API around C calling mechanics. A consumer should not
be forced to think in pointers, out-parameters, buffer/size pairs, allocation
choreography, nullable pointer conventions, or other ABI-shaped details unless
those concepts are themselves meaningful parts of the capability.

Before closing a public API, ask:

```text
If this capability had been designed directly for a JavaScript or Lua programmer,
what would the natural API feel like?
```

Start from that user-facing shape, then express it through the smallest
practical C ABI.

Use that JavaScript shape as the primary consumer-design test. Lua should
normally project the same capability model idiomatically.

A good public API should remain recognizable across languages. The operation
names, object boundaries, values, and lifecycle should describe the same
capability whether consumed from C, Lua, JavaScript, or another binding.

Use concrete JavaScript shapes as the main design check. The goal is for a
developer to use a kclib without needing systems-programming expertise merely
because its implementation happens to be written in C.

The public API should feel closer to an ordinary JavaScript library than to a
traditional low-level C interface.

For a stateless operation, prefer a direct value-oriented shape:

```lua
local html = mdp.render(markdown)
```

```js
const html = await mdp.render(markdown);
```

Do not force a scripting consumer to mirror C-only mechanics such as:

```text
allocate options
prepare output pointer
call function
check integer status
read output pointer
free temporary result
```

For a capability with persistent identity or state, expose an object naturally:

```lua
local document, status = mdp.open(markdown)
if not document then
    error(status)
end

local html = document:html()
document:close()
```

```js
const document = await mdp.open(markdown);
const html = await document.html();
await document.close();
```

For structured input, prefer ordinary language values:

```lua
listener = netl.open({
    host = "127.0.0.1",
    port = 8080
})
```

```js
const listener = await netl.open({
    host: "127.0.0.1",
    port: 8080
});
```

A binding may mechanically translate these values to public C structs, arrays,
handles, or other ABI representations. The scripting user should not need to
recreate those representations manually.

When reviewing a proposed public API, write the JavaScript form first.

If the JavaScript version requires the developer to understand pointer
ownership, memory layout, native handles, output parameters, buffer accounting,
or internal lifecycle machinery, the public API is exposing too much mechanism.

The C implementation may remain sophisticated and systems-level internally.
That complexity should be absorbed by the library so the exported capability
stays approachable to an application developer.

If the JavaScript form looks like translated C plumbing instead of a natural
library API, redesign the public C surface.

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
consumer-generated cdef / JNI / WASM mechanical bridge
```

The C ABI remains canonical, but it is the stable native bridge surface for the
kclib ecosystem. It is not an attempt to design a universal C API for every
program or embedding scenario.

Bindings must remain mechanical adapters. They may translate calling mechanics,
ownership, tables or objects into public value structures, and platform
transport details. They should not need semantic glue to make the API pleasant.

If a binding must reinterpret what an operation means, invent application-level
behavior, expose internal transport details, or compensate for a
mechanism-shaped C API, the public API is probably wrong.

Prefer opaque handles only when persistent identity or state is a real
capability-level concept. Prefer simple public value structs, explicit arrays,
plain scalar types, and clear ownership.

The private C implementation may use whatever machinery is necessary: pointers,
allocations, native handles, buffers, threads, queues, platform APIs, state
machines, or other low-level techniques. That complexity belongs behind the
public header.

Exported symbols should stay simple and capability-oriented. Do not make
consumers manually shuttle pointers, manage internal allocations, coordinate
buffer ownership, or reproduce private lifecycle mechanics when the library can
own those responsibilities itself.

When some low-level C representation is unavoidable at the ABI boundary, keep it
minimal, explicit, and mechanical so bindings can erase it cleanly. Do not let C
representation constraints define the conceptual API. Avoid public ABI
complexity that does not serve an actual kclib consumer.

## Represent intent, not mechanism

The public API must represent the user's intent and the capability being
offered, not the low-level mechanism used to implement it.

This rule applies to the public surface only: public headers, exported symbols,
and the semantics directly exposed to consumers. It does not restrict internal
implementation design.

Private implementation complexity is expected when the capability requires it.
Prefer absorbing that complexity inside the library instead of pushing it into
every caller.

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

If it primarily describes the implementation, redesign the public surface at the
capability level. The private implementation remains free to use whatever
mechanisms are necessary.

## Compatibility

Treat the public header and library behavior as compatibility contracts.

Preserve existing public symbols, ownership semantics, return codes, and lifecycle
behavior unless the task explicitly requires a change.

Do not rename or redesign stable APIs solely for family consistency.

## State and lifecycle

Use contexts only when persistent state is required.

Keep independent contexts independent.

Use established kclib lifecycle terminology for equivalent concepts, but do not
invent `context`, `run`, `exec`, `stop`, or other lifecycle operations merely
for symmetry.

Keep unavoidable process-global state narrow and explicit.

## Errors and I/O

Prefer explicit failures and established family return-code conventions where
applicable.

Do not silently ignore malformed input, unsupported behavior, or native
failures.

Use the simplest interface appropriate to the capability. No transport or
protocol is mandatory across kclibs.


## Structure and dependencies

Keep public headers, library source, tests, vendored code, and platform-specific
code easy to identify.

Do not restructure stable code solely to match another kclib.

Do not add shared runtimes, frameworks, registries, daemons, service layers, or
common infrastructure merely to remove small duplication.

Do not introduce a dependency from one kclib to another. If two capabilities
need to be combined, keep that composition outside the libraries themselves.

Small project-local or platform-specific duplication is acceptable when it keeps
behavior easier to inspect.

## Platform code

Do not force materially different native platforms through a common abstraction
solely for symmetry.

Share code only when it removes meaningful repeated complexity or inconsistent
behavior.

## Tests and documentation

Use the project's existing test model and test shipped behavior.

Do not expose private internals or redesign production code solely for tests.

Keep README organization recognizably consistent with other kclibs, but document
only actual project behavior.

Treat each kclib README as user-facing product documentation, not as a C
tutorial, implementation diary, design rationale, or ABI commentary.

A README should focus on:

*  what the kclib does;
*  practical public API examples;
*  user-visible options and behavior;
*  supported platforms and dependencies;
*  build and test commands.

Do not fill README files with low-level C mechanics that a user does not need in
order to use the library. In particular, avoid explaining pointer ownership,
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

Update documentation when public or operational behavior changes.

## Existing projects

Do not migrate an existing kclib to the current blueprint merely because it
differs.

When modifying existing code:

*  preserve compatibility;
*  avoid introducing new inconsistencies;
*  adopt current family conventions when relevant and low-risk;
*  leave harmless historical variation alone.

## Final rule

Keep kclibs consistent where they express the same concepts.

Let the concrete capability determine everything else.
