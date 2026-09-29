# libflow.c - Branch Flow Runtime

`libflow.c` is a small workflow runner for chaining shell commands and reusable
child flows. It starts at the flow entries, passes stdin through each branch,
and writes the final branch output to stdout.

The model is branch-oriented: a flow declares entry nodes, nodes can execute
commands, expand child flows, and fan out through links. Branches stay
independent and do not merge.

---
## Public API

```c
#include "libflow.h"

static void finished(
    int status,
    void *data,
    size_t size,
    const char *error,
    void *userdata
) {
    (void)userdata;

    if (status == KC_FLOW_OK) {
        /* use data/size */
        kc_flow_free(data);
    } else {
        /* inspect error */
        (void)error;
    }
}

kc_flow_t *flow = NULL;
kc_flow_run_t *run = NULL;

if (kc_flow_open(&flow, "file.flow") != KC_FLOW_OK) {
    return 1;
}

kc_flow_set(flow, "flow.hello", "Hello");

if (kc_flow_run(
        flow,
        &run,
        NULL,
        NULL,
        0,
        finished,
        NULL
    ) != KC_FLOW_OK) {
    kc_flow_close(flow);
    return 1;
}

/* The run advances independently and can be stopped at any time. */
```

After the terminal callback returns, the owning control path releases the run
with `kc_flow_run_close()` and eventually releases the reusable template with
`kc_flow_close()`.

The opened flow owns its copied source path and ordered temporary overrides.
Each `kc_flow_run()` snapshots that state, the optional entry, and its input
before returning. The run then advances independently through the existing
branch engine. Later changes to the opened flow do not alter an already-started
run, and closing the opened flow does not invalidate existing runs.

Branches keep their existing semantics: each step receives the previous step's
output, fan-out creates independent branches that do not merge, and terminal
branch outputs form the successful run result.

A cooperative stop belongs to one run, not to the opened flow.
`kc_flow_run_stop()` never kills the command currently executing. The current
step finishes normally; before another step begins, the run observes the stop
request and completes with `KC_FLOW_ESTOP`.

---

### Lifecycle

- `kc_flow_open()` opens one existing flow file and copies its path.
- `kc_flow_set()` and `kc_flow_unset()` append ordered temporary overrides to the opened flow.
- `kc_flow_run()` starts one independent non-blocking run and delivers exactly one terminal callback with status, output, and contextual error.
- `kc_flow_run_stop()` cooperatively stops that run after its current step finishes.
- `kc_flow_run_close()` releases the run. If it is still active, close requests a cooperative stop and joins it before release.
- `kc_flow_free()` releases successful output received by the terminal callback.
- `kc_flow_close()` releases the opened flow. Existing runs remain valid because they own independent snapshots.
- `kc_flow_version()` returns the build version.

Multiple runs created from one opened flow are independent. Each run owns its
stop request and branch traversal. Terminal status, successful output, and any
contextual error are delivered together through its completion callback.

### Visualization

`libflow.c` is a strict execution engine dedicated to running system command workflows.
For rendering visual graph representations of `.flow` documents, you can check the
[fldot](https://github.com/kaisarcode/fldot) conversion tool.

---
## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Other cross-compilation toolchains are required only by enabled targets.

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
