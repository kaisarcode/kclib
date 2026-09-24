# flow.c - Branch Flow Runtime

`flow.c` is a small workflow runner for chaining shell commands and reusable
child flows. It starts at the flow entries, passes stdin through each branch,
and writes the final branch output to stdout.

The model is branch-oriented: a flow declares entry nodes, nodes can execute
commands, expand child flows, and fan out through links. Branches stay
independent and do not merge.

---

## CLI

The `flow` command executes one workflow file, accepts optional input from
standard input, and prints the produced branch output to standard output.
Runtime overrides can replace entries, parameters, child imports, and commands
without editing the source file. The library opens one flow file per runtime;
overrides remain local to that runtime and are never written back to disk.

### Examples

Execute one flow file:

```bash
./bin/x86_64/linux/flow file.flow
```

Execute one explicit entry:

```bash
./bin/x86_64/linux/flow file.flow --link build
```

Pipe input through standard input:

```bash
printf "input" | ./bin/x86_64/linux/flow file.flow
```

Write a minimal flow file:

```flow
flow.link=upper

node.upper.exec=tr '[:lower:]' '[:upper:]'
```

Write the minimal flow file above to a local `file.flow` and run it:

```bash
./bin/x86_64/linux/flow file.flow
./bin/x86_64/linux/flow file.flow --link build
printf "input" | ./bin/x86_64/linux/flow file.flow
```

The progression above illustrates a small static-site request pipeline: a
router routes a request path, renders a page through a child flow, then fans
the rendered page out to emit the page, count words, and compute a checksum.
It uses only default Linux commands such as `printf`, `cat`, `wc`, `cksum`,
`cut`, and shell `case`.

Its main route is an executable heredoc `node.link`:

```flow
flow.id=tiny-site
flow.path=/robots.txt
flow.link=request

node.request.link=<<EOF
case "<flow.path>" in
    "/") printf home ;;
    "/robots.txt") printf robots ;;
    *) printf missing ;;
esac
EOF
```

Each route uses a shared child-flow behavior:

```flow
node.page.import=page.flow
node.page.site=<flow.site>

node.home.use=page
node.home.slug=home
node.home.heading=Home
node.home.body=Welcome to the tiny flow site.
node.home.status=<func.status home.status>
node.home.link=audit
```

Write a flow using `node.use` for behavior inheritance:

```flow
flow.link=backup

node.worker.label=BACKUP
node.worker.exec=<<EOF
printf "[<node.label>] Processing: <node.file>\n"
cat "<node.file>" | gzip > "<node.file>.gz"
EOF

node.backup.use=worker
node.backup.file=data.txt
```

Write a flow using `func` for reusable templates:

```flow
flow.link=main

func.log.exec=<<EOF
printf "[%s] %s: %s" "$(date +%T)" "<arg.level>" "<arg.msg>"
EOF

node.main.exec=<<EOF
printf "%s\n" "<func.log info.startup>"
printf "%s\n" "work complete"
printf "%s\n" "<func.log info.shutdown>"
EOF

node.info.startup.level=INFO
node.info.startup.msg=System starting up

node.info.shutdown.level=INFO
node.info.shutdown.msg=System shutting down
```

Functions can be called anywhere a template tag is supported, including inside other data keys:

```flow
func.greet.exec=<<EOF
printf "Hello %s" "<arg.name>"
EOF

node.data.name=World
node.data.msg=<func.greet data>

node.main.exec=printf "Message: <node.data.msg>"
```

Heredoc values are executable values:

```flow
flow.link=router
flow.env=dev

node.server.port=<<EOF
[ "<flow.env>" = "dev" ] && printf 8080 || printf 80
EOF

node.router.link=<<EOF
printf home
EOF

node.home.exec=printf "port=%s" "<node.server.port>"
```

A heredoc can be used on any `flow.*`, `node.*`, or `func.*` key. The heredoc
body is template-expanded and executed when the field is resolved. Its stdout
becomes the resolved value, with exactly one trailing newline removed when one
is present.

The key determines how the computed value is used:

- `node.exec` runs as the node action, and stdout becomes node output.
- `node.link` computes the downstream node name.
- Other fields compute the final parameter value.

### Metadata Convention

`flow.c` treats unreserved keys as ordinary data. By convention, `meta` is the
namespace for metadata about the flow document, a node, a function, or another
Flow element. This keeps display-oriented data away from runtime fields such as
`title`, `summary`, `status`, or other project-specific values.

