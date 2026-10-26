# librdvz.c - Peer rendezvous

`librdvz.c` creates named peers and maintains direct, bidirectional
relationships between them. Peers remain registered across processes until
explicitly disconnected. A relationship is symmetric: either peer can connect
or disconnect without becoming the owner of the other.

Connections do not establish a network transport or deliver application
messages. A peer may have multiple direct neighbors; relationships are not
transitive and repeated connections do not create duplicate links.

## Quick start

```c
#include "librdvz.h"

#include <stdio.h>

static void connected(const char *other, void *userdata)
{
    (void)userdata;
    printf("Connected to %s\n", other);
}

kc_rdvz_peer_t *first = NULL;
kc_rdvz_peer_t *second = NULL;

kc_rdvz_options_t options = {
    .cmd = NULL,
    .on_connect = connected,
    .userdata = NULL
};

kc_rdvz_create(&first, "peer1", &options);
kc_rdvz_create(&second, "peer2", NULL);

kc_rdvz_connect(second, "peer1", NULL, NULL);

kc_rdvz_peer_disconnect(first, "peer2", NULL, NULL);

kc_rdvz_close(first);
kc_rdvz_close(second);

kc_rdvz_disconnect("peer1");
kc_rdvz_disconnect("peer2");
```

`kc_rdvz_list()` lists registered peers. `kc_rdvz_peer_list()`
lists only direct neighbors. `kc_rdvz_open()` opens an additional
local handle for an existing peer. Closing a handle does not remove the
peer. `kc_rdvz_disconnect(id)` removes the named peer and
all its relationships.

The callback supplied when creating or opening a peer receives the **other
peer's ID as a string** whenever a new relationship is established. For
example, `peer1` receives `"peer2"` when `peer2`
connects to it. These callbacks belong to a live local handle and may
be delivered asynchronously when another process establishes the link.

The callback supplied to `kc_rdvz_connect()` or
`kc_rdvz_peer_disconnect()` runs on successful completion
and receives the other peer's ID. A peer may also have a persistent command
configured at creation, executed with the other ID when either endpoint
establishes a new link.

## CLI

```sh
rdvz peer1 'handler connected'
rdvz peer2 'handler connected'

rdvz -l
rdvz peer1 -l

rdvz peer2 -c peer1 'handler linked'
rdvz peer1 -d peer2 'handler unlinked'

rdvz peer1 -d
rdvz peer2 -d
```

`rdvz <id> <cmd>` creates or updates a named peer and
configures the command executed for incoming connections from any peer.
The command receives the other peer's ID as its final argument.
An optional command passed to `-c` or `-d <id>`
runs only for that particular successful operation. Commands are launched
without waiting for their completion.

`rdvz -l` lists every peer. `rdvz <id> -l`
lists that peer's direct neighbors. `rdvz <id1> -c <id2>`
creates the one symmetric relationship between existing peers.
`rdvz <id1> -d <id2>` removes the relationship without removing
either peer, whereas `rdvz <id> -d` removes that peer completely.

All invocations share the same per-user local rendezvous state.

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

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
