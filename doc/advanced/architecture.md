# Repository architecture

kclib collects independently reusable capabilities. This page documents the repository's organization, design constraints, and project conventions. For the current inventory, consult [INDEX.md](../../INDEX.md).

## Principles

- Public APIs express intent, not implementation mechanism.
- Design the capability first as it should feel from languages such as Lua or
    JavaScript, then map that shape to C.
- Public consumers should not need to reason about pointers, out-parameters,
    buffer/size choreography, allocation details, or private lifecycle mechanics
    when the library can encapsulate them.
- Internal C code may be as low-level or complex as necessary; that complexity
    belongs behind the public header.
- Bindings should translate representation and ownership, not invent semantics.
- One concrete capability per library, with clear boundaries.
- No dependencies between kclibs.
- Simple, legible, stable protocols instead of RPC or opaque formats.
- Local, durable, portable, and inspectable state.
- Explicit dependencies and network behavior; no implicit remote services.
- Portability through C11 and native backends where the system requires them.

## Catalog

The complete inventory of the collection - name, purpose, and when to use it -
lives in [INDEX.md](INDEX.md). It is the single source of truth for the catalog;
app and agent instructions read from the same file.

Each project directory contains detailed documentation in
`proj/libNAME.c/README.md`. That README defines the actual behavior, API,
build, and constraints of its project.

## Standalone tools

The repository also contains independent applications such as
[`kc`](proj/kc/). This is a native executable, **not a kclib**, and is
not required by any kclib. Each project defines its own build and test
contracts through its `Makefile`; repository scripts discover projects
from those files rather than from a hardcoded `libNAME.c` naming rule.

`kc` combines package management (`install`, `update`, `remove`,
`list`, `info`) and generic dispatch of installed project CLIs.
Its standalone project metadata is in `proj/kc/manifest.json`.

## Common form

A project usually has this structure:

```text
proj/libNAME.c/
|-- README.md
|-- Makefile
|-- CMakeLists.txt
|-- src/
|   |-- libNAME.c
|   |-- libNAME.h
|   `-- test.c
`-- LICENSE
```

Repository-wide ignore rules live in the monorepo root `.gitignore` and
`.kcsignore`. Repository tooling lives in `scripts/`, while generated
publication artifacts are written to the ignored `dist/` directory.

- `src/libNAME.c` contains reusable behavior.
- `src/libNAME.h` is the public library contract.
- `src/test.c` tests the portable library contract.
- `README.md` documents the effective project interface.

Justified exceptions exist for protocols, platform backends, embedded models, or
external dependencies.

## Public API

Each kclib defines the API that best matches its capability. There is no common
lifecycle or required `open/exec/close` pattern.

The public API is capability-oriented. It should describe what the caller wants
to do rather than the lower-level machinery used to do it.

A kclib API is designed from the consumer experience inward: first consider how
the capability should feel in languages such as Lua or JavaScript, then express
that model through a minimal C ABI.

Internally, the C implementation may allocate memory, manage pointers,
coordinate buffers, keep native handles, run threads, maintain queues, or use
platform-specific machinery. Those are implementation details. The public header
should expose simple capability-level operations and keep ownership and lifetime
work inside the library whenever practical.

Public C symbols normally use the `kc_NAME_` prefix. Stateful capabilities may
use opaque handles, options, callbacks, or explicit lifecycle operations when
those concepts are actually required.

The demo blueprint exposes a direct capability-oriented API:

```c
#include "libdemo.h"

char *greeting = kc_demo_greet("John");

if (greeting != NULL) {
    kc_demo_free(greeting);
}
```

The public header and project README are the contract for each individual
library. Consult `proj/libNAME.c/README.md` and
`proj/libNAME.c/src/libNAME.h` for its exact API, ownership, errors, and
supported behavior.

## Repository layout

```text
kclib/
|-- AGENTS.md
|-- README.md
|-- INDEX.md
|-- proj/
|   `-- libNAME.c/
|-- scripts/
|   |-- build.sh
|   `-- dist.sh
`-- dist/              # generated, not versioned
```

`proj/` contains the kclib source projects. `scripts/` contains
repository-wide build and distribution tooling. `dist/` is generated from
project build artifacts and is intentionally excluded from source control.

## Next step

Continue with [Project README](../../README.md).
