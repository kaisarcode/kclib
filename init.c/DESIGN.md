# init.c Design

## Purpose

`init.c` registers one-line commands with a machine's existing startup system.
It gives local operators one narrow interface over several native startup
artifact formats while keeping those artifacts visible and conventional.

The project does not replace the operating system's service manager. It writes
configuration that the existing manager consumes.

## Architecture

The CLI parses options and operations, optionally re-executes through `sudo` on
POSIX, and delegates to one library context. The context holds a metadata path,
one selected backend, and a stop flag.

The four source files have fixed responsibilities:

- `src/init.c` owns CLI parsing, argument joining, elevation, and direct calls to the public C API;
- `src/libinit.c` owns metadata, all native backend behavior;
- `src/libinit.h` defines the public contract (pure C, no JSON types);
- `src/test.c` contains all tests.

The library is pure C; the CLI calls the public API directly. Parsing, backend
selection, and all execution logic live in `libinit.c` and are exercised directly
through the public contract.

## CLI Adapter

The CLI is a thin adapter over the public C API. It owns argument parsing,
environment precedence, `stdout`, `stderr`, and exit status. For each command it
opens a context with options from environment and flags, invokes the
corresponding `kc_init_*` function, and closes the context. Elevation through
`sudo` on POSIX happens in the CLI before calling the library.

There is no runner and no JSON layer. The public API is pure C; `libinit.h`
exports only `kc_init_*` functions. Metadata handling, backend dispatch, and all
version support live in `libinit.c` and are exercised directly through the public
contract.

## Backend Selection

`KC_INIT_BACKEND` or `--backend` may select `systemd`, `runit`, `openrc`,
`sysv`, or `none`. An absent, empty, unknown, or `none` value causes local
auto-detection rather than disabling registration.

Detection examines `/proc/1/exe` and available management commands, then falls
back in the order systemd, OpenRC, and SysV. Runit fallback requires PID 1 to be
identified as runit.

Each successful POSIX registration records its backend enum in
`<name>.backend` so deletion can target the original artifact type even if the
current machine detection differs later.

## Metadata

The default metadata directory is `/etc/kaisarcode/init.c` on POSIX and
`C:\ProgramData\kaisarcode\init.c` on Windows. `KC_INIT_DIR` or `--dir` replaces
the active path, while list, execute, and delete operations may also consult the
default system directory.

Each name may have:

- `<name>` containing the command without an added newline;
- `<name>.user` containing the detected original user;
- `<name>.backend` containing the numeric POSIX backend value.

Metadata is plain durable local state. The command reader consumes only its
first line into a fixed 4,096-byte buffer.

## Privilege Model

POSIX update and delete require effective UID 0. Before those operations, and
also before immediate execution, the CLI checks for `sudo` and attempts to
re-execute its own `/proc/self/exe` path when not root.

The library itself does not elevate. Direct library callers receive an error for
unprivileged POSIX update or deletion.

The original user is inferred from `SUDO_USER`, then non-root `LOGNAME`, then
non-root `USER`, otherwise `root`. Generated systemd, OpenRC, and SysV artifacts
use that value where their current templates support user execution.

## systemd

The systemd backend writes `/etc/systemd/system/init-<name>.service` with a
simple service, network ordering, `RemainAfterExit=yes`, `Restart=on-failure`,
and the stored command as `ExecStart`.

For a non-root detected user it also writes `User`, `Group`, and a
`/home/<user>` working directory. Registration reloads systemd and enables the
unit. Deletion disables and stops it, removes the unit and metadata, then reloads
systemd.

## runit

The runit backend writes `/etc/sv/init-<name>/run` as an executable shell script
containing `exec <command>`, then links that service directory at
`/etc/service/init-<name>`.

Deletion removes the link and recursively removes only the corresponding
service directory. This path construction and confinement are security-critical.

## OpenRC

The OpenRC backend writes `/etc/init.d/init-<name>` as an executable
`openrc-run` script. Its start function invokes the command through `/bin/sh`
and `start-stop-daemon`, using the detected user when non-root.

Registration adds the script to the default runlevel. Deletion removes it from
that runlevel and deletes the script and metadata.

## SysV

The SysV backend uses `/etc/init.d` or `/etc/rc.d/init.d`, writes an executable
script, and creates `S99<name>` links in runlevels 2, 3, 4, and 5.

The script embeds the stored command directly. For a non-root original user its
`start` branch uses `start-stop-daemon`; other arguments execute the command
directly. Deletion removes metadata, script, and known runlevel links.

## Windows

Windows chooses the all-users Startup folder when its administrative check
succeeds, otherwise the current user's Startup folder. Registration writes
`<name>.cmd` and attempts a matching value under the corresponding HKLM or HKCU
Run key.

Deletion removes metadata, the selected Startup launcher, and the Run value on a
best-effort basis. The launcher and registry entry both contain the command,
which may cause duplicate startup execution depending on Windows behavior.

## Immediate Execution

`kc_init_exec()` reads the active metadata file, falling back to the default
system directory, and passes the command to `system()` synchronously.

It does not ask systemd, runit, OpenRC, SysV, or Windows to start the registered
artifact. It executes the metadata command independently. Current return handling
distinguishes shell-launch failure but does not propagate a nonzero command exit
status as an init error.

## Trust Boundary

Names and commands are trusted administrator input. Names enter filesystem
paths, service identifiers, symlink names, registry values, and shell command
strings. Commands enter metadata, generated scripts, service directives,
registry data, and `system()`.

There is no quoting or sandbox layer that makes arbitrary untrusted values safe.
Input validation, path confinement, symlink handling, command construction, and
partial privileged writes are the principal security concerns.

## Ownership and Failure

Options own allocated directory and backend strings. Opening copies the path
into the context and resolves a backend enum. Returned paths and list callback
strings are borrowed.

Registration writes metadata before some native artifacts and enable commands,
so failures can leave partial state. Deletion is best effort and missing entries
are nonfatal. External manager commands are synchronous and may block.

The stop flag does not currently interrupt metadata, shell, registry, or service
manager operations.

## Composition

`init` registers a command. The command itself owns daemonization, foreground
behavior, configuration, logs, network access, security, and application state.
Native service managers retain their normal operational interfaces.

The tool should not duplicate supervision or deployment features already owned
by those systems.

## Non-Goals

The project does not provide PID 1, a resident supervisor, a portable service
runtime, remote deployment, configuration management, package installation,
health monitoring, log aggregation, secret distribution, container management,
cluster scheduling, service discovery, user accounts, telemetry, dashboards,
hosted control infrastructure, or a plugin ecosystem.

These exclusions define the tool rather than an unfinished roadmap.

## Change Criteria

A change must solve a concrete startup-registration problem, preserve native
artifact visibility, define privilege and shell effects, constrain every derived
path, handle partial writes and cleanup, record backend compatibility, state
platform differences, and avoid adding supervision or remote management scope.

Changes justified mainly by enterprise fleets, cloud integration, generalized
orchestration, or framework parity should be rejected.

## Core Invariants

The project is defined by plain command metadata, one locally selected native
backend, direct privileged artifact generation, synchronous immediate shell
execution, best-effort deletion, explicit platform behavior, and no resident or
remote control plane.
