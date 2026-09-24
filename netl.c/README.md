# netl.c - Incoming Network Listener

`netl.c` is a small C library and CLI for accepting inbound TCP connections
and UDP datagrams.

The reusable library does not know about HTTP, templates, routing, registries,
daemons, commands, or application protocols. It exposes network input as
connections and datagrams so higher layers can compose it with any protocol.

## Model

TCP listeners own many simultaneous connections:

```text
listener
  +-- connection
  |     +-- data
  |     +-- send
  |     +-- close
  +-- connection
        +-- data
        +-- send
        +-- close
```

UDP remains datagram-oriented:

```text
listener
  +-- datagram
        +-- data
        +-- peer host
        +-- peer port
```

There is no single active TCP client. `kc_netl_poll()` multiplexes all active
connections with `poll()` on POSIX and `WSAPoll()` on Windows. A slow or idle
connection does not hold a global lock or prevent other connections from making
progress.

Sockets are non-blocking. `kc_netl_send()` may send fewer bytes than requested,
and returns `KC_NETL_EAGAIN` when the socket cannot currently accept more data.
When a send is partial or returns `KC_NETL_EAGAIN`, that connection is watched
for write readiness and later produces `KC_NETL_EVENT_WRITABLE`. The caller
decides what unsent bytes to retain and when to retry; the library does not hide
unbounded output queues.

## Public API

```c
#include "libnetl.h"

int max_pending_connections = 128;
kc_netl_options_t options = {
    .host = "0.0.0.0",
    .port = 8080,
    .protocol = KC_NETL_TCP,
    .max_pending_connections = &max_pending_connections
};

kc_netl_t *server = NULL;
kc_netl_event_t event;

if (kc_netl_open(&server, &options) != KC_NETL_OK) {
    return 1;
}

for (;;) {
    int status = kc_netl_poll(server, &event, -1);

    if (status != KC_NETL_OK) {
        break;
    }

    if (event.type == KC_NETL_EVENT_CONNECTION) {
        /* event.connection identifies exactly one accepted TCP stream. */
    } else if (event.type == KC_NETL_EVENT_DATA) {
        size_t sent = 0U;

        (void)kc_netl_send(
            event.connection,
            event.data,
            event.data_size,
            &sent
        );
    } else if (event.type == KC_NETL_EVENT_CLOSE) {
        /* The borrowed connection handle expires on the next poll. */
    }
}

kc_netl_close(server);
```

The public lifecycle is:

- `kc_netl_open()` binds one TCP or UDP listener.
- `kc_netl_poll()` returns one connection, data, writable, close, or datagram event.
- `kc_netl_send()` attempts a non-blocking TCP write to one exact connection.
- `kc_netl_sendto()` sends one UDP datagram to one peer.
- `kc_netl_connection_close()` closes one TCP connection independently.
- `kc_netl_port()` reports the actual bound port, including ephemeral ports.
- `kc_netl_close()` closes the listener and all remaining connections.

Event buffers and peer strings are borrowed until the next `kc_netl_poll()`.
A TCP connection handle is owned by the listener. After a CLOSE event or an
explicit `kc_netl_connection_close()`, it remains inspectable only until the
next poll or listener close.

## Composition

`netl` and `http` are independent capabilities. One possible composition is:

```text
TCP connection bytes
        |
        v
      netl
        |
        v
      http parser
        |
        v
 application request handling
```

The same listener can carry a different protocol or raw bytes without involving
`http.c`.

A scripting bridge can mechanically translate listener events into a natural
event API:

```js
const server = netl.listen({
    host: "0.0.0.0",
    port: 8080,
    protocol: "tcp"
});

server.on("connection", (client) => {
    client.on("data", (data) => {
        client.send(data);
    });
});
```

The C ABI remains explicit while JavaScript or Lua can expose idiomatic objects
and events.

## CLI

The CLI keeps shell composition without exposing the old persistent
registry/process-manager model.

Start a TCP listener and dispatch each accepted connection to a command:

```bash
netl 0.0.0.0:8080 'my-command'
```

Select UDP explicitly:

```bash
netl 0.0.0.0:9000 --udp 'my-command'
```

TCP launches one command per accepted connection. The connection is attached to
the command's standard input and standard output. Independent connections use
independent command processes.

UDP launches one command per datagram and writes exactly that datagram to the
command's standard input. UDP command output is not sent automatically to the
peer.

Common options are `-h` / `--help` and `-v` / `--version`.

## Platform scope

The native listener requires operating-system TCP/UDP server sockets. It is
intended for native platforms supported by the project, including POSIX systems
and Windows.

Browser WASM is not a target for `netl.c`: browser runtimes do not expose the
native bind/listen/accept TCP and UDP capability represented by this library.
Other kclibs such as `http.c` can support WASM independently.

## Build and test

Build the project artifacts:

```bash
make
```

Build all configured native cross-targets:

```bash
make all
```

Run the native contract suite:

```bash
make test
```

Run the Windows contract suite through Wine:

```bash
make test wine
```

The contract suite covers public argument validation, TCP connection identity,
simultaneous independent clients, targeted sends, independent close behavior,
UDP datagram/peer preservation, CLI basics, errors, and version reporting.
