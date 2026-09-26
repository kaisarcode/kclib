# dmn.c - Local Daemon Manager

`dmn.c` creates persistent named local daemons, keeps their processes resident, exchanges EOT-delimited requests and responses, lists registrations, sends platform signals, and deletes daemons.

`dmn.c` manages persistent named local daemons and their command exchanges.

---

## CLI

The CLI contract remains:

```text
dmn <name> <cmd>
dmn -d <name>
dmn <name> -d
dmn -l [name]
dmn <name> -l
dmn <name> -s <sig>
dmn <name>
dmn -h
dmn -v
```

Examples:

```bash
dmn worker /usr/bin/my_app -p 1
printf 'hello\n\004' | dmn worker
dmn --list
dmn worker --list
dmn worker -s 10
dmn worker --delete
```

`KC_DMN_DIR` is an advanced process-level override for the runtime directory. Normal API and CLI callers do not need to select a directory; the platform runtime location is resolved automatically.

---

## Public API

```c
typedef struct kc_dmn kc_dmn_t;

typedef struct {
    const char *cmd;
    const void *eot;
    size_t eot_size;
} kc_dmn_options_t;

typedef struct {
    const char *name;
} kc_dmn_entry_t;

#define KC_DMN_OK          0
#define KC_DMN_NOT_FOUND   1
#define KC_DMN_ERROR      -1
```

### Daemon lifecycle

```c
int kc_dmn_create(
    const char *name,
    const kc_dmn_options_t *options
);

int kc_dmn_open(
    kc_dmn_t **out,
    const char *name
);

void kc_dmn_close(kc_dmn_t *dmn);

int kc_dmn_delete(
    const char *name
);
```

`kc_dmn_create()` creates or replaces a named persistent daemon.
`options->cmd` is required. The default EOT marker is byte `0x04`.

`kc_dmn_open()` creates a local handle bound to the daemon identity and its
persistent configuration. Closing that handle does not stop the daemon.

`kc_dmn_delete()` stops and removes the named daemon.

### Configuration

```c
int kc_dmn_set_cmd(kc_dmn_t *dmn, const char *cmd);
const char *kc_dmn_get_cmd(const kc_dmn_t *dmn);

int kc_dmn_set_eot(
    kc_dmn_t *dmn,
    const void *eot,
    size_t eot_size
);

const void *kc_dmn_get_eot(
    const kc_dmn_t *dmn,
    size_t *out_size
);
```

`set_cmd` replaces the command while preserving the daemon identity.

The EOT marker frames complete exchanges with the resident process.
`set_eot` changes that marker and `get_eot` returns the configured value.
Passing `NULL, 0` to `set_eot` restores the default byte `0x04`.

### Request and response

```c
int kc_dmn_send_data(
    kc_dmn_t *dmn,
    const void *data,
    size_t data_size,
    void **out_data,
    size_t *out_size
);
```

`kc_dmn_send_data()` performs one complete exchange against the already
running process:

1. write the request bytes;
2. append the configured EOT marker;
3. read until the same EOT marker terminates the response;
4. return the complete response without that marker.

The daemon process remains resident after the exchange, so repeated requests do
not restart or reload the underlying process.

### Signals

```c
int kc_dmn_send_signal(
    kc_dmn_t *dmn,
    int signal
);
```

`kc_dmn_send_signal()` sends one platform signal request to the managed
process.

### Listing

```c
int kc_dmn_list(
    kc_dmn_entry_t **out_entries,
    size_t *out_count
);

void kc_dmn_free(void *ptr);
```

Public list entries expose daemon identity only. Runtime socket or pipe
endpoints are transport details and are not part of the library contract.

### Version

```c
uint64_t kc_dmn_version(void);
```

### Runtime Model

Created daemons continue running independently of the process that created or
opened them. The underlying process remains resident between EOT-delimited
request/response exchanges until the daemon is replaced or removed.

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
