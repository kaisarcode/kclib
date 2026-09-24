# wch.c - Resident File Watcher

`wch.c` manages named resident filesystem watchers.

A watcher has its own system lifecycle. Creating one starts a resident process
that continues watching after the caller exits. Opening a watcher creates only a
local management handle. Closing that handle does not stop the resident watcher.

Each filesystem event can execute the configured persistent command and can also
be observed temporarily by processes that open the watcher and subscribe with
`kc_wch_on()`.

## CLI

The CLI contract remains:

```text
wch <name> <path> <cmd>
wch <name> -r <path> <cmd>
wch -l [name]
wch <name> -l
wch -d <name>
wch <name> -d
wch -h
wch -v
```

Examples:

```bash
wch frontend ./src make build
wch frontend --recursive ./src make build
wch --list
wch frontend --list
wch frontend --delete
```

Registering an existing name replaces that watcher while preserving its logical
identity.

For each filesystem event, the persistent command receives the normalized event
name and path:

```text
<command...> <event> <path>
```

The event names are `add`, `upd`, and `del`.

`KC_WCH_DIR` is an advanced process-level override for the runtime directory. Normal API and CLI callers do not need to select a directory.

## Public API

```c
typedef struct kc_wch kc_wch_t;

typedef struct {
    const char *path;
    const char *cmd;
    int recursive;
} kc_wch_options_t;

typedef struct {
    const char *name;
    const char *path;
    const char *cmd;
    int recursive;
    int running;
} kc_wch_entry_t;

typedef void (*kc_wch_handler_t)(
    const char *path,
    void *userdata
);

#define KC_WCH_OK          0
#define KC_WCH_NOT_FOUND   1
#define KC_WCH_ERROR      -1
```

### Resident lifecycle

```c
int kc_wch_create(
    const char *name,
    const kc_wch_options_t *options
);

int kc_wch_open(
    kc_wch_t **out,
    const char *name
);

void kc_wch_close(kc_wch_t *w);

int kc_wch_delete(
    const char *name
);
```

`kc_wch_create()` creates or replaces the named resident watcher.

`kc_wch_open()` creates a local handle bound to a watcher name. It does not
start or stop the watcher.

`kc_wch_close()` releases only the local handle.

`kc_wch_delete()` stops and removes the resident watcher.

### Configuration

```c
int kc_wch_set_path(kc_wch_t *w, const char *path);
const char *kc_wch_get_path(const kc_wch_t *w);

int kc_wch_set_cmd(kc_wch_t *w, const char *cmd);
const char *kc_wch_get_cmd(const kc_wch_t *w);


int kc_wch_set_recursive(kc_wch_t *w, int recursive);
int kc_wch_get_recursive(const kc_wch_t *w);
```

Each public mutable property has a matching getter.

Changing `path`, `cmd`, or `recursive` updates the persistent watcher and
restarts its resident process while preserving its name.

### Temporary subscriptions

```c
int kc_wch_on(
    kc_wch_t *w,
    const char *event,
    kc_wch_handler_t handler,
    void *userdata
);
```

Supported events are:

```text
add
upd
del
```

The callback receives the changed path directly.

Subscriptions belong to the current process. Closing the handle removes those
subscriptions, but the resident watcher keeps running and keeps executing its
persistent command.

Passing a NULL handler clears the selected event subscription.

### Listing

```c
int kc_wch_list(
    kc_wch_entry_t **out_entries,
    size_t *out_count
);

void kc_wch_free(void *ptr);
```

The returned entries and their strings share one allocation released with
`kc_wch_free()`.

`running` reports whether the resident process is currently alive.

### Version

```c
uint64_t kc_wch_version(void);
```

## Lua model

```lua
wch.create("my_watcher", {
    path = "./src",
    cmd = "make build",
    recursive = true
})

local watchers = wch.list()

local watcher = wch.open("my_watcher")
print(watcher:get_path())
print(watcher:get_cmd())
print(watcher:get_recursive())

watcher:set_path("./lib")
watcher:set_cmd("make test")
watcher:set_recursive(false)

watcher:on("add", function(path)
    print(path)
end)

watcher:on("upd", function(path)
    print(path)
end)

watcher:on("del", function(path)
    print(path)
end)

watcher:close()

wch.delete("my_watcher")
```

The intended binding is mechanical. The public C names map directly to the
scripting operations.

## Runtime model

The native filesystem backends remain private implementation details:

- Linux uses inotify.
- Apple platforms use kqueue.
- Windows uses ReadDirectoryChangesW.

The resident watcher persists registration metadata and process state in its runtime directory. On POSIX, wch prefers `XDG_RUNTIME_DIR`, then `/run/user/<uid>`, with `/tmp` only as a fallback; Windows uses the system temporary directory. This is runtime state rather than persistent configuration.

The persistent command continues running on events even when no client has the
watcher open.

Temporary subscriptions use a private local event transport. That transport is
not part of the public API.

Browser WebAssembly is not applicable to the resident product because a browser
runtime cannot provide a system process that survives the host application.

## Build

```bash
make
make all
```

Tests:

```bash
make test
make test wine
```

## License

GNU General Public License version 3 (GPLv3).
