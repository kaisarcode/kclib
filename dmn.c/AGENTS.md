# AGENTS.md

## Project Context

`dmn.c` is a small C library and CLI for keeping a named local command available
behind IPC and relaying standard streams through it.

It serves one operator on one machine. It is not an incomplete init system,
container runtime, distributed process manager, or enterprise control plane.

Read `README.md` before modifying the project.

## Core Invariants

- Daemons are addressed by short local names.
- IPC remains local: Unix domain sockets on POSIX and named pipes on Windows.
- Commands are trusted operator configuration executed through the platform
    shell.
- POSIX keeps one backend process per name and serves relay clients sequentially.
- ASCII EOT, byte value 4, is the explicit POSIX response-cycle boundary.
- EOT is relayed to the client and allows backend stdin to remain open.
- Client EOF without EOT closes backend stdin and causes backend restart after
    that relay.
- Backend stdout and stderr share the response stream.
- Updating a name replaces and terminates the old manager and backend.
- Deleting a name performs best-effort process and runtime-file cleanup.
- Runtime state remains local, temporary, plain, and inspectable.
- No account, subscription, hosted service, or network connection is required.

## Platform Semantics

Do not hide POSIX and Windows differences.

POSIX uses one detached manager, one resident shell backend, a Unix stream
socket, a manager PID file, and a backend PID file. Signals target the backend
PID with the requested signal number.

Windows uses one detached named-pipe server but starts a new command process for
each client connection. Its signal operation sets one named event for the server
PID and does not preserve the requested numeric signal. EOT does not currently
provide the same resident-cycle protocol as POSIX.

Do not claim equivalent resident state, EOT handling, or signal delivery across
platforms until those behaviors are implemented and runtime tested.

## IPC and Framing Boundary

Preserve byte-stream relay behavior. `dmn` does not parse application messages.
EOT is the only built-in cycle marker and belongs to the IPC contract.

Changes to EOT recognition must define split reads, bytes before and after the
marker, output forwarding, client disconnects, backend EOF, restart behavior,
and compatibility with existing backends.

Do not add JSON-RPC, HTTP, message schemas, multiplexing, remote sockets, request
IDs, routing, or a generic protocol layer.

## Runtime and Process State

Preserve `KC_DMN_DIR` and the current runtime-directory resolution order. Keep
socket or pipe names and `.pid` and `.bpid` files directly inspectable.

PID files are minimal local lifecycle hints, not authoritative identity. Do not
turn runtime state into a database, registry service, journal, lock service,
cluster catalog, or source of distributed truth.

Keys become filesystem or named-pipe components. Treat them as trusted local
identifiers. Any validation change must explicitly address separators,
traversal, length, existing compatibility, and runtime-directory confinement.

## Security and Ownership

Registered commands are executed with `/bin/sh -c` on POSIX or `cmd.exe /c` on
Windows. Never build command text from relay input. Do not describe commands as
sandboxed, authenticated, isolated, or privilege-separated.

Keep runtime directories private where the platform permits. Preserve explicit
ownership and cleanup for sockets, pipes, process handles, PID files, backend
pipes, and context allocations.

Application authentication, authorization, payload encryption, sandboxing,
privilege changes, and command policy remain outside this local IPC primitive.

## Resource and Failure Model

Relay buffers remain fixed at 4,096 bytes and the POSIX listen backlog remains
bounded. POSIX serves one client at a time, intentionally avoiding hidden
concurrent access to backend state.

Current relay and backend operations can wait indefinitely. Stop is cooperative
for the POSIX client relay and is not a complete manager cancellation protocol.
Changes must define timeouts and cleanup explicitly rather than adding hidden
queues, retry loops, watchdog services, or background orchestration.

Keep stale state and missing processes legible. Do not silently claim liveness
from the presence of a socket, pipe name, or PID file.

## Public API and Ownership

Treat `src/libdmn.h` as a compatibility boundary. Preserve status codes, option
ownership, runtime-path lifetime, callbacks, list behavior, EOT relay semantics,
and platform-specific signal behavior unless explicitly instructed otherwise.

