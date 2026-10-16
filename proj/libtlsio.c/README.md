# libtlsio.c - TLS session layer

`libtlsio.c` maintains authenticated client or server TLS sessions over a
caller-supplied byte transport. It accepts TLS wire bytes, emits authenticated
plaintext, and produces TLS wire bytes for the caller to transport.

It does not open sockets, resolve hosts, listen on ports, or implement
application protocols.

## Quick start

```c
#include "libtlsio.h"

static void plaintext(const void *data, size_t size, void *userdata) {
    /* consume authenticated application bytes */
}

static void ciphertext(const void *data, size_t size, void *userdata) {
    /* pass TLS wire bytes to the chosen byte transport */
}

kc_tlsio_t *tls = NULL;
kc_tlsio_options_t options = {
    .role = KC_TLSIO_CLIENT,
    .host = "example.com"
};

if (kc_tlsio_open(
        &tls,
        &options,
        plaintext,
        ciphertext,
        NULL
    ) == KC_TLSIO_OK) {
    kc_tlsio_send(tls, "hello", 5U);

    /* As wire bytes arrive from the transport: */
    /* kc_tlsio_receive(tls, wire_data, wire_size); */

    kc_tlsio_close(tls);
}
```

Client sessions start the handshake when opened. Application bytes passed to
`kc_tlsio_send()` before authentication completes are retained and sent
automatically after the peer is verified.

`kc_tlsio_receive()` accepts arbitrary TLS stream chunks. One call may produce
zero, one, or multiple plaintext or ciphertext callbacks. An authenticated peer
`close_notify` is reported as `KC_TLSIO_CLOSED`.

## Client authentication

Client mode requires `host`, which is used for SNI and hostname verification.

When `ca` is omitted, peer trust uses the native platform trust roots:

- Linux and Android load the system certificate store into Mbed TLS.
- Windows validates the peer chain with CryptoAPI.
- macOS and iOS validate the peer chain with Security.framework.

`ca` and `ca_size` may instead provide a PEM or DER trust chain.

## Server sessions

Server mode requires a certificate chain and private key:

```c
kc_tlsio_options_t options = {
    .role = KC_TLSIO_SERVER,
    .certificate = certificate_bytes,
    .certificate_size = certificate_size,
    .private_key = private_key_bytes,
    .private_key_size = private_key_size
};
```

PEM and DER inputs are accepted. Server mode currently authenticates the server
to the client and does not require a client certificate.

## TLS backend

TLS protocol and cryptography are provided by Mbed TLS 3.6.7 vendored under
`lib/mbedtls` and compiled into the library. OpenSSL and a system Mbed TLS
installation are not required.

TLS 1.2 is the minimum protocol version. TLS 1.3 is enabled by the embedded
backend.

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
