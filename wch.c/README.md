# wch.c - File and Directory Change Watcher

`wch.c` provides an asynchronous file-watching library and a managed command-line
product built on top of it.

The library observes one file or directory and emits normalized `add`, `upd`,
and `del` events through a callback. Native waiting happens outside the
caller's execution thread.

The consumer model projects directly to JavaScript and Lua:

```js
const watcher = wch(path, {
    recursive: true
});

watcher.on(event => {
    // { type: "add", path: "..." }
    // { type: "upd", path: "..." }
    // { type: "del", path: "..." }
});

watcher.close();
```

Native backends are inotify on Linux, kqueue on macOS/BSD, and
`ReadDirectoryChangesW` on Windows.

---

## CLI

The `wch` executable manages named resident watchers. A registration associates
a name with a path, recursive mode, and a command.

Register a watcher:

```bash
wch frontend ./src make build
```

Register a recursive watcher:

```bash
wch frontend --recursive ./src make build
```

Registering an existing name replaces the previous resident watcher.

Each filesystem event starts the registered command with two arguments appended:

```text
<command...> <event> <path>
```

For example, an update to `./src/app.c` from the registration above dispatches:

```bash
make build upd ./src/app.c
```

The resident watcher continues observing while dispatched commands execute.

List all registrations:

```bash
wch --list
```

List one registration:

```bash
wch frontend --list
wch --list frontend
```

The list output contains the registration name, runtime status, watch mode,
path, and command:

```text
frontend    running    recursive    ./src    make build
```

Stop and remove a watcher:

```bash
wch frontend --delete
wch --delete frontend
```

Deleting a watcher terminates its resident process and removes its registration
state.

### CLI forms

| Command | Description |
| :--- | :--- |
| `wch <name> <path> <command...>` | Register or replace a watcher |
| `wch <name> -r <path> <command...>` | Register or replace a recursive watcher |
| `wch -l`, `wch --list` | List registered watchers |
| `wch <name> -l`, `wch --list <name>` | List one watcher |
| `wch <name> -d`, `wch --delete <name>` | Stop and remove one watcher |
| `wch -h`, `wch --help` | Show help and usage |
| `wch -v`, `wch --version` | Show version |

Watcher names may contain letters, digits, `-`, `_`, and `.`.

### Runtime state

The CLI stores registration metadata and resident process IDs in a per-user
runtime directory. Set `KC_WCH_DIR` to override that location.

Runtime state belongs to the CLI product only. It is not part of the
`libwch` API.

---

## Public API

```c
#include "libwch.h"

static void changed(const kc_wch_event_t *event, void *userdata) {
    (void)userdata;
    printf("%d:%s\n", event->type, event->path);
}

int main(void) {
    kc_wch_t *watcher = NULL;
    kc_wch_options_t options = { 0 };

    options.recursive = 1;

    if (kc_wch_open(&watcher, ".", &options) != KC_WCH_OK) {
        return 1;
    }

    if (kc_wch_on(watcher, changed, NULL) != KC_WCH_OK) {
        kc_wch_close(watcher);
        return 1;
    }

    /* Application continues doing other work here. */

    kc_wch_close(watcher);
    return 0;
}
```

### API semantics

- `kc_wch_open()` creates one watcher associated with a copied path. Passing
    `NULL` options selects non-recursive behavior.
- Existing directories are watched directly.
- Existing files and missing file targets are watched through their parent
    directory and filtered by basename. The parent directory must exist.
- `kc_wch_on()` registers one handler and starts asynchronous observation.
    Calling it again on the same watcher returns `KC_WCH_ERROR`.
- The handler receives normalized `KC_WCH_ADD`, `KC_WCH_UPD`, or
    `KC_WCH_DEL` events.
- The handler runs on the watcher worker thread. `event->path` is borrowed and
    valid only for that callback invocation.
- `kc_wch_close()` stops observation and releases the watcher. It is NULL-safe
    and may be called from inside the event handler.
- `kc_wch_version()` returns the generated build version.

Event types map mechanically to consumer values:

```text
KC_WCH_ADD -> "add"
KC_WCH_UPD -> "upd"
KC_WCH_DEL -> "del"
```

There is no public polling API.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/`.

```bash
make
```

### Tests

Build project artifacts first, then run the native contract suite:

```bash
make
make test
```

Run the Windows contract suite through Wine after building Windows artifacts:

```bash
make x86_64/windows
make test wine
```

The reusable library contract cases cover `open`, `on`, `close`, and
`version`, including asynchronous filesystem notification delivery.

### Multiarch Builds

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
- `gcc` or `clang` with C11 support

### Native Facilities

- Linux: inotify and pthreads
- macOS / iOS: kqueue and pthreads
- Windows: `ReadDirectoryChangesW` and native threads
- Android: native filesystem notification support and pthreads

### Optional Cross-Compilation SDKs

Required only for corresponding multiarch targets:

- MinGW for Windows cross-compilation
- Wine for Windows contract tests on Linux
- osxcross for macOS and iOS targets
- Android NDK for Android targets

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3
(GPLv3)**.
