# libmenu.c - Native application menu entries

`libmenu.c` adds and removes applications from the current user's native
application menu. The platform-specific registration mechanism is selected
internally.

## Quick start

```c
#include "libmenu.h"

kc_menu_entry_t entry = {
    .id = "myapp",
    .name = "My App",
    .description = "My application",
    .command = "myapp"
};

if (kc_menu_add(&entry) == KC_MENU_OK) {
    /* The entry remains registered until it is removed. */
}

kc_menu_delete("myapp");
```

Description, icon, and category values are optional.

## Platform support

Linux uses the current user's XDG application menu. Windows uses the current
user's Start Menu Programs folder.

## Documentation

See the [public header](src/libmenu.h) for entry fields, platform
behavior, and status codes.

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
