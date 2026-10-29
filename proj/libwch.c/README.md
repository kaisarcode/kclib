# libwch.c - Resident file watcher

`libwch.c` creates named filesystem watchers that continue running after the
client process exits. A watcher can execute a persistent command for filesystem
events while local clients temporarily subscribe to those same events.

## Quick start

```c
#include "libwch.h"

kc_wch_options_t options = {
    .path = ".",
    .cmd = "my-handler",
    .recursive = 1
};

if (kc_wch_create("source", &options) == KC_WCH_OK) {
    kc_wch_t *watcher = NULL;

    if (kc_wch_open(&watcher, "source") == KC_WCH_OK) {
        kc_wch_close(watcher);
    }
}
```

Closing a local handle does not stop the resident watcher. Delete the named
watcher when its persistent lifecycle should end.

## Documentation

Read the [runtime model](doc/runtime-model.md) for persistent configuration,
temporary subscriptions, restarts, and ownership.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Notice

No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
