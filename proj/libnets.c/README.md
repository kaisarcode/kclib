# libnets.c - Outbound network transport

`libnets.c` maintains one persistent outbound TCP or UDP endpoint and exchanges
raw bytes with the remote destination.

The library owns host resolution, the socket, transport buffering, partial
writes, and output ordering. The public API remains focused on opening an
endpoint, sending bytes, receiving bytes, and closing the endpoint.

## Features

- Opens persistent TCP or UDP endpoints.
- Sends multiple byte buffers through the same endpoint.
- Delivers incoming TCP stream chunks or UDP datagrams through callbacks.
- Preserves TCP output order and UDP datagram boundaries.
- Supports `send()` from callbacks and other threads.
- Reports remote TCP disconnects and asynchronous network failures.

## Quick start

```c
#include "libnets.h"

static void receive(
    const void *data,
    size_t data_size,
    void *userdata
) {
    (void)data;
    (void)data_size;
    (void)userdata;
}

int main(void) {
    kc_nets_options_t options = {
        .host = "127.0.0.1",
        .port = 8080,
        .protocol = KC_NETS_TCP
    };
    kc_nets_t *network = NULL;

    if (
        kc_nets_open(
            &network,
            &options,
            receive,
            NULL,
            NULL,
            NULL
        ) != KC_NETS_OK
    ) {
        return 1;
    }

    if (kc_nets_send(network, "hello", 5) != KC_NETS_OK) {
        kc_nets_close(network);
        return 1;
    }

    kc_nets_close(network);
    return 0;
}
```

`kc_nets_open()` establishes the destination before returning. The `receive`
callback is required. TCP `disconnect` and asynchronous `error` callbacks are
optional.

`kc_nets_send()` copies its input before returning. Repeated TCP sends form one
ordered byte stream. Repeated UDP sends remain separate datagrams.

`kc_nets_close()` stops local I/O and releases the endpoint. It may also be
called from a callback. A local close does not report a remote disconnect or a
network error.

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

Questions can be sent to
[kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Pull requests are not
accepted; the project avoids depending on GitHub as long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License
version 3 (LGPLv3)**.
