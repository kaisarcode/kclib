# libstun.c - STUN UDP mappings and endpoints

`libstun.c` discovers externally observed UDP endpoints with STUN Binding and
can run an autonomous UDP Binding endpoint for other clients. A discovered
mapping keeps the same local UDP socket alive for application datagrams.

## Quick start

```c
#include "libstun.h"

kc_stun_options_t options = {
    .server = "stun.example.com:3478",
    .port = 0,
    .timeout_ms = 3000
};
kc_stun_mapping_t *mapping = NULL;

if (kc_stun_discover(&mapping, &options) == KC_STUN_OK) {
    const char *host = kc_stun_mapped_host(mapping);
    unsigned short port = kc_stun_mapped_port(mapping);

    (void)host;
    (void)port;
    kc_stun_close(mapping);
}
```

The current transport is UDP. STUN responses from the configured server are
handled internally rather than returned as application datagrams.

## Documentation

Read the [mapping lifecycle](doc/mapping-lifecycle.md) for discovery, refresh,
listener behavior, address families, and application datagrams.

See the [public header](src/libstun.h) for the complete C interface and status
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
