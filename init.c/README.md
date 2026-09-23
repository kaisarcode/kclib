# init.c - Persistent Startup Registration

`init.c` provides a synchronous C library and CLI for registering named commands with the startup mechanism available on the local machine.

The library supports Linux and Windows. Linux can autodetect systemd, runit, OpenRC, or SysV, or the caller can explicitly select one of those backends. Windows uses the Startup folder and Run registry integration.

## CLI

Register or replace an entry:

```bash
init myapp /usr/local/bin/myapp --daemon
```

Execute a registered command immediately:

```bash
init myapp
```

List registrations:

```bash
init --list
init myapp --list
init --list myapp
```

Delete a registration:

```bash
init myapp --delete
init --delete myapp
```

The CLI also supports:

```text
--dir <path>
--backend <name>
-h, --help
-v, --version
```

`KC_INIT_DIR` and `KC_INIT_BACKEND` remain CLI environment overrides.

On POSIX, the CLI may re-execute itself through an installed `sudo` command before operations that currently require its elevation policy. Privilege escalation is CLI policy; the reusable library never invokes `sudo`.

## Public API

A `kc_init_t` represents one configured local startup registry.

```c
#include "libinit.h"

kc_init_options_t options = {
    .dir = NULL,
    .backend = NULL
};

kc_init_t *startup = NULL;

if (kc_init_open(&startup, &options) != KC_INIT_OK) {
    return 1;
}

if (kc_init_set(
        startup,
        "myapp",
        "/usr/local/bin/myapp --daemon"
    ) != KC_INIT_OK) {
    const char *error = kc_init_error(startup);
    (void)error;
}

kc_init_entry_t *entries = NULL;
size_t count = 0;

if (kc_init_list(startup, NULL, &entries, &count) == KC_INIT_OK) {
    size_t i;

    for (i = 0; i < count; i++) {
        /* entries[i].key, entries[i].user, entries[i].cmd */
    }
}

kc_init_free(entries);
kc_init_close(startup);
```

## Options

`kc_init_options_t` is a plain public structure:

```c
typedef struct {
    const char *dir;
    const char *backend;
} kc_init_options_t;
```

`dir == NULL` selects the platform default metadata directory.

On Linux, `backend == NULL` autodetects the local startup backend. An explicit backend must be one of:

```text
systemd
runit
openrc
sysv
```

An unknown explicit backend is an error.

The library copies the resolved directory and backend state during `kc_init_open()`. Option strings only need to remain valid for the duration of the call.

## Registrations

Registration keys are names, not paths. Valid keys contain only ASCII letters, digits, dot, underscore, or hyphen. Empty keys, `.`, `..`, path separators, and other characters are rejected.

Commands remain shell commands by design. They must be non-empty, one line, and shorter than the library command buffer limit.

`kc_init_set()` registers or replaces an entry.

`kc_init_exec()` executes the stored command synchronously. A missing registration returns `KC_INIT_NOT_FOUND`. A non-zero command exit status returns `KC_INIT_ERROR`.

`kc_init_delete()` removes the registration and its startup artifacts. Deleting an entry that does not exist is a successful no-op.

## Listing and ownership

`kc_init_list()` returns a finite synchronous result:

```c
kc_init_entry_t *entries = NULL;
size_t count = 0;

kc_init_list(startup, NULL, &entries, &count);
kc_init_free(entries);
```

Passing a key lists only that registration. A missing specific key succeeds with an empty result.

The returned entry array and all `key`, `user`, and `cmd` strings share one allocation. Release it once with `kc_init_free()`.

Internal metadata sidecars such as `.user` and `.backend` are never exposed as registrations.

## Errors

Public status codes are:

```text
KC_INIT_OK          0
KC_INIT_NOT_FOUND   1
KC_INIT_ERROR      -1
```

`kc_init_error()` returns borrowed context-owned error text after an operation reports additional error detail. The pointer remains owned by the context.

## Platform behavior

Linux startup artifacts are backend-native:

- systemd: `/etc/systemd/system/init-<name>.service`
- runit: `/etc/sv/init-<name>/run` and `/etc/service/init-<name>`
- OpenRC: `/etc/init.d/init-<name>`
- SysV: init script plus runlevel links

Windows writes a Startup-folder `.cmd` launcher and attempts the corresponding Run registry value.

Metadata remains plain local files. The library does not provide process supervision, service state, restart orchestration, logging, remote deployment, or a resident daemon.

## WASM

WASM support is not applicable. Browser WASM cannot register startup services or modify the host operating system's startup configuration without changing the capability contract.

## Build

A plain build targets the current supported host:

```bash
make
```

Supported artifact platforms are Linux and Windows.

Multi-architecture builds:

```bash
make all
```

## Tests

Build artifacts before running contract tests:

```bash
make
make test
```

Windows artifacts can be validated through Wine:

```bash
make x86_64/windows
make test wine
```

The contract suite exercises the reusable API through isolated metadata fixtures and contains one grouped `kc_init_cli` case for the shipped command-line interface.

## Dependencies

Build tools:

- GNU Make
- CMake 3.14 or newer
- Ninja
- a C11 compiler

Windows links against `advapi32`.

Optional development tools include MinGW for Windows cross-compilation and Wine for Windows test execution.
