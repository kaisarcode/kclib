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

## UDP with multiple destinations

A UDP instance keeps one socket and one local port. The original `kc_nets_send()`
queues datagrams for the configured default destination. The additional
`kc_nets_send_to(network, peer, bytes, size)` queues a datagram to any
destination using **that same UDP socket**, without changing the default.

Use one `kc_nets_open()` call with a `kc_nets_options_t` containing `host`,
`port`, `protocol`, and optional `local_host` and `local_port` for UDP.
The receive callback is a separate opening argument; it receives the bytes
and an optional source endpoint. For UDP this endpoint contains the numeric
sender host and port; TCP passes NULL. The original `send()` behavior remains
available. Query the local UDP port with `kc_nets_local_port()`.

A UDP sender is not restricted to datagrams from the initial destination.
The configured default host determines the address family of the socket;
additional destinations must use that same address family. TCP behavior is
unchanged.

## CLI: persistent UDP sender

The original `nets <host:port> -t udp` stream relay still reads stdin and
writes network replies to stdout. `--local-host` and `--local-port` configure
its UDP source address.

To keep a single socket open across separate invocations, register an optional
resident sender:

```sh
nets 127.0.0.1:5000 -t udp --id sender --local-port 4000 --receive 'handler'
printf hello | nets sender -s
printf hello | nets sender -s 127.0.0.1:6000
nets -l sender
nets sender -d
```

The `handler` command receives `<host> <port>` arguments and one complete
binary UDP datagram on stdin. The `-s` command queues the outgoing datagram
using local IPC, not a new UDP socket. The resident process is the only owner
of network receive.

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
