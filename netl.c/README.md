# netl.c - Incoming Network Listener

`netl.c` is a small C library and CLI for accepting inbound TCP connections
and UDP datagrams.

The reusable library does not know about HTTP, templates, routing, registries,
daemons, commands, or application protocols. It exposes network input as
connections and datagrams so higher layers can compose it with any protocol.

---

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

---

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

### Model

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

### Composition

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

### Platform scope

The native listener requires operating-system TCP/UDP server sockets. It is
intended for native platforms supported by the project, including POSIX systems
and Windows.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Other cross-compilation toolchains are required only by enabled targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
