# AGENTS.md

## Project Context

`init.c` is a small C library and CLI for registering named commands with the
startup mechanism already present on one machine.

It writes native startup artifacts for systemd, runit, OpenRC, SysV, or Windows.
It is not PID 1, a portable service supervisor, a deployment platform, or an
enterprise process-control system.

Read `README.md` and `DESIGN.md` before modifying the project.

## Core Invariants

- Registrations remain named, local, and directly inspectable.
- Metadata stores the exact one-line command used to generate startup artifacts.
- POSIX backend selection is explicit or locally auto-detected.
- The backend used for a registration is recorded for later deletion.
- systemd, runit, OpenRC, and SysV artifacts remain native to those systems.
- Windows uses Startup-folder command files and a Run registry value.
- Immediate execution reads metadata and invokes the platform shell.
- POSIX registration and deletion require root privileges.
- The CLI may re-execute itself through locally installed `sudo` on POSIX.
- Deletion remains best-effort and removes artifacts for the recorded backend.
- No hosted service, account, subscription, or network connection is required.

## Scope Boundary

`init` owns startup registration, metadata, native artifact generation, listing,
immediate execution, and deletion. The operating system remains responsible for
boot ordering, process execution, supervision, logs, and service state.

Do not add PID 1 behavior, a resident daemon, process monitoring, restart loops,
health checks, log collection, dependency graphs, remote deployment, package
installation, secret management, or application lifecycle policy.

Native backend directives already emitted by the project are compatibility
boundaries. Do not expand them into a generic service-description language.

## Backend Boundaries

Preserve concrete backend behavior:

- systemd writes `/etc/systemd/system/init-<name>.service`, reloads systemd, and
    enables the unit;
- runit writes `/etc/sv/init-<name>/run` and links it under `/etc/service`;
- OpenRC writes `/etc/init.d/init-<name>` and adds it to the default runlevel;
- SysV writes an init script and `S99` links for runlevels 2 through 5;
- Windows writes a Startup-folder `.cmd` file and attempts a matching Run value.

Do not silently change service names, paths, runlevels, enable behavior, user
selection, command wrapping, or cleanup targets.

Keep platform support truthful. iOS has no supported startup backend. Successful
cross-compilation does not establish runtime behavior.

## Metadata and Compatibility

Preserve `KC_INIT_DIR`, `KC_INIT_BACKEND`, the default system metadata directory,
and fallback reads from that default directory. Command, `.user`, and `.backend`
files are plain local state and must remain inspectable.

Commands are currently one line and bounded by 4,096-byte buffers. Keys become
filesystem names, unit names, symlink names, registry value names, and fragments
of shell commands. Any validation change must define separators, traversal,
shell metacharacters, length, existing compatibility, and confinement.

Do not replace plain metadata with a database, registry service, remote catalog,
or opaque serialized format.

## Security and Privilege

Names and commands are trusted administrator input. Commands are embedded into
shell scripts, service files, command lines, and registry values, then executed
through shell facilities. Never derive them from untrusted data.

Preserve direct privilege boundaries and legible failures. Do not add hidden
elevation helpers, policy agents, credential storage, remote authorization, or
automatic privilege escalation beyond the current explicit `sudo` re-exec.

Generated artifacts must have deliberate paths and permissions. Security work
should prioritize path confinement, shell quoting, partial-update cleanup,
symlink safety, backend command construction, and exact ownership.

## Execution and Failure Model

Immediate execution is synchronous and uses the stored command. It does not
start the generated service artifact or report service-manager state. Current
`system()` handling treats command launch as success even when the child exits
nonzero; do not document stronger status propagation than implemented.

Updates can touch metadata and privileged backend files before a later step
fails. Changes must keep rollback and partial state explicit. Deletion is
best-effort and missing registrations are nonfatal.

The context stop flag does not cancel shell or backend operations. Do not claim
operational cancellation without implementing and testing it.

## Public API and Ownership

Treat `src/libinit.h` as a compatibility boundary. Preserve option ownership,
status codes, callback signatures, path lifetime, list fields, backend selection,
and synchronous operations unless explicitly instructed otherwise.

Options own allocated directory and backend strings. Contexts copy only the
resolved path and backend value. Returned paths and list callback strings are
borrowed.

## Source Layout

Preserve exactly:

- `src/init.c` for CLI parsing, command assembly, and POSIX sudo re-exec;
- `src/libinit.c` for metadata, backend, artifact, and execution logic;
- `src/libinit.h` for the public API;
- `src/test.c` for all tests.

Do not create additional source, header, backend, platform, service, metadata,
or test files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add container runtimes, Kubernetes, remote agents, configuration
management platforms, service meshes, cluster schedulers, deployment servers,
databases, distributed locks, user accounts, OAuth, SSO, tenant models,
dashboards, telemetry, analytics, distributed tracing, fleet management, plugin
systems, hosted control planes, or cloud startup APIs.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover explicit and
detected backends, metadata round trips, one-line command limits, user metadata,
generated artifact bytes and permissions, enable and disable commands, partial
failure cleanup, missing entries, immediate execution status, key confinement,
privilege behavior, and Windows registry and Startup paths.

Use isolated compile-time or test paths for artifact tests. Never modify the
operator's real startup configuration during ordinary tests. Do not weaken tests
to accommodate an implementation change.

## Build and Completion

For documentation-only changes run `kcs AGENTS.md DESIGN.md`. For behavior
changes use the repository build and tests without cleaning unless authorized.

A change is complete when metadata, generated native artifacts, privilege and
shell boundaries, backend cleanup, ownership, tests, and documentation agree.

The goal is one small, direct startup-registration tool.
