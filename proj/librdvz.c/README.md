# librdvz.c - Peer rendezvous

`librdvz.c` creates independent peers and maintains direct, bidirectional
relationships between them. It provides local rendezvous without imposing
networking, processes, messaging, roles, rooms or application policy.

Each peer receives a unique ID within its registry. Connecting with another
peer's ID creates a new peer and links the two. Disconnecting a peer removes
its links without disconnecting other peers. Applications may observe every
new or removed link and act on the encounter themselves.

## Quick start

```c
#include "librdvz.h"

kc_rdvz_t *rdvz = NULL;
kc_rdvz_peer_t *first = NULL;
kc_rdvz_peer_t *second = NULL;

if (kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK) {
    kc_rdvz_connect(rdvz, NULL, &first);
    kc_rdvz_connect(rdvz, kc_rdvz_peer_id(first), &second);

    kc_rdvz_disconnect(first);

    /* The second peer still belongs to the registry. */
    kc_rdvz_connect(rdvz, kc_rdvz_peer_id(second), &first);

    kc_rdvz_disconnect(first);
    kc_rdvz_disconnect(second);
    kc_rdvz_close(rdvz);
}
```

`kc_rdvz_list()` visits all peers in a registry.
`kc_rdvz_peer_list()` visits only directly linked peers. Connections are
symmetric, but not transitive.

## Encounter notifications

Pass `on_connect` and `on_disconnect` callbacks to `kc_rdvz_open()` to
observe when a pair of peers becomes linked or is unlinked. Notifications
are synchronous and occur once per direct link. The application decides what
to do after an encounter. It may inspect the registry during a callback;
mutations and registry closure are not permitted until the callback returns.

Disconnecting and reconnecting creates a new peer identity. Applications
that recognize returning participants manage that identity separately.
The registry does not transport data or retain peers after disconnect.

## CLI

The `rdvz` executable demonstrates the in-process primitive:

```sh
rdvz connect
rdvz match
```

`connect` creates one peer and prints its ID. `match` links two peers and
prints both IDs. Every invocation uses its own temporary registry; the CLI
does not provide a cross-process service.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for configured cross-platform targets when their toolchains
are installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/graphics/lgplv3-147x51.png)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
