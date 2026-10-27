# libnetl.c - Incoming network transport

`libnetl.c` maintains TCP or UDP listeners and exchanges raw bytes with incoming
peers. It owns bind, listen, accept, transport buffering, partial writes, peer
lifetime, and disconnect handling.

Application protocols remain outside the library.

## Features

- Listens on TCP or UDP endpoints.
- Keeps one stable peer identity for each live TCP connection.
- Delivers multiple receives for the same TCP peer.
- Sends multiple ordered responses to a TCP peer.
- Preserves UDP datagram boundaries.
- Allows `respond()` from receive callbacks, later code, or another thread while
    a TCP peer remains connected.
- Reports TCP disconnects and asynchronous listener failures.

## Quick start

```c
#include "libnetl.h"

static void receive(
    const kc_netl_input_t *input,
    void *userdata
) {
    static const char reply[] = "ok";

    (void)userdata;
    (void)kc_netl_respond(input->peer, reply, sizeof(reply) - 1U);
}

int main(void) {
    kc_netl_options_t options = {
        .host = "0.0.0.0",
        .port = 8080,
        .protocol = KC_NETL_TCP
    };
    kc_netl_t *listener = NULL;

    if (
        kc_netl_open(
            &listener,
            &options,
            receive,
            NULL,
            NULL,
            NULL
        ) != KC_NETL_OK
    ) {
        return 1;
    }

    kc_netl_close(listener);
    return 0;
}
```

For TCP, the same `kc_netl_peer_t` identifies a connection across receive
callbacks until that peer disconnects or is closed. `kc_netl_respond()` copies
its input before returning, may be called repeatedly, and preserves response
order. It may be called from the receive callback or from other threads while
the peer remains live.

`kc_netl_peer_close()` allows already queued TCP output to complete before the
peer closes. A disconnect callback is terminal for that peer; the peer must not
be retained after that callback returns.

For UDP, each receive describes one datagram and its origin. A response through
that receive-local peer produces one datagram back to the same origin.

## Deferred UDP replies

Incoming UDP datagrams include the source host and port. Calling
`kc_netl_respond(input->peer, bytes, size)` during the receive callback sends
a response from the listening socket to that source.

For an asynchronous or delayed response, the application may retain the
numeric source address and call
`kc_netl_reply_to(listener, host, port, bytes, size)` later. This copies the
datagram into the listener's existing send queue and uses the **same bound UDP
socket**. The destination must have an address family compatible with the
listener's bound socket. It does not require keeping the callback-scoped peer
pointer alive.

TCP behavior is unchanged.

## CLI: delayed UDP replies

The existing `netl <id> <port> <command...>` registration remains unchanged.
A received UDP datagram invokes `<command...> <host> <port>`; the complete
binary datagram is supplied on command stdin.

```sh
netl servidor 5000 'handler' -t udp
printf respuesta | netl servidor -r 127.0.0.1 6000
netl -l servidor
netl servidor -d
```

`-r` submits one binary datagram to the resident listener by local IPC. The
registered listener sends from its original bound UDP socket, even when the
incoming callback has already ended.

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
