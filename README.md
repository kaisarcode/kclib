# kclib

`kclib` is the public development repository for a collection of small,
independent, and composable native primitives. Each project solves one specific
problem through a reusable C library and, usually, a thin CLI built on the same
public API.

All kclib projects are developed together in this monorepo under `NAME.c/`
directories. They are not maintained as separate Git repositories.

It is not a framework or a monolithic library. The tools work independently or
compose through stdin, stdout, files, sockets, and explicit text protocols. The
goal is to preserve local control, inspectable state, and minimal
infrastructure.

## Principles

- Library first; the CLI only adapts arguments, input, and output.
- Small tools with clear boundaries instead of a general-purpose platform.
- Unix composition through pipes, files, processes, and sockets.
- Simple, legible, stable protocols instead of RPC or opaque formats.
- Local, durable, portable, and inspectable state.
- Explicit dependencies and network behavior; no implicit remote services.
- Portability through C11 and native backends where the system requires them.

## Catalog

The complete inventory of the collection - name, purpose, and when to use it -
lives in [INDEX.md](INDEX.md). It is the single source of truth for the catalog;
app and agent instructions read from the same file.

Each project directory contains detailed documentation in `NAME.c/README.md`.
That README defines the actual behavior, options, API, and constraints of its
project.

## Common form

A project usually has this structure:

```text
NAME.c/
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
`.kcsignore`.

- `src/libNAME.c` contains reusable behavior.
- `src/libNAME.h` is the public library contract.
- `src/NAME.c` implements the CLI over the public API.
- `src/test.c` tests the portable contract.
- `README.md` documents the effective project interface.

Justified exceptions exist for protocols, platform backends, embedded models,
or external dependencies.

## API and execution

The APIs usually use the `kc_NAME_` prefix, opaque contexts, explicit options,
and a lifecycle similar to:

```c
#include "libNAME.h"

kc_NAME_options_t opts = kc_NAME_options_default();
kc_NAME_t *ctx = NULL;

kc_NAME_open(&ctx, &opts);
kc_NAME_exec(ctx, input);
kc_NAME_close(ctx);
kc_NAME_options_free(&opts);
```

This is a conceptual pattern, not a common ABI. Consult each library header and
README for exact signatures and ownership rules.

The CLIs normally:

- Receive data through stdin or positional arguments.
- Write processable results to stdout and diagnostics to stderr.
- Provide `-h`/`--help` and `-v`/`--version`.
- Apply CLI flags over environment variables over built-in defaults.
- Use `KC_NAME_*` environment variables, with documented exceptions.

Some tools process multiple requests separated by `--until 4` (EOT). This does
not imply a runtime control plane: Unix-style tools use flags, stdin/stdout,
files, and signals. `llm.c` is the exception because keeping a loaded model and
its generation state resident is valuable, so it exposes a Unix control socket
through `--ctrl`. Protocol-specific control channels, such as `redp2p.c`, remain
part of their respective protocols rather than a common kclib facility.

## Build and tests

The normal operator entry points are:

```sh
make
make test
```

Projects normally use Make over CMake and Ninja. A local build creates
artifacts under `bin/{arch}/{platform}/`; exact multiarch targets depend on the
project. The code is primarily C11. `llm.c` also requires C++17 for its
llama.cpp integration.

Important special dependencies:

- `llm.c` uses an external llama.cpp checkout and local GGUF models.
- `emb.c` embeds the GGML runtime and the weights required by its model.
- `wvw.c` uses the native WebView backend available on each platform.
- `redp2p.c` uses the index only for coordination; data travels directly between
    peers and no data relay exists.

Each project declares its requirements and supported targets in its own
README and Makefile.

## Status and license

The projects are beta software and are validated primarily on Debian x86_64,
although many include targets for other architectures and platforms. They are
distributed under GPLv3; consult each project for its exact terms.
