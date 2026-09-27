# menu.c - Native Application Menu Entries

`menu.c` adds and deletes applications from the native application menu.

It uses the current user's application menu and selects the platform mechanism
internally.

---

## CLI

Add or replace an application menu entry:

```bash
menu myapp \
  --name "My App" \
  --description "My application" \
  --command "/path/to/myapp --foo bar" \
  --icon "/path/to/icon.png"
```

Delete an entry:

```bash
menu --delete myapp
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `-n`, `--name <name>` | Set the display name. |
| `-D`, `--description <text>` | Set the optional description. |
| `-c`, `--command <command>` | Set the command executed by the entry. |
| `-i`, `--icon <path>` | Set the optional icon. |
| `-d`, `--delete <id>` | Delete an entry by id. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show the build version. |

`id`, `name`, and `command` are required when adding an entry. Adding an
existing id replaces that entry. Deleting a missing id succeeds without error.

---

## Public API

```c
#include "libmenu.h"

kc_menu_entry_t entry = {
    .id = "myapp",
    .name = "My App",
    .description = "My application",
    .command = "/path/to/myapp --foo bar",
    .icon = "/path/to/icon.png"
};

if (kc_menu_add(&entry) != KC_MENU_OK) {
    /* handle error */
}

/* Later */
kc_menu_delete("myapp");
```

The public API contains three operations:

```c
int kc_menu_add(const kc_menu_entry_t *entry);
int kc_menu_delete(const char *id);
uint64_t kc_menu_version(void);
```

`description` and `icon` may be `NULL`.

---

## Platform Scope

| Platform | Behavior |
| :--- | :--- |
| Linux | Adds a desktop application entry in the current user's XDG application menu. |
| Windows | Adds a shortcut in the current user's Start Menu Programs folder. |

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/`.

```bash
make
```

### Tests

Build project artifacts first, then run the portable contract tests:

```bash
make
make test
```

To run Windows tests through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured Linux and Windows targets.

```bash
make all
make x86_64/linux
make x86_64/windows
```

---

## Development Requirements

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)
- MinGW for Windows cross-compilation
- Wine for Windows tests on Linux

---

## Beta Notice

This is beta software tested primarily on Debian x86_64. It was created out of
a personal need for these libraries, but no guarantees are provided regarding
its stability or future support. You are free to test it, use it, and modify it
as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

This project is distributed under the **GNU General Public License version 3
(GPLv3)**.
