# tray.c — native system tray

`libtray` exposes a persistent desktop tray and persistent child menu items on Windows, Linux (GTK 3), and macOS (AppKit). `kc_tray_open()` returns without running a caller-visible event loop. The standalone `tray` program is a small consumer of the same public API.

## Public C API

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

`kc_tray_open(&tray, NULL)` omits explicit initial properties. The library copies option strings. The tray owns all items, and `kc_tray_close()` releases them. Each `kc_tray_add_item()` callback receives the exact activated item plus retained caller `userdata`; the caller keeps `userdata` valid until item removal or tray close. Both `kc_tray_item_remove(item)` and `kc_tray_close(tray)` may be called inside a menu callback. After either call the removed pointer has ended its public lifetime, and close prevents future callbacks.

`kc_tray_add_separator()` creates a removable child whose text getter returns NULL. `kc_tray_item_set_text()`, `kc_tray_set_icon()`, and `kc_tray_set_tooltip()` update the native object. NULL clears the icon or tooltip; text must be nonempty. Windows icon values are `.ico` file paths and tooltips are limited to 127 UTF-16 units. Linux accepts file paths or icon names; macOS accepts file paths or named AppKit images. An unset icon uses the platform default.

The string getters return borrowed pointers. A tray property's string is invalidated by its next setter or tray close; an item's text is invalidated by its next setter, removal, or tray close. `kc_tray_get_error()` returns NULL if no error, otherwise a borrowed message invalidated by the next operation on that tray or close. Do not use any tray or child pointer after its lifetime ends. Synchronize concurrent calls by the application when sharing a tray across caller threads.

## CLI

```sh
make
./bin/x86_64/linux/tray --icon /path/to/icon.png --tooltip "My app" \
  --item "Greet" --sep --quit "Quit"
```

`--item TEXT` prints TEXT on selection; `--quit TEXT` ends the process. Repeated `--item` and `--sep` flags build the menu in order. `--icon PATH`, `--tooltip TEXT`, `-h`/`--help`, and `-v`/`--version` are also supported. `KC_TRAY_ICON` and `KC_TRAY_TOOLTIP` provide defaults overridden by their corresponding flags. Unknown flags and missing values fail with a diagnostic and status 1. The CLI no longer runs external commands.

## Platform behavior

Windows owns a message window and message loop on a worker thread. Linux initializes its GTK status icon and GTK event context on a worker thread. Applications that already use GTK should avoid accessing GTK objects from other threads concurrently. On macOS AppKit objects are created and mutated on the main thread, and the hosting process must run its AppKit event loop to receive menu actions. The CLI starts that loop itself. A Linux desktop session also needs a notification-area host; otherwise the icon may not be visible.

## Build and tests

```sh
make all
make test
make test wine
```

`make all` needs GTK 3 development packages, MinGW and macOS SDK cross-compilers for all targets. `make test` runs the native C contract and one grouped CLI case; a desktop session is needed for native cases. `make test wine` uses a Windows cross-compiler and Wine. To check actual native callback dispatch, run the built `tray_contract_test interactive` in a desktop session and select its three menu items in order. macOS requires an AppKit desktop session. Cross compilation establishes compilation, not runtime correctness. WASM is inapplicable because this library requires native desktop status-area facilities.

Artifacts are written under `bin/{arch}/{platform}/`. The library is distributed under GPLv3; see [LICENSE](LICENSE).
