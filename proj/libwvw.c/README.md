# libwvw.c - Native WebView window

`libwvw.c` creates a native WebView window with a portable C API for navigation,
window state, initialization scripts, and an optional native bridge. Backends
use WebView2 on Windows, WebKitGTK on Linux, and WKWebView on macOS.

## Quick start

```c
#include "libwvw.h"

kc_wvw_t *window = NULL;
int width = 800;
int height = 600;
kc_wvw_options_t options = {
    .url = "about:blank",
    .title = "Example",
    .width = &width,
    .height = &height
};

if (kc_wvw_open(&window, &options) == KC_WVW_OK) {
    kc_wvw_set_title(window, "Ready");
    kc_wvw_show(window);
    kc_wvw_close(window);
}
```

The URL is required. Omitted size values default to 1280x720 and omitted
boolean options are false.

## Requirements

Linux builds require GTK 3, WebKitGTK 4.1, and `pkg-config`. Windows execution
requires the Microsoft Edge WebView2 Runtime. macOS uses AppKit, WebKit, and
Foundation.

## Documentation

Read the [native bridge guide](doc/native-bridge.md) before exposing application
methods, executable commands, or privileged origins to WebView content.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets. See the
[repository build guide](../../README.md#build) for shared toolchain and target
information.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
