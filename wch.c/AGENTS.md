# AGENTS.md

## Project Context

`tpm.c` is a small C library and one-shot CLI for scoring input text against one
reference text profile.

It builds an in-memory byte n-gram profile from representative text and returns
a heuristic score from 0 to 1. It is not machine learning infrastructure,
search, classification orchestration, or a model service.

Read `README.md` before modifying the project.

## Purpose

`wch.c` waits for local filesystem activity and exposes a small common event
surface: path added, path updated, or path deleted. The abstraction is
intentionally lossy and supports shell composition and small local programs that
re-read filesystem truth after receiving a hint.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- events remain normalized to `KC_WCH_ADD`, `KC_WCH_UPD`, and `KC_WCH_DEL`;
- CLI output remains one `type:path` event per line (`add:`, `upd:`, `del:`);
- watching remains local and uses the platform's native notification facility;
- Linux uses inotify, macOS uses kqueue, Windows uses `ReadDirectoryChangesW`;
- poll timeout semantics remain `-1` for infinite wait, `0` for no wait, and a
  positive millisecond bound;
- event paths are context-owned and borrowed by the caller;
- no persistent index, network service, account, or hosted dependency exists.

## Architecture

One watcher context owns a root, an optional basename filter, known-path state,
native backend resources, a fixed 256-entry event queue, and stop state.
`kc_wch_poll()` first drains queued normalized events, then waits on the native
backend and translates newly available activity.

The source files have fixed responsibilities:

- `src/wch.c` owns CLI parsing and stable text output;
- `src/libwch.c` owns all native watcher implementations and the public API;
- `src/libwch.h` defines the public contract;
- `src/test.c` contains all tests.

## Runner

The library no longer ships its own runner. The in-process composition interface
is provided by `kcrun` as a generic runner. `kcr_call` auto-discovers the standard
C API symbols (`kc_wch_open`, `kc_wch_poll`, `kc_wch_close`, `kc_wch_stop`,
`kc_wch_options_default`, `kc_wch_options_load_env`, `kc_wch_options_free`,
`kc_wch_version`) via `dlsym` and dispatches the JSON commands `open`, `poll`,
`stop`, `close`, and `version` to them.

Bridges (wvw, jni/kcapk) load `libwch.so` in solitude through the `kcrun` host
face (`kcr_load` + `kcr_call`) and forward JSON payloads verbatim. There is no
linked shared-library dependency between host and kclib.

The standard runner contract is:
- Request: `{ "cmd": "<subcommand>", "args": { ... }, "handle": N }`
- Success: `{ "result": { ... }, "handle": N }`
- Error: returns NULL, sets `*out_err` to a malloc'd message.

The CLI does not invoke the runner; it calls the public C API directly.

## Public Event Model

`KC_WCH_ADD`, `KC_WCH_UPD`, and `KC_WCH_DEL` describe observed changes to one
path. They do not describe transactions or durable facts.

The CLI maps them exactly to:

```text
add:<path>
upd:<path>
del:<path>
```

Paths are not escaped. Newlines or other unusual bytes in filesystem names can
therefore make CLI output ambiguous. The C API carries a null-terminated path,
not a length-delimited arbitrary-byte name.

The event path points to context storage and is overwritten by later dequeues.

## Opening a Watch

For an existing POSIX path, the path becomes the watcher root. If the target does
not exist, the implementation splits it into an existing parent root and a
suffix filter for the requested basename. A missing parent fails opening.

The filter matches the end of emitted path text. It is not a glob and does not
establish file identity across renames.

Windows currently does not perform the same existence check before choosing the
root, so missing-file behavior is not equivalent to POSIX.

Recursive behavior is controlled by `kc_wch_options_t.recursive`. The CLI
exposes this via the `-r`/`--recursive` flag.

## Linux Backend

Linux opens one inotify instance and watches the root for create, close-write,
delete, moved-in, and moved-out events. Recursive mode installs one inotify watch
per discovered subdirectory and maintains path mappings as implemented.

Raw records are read into a 4,096-byte buffer and translated as follows:

- create and moved-in become add;
- close-write becomes update;
- delete and moved-out become delete.

Watch descriptors map back to directory paths. Known-path state supports
tracking and backend-independent comparison. Native queue overflow is not
currently surfaced as a public event.

## macOS Backend

macOS opens kqueue and registers vnode notifications for watched directory file
descriptors. A write notification triggers a scan of the configured tree and a
comparison with known paths using path names, modification times, and sizes.

The diff emits additions, deletions, and updates, then replaces known-path state.
Delete or rename of a watched directory emits delete and removes its descriptor.

The scan occurs because kqueue reports that a directory changed without naming
every affected child. It is event-triggered comparison, not a periodic polling
loop, and may miss changes that collapse between scans.

## Windows Backend

