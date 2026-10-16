# libtray.c - Native system tray

`libtray.c` creates native system tray icons and menu items on Windows, Linux,
and macOS. Menu items can be added, updated, removed, and handled through
callbacks while the tray remains open.

## Quick start

```c
#include "libtray.h"

static void quit(
    kc_tray_item_t *item,
    void *userdata
) {
    (void)item;
    kc_tray_close((kc_tray_t *)userdata);
}

kc_tray_t *tray = NULL;
kc_tray_item_t *item = NULL;
kc_tray_options_t options = {
    .tooltip = "My app"
};

if (kc_tray_open(&tray, &options) == KC_TRAY_OK) {
    kc_tray_add_item(tray, &item, "Quit", quit, tray);
}
```

An unset icon uses the platform default.

## Requirements

Linux builds require GTK 3 development packages and a notification-area host
for a visible tray icon. Windows and macOS use native platform facilities.

Native tests require a desktop session.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets. See the
[repository build guide](../../README.md#build) for shared toolchain and target
information.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
