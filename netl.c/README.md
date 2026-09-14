# netl.c - Network Listener

`netl.c` is a small C library and CLI for registering named TCP or UDP listeners that dispatch each incoming connection or datagram to a shell command.

---

## CLI

### Examples

Register a TCP listener on port 8080:

```bash
netl proxy 127.0.0.1:8080 cat
```

Start the registered listener:

```bash
netl proxy
```

List all registrations:

```bash
netl --list
```

List one registration:

```bash
netl proxy --list
```

Remove a registration:

```bash
netl proxy --delete
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `<name> <addr>[:port] <cmd>` | Register or replace a listener. TCP by default. |
| `<name> <addr>[:port] --udp <cmd>` | Register a UDP listener. |
| `<name> -d`, `<name> --delete` | Remove a listener if it exists. |
| `<name> -l`, `<name> --list` | List one listener if it exists. |
| `-l`, `--list` | List all listeners as `name<TAB>addr:port`. |
| `<name>` | Start the registered listener. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

```c
#include "libnetl.h"

void on_entry(const char *key, const char *addrport, void *userdata) {
    printf("%s\t%s\n", key, addrport);
}

kc_netl_t *ctx;
kc_netl_options_t opts = kc_netl_options_default();
kc_netl_open(&ctx, &opts);

kc_netl_update(ctx, "proxy", "127.0.0.1", 8080, KC_NETL_TCP, "cat");
kc_netl_list(ctx, "proxy", on_entry, NULL);
kc_netl_exec(ctx, "proxy");
kc_netl_delete(ctx, "proxy");

kc_netl_close(ctx);
kc_netl_options_free(&opts);
```

---

## Lifecycle

- `kc_netl_open()` - resolves metadata state and returns a context owned by the caller.
- `kc_netl_update()` - registers or replaces a named listener.
- `kc_netl_exec()` - starts the registered listener. Blocking on success, never returns.
- `kc_netl_list()` - lists all registrations or one named registration. Calls a user-provided callback per entry, or NULL for silent iteration.
- `kc_netl_delete()` - removes a named registration.
- `kc_netl_path()` - returns the resolved metadata directory for diagnostics.
- `kc_netl_close()` - releases the context.

---

## Notes

- `kc_netl_exec` is blocking. The caller is responsible for forking if background operation is needed.
- `kc_netl_update` does not require elevated privileges.
- Registration metadata is stored under `~/.local/share/netl/` on Linux and `%APPDATA%\netl\` on Windows.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

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
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
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

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- `ws2_32`

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