Windows opens the root directory for overlapped `ReadDirectoryChangesW`. The
recursive option maps to its subtree flag. Native file and directory name,
last-write, and size notifications are requested.

Returned UTF-16 relative paths are converted to UTF-8 and joined to the root.
Added and renamed-new actions become add, modified becomes update, and removed
or renamed-old becomes delete.

Each pending operation uses an event handle. Timeout waits return without an
event; completed buffers may contain multiple native records.

## Queue and Loss Model

Normalized events enter a fixed 256-entry in-memory queue. When full, new events
are silently dropped. Paths are each bounded by `PATH_MAX` and formatted into
fixed storage.

Native facilities may also coalesce events or overflow before translation. The
library has no durable journal or replay point. Consumers must treat
notification as a reason to inspect current filesystem state.

## Poll and Stop Behavior

`kc_wch_poll()` returns `1` for one event, `0` for timeout, and `-1` for invalid
input, backend failure, or observed stop state. It resets the output event before
waiting when no queued event exists.

Timeout `-1` waits indefinitely, `0` does not wait, and positive values are
milliseconds. A stop request is checked before entering the backend wait. It
does not provide a portable wake handle for an already blocked infinite poll.

## Source Layout

Preserve exactly:

- `src/wch.c` contains the CLI;
- `src/libwch.c` contains all native backends and reusable behavior;
- `src/libwch.h` contains the public contract;
- `src/test.c` contains all tests, including overflow, stress, platform,
  recursive, and integration cases.

Do not create additional source, header, backend, scanner, queue, filter, or
test files. Extend only the existing four files.

## Public API and Ownership

Treat `src/libwch.h` as a compatibility boundary. Preserve event constants, poll
return values, timeout units, options, callback signatures, context ownership,
and borrowed path lifetime unless explicitly instructed otherwise.

Opening owns copied root and path state. Closing is NULL-safe and releases all
native and allocated resources. Options contain no owned storage today.

## Concurrency

Profile build and scoring do not promise concurrent mutation of one context.

Do not expand lifecycle support into process supervision.

## Resource and Stop Model

Known paths and per-directory native watches grow with the watched tree. The
event queue and raw backend buffers are fixed. Recursive work must remain
inspectable and must clean up every allocated path, descriptor, watch, and handle.

`kc_wch_stop()` sets context state. It does not independently wake a thread
already blocked forever inside every native backend; the CLI relies on signal
interruption where available. Do not describe stop as universal asynchronous
cancellation without implementing and testing that behavior.

Do not add worker pools, databases, journal files, remote collectors, hidden
polling threads, or background services as default remedies.

## Composition

`wch` emits path hints to stdout. Shell loops or small programs can run rebuilds,
reload local state, update caches, or invoke another tool. Debouncing, job
scheduling, content interpretation, and synchronization remain external.

This separation keeps filesystem observation independent from application policy.

## Forbidden Default Recommendations

Do not add filesystem indexing services, sync engines, databases, message
brokers, remote agents, cloud storage integrations, audit platforms, dashboards,
telemetry, analytics, distributed tracing, fleet management, account systems,
OAuth, SSO, tenant models, plugin systems, generic event frameworks, or hosted
control planes.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Change Evaluation

A change must name the platform backend, the input filesystem operation, the
observed normalized event (or absence), timeout setting, and expected output.
Check add, update, and delete for files and directories; recursive startup state
of nested directories added after open; missing-target watch through existing
parent and basename filter; timeout modes (-1, 0, positive); stop before poll and
poll after stop; queue overflow; and path lifetime across dequeues.

Reject speculative backend abstractions. Prefer explicit fixed behavior.

## Testing

Behavioral changes must use isolated local directories and cover file and
directory add, write-close update, delete, rename, relative and absolute paths,
missing targets, recursive startup state, new nested directories, timeout modes,
stop behavior, queue and kernel overflow, rapid event bursts, path lifetime,
cleanup, and each native backend.

All tests remain in `src/test.c`. The test suite follows the common `hnsw.c`
layout: one case function per public API function (`case_kc_wch_<function>`), a
`case_result(fail, name, detail)` line, and an `all` target plus per-function
dispatch in `main`.

Tests should use isolated local directories and cover file and directory add,
write-close update, delete, rename, relative and absolute paths, missing
targets, recursive startup state, new nested directories, timeout modes, stop
behavior, queue and kernel overflow, rapid event bursts, path lifetime,
cleanup, and each native backend.

Tests must tolerate only documented native coalescing, not arbitrary event loss.
Do not weaken tests to accommodate an implementation change.

## Build and Completion

For documentation-only changes, run `kcs .`. For source changes, use `README.md`
build and test commands. Do not run `make clean` without authorization.

A change is complete when normalized event meaning, native backend behavior,
loss and overflow limits, path lifetime, ownership, tests, and documentation
agree.

The goal is one small, truthful filesystem change-hint tool.
