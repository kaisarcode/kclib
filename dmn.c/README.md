# dmn.c - Local Daemon Manager

`dmn.c` creates named local daemons, opens them for interaction, exchanges binary data, exposes raw streams when needed, lists registrations, sends platform signals, and deletes daemons.

The public API is designed around daemon identities rather than sockets or pipes. Unix Domain Sockets and Windows Named Pipes are implementation details.

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

## Scripting model

Lua:

```lua
dmn.create("my_daemon", {
    cmd = "/usr/bin/my_app -p 1",
    eot = "\4"
})

local daemons = dmn.list()

local daemon = dmn.open("my_daemon")
print(daemon:get_cmd())
print(daemon:get_eot())

daemon:set_cmd("/usr/bin/my_app -p 2")
daemon:set_eot("==END==")

daemon:on("data", function(data)
    io.write(data)
end)

local response = daemon:send_data("Some data")
daemon:send_signal(10)

local stream = daemon:stream()
stream:write("raw bytes")
local chunk = stream:read()
stream:close()

daemon:close()
dmn.delete("my_daemon")
```

The binding should remain mechanical: daemon methods map directly to the public C functions, while raw streams map to `kc_dmn_stream_*`.

## Runtime model

Runtime state is local and temporary. On POSIX, dmn prefers `XDG_RUNTIME_DIR`, then `/run/user/<uid>`, with `/tmp` only as a fallback. Windows uses the system temporary directory. These locations are runtime state, not persistent registration storage.

POSIX uses Unix Domain Sockets plus manager/backend PID files. The backend can remain resident across response cycles. The configured EOT marks the end of one cycle while preserving the resident backend.

Windows uses Named Pipes and the existing detached server process model. Platform process mechanics are intentionally allowed to differ; the public API describes daemon operations rather than requiring identical internals.

Registered commands are trusted local operator input and execute through the platform shell.

## Build

```bash
make
make all
```

Tests are run through:

```bash
make test
make test wine
```

WASM is not applicable to this library because the product depends on host process creation and local operating-system IPC.

## License

GNU General Public License version 3 (GPLv3).
