# libtlsio.c - Authenticated TLS byte connection

`libtlsio.c` opens authenticated client TLS connections and exchanges plaintext
bytes over them. It provides transport security only and does not implement HTTP
or another application protocol.

## Quick start

```c
#include "libtlsio.h"

kc_tlsio_t *tls = NULL;
kc_tlsio_options_t options = {
    .host = "example.com",
    .port = 443
};

if (kc_tlsio_connect(&tls, &options) == KC_TLSIO_OK) {
    const char request[] = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    void *data = NULL;
    size_t size = 0;

    if (kc_tlsio_write(tls, request, sizeof(request) - 1U) == KC_TLSIO_OK &&
        kc_tlsio_read(tls, &data, &size) == KC_TLSIO_OK) {
        /* Use data and size. */
    }

    kc_tlsio_free(data);
    kc_tlsio_close(tls);
}
```

The connection verifies the peer certificate and requested host against the
system OpenSSL trust paths. Port zero selects port 443. `kc_tlsio_read()` returns
one allocated plaintext chunk at a time and reports `KC_TLSIO_EOF` after a clean
peer shutdown.

## Dependency

The current native backend requires OpenSSL 1.1.1 or newer. OpenSSL remains a
private implementation dependency and does not appear in the public API.

## Build

From this project directory:

```bash
make
make test
```

The native build creates static and shared artifacts under `bin/{arch}/{platform}`.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
