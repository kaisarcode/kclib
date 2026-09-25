# init.c - Persistent Startup Registration

Persistent operating-system startup registration.

init.c registers named commands so the operating system starts them during boot or user startup. It does not start, stop, supervise, or execute those commands in the caller process.

---

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

---

## Public API

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

### Public model

A registration is a persistent operating-system entity identified by name.

```text
create / delete = lifecycle of the persistent boot registration
open / close    = lifecycle of a local handle
set/get_cmd     = persistent command configuration
get_user        = registration metadata
```

The operating-system backend is private. Linux selects the available init mechanism automatically, such as systemd, runit, OpenRC, or SysV. Windows uses the native startup mechanism.

### Metadata namespace

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

### Platform scope

init.c is an operating-system library. Browser WebAssembly is not applicable because browser code cannot register operating-system startup commands without changing the capability contract.

Supported artifact platforms are Linux and Windows.

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
