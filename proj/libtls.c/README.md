# libtls.c - TLS session layer

`libtls.c` maintains authenticated client or server TLS sessions over a
caller-supplied byte transport. It accepts TLS wire bytes, emits authenticated
plaintext, and produces TLS wire bytes for the caller to transport.

It does not open sockets, resolve hosts, listen on ports, or implement
application protocols.

## Quick start

```c
#include "libtls.h"

static void plaintext(const void *data, size_t size, void *userdata) {
    /* consume authenticated application bytes */
}

static void ciphertext(const void *data, size_t size, void *userdata) {
    /* pass TLS wire bytes to the chosen byte transport */
}

kc_tls_t *tls = NULL;
kc_tls_options_t options = {
    .role = KC_TLS_CLIENT,
    .host = "example.com"
};

if (kc_tls_open(
        &tls,
        &options,
        plaintext,
        ciphertext,
        NULL
    ) == KC_TLS_OK) {
    kc_tls_send(tls, "hello", 5U);

    /* As wire bytes arrive from the transport: */
    /* kc_tls_receive(tls, wire_data, wire_size); */

    kc_tls_close(tls);
}
```

Client sessions start the handshake when opened. Application bytes passed to
`kc_tls_send()` before authentication completes are retained and sent
automatically after the peer is verified.

`kc_tls_receive()` accepts arbitrary TLS stream chunks. One call may produce
zero, one, or multiple plaintext or ciphertext callbacks. An authenticated peer
`close_notify` is reported as `KC_TLS_CLOSED`.

## Documentation

- [Security and sessions](doc/security.md) - peer verification, server certificates, and TLS backend.

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
