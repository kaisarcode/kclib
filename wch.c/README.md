# wch.c - File and Directory Change Watcher

`wch.c` exposes one asynchronous watcher for a file or directory. Observation
runs in a native worker thread and events are delivered through one callback.
The caller does not poll or block its own execution thread.

The consumer model is designed to project naturally to JavaScript and Lua:

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

The native backends remain platform-specific: inotify on Linux, kqueue on
macOS/BSD, and `ReadDirectoryChangesW` on Windows.

---

## CLI

The CLI is a blocking process adapter over the same asynchronous watcher.

Watch the current directory:

```bash
./bin/x86_64/linux/wch .
```

Watch recursively:

```bash
./bin/x86_64/linux/wch . --recursive
```

Watch one file, including a file that does not exist yet:

```bash
./bin/x86_64/linux/wch ./file.txt
```

Events are emitted one per line:

```text
add:/tmp/demo/a.txt
upd:/tmp/demo/a.txt
del:/tmp/demo/a.txt
```

| Flag | Description |
| :--- | :--- |
| `<path>` | File or directory to watch |
| `-r`, `--recursive` | Watch directories recursively |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

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
    `NULL` options uses non-recursive defaults.
- Existing directories are watched directly.
- Existing files and missing file targets are watched through their parent
    directory and filtered by basename. The parent directory must exist.
- `kc_wch_on()` registers one handler and starts asynchronous observation.
    Calling it again on the same watcher returns `KC_WCH_ERROR`.
- The handler receives normalized `KC_WCH_ADD`, `KC_WCH_UPD`, or
    `KC_WCH_DEL` events.
- The handler runs on the watcher worker thread. `event->path` is borrowed and
    is valid for the duration of that callback. Copy it if it must outlive the
    callback.
- `kc_wch_close()` stops the worker and releases the watcher. It is NULL-safe
    and may also be called from inside the event handler.
- `kc_wch_version()` returns the generated build version.

The binding maps event types mechanically:

```text
KC_WCH_ADD -> "add"
KC_WCH_UPD -> "upd"
KC_WCH_DEL -> "del"
```

No polling API is exposed publicly. Native waiting happens in the worker, so a
JavaScript or Lua consumer can remain event-driven without blocking its main
execution thread.

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

The reusable contract cases cover `open`, `on`, `close`, and `version`,
including real asynchronous filesystem notification delivery.

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

Required only for the corresponding multiarch targets:

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
