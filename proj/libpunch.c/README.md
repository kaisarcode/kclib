# libpunch.c - Direct UDP channels

`libpunch.c` establishes and maintains direct bidirectional UDP channels between
peers. The application supplies candidate remote endpoints; the library handles
probing, path selection, keepalive traffic, and channel liveness.

## Quick start

```c
#include "libpunch.h"

kc_punch_endpoint_t peers[] = {
    { "203.0.113.20", 5000 },
    { "192.168.1.20", 5000 }
};
kc_punch_options_t options = {
    .port = 5000,
    .peers = peers,
    .peer_count = 2,
    .timeout_ms = 5000
};
kc_punch_path_t *path = NULL;

if (kc_punch_connect(&path, &options) == KC_PUNCH_OK) {
    kc_punch_send(path, "hello", 5);
    kc_punch_close(path);
}
```

Peer discovery and endpoint exchange are intentionally outside the library.

## Documentation

Read the [channel lifecycle](doc/channel-lifecycle.md) for owned versus borrowed
UDP mappings, receive ownership, liveness, and datagram behavior.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
