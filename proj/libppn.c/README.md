# libppn.c - Native Popup Notifications

`libppn.c` displays native operating-system notifications on Linux, Windows,
and macOS.

---

## Public API

```c
#include "libppn.h"

kc_ppn_notification_t notification = {
    .title = "Download complete",
    .message = "package.tar.gz is ready"
};

if (kc_ppn_show(&notification) != KC_PPN_OK) {
    /* The notification could not be submitted to the operating system. */
}
```

`title` and `message` are required UTF-8 strings.

---

## Platform Scope

| Platform | Behavior |
| :--- | :--- |
| Linux | Sends `org.freedesktop.Notifications.Notify` through the current D-Bus session. |
| Windows | Uses the Windows shell notification-area API. |
| macOS | Delivers the notification through Notification Center. |

The operating system controls presentation, duration, grouping, and whether a
notification is visible according to the user's notification settings.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/`.

```bash
make
```

### Tests

Build the native library and run the contract tests:

```bash
make
make test
```

The default tests do not display a notification. To exercise the native
notification path in an interactive desktop session:

```bash
PPN_TEST_NOTIFY=1 make test
```

To run the contract tests through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

A plain `make` builds the current Linux x86_64 target. `make all` builds all
configured targets.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make aarch64/macos
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

Linux has no additional compile-time library dependency. A desktop session with
a Freedesktop notification service is required to display notifications.

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS.

---

## Beta Notice

This is a beta project tested primarily on Debian x86_64. It was created out of
a personal need for these libraries, but no guarantees are provided regarding
its stability or future support. You are free to test it, use it, and modify it
as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
