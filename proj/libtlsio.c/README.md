# libtlsio.c - Authenticated TLS byte connection

`libtlsio.c` opens authenticated client TLS connections and exchanges plaintext
bytes over them. It provides transport security only and does not implement HTTP
or another application protocol.

## Quick start

```c
#include "libtlsio.h"

#include <stdio.h>

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
        fwrite(data, 1U, size, stdout);
    }

    kc_tlsio_free(data);
    kc_tlsio_close(tls);
}
```

Port zero selects port 443. `kc_tlsio_read()` returns one allocated plaintext
chunk at a time and reports `KC_TLSIO_EOF` only after a clean TLS shutdown.

## TLS backend

TLS protocol and cryptography are provided by Mbed TLS 3.6.7 vendored under
`lib/mbedtls` and compiled into the library. OpenSSL and a system Mbed TLS
installation are not required.

Peer trust uses the native platform trust roots:

- Linux and Android load the system certificate store into Mbed TLS.
- Windows validates the peer chain with CryptoAPI.
- macOS and iOS validate the peer chain with Security.framework.

TLS 1.2 is the minimum protocol version. TLS 1.3 is enabled by the embedded
backend.

## Targets

The project follows the native kclib target matrix:

- Linux: x86_64, i686, aarch64, armv7, armv7hf, riscv64, powerpc64le, mips,
    mipsel, mips64el, s390x, and loongarch64.
- Windows: x86_64 and i686.
- macOS: x86_64 and aarch64.
- iOS: aarch64 devices plus aarch64 and x86_64 simulators.
- Android: aarch64 and armv7.

## Build

From this project directory:

```bash
make
make test
make test wine
make all
```

`make all` builds every configured native and mobile target when its toolchain is
installed. All TLS implementation sources required by the build are shipped in
this project.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License
version 3 (LGPLv3)**. Mbed TLS is distributed under Apache-2.0 OR
GPL-2.0-or-later and is used as a private implementation dependency.
