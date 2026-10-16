# libdmn.c - Local daemon manager

`libdmn.c` creates and manages persistent named local daemons. A daemon keeps
its process resident independently of the client handle and exchanges framed
request and response data with that process.

## Quick start

```c
#include "libdmn.h"

kc_dmn_options_t options = {
    .cmd = "my-worker"
};

if (kc_dmn_create("worker", &options) == KC_DMN_OK) {
    kc_dmn_t *dmn = NULL;

    if (kc_dmn_open(&dmn, "worker") == KC_DMN_OK) {
        void *reply = NULL;
        size_t reply_size = 0;

        kc_dmn_send_data(
            dmn,
            "ping",
            4,
            &reply,
            &reply_size
        );
        kc_dmn_free(reply);
        kc_dmn_close(dmn);
    }
}
```

Closing a handle does not stop the resident daemon. Delete the named daemon when
its persistent lifecycle should end.

## Documentation

Read the [runtime model](doc/runtime-model.md) for daemon lifetime, EOT framing,
configuration changes, signals, and persistent behavior.

See the [public header](src/libdmn.h) for the complete C interface and status
codes.

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
