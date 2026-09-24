# init.c

Persistent operating-system startup registration.

init.c registers named commands so the operating system starts them during boot or user startup. It does not start, stop, supervise, or execute those commands in the caller process.

## Public model

A registration is a persistent operating-system entity identified by name.

```text
create / delete = lifecycle of the persistent boot registration
open / close    = lifecycle of a local handle
set/get_cmd     = persistent command configuration
get_user        = registration metadata
```

The operating-system backend is private. Linux selects the available init mechanism automatically, such as systemd, runit, OpenRC, or SysV. Windows uses the native startup mechanism.

## C API

```c
typedef struct kc_init kc_init_t;

typedef struct {
    const char *cmd;
} kc_init_options_t;

typedef struct {
    const char *name;
    const char *user;
    const char *cmd;
} kc_init_entry_t;

int kc_init_create(
    const char *name,
    const kc_init_options_t *options
);

int kc_init_open(
    kc_init_t **out,
    const char *name
);

int kc_init_list(
    kc_init_entry_t **out_entries,
    size_t *out_count
);

int kc_init_delete(const char *name);

int kc_init_set_cmd(
    kc_init_t *init,
    const char *cmd
);

const char *kc_init_get_cmd(const kc_init_t *init);
const char *kc_init_get_user(const kc_init_t *init);
const char *kc_init_error(const kc_init_t *init);

void kc_init_free(void *ptr);
void kc_init_close(kc_init_t *init);
uint64_t kc_init_version(void);
```

## Scripting model

JavaScript-style use:

```js
init.create("server", {
    cmd: "node server.js"
});

const service = init.open("server");

service.getCmd();
service.getUser();
service.setCmd("node app.js");

init.delete("server");
```

Lua-style use:

```lua
init.create("server", {
    cmd = "node server.js"
})

local service = init.open("server")

service:get_cmd()
service:get_user()
service:set_cmd("node app.js")

init.delete("server")
```

Bindings only need mechanical adaptation around the C ABI.

## Metadata namespace

The default metadata namespace is per-user.

On Linux and other XDG environments:

```text
$XDG_DATA_HOME/kaisarcode/init.c
```

When `XDG_DATA_HOME` is not set:

```text
$HOME/.local/share/kaisarcode/init.c
```

On Windows, init.c uses the corresponding per-user application-data directory under `kaisarcode\init.c`.

`KC_INIT_DIR` is an advanced process-level override. The CLI exposes the same capability through `--dir <path>`. Normal callers do not need to select a directory.

## CLI

```text
Usage: init <name> [command...]

Commands:
  init <name> <cmd>    Register or replace a startup entry
  init -l [name]       List registrations
  init <name> -l       List one registration
  init -d <name>       Remove a registration
  init <name> -d       Remove a registration

Options:
  -l, --list           List registrations
  -d, --delete         Remove a registration
  --dir <path>         Override metadata directory
  -h, --help           Show this help
  -v, --version        Show version
```

Registering or replacing an entry updates only the startup registration. init.c does not execute the registered command immediately.

## Platform scope

init.c is an operating-system library. Browser WebAssembly is not applicable because browser code cannot register operating-system startup commands without changing the capability contract.

Supported artifact platforms are Linux and Windows.

## Build

```bash
make
```

Build all configured targets:

```bash
make all
```

## Tests

Build artifacts first, then run the native contract suite:

```bash
make test
```

Windows artifacts can be validated through Wine:

```bash
make test wine
```

The contract suite uses an isolated `KC_INIT_DIR` fixture for metadata-only API checks and contains one grouped CLI case.
