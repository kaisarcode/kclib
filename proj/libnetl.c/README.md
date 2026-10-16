# libnetl.c - Incoming network listener

`libnetl.c` listens for inbound TCP or UDP traffic and delivers received bytes
with the remote peer that sent them. It handles transport only; application
protocols remain separate.

## Quick start

```c
#include "libnetl.h"

static void receive(
    const kc_netl_input_t *input,
    void *userdata
) {
    (void)userdata;

    static const char reply[] = "ok";
    kc_netl_respond(input->peer, reply, sizeof(reply) - 1);
}

kc_netl_options_t options = {
    .host = "0.0.0.0",
    .port = 8080,
    .protocol = KC_NETL_TCP
};
kc_netl_t *listener = NULL;

if (kc_netl_open(
        &listener,
        &options,
        receive,
        NULL,
        NULL,
        NULL
    ) == KC_NETL_OK) {
    kc_netl_close(listener);
}
```

TCP peers keep a stable identity across receives so applications can associate
incremental protocol state with a connection. UDP inputs represent individual
datagrams.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets. See the
[repository build guide](../../README.md#build) for shared toolchain and target
information.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
