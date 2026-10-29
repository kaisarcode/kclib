# libstun.c - STUN Binding client and server

`libstun.c` performs STUN Binding transactions and serves Binding requests over
UDP. A client discovers the endpoint observed by a peer and keeps its UDP
mapping available for application traffic. A server answers Binding requests.
Both roles support UDP transports supplied by another composition.

Client transactions support short-term STUN credentials, SHA-1 and SHA-256
message integrity, fingerprint validation, retransmissions, extension attributes,
error responses, unauthenticated Binding Indication keepalives and IPv4/IPv6.
STUN does not implement ICE or require another kclib.

## Quick start

```c
#include "libstun.h"

kc_stun_client_t *client = NULL;
kc_stun_client_options_t options = {0};
options.peer.host = "stun.example.com:3478";

if (kc_stun_client(&client, &options) == KC_STUN_OK) {
    int status;
    do {
        status = kc_stun_client_poll(client, 100);
    } while (status == KC_STUN_PENDING);

    if (status == KC_STUN_OK) {
        const kc_stun_endpoint_t *public_address = kc_stun_client_mapped(client);
        /* public_address->host, public_address->port */
        (void)public_address;
    }
    kc_stun_client_close(client);
}
```

## CLI

```sh
stun client stun.example.com:3478
stun client stun.example.com:3478 --host 0.0.0.0 -p 5000 -t 3000
stun server -p 3478
stun -h
```

`client` writes the observed host and port as tab-separated fields.
`server` writes its listening port and continues serving until stopped.

## Documentation

- [Binding lifecycle](doc/mapping-lifecycle.md) - mapping behavior and state.
- [Codec and composition](doc/integration.md) - message encoding and integrating an owned transport.

## Build

```sh
make
make test
```

Use `make all` for configured cross-platform targets when their toolchains are
installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
