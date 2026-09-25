# dmn.c - Local Daemon Manager

`dmn.c` creates named local daemons, opens them for interaction, exchanges binary data, exposes raw streams when needed, lists registrations, sends platform signals, and deletes daemons.

The public API is designed around daemon identities rather than sockets or pipes. Unix Domain Sockets and Windows Named Pipes are implementation details.

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
typedef struct kc_dmn_stream kc_dmn_stream_t;

typedef struct {
    const char *cmd;
    const void *eot;
    size_t eot_size;
} kc_dmn_options_t;

typedef struct {
    const char *name;
    const char *endpoint;
} kc_dmn_entry_t;

typedef void (*kc_dmn_handler_t)(
    const void *data,
    size_t size,
    void *userdata
);

#define KC_DMN_OK          0
#define KC_DMN_NOT_FOUND   1
#define KC_DMN_EOF         2
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

`kc_dmn_create()` creates or replaces a named daemon. `options->cmd` is required. The runtime directory is resolved automatically. `options->eot == NULL` with `eot_size == 0` uses the default EOT byte `0x04`.

`kc_dmn_open()` creates a local handle bound to a daemon name and uses the automatically resolved runtime directory.

`kc_dmn_close()` releases only the local handle. It does not delete the daemon.

`kc_dmn_delete()` stops and removes the named daemon from the active runtime directory.

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

Each mutable public property has a matching getter.

`set_cmd` replaces the running daemon command.

`set_eot` changes the response-cycle marker. Passing `NULL, 0` restores the default single byte `0x04`. EOT values are binary and may contain any byte sequence.

Getter results are borrowed from the daemon handle and remain valid until the corresponding value changes or the handle closes.

### Data events and complete exchanges

```c
int kc_dmn_on(
    kc_dmn_t *dmn,
    const char *event,
    kc_dmn_handler_t handler,
    void *userdata
);

int kc_dmn_send_data(
    kc_dmn_t *dmn,
    const void *data,
    size_t data_size,
    void **out_data,
    size_t *out_size
);
```

The supported event is `"data"`. Its handler receives response chunks directly as binary data.

`kc_dmn_send_data()` performs one complete high-level exchange:

1. open a daemon stream;
2. write the supplied data;
3. append the configured EOT;
4. read response bytes;
5. emit `"data"` chunks;
6. finish when the configured EOT is received, or when the platform stream ends;
7. return the complete response without the EOT marker.

The returned response is owned by the caller and must be released with `kc_dmn_free()`.

### Raw stream

```c
int kc_dmn_stream(
    kc_dmn_t *dmn,
    kc_dmn_stream_t **out
);

int kc_dmn_stream_write(
    kc_dmn_stream_t *stream,
    const void *data,
    size_t size
);

int kc_dmn_stream_read(
    kc_dmn_stream_t *stream,
    void *data,
    size_t capacity,
    size_t *out_size
);

void kc_dmn_stream_close(kc_dmn_stream_t *stream);
```

The raw stream API does not add, remove, or interpret EOT. It exposes the daemon byte stream directly.

`kc_dmn_stream_read()` returns `KC_DMN_EOF` when the stream ends.

### Signals

```c
int kc_dmn_send_signal(
    kc_dmn_t *dmn,
    int signal
);
```

On POSIX, the numeric signal is sent to the managed backend process. Windows uses the daemon's existing platform signal mechanism.

### Listing

```c
int kc_dmn_list(
    kc_dmn_entry_t **out_entries,
    size_t *out_count
);

void kc_dmn_free(void *ptr);
```

The returned entries and their strings live in one allocation and are released with one `kc_dmn_free()`.

### Version

```c
uint64_t kc_dmn_version(void);
```

### Runtime model

Runtime state is local and temporary. On POSIX, dmn prefers `XDG_RUNTIME_DIR`, then `/run/user/<uid>`, with `/tmp` only as a fallback. Windows uses the system temporary directory. These locations are runtime state, not persistent registration storage.

POSIX uses Unix Domain Sockets plus manager/backend PID files. The backend can remain resident across response cycles. The configured EOT marks the end of one cycle while preserving the resident backend.

Windows uses Named Pipes and the existing detached server process model. Platform process mechanics are intentionally allowed to differ; the public API describes daemon operations rather than requiring identical internals.

Registered commands are trusted local operator input and execute through the platform shell.

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
