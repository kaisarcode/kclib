# libflow.c - Branch flow runtime

`libflow.c` runs `.flow` workflows composed of shell-command nodes and reusable
child flows. Runs are asynchronous, branch-oriented, and isolated from later
changes to the opened flow template.

## Quick start

```c
#include "libflow.h"

static void finished(
    int status,
    void *data,
    size_t size,
    const char *error,
    void *userdata
) {
    (void)error;
    (void)userdata;

    if (status == KC_FLOW_OK) {
        kc_flow_free(data);
    }
}

kc_flow_t *flow = NULL;
kc_flow_run_t *run = NULL;

if (kc_flow_open(&flow, "file.flow") == KC_FLOW_OK) {
    kc_flow_run(
        flow,
        &run,
        NULL,
        NULL,
        0,
        finished,
        NULL
    );
}
```

Each run snapshots its flow state and advances independently. Stop requests are
cooperative and apply to one run.

## Documentation

Read the [runtime model](doc/runtime-model.md) for snapshots, branch semantics,
completion, and cooperative stopping.

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
