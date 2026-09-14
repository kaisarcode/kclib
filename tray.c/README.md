# tray.c - Native System Tray

`tray.c` is a small C library (`libtray`) and CLI (`tray`) that provides native system tray / notification-area / status-bar integration for Windows, Linux, and macOS. The CLI builds one tray icon and lightweight menu from command-line arguments and runs an explicitly configured local program when a menu item is activated. It is independent of WebViews, browsers, and other kclib runtimes.

---

## CLI

Build the tray binary first, then run it with a menu configuration:

```bash
make
./bin/x86_64/linux/tray --icon "/path/to/icon.png" \
    --tooltip "Local weather" \
    --item "Refresh weather" "refresh" --exec /usr/local/bin/weather \
        --arg "--station" --arg "City Center" \
    --sep \
    --quit "Quit"
```

The tray stays resident. Selecting `Refresh weather` runs `/usr/local/bin/weather --station "City Center"` once. Selecting `Quit` leaves the tray.

> Running a menu item requires an active desktop session with a system tray / notification area (for example MATE's notification area, GNOME Legacy Tray, xfce4-panel, or a Windows/macOS notification area).

### Parameters

| Flag | Description |
| :--- | :--- |
| `--icon <path>` | Icon file path, or Linux icon name, e.g. `system-run` |
| `--tooltip <text>` | Hover tooltip text |
| `--item <label> <action>` | Add a menu item; `action` is the stable item identity |
| `--sep` | Add a menu separator |
| `--quit <label>` | Add an explicit exit item that leaves the tray |
| `--exec <path>` | Program to run when a menu item is activated; an executable path or a program name resolved through `PATH` |
| `--arg <value>` | One argument passed to the program; repeatable, order preserved |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

CLI flags override environment variables, which override built-in defaults.

The following environment variables map to the parameters above:

`KC_TRAY_ICON`, `KC_TRAY_TOOLTIP`

### Process Execution

Activation spawns the configured executable with its configured arguments:

* the executable may be an explicit filesystem path or a program name resolved through `PATH`;
* executable identity and arguments stay separate;
* each `--arg` value is one exact argument, spaces preserved;
* shell metacharacters stay literal;
* no shell is involved;
* execution is one-shot and the tray remains resident.

The `--quit` item maps to the reserved `kc:tray:quit` action, which is tray
process lifecycle behavior only. Caller-defined program actions are never
interpreted. A normal `--item` may not use the reserved `kc:tray:quit` action,
and executable actions must each be distinct; both malformed configurations are
rejected on stderr during parsing.

---

## Public API

```c
#include "libtray.h"

kc_tray_options_t opts = kc_tray_options_default();
kc_tray_t *ctx = NULL;

kc_tray_options_set(opts, "icon", "/path/to/icon.png");
kc_tray_options_set(opts, "tooltip", "Local weather");

if (kc_tray_open(&ctx, opts, callback, NULL) == KC_TRAY_OK) {
    kc_tray_item_t items[] = {
        { "Refresh", "refresh" },
        { NULL, NULL },
        { "Exit", "exit" },
    };
    kc_tray_set_menu(ctx, items, 3);
    kc_tray_run(ctx);
    kc_tray_close(ctx);
}

kc_tray_options_free(opts);
```

`callback(userdata, action)` receives the opaque caller-defined action identity of the activated item. NULL label and action in a `kc_tray_item_t` marks a separator. All strings are copied; the caller keeps ownership of its own arrays. `kc_tray_set_menu()` replaces the active menu atomically and `items == NULL` with a zero count clears it.

---

## Lifecycle

- `kc_tray_options_default()` - returns caller-owned options.
- `kc_tray_options_set()` - sets one option; supported keys are `"icon"` and `"tooltip"`.
- `kc_tray_open()` - allocates a caller-owned context and copies effective option state; requires a desktop session on each backend. Each open creates an independent context where the native platform permits independent instances.
- `kc_tray_set_menu()` - replaces the active menu from caller-supplied items.
- `kc_tray_run()` - owns the blocking native event loop for one context until `kc_tray_stop()`. A stop requested before `run()` causes `run()` to return promptly without entering a blocking loop; repeated `run()` after a stop returns promptly.
- `kc_tray_stop()` - requests event-loop termination for one context; context-local. Wakes a blocked native loop promptly, is harmless when repeated, and does not destroy the context. Stopping one context does not terminate another independently running context.
- `kc_tray_close()` - releases the context and all native resources; the ownership-destruction boundary, distinct from stop.
- `kc_tray_get_error()` - returns the last error message, or NULL.
- `kc_tray_version()` - returns the build version.

Platform lifecycle notes:
- Windows: each context owns a hidden message window; stop wakes that context's message loop without terminating the thread's message processing.
- Linux: each running context uses its own loop object, so stopping one context does not globally stop GTK processing.
- macOS: stop wakes the Cocoa event loop by posting an application-defined event; context-stop does not require a thread-global termination mechanism. The Objective-C backend is compiled without ARC; `alloc`/`init` objects are handed to their retaining containers with balanced autorelease ownership.

Multiple contexts can be opened and stopped independently on every platform. A blocking `run()` owns the calling thread's event loop, so concurrent blocking runs follow each platform's native model: one Windows message loop per thread, one GTK default main context, and one Cocoa application event loop. A stop on one context never terminates another context's loop.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile the test executable, link dynamically against the generated shared library, and run directly. Native tray cases require a desktop session.

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

The project builds artifacts for supported architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make aarch64/macos
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
- `GTK+ 3` development package (`libgtk-3-dev`)
- `libpthread`, `libm`, `libdl`

Windows (MinGW):
- No additional system libraries required (`user32`, `shell32` are linked)

macOS:
- `Cocoa` and `Foundation` frameworks

### Runtime Requirements

- Linux: a desktop session with a system tray / notification area host; the environment variable `GDK_BACKEND` should not be forced to a bare headless backend.
- Windows: no additional runtime required.
- macOS: no additional runtime required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with a macOS SDK for macOS targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
