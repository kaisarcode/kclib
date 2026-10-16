# libhttp.c - HTTP parser and builder

`libhttp.c` incrementally parses HTTP byte streams into structured requests and
responses and builds HTTP wire bytes from structured values. It is a protocol
layer only: sockets, listening, routing, and outgoing transport belong to other
libraries.

## Quick start

```c
#include "libhttp.h"

static void on_request(
    const kc_http_request_t *request,
    void *userdata
) {
    (void)request;
    (void)userdata;
}

kc_http_parser_t *parser = NULL;

if (kc_http_parser_open(
        &parser,
        on_request,
        NULL,
        NULL,
        NULL
    ) == KC_HTTP_OK) {
    kc_http_parser_write(parser, bytes, byte_count);
    kc_http_parser_close(parser);
}
```

One parser belongs to one HTTP byte stream. A write may complete zero, one, or
multiple messages.

## Documentation

Read the [integration guide](doc/integration.md) for parser lifecycle, message
builders, transport composition, defaults, and supported protocol behavior.

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
