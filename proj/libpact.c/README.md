# libpact.c - Persistent scoped trust

`libpact.c` establishes persistent scoped trust relationships and protects
binary messages with the Noise Protocol Framework. Applications remain
responsible for moving invitations, confirmations, identifiers, and protected
messages between endpoints.

## Quick start

```c
#include "libpact.h"

kc_pact_t *pact = NULL;
char *uid = NULL;
char *code = NULL;

if (kc_pact_init(&pact) == KC_PACT_OK) {
    if (kc_pact_invite(pact, &uid, &code) == KC_PACT_OK) {
        /* Transport code to the invited endpoint. */
    }

    kc_pact_free(code);
    kc_pact_free(uid);
    kc_pact_close(pact);
}
```

Trust relationships outlive the local handle. The library protects message
contents but does not provide transport, ordering, retries, timeouts, or replay
policy.

## Documentation

Read the [integration guide](doc/integration.md) before implementing the full
invite, join, confirm, seal, framing, and unseal flow.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Notice

No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
