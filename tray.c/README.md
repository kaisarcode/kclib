# tray.c - Native System Tray

`tray.c` is a C library and CLI for creating a persistent system tray and
persistent child menu items on Windows, Linux (GTK 3), and macOS (AppKit).
`kc_tray_open()` returns without running a caller-visible event loop. The
standalone `tray` program is a small consumer of the same public API.

---

## CLI

Create a tray with a greeting item and a quit item:

```bash
tray --icon /path/to/icon.png --tooltip "My app" \
  --item "Greet" --sep --quit "Quit"
```

`--item TEXT` prints `TEXT` on selection; `--quit TEXT` ends the process.
Repeated `--item` and `--sep` flags build the menu in order. The CLI does not
run external commands.

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `--icon PATH` | Set the tray icon. |
| `--tooltip TEXT` | Set the tray tooltip. |
| `--item TEXT` | Add an item that prints `TEXT` when selected. |
| `--sep` | Add a separator. |
| `--quit TEXT` | Add an item that ends the process when selected. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show the build version. |

Unknown flags and missing values fail with a diagnostic and status 1. At least
one menu item or separator is required.

### Environment

| Variable | Description |
| :--- | :--- |
| `KC_TRAY_ICON` | Default icon, overridden by `--icon`. |
| `KC_TRAY_TOOLTIP` | Default tooltip, overridden by `--tooltip`. |

---

## Public API

```c
#include "libtray.h"

static void goodbye(kc_tray_item_t *item, void *userdata) {
    (void)item;
    kc_tray_close((kc_tray_t *)userdata);
}

kc_tray_t *tray = NULL;
kc_tray_item_t *quit = NULL;
kc_tray_options_t options = { .icon = NULL, .tooltip = "My app" };

if (kc_tray_open(&tray, &options) == KC_TRAY_OK) {
    kc_tray_add_item(tray, &quit, "Quit", goodbye, tray);
    /* Continue other application work while the tray remains open. */
}
```

`kc_tray_open(&tray, NULL)` omits explicit initial properties. The library
copies option strings. The tray owns all items, and `kc_tray_close()` releases
them. Each `kc_tray_add_item()` callback receives the exact activated item plus
retained caller `userdata`; the caller keeps `userdata` valid until item
removal or tray close. Both `kc_tray_item_remove(item)` and
`kc_tray_close(tray)` may be called inside a menu callback. After either call,
the removed pointer has ended its public lifetime, and close prevents future
callbacks.

### Properties and Errors

`kc_tray_add_separator()` creates a removable child whose text getter returns
`NULL`. `kc_tray_item_set_text()`, `kc_tray_set_icon()`, and
`kc_tray_set_tooltip()` update the native object. `NULL` clears the icon or
tooltip; item text must be nonempty.

The string getters return borrowed pointers. A tray property's string is
invalidated by its next setter or tray close; an item's text is invalidated by
its next setter, removal, or tray close. `kc_tray_get_error()` returns `NULL`
if no error, otherwise a borrowed message invalidated by the next operation on
that tray or close. Do not use any tray or child pointer after its lifetime
ends. Synchronize concurrent calls by the application when sharing a tray
across caller threads.

---

## Platform Scope

| Platform | Behavior |
| :--- | :--- |
| Windows | Icon values are `.ico` file paths; tooltips are limited to 127 UTF-16 units. A message window and loop run on a worker thread. |
| Linux | Icons may be file paths or icon names. GTK status-icon and event-context work run on a worker thread; applications already using GTK should not access GTK objects from other threads concurrently. A notification-area host is required for visibility. |
| macOS | Icons may be file paths or named AppKit images. AppKit objects are created and mutated on the main thread; the hosting process must run its AppKit event loop to receive menu actions. The CLI starts that loop itself. |

An unset icon uses the platform default. Browser WASM is not a target because
this library requires native desktop status-area facilities.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

`make test` runs the native C contract and one grouped CLI case; a desktop
session is needed for native cases. To run through Wine:

```bash
make x86_64/windows
make test wine
```

To check actual native callback dispatch, run the built `tray_contract_test
interactive` in a desktop session and select its three menu items in order.
macOS requires an AppKit desktop session. Cross-compilation establishes
compilation, not runtime correctness.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

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
- GTK 3 development packages for Linux builds

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS.

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

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