Contexts own copied options. Returned paths and list callback values are
borrowed. stdin and stdout remain process-owned streams.

## Source Layout

Preserve exactly:

- `src/dmn.c` for CLI parsing and command assembly;
- `src/libdmn.c` for runtime, IPC, process, relay, and signal behavior;
- `src/libdmn.h` for the public API;
- `src/test.c` for all tests, including process, IPC, stress, platform, and
    integration cases.

Do not create additional source, header, backend, protocol, platform, process,
runtime, or test files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add systemd integration layers, init frameworks, container runtimes,
Kubernetes, service meshes, remote agents, cluster schedulers, service
discovery, databases, distributed locks, account systems, OAuth, SSO, tenant
models, dashboards, telemetry, analytics, distributed tracing, fleet management,
plugin systems, hosted control planes, or automatic restart policy frameworks.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover runtime path
resolution, key validation, update and replacement cleanup, missing and stale
PID files, exact binary relay, EOT split boundaries, repeated resident cycles,
ordinary EOF restart, backend exit, stderr forwarding, sequential clients,
signals, stop behavior, socket or pipe cleanup, and POSIX and Windows differences.

Prefer isolated temporary runtime directories and public API behavior. Do not
weaken tests to accommodate an implementation change.

## Build and Completion

For documentation-only changes run `kcs AGENTS.md`. For behavior
changes use the repository build and tests without cleaning unless authorized.

A change is complete when runtime state, manager and backend lifecycle, exact IPC
bytes, EOT behavior, platform truth, ownership, tests, and documentation agree.

The goal is one small, inspectable local resident-command bridge.

## Design Summary

### Purpose

`dmn.c` keeps named commands available behind local IPC so repeated CLI
invocations can relay stdin and stdout without rebuilding application state each
time where the platform backend supports residence.

The primary use is direct local composition for stateful command-line programs,
including model processes whose loaded state is expensive to recreate. It is a
single-machine, single-operator mechanism.

### Architecture

The CLI selects update, relay, list, delete, or signal behavior. The CLI calls
the public C API directly (no JSON runner). Relay is a direct library call
because streaming stdin/stdout does not fit a JSON payload contract.

The library resolves a runtime directory, owns local IPC and PID state, starts
detached manager processes, and relays bytes between clients and commands.

The four source files have fixed responsibilities:

- `src/dmn.c` owns CLI parsing and command assembly;
- `src/libdmn.c` owns all reusable IPC and process behavior;
- `src/libdmn.h` defines the public contract;
- `src/test.c` contains all tests.

### Runtime State

`KC_DMN_DIR` overrides runtime placement. Otherwise POSIX tries
`XDG_RUNTIME_DIR`, `/run/user/<uid>`, then a UID-specific path under `/tmp`.
Windows uses a `kaisarcode\dmn` directory below the system temporary path.

POSIX uses three entries per name:

- the Unix domain socket at `<dir>/<name>`;
- `<dir>/<name>.pid` for the detached manager;
- `<dir>/<name>.bpid` for the resident backend.

Windows uses `\\.\pipe\kc-dmn-<name>` plus `<dir>\<name>.pid` for its detached
server.

This state is temporary and operator-inspectable. Presence indicates a
registration artifact, not proven process health or authenticated identity.

### POSIX Process Model

Updating a name terminates recorded old processes and removes old IPC state. A
new detached manager binds the Unix socket, reports readiness to its parent,
redirects manager stdio to `/dev/null`, and starts one backend with
`/bin/sh -c`.

The backend runs in its own session. Its stdin is a manager-owned pipe. Its
stdout and stderr share another pipe. The manager accepts one client at a time
and relays between that Unix socket and the backend pipes.

Sequential service is deliberate because one resident backend owns one mutable
stream state. Concurrent multiplexing would require an application protocol and
does not belong in the current design.

### EOT Cycle Protocol

ASCII EOT, byte value 4, terminates one logical POSIX response while preserving
the backend for another client.

When EOT arrives from the backend, the manager forwards bytes through and
including the marker, then closes that client connection. Backend pipes remain
open. A later relay connects to the same backend process and preserved state.

