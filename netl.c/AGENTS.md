# AGENTS.md

## Project Context

`netl.c` is a small C library and CLI for registering named TCP or UDP
listeners and dispatching incoming traffic to shell commands.

`netl.c` gives a local operator a small named registry of TCP and UDP
listeners. Each listener binds an address and dispatches incoming traffic to
one configured shell command. It is a local composition primitive for
independently operated systems, not an incomplete service platform, process
supervisor, or enterprise network layer.

The project is designed for direct composition on personal systems, local
servers, workshops, kiosks, small businesses, and modest VPS or SBC deployments.
Small scale is the intended operating model.

Read `README.md` before modifying the project.

## Architecture

The CLI parses one registration or lifecycle operation. The library resolves a
local metadata directory, reads and writes one plain file per listener name,
binds sockets, dispatches commands, and records the listener PID.

The source files have fixed responsibilities:

- `src/netl.c` owns CLI parsing and detached startup;
- `src/libnetl.c` owns all reusable behavior;
- `src/libnetl.h` defines the public contract;
- `src/test.c` contains all tests.

## Core Invariants

- Registrations remain local, named, plain-file records.
- Each record stores address, protocol, shell command, and listener PID.
- TCP dispatch preserves a bidirectional byte stream through command stdin and
    stdout.
- UDP dispatch gives one datagram to one command through stdin.
- UDP command output is not returned to the sender.
- The registered command is trusted operator configuration, not network input.
- `kc_netl_exec()` and `kc_netl_serve()` remain blocking on success.
- CLI backgrounding remains a thin process-detachment step around the library.
- Updating a registration stops its stored PID before replacing metadata.
- TCP and UDP remain explicit modes rather than a generic transport framework.
- No application protocol interpretation belongs in the listener.
- Local operation requires no account, subscription, hosted service, or
    external control plane.

## Scope Boundary

`netl` owns local registration metadata, socket binding, listener execution,
per-connection or per-datagram command dispatch, listing, deletion, and stopping
the recorded listener process.

Do not add HTTP behavior, routing, reverse proxying, TLS termination,
authentication, authorization, command discovery, command sandboxing, payload
storage, scheduling, retries, health checks, or application lifecycle policy.

Compose protocol filters and application commands through standard streams.
Use a separate operating-system facility when stronger supervision, privilege
separation, firewall policy, or sandboxing is required.

## Registration State

Each named file stores four newline-delimited fields:

1. bind address and port;
2. `tcp` or `udp`;
3. shell command;
4. listener process ID.

POSIX state resolves below `XDG_DATA_HOME/netl` or
`HOME/.local/share/netl`. Windows state resolves below `APPDATA\netl`.

Updating a name first attempts to stop the recorded process, then replaces the
file. Listing reads records directly. Deletion removes the named file. The
registry is local durable operator state, not a network service or source of
distributed truth.

Treat names, addresses, protocol text, commands, and PID fields as
compatibility boundaries. PID storage is deliberately simple. Do not turn it
into a service database, lock manager, job scheduler, distributed registry, or
persistent event log. Keep stop behavior direct and inspectable, including its
bounded graceful wait before forced termination on POSIX.

Do not silently change shell invocation, stdin/stdout wiring, detach behavior,
signal behavior, bind defaults, default port, list output, or replacement
semantics.

## TCP Dispatch

TCP mode listens with a bounded socket backlog and assigns each accepted
connection to one command invocation. The connection becomes command stdin and
stdout, preserving a bidirectional byte stream until either side closes.

On POSIX this uses a child process. On Windows a worker thread starts a child
and relays bytes through pipes. The platform mechanisms differ, but the public
stream behavior remains the same.

`netl` does not frame, parse, route, retry, cache, or interpret the stream.

## UDP Dispatch

UDP mode receives at most 65,536 bytes for one datagram and starts one command
with exactly those received bytes on stdin. Command stdout is not sent back to
the datagram source.

POSIX may run one receive worker per available processor through `SO_REUSEPORT`.
This is local receive concurrency, not a general worker-pool abstraction.

Datagram boundaries must remain explicit. `netl` does not add sessions,
ordering, retransmission, response routing, or application-level reliability.

## Lifecycle

Library execution is blocking and does not return after a successful listener
start. The CLI starts a detached process when invoked with only a registered
name. It records that process ID in the registration for later stopping.

