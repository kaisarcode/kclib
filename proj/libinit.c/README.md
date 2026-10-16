# libinit.c - Persistent startup registration

`libinit.c` registers named commands with the operating system so they start at
boot or user startup. Registrations are persistent and use the current user's
platform-native startup mechanism.

## Quick start

```c
#include "libinit.h"

kc_init_options_t options = {
    .cmd = "my-service"
};

if (kc_init_create("my-service", &options) == KC_INIT_OK) {
    kc_init_entry_t *entry = NULL;

    if (kc_init_get("my-service", &entry) == KC_INIT_OK) {
        kc_init_free(entry);
    }
}
```

`kc_init_create()` creates or replaces a named registration. Use
`kc_init_delete()` to remove it.

## Platform support

Linux, macOS, and Windows are supported. The library selects the native
per-user startup and metadata locations; callers do not choose a storage
directory.

## Documentation

See the [public header](src/libinit.h) for entry fields, allocation rules, and status
codes.

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