```flow
flow.meta.title=Tiny Site Runtime
flow.meta.summary=Routes one request path to a page or asset response.

node.request.meta.title=Request Router
node.request.meta.summary=Maps request paths to route nodes.

func.response.meta.summary=Builds the HTTP response envelope.
```

The runtime does not give `meta` special behavior. Tools may use
`*.meta.title`, `*.meta.summary`, or other `*.meta.*` fields to render graph
labels, tooltips, inspectors, or documentation.

Overlay one effective flow document:

```bash
./bin/x86_64/linux/flow file.flow \
    --unset flow.link \
    --set flow.link=server \
    --set node.server.exec='printf "%s" "<flow.message>"'
```

Overlays apply in the exact command-line order, and at most 256 overlay
operations are accepted (further ones fail with "too many overlays").

Run the illustrated pipeline against a local `file.flow`:

```bash
./bin/x86_64/linux/flow file.flow --set flow.link=request
./bin/x86_64/linux/flow file.flow --set flow.path=/
./bin/x86_64/linux/flow file.flow --set flow.path=/missing
printf "Hello" | ./bin/x86_64/linux/flow file.flow --link page
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `file.flow` | Execute one flow file |
| `--link <name>` | Execute one explicit entry node |
| `--set key=value` | Append one overlay record |
| `--unset <key>` | Remove prior records for one exact key |

| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Public API

```c
#include "libflow.h"

kc_flow_t *flow = NULL;
kc_flow_run_t *run = NULL;
void *output = NULL;
size_t output_size = 0;

if (kc_flow_open(&flow, "file.flow") != KC_FLOW_OK) {
    return 1;
}

kc_flow_set(flow, "flow.hello", "Hello");

if (kc_flow_run(flow, &run, NULL, NULL, 0) != KC_FLOW_OK) {
    kc_flow_close(flow);
    return 1;
}

/* The run now advances independently and can be stopped at any time. */
if (kc_flow_run_wait(run, &output, &output_size) != KC_FLOW_OK) {
    const char *error = kc_flow_run_error(run);
    (void)error;
}

/* Successful non-empty output is caller-owned. */
kc_flow_free(output);
kc_flow_run_close(run);
kc_flow_close(flow);
```

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

A natural JavaScript projection is:

```js
const f = flow.open("file.flow");

f.set("flow.hello", "Hello");

const run = f.run({
    input: "input data"
});

setTimeout(() => {
    run.stop();
}, 5000);

const output = await run;
```

The C ABI remains handle-based; JavaScript or Lua bindings may project those
handles into natural objects. Native close functions remain lifecycle plumbing
that bindings can integrate with their normal finalizers.

---

## Lifecycle

- `kc_flow_open()` opens one existing flow file and copies its path.
- `kc_flow_set()` and `kc_flow_unset()` append ordered temporary overrides to the opened flow.
- `kc_flow_run()` starts one independent non-blocking run from a snapshot of the opened flow, optional entry, and input.
- `kc_flow_run_stop()` cooperatively stops that run after its current step finishes.
- `kc_flow_run_wait()` waits for completion and returns `KC_FLOW_OK`, `KC_FLOW_ESTOP`, or `KC_FLOW_ERROR`. Successful non-empty output is caller-owned and released with `kc_flow_free()`.
- `kc_flow_run_error()` returns the borrowed contextual error for that run.
- `kc_flow_run_close()` releases the run. If it is still active, close requests a cooperative stop and joins it before release.
- `kc_flow_free()` releases successful output transferred to the caller.
- `kc_flow_close()` releases the opened flow. Existing runs remain valid because they own independent snapshots.
- `kc_flow_version()` returns the build version.

Multiple runs created from one opened flow are independent. Each run owns its
stop request, execution error, branch traversal, and final result.

WebAssembly is not a supported target. Executing local commands and child
flows through native process facilities is the core capability of `flow.c`,
so a WASM build would not provide the same runtime contract.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile the test executable, link dynamically against the generated shared library, and run directly.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`. The normal suite includes one grouped CLI contract case covering the shipped command interface.

WebAssembly is not a supported target. Executing local commands and child
flows through native process facilities is the core capability of `flow.c`,
so a WASM build would not provide the same runtime contract.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Visualization

`flow.c` is a strict execution engine dedicated to running system command workflows.
For rendering visual graph representations of `.flow` documents, you can check the
[fldot](https://github.com/kaisarcode/fldot) conversion tool.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
