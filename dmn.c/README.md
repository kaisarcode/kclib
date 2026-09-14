# dmn.c - IPC Daemon Manager

`dmn.c` provides a small C library and CLI for daemonizing named application processes behind local IPC endpoints. Applications can register a command under a name, keep it resident, relay stdin/stdout through it, list active registrations, remove them, and dispatch signals to managed processes.

On POSIX, `dmn.c` uses Unix Domain Sockets and signals (`kill`). On Windows, it uses Named Pipes and named kernel events (`CreateEvent`/`SetEvent`). The CLI keeps the direct named-runtime workflow and is implemented on top of `libdmn`.

---

## CLI

### Examples

Start or update a daemon:

```bash
dmn mydaemon /bin/cat
```

Relay stdin to a daemon:

```bash
printf 'hello\n' | dmn mydaemon
```

Resident backends keep their process state across relays. A backend may emit
EOT (`ASCII 4`) to mark the end of one response while staying alive for the
next request. This allows the backend to remain loaded and preserve its
conversation state (e.g., KV cache) across multiple relay cycles.

List all daemons:

```bash
dmn --list
dmn mydaemon --list
```

Remove a daemon:

```bash
dmn --delete mydaemon
dmn mydaemon --delete
```

Send a signal (SIGUSR1 = 10) to a daemon:

```bash
dmn mydaemon -s 10
```

On POSIX, the signal is delivered to the backend process via `kill(pid, signo)`.
On Windows, `dmn` fires a named kernel event that the child process can wait on
via `WaitForSingleObject`.

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `<name> <cmd>` | Start or update a resident daemon command. Replaces any existing one. |
| `<name> -d`, `<name> --delete` | Remove a daemon if it exists. |
| `<name> -l`, `<name> --list` | List one daemon if it exists. |
| `-l`, `--list` | List all daemons as `name<TAB>socket`. |
| `<name> -s <sig>`, `<name> --signal <sig>` | Send a signal to a daemon process. |
| `<name>` | Relay standard input to a daemon. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

### Types and Status

```c
typedef struct kc_dmn kc_dmn_t;

#define KC_DMN_OK      0
#define KC_DMN_ERROR  -1
#define KC_DMN_ESTOP  -3
```

### Functions

| Function | Returns | Description |
| :------- | :------ | :---------- |
| `kc_dmn_version(void)` | `uint64_t` | Return the build version timestamp. |
| `kc_dmn_options_default(void)` | `kc_dmn_options_t` | Return default options. |
| `kc_dmn_options_load_env(opts)` | `void` | Load `KC_DMN_DIR` into options. |
| `kc_dmn_options_free(opts)` | `void` | Release resources owned by options. |
| `kc_dmn_open(out, opts)` | `int` | Allocate and initialize a daemon manager context. |
| `kc_dmn_close(ctx)` | `void` | Release a context. |
| `kc_dmn_stop(ctx)` | `int` | Request stop for a context. |
| `kc_dmn_path(ctx)` | `const char *` | Return the resolved runtime directory. |
| `kc_dmn_update(ctx, key, cmd)` | `int` | Register or replace a named daemon command. |
| `kc_dmn_delete(ctx, key)` | `int` | Remove a named daemon registration. |
| `kc_dmn_list(ctx, key, cb, userdata)` | `int` | List one or all daemon registrations. |
| `kc_dmn_relay(ctx, key)` | `int` | Relay stdin/stdout through a named daemon. |
| `kc_dmn_signal(ctx, key, signo)` | `int` | Signal a managed daemon process. |
| `kc_dmn_connect(ctx, key, out_handle)` | `int` | Connect to a daemon for manual relay. |
| `kc_dmn_send(handle, data, len)` | `int` | Send data to a connected daemon. |
| `kc_dmn_recv(handle, buf, cap)` | `int` | Receive data from a connected daemon. |
| `kc_dmn_disconnect(handle)` | `int` | Disconnect from a daemon. |

### Example

```c
#include "libdmn.h"

void on_entry(const char *key, const char *sock, void *userdata) {
    printf("%s\t%s\n", key, sock);
}

kc_dmn_options_t opts = kc_dmn_options_default();
kc_dmn_t *ctx = NULL;

if (kc_dmn_open(&ctx, &opts) == KC_DMN_OK) {
    kc_dmn_update(ctx, "app", "/bin/cat");
    kc_dmn_list(ctx, "app", on_entry, NULL);
    kc_dmn_signal(ctx, "app", 10);
    kc_dmn_delete(ctx, "app");
    kc_dmn_close(ctx);
}

kc_dmn_options_free(&opts);
```

---

## Lifecycle

- `kc_dmn_open()` - resolves runtime state and returns a context owned by the caller.
- `kc_dmn_update()` - starts or replaces a named resident daemon command.
- `kc_dmn_relay()` - relays stdin/stdout through a named daemon.
- `kc_dmn_signal()` - sends a signal (POSIX) or fires a named event (Windows) to a daemon.
- `kc_dmn_list()` - lists all daemon registrations or one named registration.
- `kc_dmn_delete()` - removes a named daemon registration and terminates its process.
- `kc_dmn_path()` - returns the resolved runtime directory for diagnostics.
- `kc_dmn_close()` - releases the context.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile the test executable, link dynamically against the generated shared library, and run directly.

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
- No additional system libraries required.

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
