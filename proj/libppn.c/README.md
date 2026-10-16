# libppn.c - Native popup notifications

`libppn.c` submits native desktop notifications on Linux, Windows, and macOS.
The operating system controls presentation, duration, grouping, and user
notification policy.

## Quick start

```c
#include "libppn.h"

kc_ppn_notification_t notification = {
    .title = "Download complete",
    .message = "package.tar.gz is ready"
};

kc_ppn_show(&notification);
```

Title and message are required UTF-8 strings.

## Platform support

Linux uses the current D-Bus notification service, Windows uses the shell
notification-area API, and macOS uses Notification Center.

Native tests require an appropriate desktop session because they display a real
notification.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
