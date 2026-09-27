# kclib

`kclib` is the public development repository for the native primitives of the
KaisarCode ecosystem. Each project provides one concrete capability through an
independent reusable C library and, usually, a thin CLI built on the same public
API.

All kclib projects are developed together in this monorepo under `proj/NAME.c/`
directories.

It is not a framework or a monolithic library. Every kclib is independent: no
kclib depends on another kclib. Capabilities compose outside the libraries
through applications, processes, stdin/stdout, files, sockets, and explicit
protocols.

The goal is to provide durable native building blocks with local control,
inspectable state, explicit dependencies, and minimal infrastructure.

## Principles

-  Public APIs express intent, not implementation mechanism.
-  Design the capability first as it should feel from languages such as Lua or
  JavaScript, then map that shape to C.
-  Public consumers should not need to reason about pointers, out-parameters,
  buffer/size choreography, allocation details, or private lifecycle mechanics
  when the library can encapsulate them.
-  Internal C code may be as low-level or complex as necessary; that complexity
  belongs behind the public header.
-  Bindings should translate representation and ownership, not invent semantics.
-  Library first; the CLI only adapts arguments, input, and output.
-  One concrete capability per library, with clear boundaries.
-  No dependencies between kclibs.
-  Unix composition through pipes, files, processes, and sockets.
-  Simple, legible, stable protocols instead of RPC or opaque formats.
-  Local, durable, portable, and inspectable state.
-  Explicit dependencies and network behavior; no implicit remote services.
-  Portability through C11 and native backends where the system requires them.

## Catalog

The complete inventory of the collection - name, purpose, and when to use it -
lives in [INDEX.md](INDEX.md). It is the single source of truth for the catalog;
app and agent instructions read from the same file.

Each project directory contains detailed documentation in
`proj/NAME.c/README.md`. That README defines the actual behavior, options, API,
and constraints of its project.

## Common form

A project usually has this structure:

```text
proj/NAME.c/
|-- README.md
|-- Makefile
|-- CMakeLists.txt
|-- src/
|   |-- NAME.c
|   |-- libNAME.c
|   |-- libNAME.h
|   `-- test.c
`-- LICENSE
```

Repository-wide ignore rules live in the monorepo root `.gitignore` and
`.kcsignore`. Repository tooling lives in `scripts/`, while generated
publication artifacts are written to the ignored `dist/` directory.

-  `src/libNAME.c` contains reusable behavior.
-  `src/libNAME.h` is the public library contract.
-  `src/NAME.c` implements the CLI over the public API.
-  `src/test.c` tests the portable contract.
-  `README.md` documents the effective project interface.

Justified exceptions exist for protocols, platform backends, embedded models, or
external dependencies.

## Public API and CLI

Each kclib defines the API that best matches its capability. There is no common
lifecycle or required `open/exec/close` pattern.

The public API is intentionally capability-oriented. It should describe what the
caller wants to do rather than the lower-level machinery used to do it.

A kclib API is designed from the consumer experience inward: first consider how
the capability should feel in languages such as Lua or JavaScript, then express
that model through a minimal C ABI.

Internally, the C implementation may allocate memory, manage pointers,
coordinate buffers, keep native handles, run threads, maintain queues, or use
platform-specific machinery. Those are implementation details. The public header
should expose simple capability-level operations and keep ownership and lifetime
work inside the library whenever practical.

The same conceptual API should therefore remain recognizable when projected into
other languages, without requiring each consumer to recreate the library's
internal memory or pointer management.

Public C symbols normally use the `kc_NAME_` prefix. Some libraries expose
simple functions, while stateful capabilities may use opaque handles, options,
callbacks, or explicit lifecycle operations where those concepts are actually
required.

For example, an internal implementation may use sockets, polling, queues, native
handles, or platform APIs, but the public surface should expose capability-level
operations such as `listen`, `receive`, `respond`, `render`, `search`, or `stop`
when those are the user-visible actions.

For example, the demo blueprint exposes a direct capability-oriented API:

```c
#include "libdemo.h"

char *greeting = kc_demo_greet("John");

if (greeting != NULL) {
    kc_demo_free(greeting);
}
```

The public header and project README are the contract for each individual
library. Consult `proj/NAME.c/README.md` and `proj/NAME.c/src/libNAME.h` for its
exact API, ownership, errors, and supported behavior.

The CLIs normally:

-  Receive data through stdin or positional arguments.
-  Write processable results to stdout and diagnostics to stderr.
-  Provide `-h`/`--help` and `-v`/`--version`.
-  Apply CLI flags over environment variables over built-in defaults.
-  Use `KC_NAME_*` environment variables, with documented exceptions.

Some tools process multiple requests separated by `--until 4` (EOT). This does
not imply a runtime control plane: Unix-style tools use flags, stdin/stdout,
files, and signals. `llm.c` is the exception because keeping a loaded model and
its generation state resident is valuable, so it exposes a Unix control socket
through `--ctrl`. Protocol-specific control channels, such as `redp2p.c`, remain
part of their respective protocols rather than a common kclib facility.

## Repository layout

```text
kclib/
|-- AGENTS.md
|-- README.md
|-- INDEX.md
|-- proj/
|   `-- NAME.c/
|-- scripts/
|   |-- build.sh
|   |-- dist.sh
|   `-- link.sh
`-- dist/              # generated, not versioned
```

`proj/` contains the kclib source projects. `scripts/` contains repository-wide
maintenance and distribution tooling. `dist/` is generated from project build
artifacts and is intentionally excluded from source control.

## Build and tests

Build every kclib sequentially:

```sh
./scripts/build.sh all
```

Build one project:

```sh
./scripts/build.sh demo.c
```

The build script enters each selected project directory and runs `make all`.

The normal project-local entry points are:

```sh
make
make test
```

Projects normally use Make over CMake and Ninja. A local build creates artifacts
under `bin/{arch}/{platform}/`; exact multiarch targets depend on the project.
The code is primarily C11. `llm.c` also requires C++17 for its llama.cpp
integration.

Important special dependencies:

-  `llm.c` uses an external llama.cpp checkout and local GGUF models.
-  `emb.c` embeds the GGML runtime and the weights required by its model.
-  `wvw.c` uses the native WebView backend available on each platform.
-  `redp2p.c` uses the index only for coordination; data travels directly
  between
peers and no data relay exists.

Each project declares its requirements and supported targets in its own README
and Makefile.

## Status and license

The projects are beta software and are validated primarily on Debian x86_64,
although many include targets for other architectures and platforms. They are
distributed under GPLv3; consult each project for its exact terms.
