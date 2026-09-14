# init.c - Persistent Startup Registration

`init.c` provides a small C library and CLI for registering named commands as persistent startup entries using only classic OS startup filesystem primitives. Applications can register a command under a name, execute it immediately, list registrations, and remove them.

On POSIX, `init.c` writes executable scripts to `/etc/init.d/` and creates `S99` symlinks in `/etc/rc{2,3,4,5}.d/`. On Windows, it creates `.cmd` launchers in the user Startup folder and optionally adds a `HKCU\...\Run` registry entry. Registration metadata is stored under `~/.local/share/init/` on Linux and `%APPDATA%\init\` on Windows. The CLI is implemented on top of `libinit`.

---

## CLI

### Examples

Register a startup command:

```bash
init myapp /usr/local/bin/myapp --daemon
```

Execute the registered command immediately:

```bash
init myapp
```

List all registrations:

```bash
init --list
```

List one registration:

```bash
init myapp --list
```

```bash
init --list myapp
```

Remove a registration:

```bash
init myapp --delete
```

```bash
init --delete myapp
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `<name> <cmd>` | Register or replace a startup entry. |
| `<name> -d`, `<name> --delete` | Remove a registration if it exists. |
| `-d <name>`, `--delete <name>` | Remove a registration if it exists. |
| `<name> -l`, `<name> --list` | List one registration if it exists. |
| `-l <name>`, `--list <name>` | List one registration if it exists. |
| `-l`, `--list` | List all registrations as `name<TAB>path`. |
| `<name>` | Execute the registered command immediately. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

```c
#include "libinit.h"

void on_entry(const char *key, const char *user, const char *cmd, void *userdata) {
    printf("%s\t[%s]\t%s\n", key, user, cmd);
}

kc_init_options_t opts = kc_init_options_default();
kc_init_options_load_env(&opts);

kc_init_t *ctx = kc_init_open(&opts);

kc_init_update(ctx, "app", "/usr/local/bin/app --daemon");
kc_init_list(ctx, "app", on_entry, NULL);
kc_init_exec(ctx, "app");
kc_init_delete(ctx, "app");

kc_init_close(ctx);
kc_init_options_free(&opts);
```

---

## Lifecycle

- `kc_init_options_default()` - creates default init options.
- `kc_init_options_load_env()` - applies `KC_INIT_*` environment overrides.
- `kc_init_options_free()` - releases option resources.
- `kc_init_open()` - resolves metadata state and returns a context owned by the caller.
- `kc_init_update()` - registers or replaces a named startup command.
- `kc_init_exec()` - executes the registered command immediately.
- `kc_init_list()` - lists all registrations or one named registration.
- `kc_init_delete()` - removes a named registration and its startup artifacts.
- `kc_init_path()` - returns the resolved metadata directory for diagnostics.
- `kc_init_close()` - releases the context.

---

## Environment

| Variable | Description |
| :--- | :--- |
| `KC_INIT_DIR` | Metadata directory path. |
| `KC_INIT_BACKEND` | POSIX backend override: `systemd`, `runit`, `openrc`, or `sysv`. |

---

## Notes

- `kc_init_update` requires write access to `/etc/init.d/` and `/etc/rc*.d/` on Linux, which typically requires root.
- `kc_init_exec` reads from the user metadata directory and does not require root.
- No service managers, PID1 replacements, or external dependencies are used.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only the test executable and link dynamically against the generated shared library.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

**Note:** iOS targets are not available. `init.c` manages system services using `system()` calls, which are explicitly unavailable in the iOS SDK. iOS apps run in a sandboxed environment with no equivalent to systemd or init systems - Apple's launchd is not accessible to third-party apps.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- `advapi32`

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