On POSIX, stopping sends `SIGTERM`, waits for a bounded interval, then sends
`SIGKILL` if the PID still exists. Windows opens and terminates the recorded
process directly.

This PID mechanism is intentionally minimal. It is not full process supervision
and does not provide restart policy, status reconciliation, health monitoring,
logs, dependencies, or crash recovery.

## Security and Resource Model

Treat all network bytes as untrusted. Preserve checked lengths, bounded address
and command storage, the 65,536-byte UDP receive bound, socket cleanup, child
reaping, and explicit setup failures.

The registered command is trusted operator configuration. On POSIX it is
executed with `/bin/sh -c`; Windows delegates to `CreateProcessA` command-line
parsing. Never construct command text from incoming network data. Do not
misrepresent registered commands as sandboxed, authenticated, or isolated.

The project does not provide command sandboxing, privilege separation,
authentication, authorization, encryption, firewall policy, or application
security. Operators compose those controls externally when needed.

The implementation uses fixed-size address, command, path, and datagram buffers,
a bounded TCP listen backlog, direct socket ownership, and explicit child
cleanup. There is no connection database, persistent queue, cache, scheduler,
remote dependency, or hidden temporary storage.

The current model creates one handler process or thread per TCP connection and
one command process per UDP datagram. Changes must define strict resource bounds
and overload behavior rather than adding hidden queues or background services.
This model can exhaust process resources under hostile load. Any remedy must
have a concrete local requirement and explicit bounded behavior; hidden queues
and infrastructure dependencies are not acceptable defaults.

## Public API and Portability

Treat `src/libnetl.h` as a compatibility boundary. Preserve error codes,
protocol constants, context ownership, callback signatures, blocking behavior,
and caller-visible metadata paths unless explicitly instructed otherwise.

The caller owns an opened context and releases it with `kc_netl_close()`. The
context owns copied options and its metadata path. Listing callbacks and
returned path pointers borrow library-owned data for their documented lifetime.

Invalid arguments, malformed registration data, bind failures, and unavailable
metadata fail through the existing error contract. Socket, process, pipe, and
allocation ownership must remain explicit on every path.

Keep POSIX and Windows behavior visibly aligned where their process and socket
models permit. Do not claim runtime portability from cross-compilation alone.

## Composition

`netl` turns network input into standard streams. Commands such as `http`, `tpl`,
`min`, or a local application can consume those streams without being embedded
in the listener.

External tools may provide supervision, encryption, protocol handling, logging,
or access control. Composition is preferred over expanding `netl` into a server
framework.

## Source Layout

Preserve exactly:

- `src/netl.c` for CLI parsing and process detachment;
- `src/libnetl.c` for registration, sockets, dispatch, process, and signal logic;
- `src/libnetl.h` for the public API;
- `src/test.c` for all tests, including network, process, stress, platform, and
    integration cases.

Do not create additional source, header, platform, transport, process, metadata,
or test files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add daemon frameworks, service meshes, API gateways, load balancers,
container orchestration, remote registries, distributed coordination, service
discovery, user accounts, OAuth, SSO, tenant models, databases, dashboards,
telemetry, analytics, distributed tracing, fleet management, cloud dependencies,
plugin systems, or generic transport abstractions. The project is not an HTTP
server, reverse proxy, daemon manager, cluster orchestrator, application router,
TLS endpoint, user system, policy engine, command sandbox, or plugin platform.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover public API
validation, metadata replacement, list and delete semantics, PID handling,
stop behavior, bind failures, TCP stream bridging, UDP datagram boundaries,
child cleanup, malformed metadata, resource limits, and relevant POSIX and
Windows differences.

Prefer loopback sockets and public API behavior. Do not weaken tests to preserve
an implementation change.

## Build and Completion

For documentation-only changes run `kcs .`. For behavior changes use the
repository build and tests without cleaning unless authorized.

A change must solve a concrete listener or registration problem, preserve the
CLI and public API unless explicitly revised, define process and socket
ownership, retain bounded input handling, keep metadata inspectable, account for
POSIX and Windows semantics, and avoid absorbing application or infrastructure
responsibilities. Changes justified mainly by enterprise scale, managed
operation, generalized extensibility, or ecosystem expectations should be
rejected.

A change is complete when command dispatch, metadata compatibility, process and
socket ownership, limits, failure behavior, tests, and documentation agree.

The goal is one sharp, local network-listener primitive.