When client input contains EOT, the manager stops reading that client but keeps
backend stdin open. When client input reaches ordinary EOF without EOT, the
manager closes backend stdin. After the relay, a closed pipe or exited backend
causes the manager to terminate remaining backend state and start a replacement.

EOT is transport framing, not text syntax. It can occur in binary data and must
be handled according to exact byte position and read boundaries.

### Windows Process Model

Windows starts one detached self-hosted named-pipe server per name. The server
accepts one client at a time and starts `cmd.exe /c <command>` for that
connection, bridging pipe bytes to child stdin and combined stdout and stderr.

The child is not currently resident across client connections. Windows relay
also does not implement the POSIX EOT cycle protocol. These are real platform
differences, not interchangeable implementation details.

### Relay Behavior

POSIX client relay uses `select()` to forward stdin and daemon output in 4,096
byte chunks. stdin EOF shuts down the client socket write side. Daemon output is
written directly to stdout until the daemon closes the connection or the local
context observes stop.

Windows writes stdin to the named pipe, then drains bytes reported immediately
available from the pipe. It does not provide the same full-duplex wait behavior
as POSIX.

The relay does not parse application messages, assign request IDs, route calls,
or interpret backend output.

### Signals and Lifecycle

On POSIX, `kc_dmn_signal()` reads the backend PID file and sends the requested
signal to that process. Delete and replacement terminate both manager and
backend state on a best-effort basis.

On Windows, the detached server creates a named auto-reset event based on its
PID. `kc_dmn_signal()` sets that event and ignores the numeric signal value. It
does not deliver a POSIX-equivalent signal directly to each command child.

### Trust Boundary

Names and command strings are trusted local operator input. Commands execute
through the platform shell. Relay bytes are application data and must never be
concatenated into command text.

The project does not provide authentication, authorization, encryption,
sandboxing, privilege separation, resource quotas, or command allowlists.
Runtime-directory permissions and operating-system process ownership form the
local access boundary.

### Context and Ownership

Options own an optional allocated runtime-directory string. Opening a context
copies it. The context owns its copied options, resolved path, and stop flag.
Closing frees owned memory.

Returned runtime paths and callback strings are borrowed. IPC handles, process
handles, file descriptors, and PID artifacts have explicit operation-specific
ownership.

### Resource and Failure Model

Paths are bounded to 512 bytes, command and relay buffers to 4,096 bytes, and the
POSIX listen backlog to 16. Each name has one manager and at most one POSIX
resident backend. Clients are handled sequentially.

Relay, accept, backend reads, and process waits may block without timeout. PID
files can become stale and PID reuse is not guarded by process identity metadata.
Deletion is best effort and reports missing names without treating them as fatal.

These limitations should remain explicit. A concrete fix may add validation or
bounded waiting, but should not introduce a database, watchdog fleet, remote
coordinator, hidden queue, or service framework.

### Composition

`dmn` supplies residence and local stream relay. The backend owns its request
format, state, security, and application behavior. A backend that wants repeated
POSIX cycles emits EOT after each complete response.

Other tools may provide protocol framing, templates, network exposure, or model
interaction. Those responsibilities remain separate from process residence.

### Non-Goals

The project does not provide an init system, machine boot integration, container
runtime, cluster scheduler, remote process manager, service mesh, application
protocol, concurrent session multiplexer, user account system, permission
database, restart-policy engine, health-check platform, log service, metrics
collector, dashboard, hosted control plane, or plugin ecosystem.

These exclusions define the tool rather than an unfinished roadmap.

### Change Criteria

A change must solve a concrete local residence or relay problem, preserve exact
stream bytes and EOT semantics, define manager and backend ownership, retain
inspectable runtime state, account for stale PID behavior, state all platform
differences, and avoid absorbing application or orchestration policy.

Changes justified mainly by enterprise scale, managed fleets, generalized
supervision, or ecosystem expectations should be rejected.

### Core Invariants

The project is defined by named local IPC, trusted shell commands, a sequential
POSIX resident backend, explicit EOT response cycles, temporary plain runtime
state, direct PID lifecycle control, visible Windows differences, and no remote
control infrastructure.
