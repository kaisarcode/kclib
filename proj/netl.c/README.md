# netl.c - Incoming Network Listener

`netl.c` is a small C library and CLI for listening for inbound TCP or UDP
traffic.

The library delivers incoming bytes through a callback together with the remote
peer that sent them. The application may process the input directly or pass it
to another protocol library, and may optionally respond to the same peer.

---

## CLI

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

static void on_input(
    const kc_netl_input_t *input,
    void *userdata
) {
    (void)userdata;

    if (should_respond(input->data, input->data_size)) {
        static const char reply[] = "ok";

        (void)kc_netl_respond(
            input->peer,
            reply,
            sizeof(reply) - 1U
        );
    }
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
            on_input,
            NULL,
            NULL,
            NULL
        ) != KC_NETL_OK
    ) {
        return 1;
    }

    /* The listener is operational until the application closes it. */

    kc_netl_close(listener);
    return 0;
}
```

Each input contains:

- the remote `peer`;
- `KC_NETL_TCP` or `KC_NETL_UDP`;
- the remote host and port;
- the bytes that arrived.

A TCP peer keeps the same identity across successive input callbacks, so an
application can associate incremental protocol state with that peer. A UDP input
represents one datagram and its origin.

`kc_netl_respond()` replies to the supplied peer. The application does not
choose a separate destination for the response.

`kc_netl_peer_close()` stops communication with one peer without closing the
listener. `kc_netl_port()` reports the actual bound port, including an
ephemeral port selected by the operating system.

### Protocol composition

`netl.c` does not interpret application protocols. For example, TCP bytes can
be passed incrementally to an HTTP parser while keeping one parser per peer:

```text
incoming bytes
      |
      v
    netl
      |
      v
per-peer HTTP parser
      |
      v
application handling
```

This keeps transport listening independent from the protocol carried by the
connection.

### Platform scope

The listener uses native operating-system TCP/UDP support and is intended for
the native platforms built by the project.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

Build project artifacts first, then run the portable test entry point:

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
