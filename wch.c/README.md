# wch.c - File and Directory Change Watcher

`wch` is a portable native file change notification tool. It watches files and directories for changes and emits `add`, `upd`, and `del` events.

Uses the native kernel API on each platform: inotify (Linux), kqueue (macOS/BSD), ReadDirectoryChangesW (Windows). No polling, no loops - the process blocks in the kernel until a change occurs.

---

## CLI

### Examples

Watch the current directory recursively:

```bash
./bin/x86_64/linux/wch .
```

Watch a single file (even if it doesn't exist yet):

```bash
./bin/x86_64/linux/wch ./file.txt
```

### Output example

```bash
$ ./bin/x86_64/linux/wch /tmp/demo &
[1] 12345
$ echo "first" > /tmp/demo/a.txt
$ echo "more" >> /tmp/demo/a.txt
$ rm /tmp/demo/a.txt
add:/tmp/demo/a.txt
upd:/tmp/demo/a.txt
del:/tmp/demo/a.txt
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `<path>` | Path to file or directory |
| `-r`, `--recursive` | Watch directories recursively |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

### Output

Events are emitted one per line in `type:path` format:

```
add:/tmp/dir/newfile.txt
upd:/tmp/dir/existing.txt
del:/tmp/dir/oldfile.txt
```

---

## Public API

```c
#include "libwch.h"

kc_wch_t *w = NULL;
if (kc_wch_open(&w, "/path/to/watch", 1) == KC_WCH_OK) {
    kc_wch_event_t ev;
    while (kc_wch_poll(w, &ev, -1) == KC_WCH_EVENT) {
        switch (ev.type) {
        case KC_WCH_ADD: /* add */ break;
        case KC_WCH_UPD: /* update */ break;
        case KC_WCH_DEL: /* delete */ break;
        }
    }
    kc_wch_close(w);
}
```

---

## Lifecycle

- `kc_wch_open()` - Opens a watcher on the given path. Its third argument is `recursive`: `0` watches non-recursively and any nonzero value watches directories recursively. Returns `KC_WCH_OK` on success or `KC_WCH_ERROR` on failure. If the path does not exist, it watches the parent directory and filters for the target filename.
- `kc_wch_poll()` - Waits for the next file-change event. It returns `KC_WCH_EVENT` on an event, `KC_WCH_TIMEOUT` on timeout, or `KC_WCH_ERROR` on error. `timeout_ms` is `-1` to wait indefinitely, `0` for no wait, or a positive number of milliseconds to wait.
- `ev.path` is context-owned. Do not free or modify it; it is valid only after `kc_wch_poll()` returns `KC_WCH_EVENT`, until the next event from the same watcher or `kc_wch_close()`. Copy it for longer use.
- `kc_wch_close()` - Releases the watcher and all associated resources. Safe to call with `NULL`.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run directly.

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
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

### Test Dependencies

- No additional test dependencies required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
