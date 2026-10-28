# kclib

`kclib` is the public development repository for the native primitives of the
KaisarCode ecosystem. Each project provides one concrete capability through an
independent reusable C library.

All reusable kclibs are developed under `proj/libNAME.c/` directories.
Independent native tools also live in `proj/` with their own project names.

It is not a framework or a monolithic library. Every kclib is independent: no
kclib depends on another kclib. Capabilities compose outside the libraries
through applications, processes, files, sockets, and explicit protocols.

The goal is to provide durable native building blocks with local control,
inspectable state, explicit dependencies, and minimal infrastructure.

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

## Build and tests

Build every kclib sequentially (omitting `all` is equivalent):

```sh
./scripts/build.sh
./scripts/build.sh all
```

Build one project:

```sh
./scripts/build.sh demo
```

In an interactive terminal, builds show a single updating progress line.
Use `-l` to display the full build log instead:

```sh
./scripts/build.sh -l demo
./scripts/build.sh -l
./scripts/build.sh -l all
```

When output is redirected, the default mode prints one line per completed
target and preserves the detailed log if a build fails.

The build script resolves the logical library name to its project directory and runs `make all`.

The test runner also defaults to `all` when no project is specified.
It uses the same compact terminal display as the build script.
It updates a single line across native, Wine and WASM tests and between
projects. Add `-l` to see every test result and the original platform
separators:

```sh
./scripts/test.sh
./scripts/test.sh all
./scripts/test.sh -l
./scripts/test.sh -l all
./scripts/test.sh -l demo
```

With redirected output, the default prints one summary per runtime. When a
runtime fails, its complete test log is shown.

The normal project-local entry points are:

```sh
make
make test
```

Projects normally use Make over CMake and Ninja. A local build creates artifacts
under `bin/{platform}/{arch}/`; exact multiarch targets depend on the project.
For example, `make linux/x86_64` builds to `proj/libdemo.c/bin/linux/x86_64/`,
while `make wasm/wasm32` uses `bin/wasm/wasm32/`. Distribution
publishes individual files under `dist/libNAME.c/{platform}/{arch}/`,
for example `dist/libdemo.c/linux/x86_64/libdemo.so`. Each target
directory contains its compiled artifacts, public header, `LICENSE` or
`COPYING` when provided, a generated minimal `README.md` (project name,
author and website), and `SHA256SUMS` covering the published files.
Every kclib owns a source `proj/libNAME.c/manifest.json` with stable,
machine-readable metadata (name, description, author, license, website and
repository). `scripts/dist.sh` publishes a per-library
`dist/libNAME.c/manifest.json`, including that metadata plus the list of
available target artifacts, their paths, sizes, SHA-256 hashes, build
checksums, license checksums, and README checksums. There is no root
distribution manifest. Changes to source metadata republish the affected
manifest; changes to binaries, headers, licensing files or README content
republish the affected targets. No ZIP archives are required.

Publish every kclib (omitting `all` is equivalent) or one project:

```sh
./scripts/dist.sh
./scripts/dist.sh all
./scripts/dist.sh demo
```

By default, each repository distributes into its own local `dist/` directory.
`KCDIST_DIR` selects a shared distribution destination for every project,
including native tools such as `kc`. All projects use their own
metadata and artifact manifest, without a root inventory.
For example:

```sh
export KCDIST_DIR="$HOME/kaisarcode-dist"
./scripts/dist.sh all
```

The matching public distribution root is `https://kaisarcode.com/dist`.
When `KCDIST_DIR` is set, the script never prunes projects owned by other
repositories.

The code is primarily C11.

Each project declares its requirements and supported targets in its own README
and Makefile.

## Status and license

The projects are beta software and are validated primarily on Debian x86_64,
although many include targets for other architectures and platforms. The monorepo infrastructure and root-level build and distribution tooling are
licensed under GNU GPL v3. Each independent library in `proj/` retains its
own license, normally GNU LGPL v3, which governs that library's source and
published artifacts. The repository's GPLv3 license does not replace the
licenses of contained libraries.
