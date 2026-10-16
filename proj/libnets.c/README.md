# libnets.c - Network sender

`libnets.c` starts asynchronous raw-byte transfers to one TCP, UDP, or optional
TLS destination. One transfer handle represents one operation and completes
through a terminal callback.

## Quick start

```c
#include "libnets.h"

static void complete(
    int status,
    const void *data,
    size_t size,
    void *userdata
) {
    (void)status;
    (void)data;
    (void)size;
    (void)userdata;
}

kc_nets_t *transfer = NULL;
const unsigned char message[] = "hello";

if (kc_nets_send(
        &transfer,
        "127.0.0.1",
        8080,
        KC_NETS_TCP,
        message,
        sizeof(message) - 1,
        complete,
        NULL
    ) != KC_NETS_OK) {
    return 1;
}
```

TCP and TLS can return response bytes. UDP sends one datagram and does not wait
for a response. `kc_nets_stop()` requests interruption; close the transfer after
its terminal completion.

## TLS

TLS is optional at build time. Use `kc_nets_tls_available()` to determine
whether the current build includes TLS support.

## Documentation

See the [public header](src/libnets.h) for lifecycle, completion
semantics, and status codes.

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
