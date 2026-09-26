# wvw.c - Native WebView Window

`wvw.c` provides a reusable native WebView window library with a CLI consumer,
portable contract tests, and platform backends for Windows, Linux, and macOS.

The native backends are WebView2 on Windows, WebKitGTK on Linux, and WKWebView
on macOS.

---

## CLI

Open a URL with the default window settings:

```bash
./bin/x86_64/linux/wvw --url https://example.com
```

Set the title, size, and position:

```bash
./bin/x86_64/linux/wvw \
    --url https://example.com \
    --title Example \
    --width 1280 \
    --height 720 \
    --posx 100 \
    --posy 100
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `--url <url>` | Initial URL |
| `--title <title>` | Window title |
| `--background <hex>` | Background as `RRGGBB` or `AARRGGBB` |
| `--width <px>` | Initial window width |
| `--height <px>` | Initial window height |
| `--posx <px>` | Initial horizontal position |
| `--posy <px>` | Initial vertical position |
| `--fullscreen` | Start in fullscreen mode |
| `--borderless` | Start without window decorations |
| `--always-on-top` | Keep the window above normal windows |
| `--click-through` | Ignore mouse input on the host window |
| `--no-focus` | Do not activate the window for keyboard focus |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

The CLI is a thin consumer of the reusable public API. It blocks until the
window closes, but that waiting behavior is private to the CLI.

The same settings can be supplied through environment variables:

```text
KC_WVW_URL
KC_WVW_TITLE
KC_WVW_BACKGROUND
KC_WVW_WIDTH
KC_WVW_HEIGHT
KC_WVW_POSX
KC_WVW_POSY
KC_WVW_FULLSCREEN
KC_WVW_BORDERLESS
KC_WVW_ALWAYS_ON_TOP
KC_WVW_CLICK_THROUGH
KC_WVW_NO_FOCUS
```

Command-line arguments override environment defaults.

---

## Public API

```c
#include "libwvw.h"

kc_wvw_t *wvw = NULL;
int width = 800;
int height = 600;
int posx = 100;
int posy = 100;

kc_wvw_options_t options = {
    .url = "https://example.com",
    .title = "Example",
    .width = &width,
    .height = &height,
    .posx = &posx,
    .posy = &posy
};

if (kc_wvw_open(&wvw, &options) != KC_WVW_OK) {
    const char *error = kc_wvw_get_error(wvw);
    kc_wvw_close(wvw);
    return 1;
}

kc_wvw_set_title(wvw, "Ready");
kc_wvw_set_size(wvw, 1024, 768);
kc_wvw_set_position(wvw, 200, 150);
kc_wvw_navigate(wvw, "https://kaisarcode.com");

kc_wvw_close(wvw);
```

`kc_wvw_open()` returns an operational WebView. The caller does not need to
run a public event loop after opening it.

`url` is required. Omitted width and height use 1280x720. Omitted position
values use native placement. Omitted boolean options are false.

The public window API includes:

```c
int kc_wvw_open(kc_wvw_t **out, const kc_wvw_options_t *options);
const char *kc_wvw_get_error(const kc_wvw_t *wvw);

int kc_wvw_navigate(kc_wvw_t *wvw, const char *url);
int kc_wvw_add_init_script(kc_wvw_t *wvw, const char *javascript);
int kc_wvw_enable_bridge(
    kc_wvw_t *wvw,
    const kc_wvw_bridge_options_t *options
);
int kc_wvw_post_bridge_event(kc_wvw_t *wvw, const char *json);

int kc_wvw_hide(kc_wvw_t *wvw);
int kc_wvw_show(kc_wvw_t *wvw);
int kc_wvw_minimize(kc_wvw_t *wvw);
int kc_wvw_maximize(kc_wvw_t *wvw);
int kc_wvw_restore(kc_wvw_t *wvw);

int kc_wvw_set_title(kc_wvw_t *wvw, const char *title);
const char *kc_wvw_get_title(const kc_wvw_t *wvw);

int kc_wvw_set_size(kc_wvw_t *wvw, int width, int height);
int kc_wvw_get_size(
    const kc_wvw_t *wvw,
    int *out_width,
    int *out_height
);

int kc_wvw_set_position(kc_wvw_t *wvw, int x, int y);
int kc_wvw_get_position(
    const kc_wvw_t *wvw,
    int *out_x,
    int *out_y
);

int kc_wvw_is_visible(const kc_wvw_t *wvw);
int kc_wvw_is_minimized(const kc_wvw_t *wvw);
int kc_wvw_is_maximized(const kc_wvw_t *wvw);
int kc_wvw_is_fullscreen(const kc_wvw_t *wvw);

void kc_wvw_close(kc_wvw_t *wvw);
uint64_t kc_wvw_version(void);
```

Actions remain actions. Named properties use `set_*` and `get_*`.
Boolean window properties use `is_*`.

`kc_wvw_get_title()` and `kc_wvw_get_error()` return borrowed strings.

`kc_wvw_close(NULL)` is safe.

### Native Bridge

The bridge is disabled by default. Applications can install document-start
JavaScript and expose a fixed method whitelist to trusted content.

```c
static int app_bridge(
    kc_wvw_t *wvw,
    const char *method,
    const char *params_json,
    const char **out_result_json,
    void *userdata
) {
    (void)wvw;
    (void)params_json;
    (void)userdata;

    if (!strcmp(method, "get_version")) {
        *out_result_json = "{\"version\":1}";
        return KC_WVW_OK;
    }

    *out_result_json =
        "{\"code\":\"UNKNOWN_METHOD\",\"message\":\"Unknown method\"}";
    return KC_WVW_ERROR;
}

const char *methods[] = { "get_version" };

kc_wvw_bridge_options_t bridge = {
    .methods = methods,
    .method_count = 1,
    .callback = app_bridge,
    .allow_file = 1
};

kc_wvw_add_init_script(wvw, "window.APP_VERSION = '1.0';");
kc_wvw_enable_bridge(wvw, &bridge);
```

Bridge callback results are borrowed. The library validates and consumes the
returned JSON synchronously and does not free it.

When the bridge is active, navigation is restricted to trusted origins.
`file:`, `data:`, and localhost access are controlled by the corresponding
bridge options.

### Window Options

`background` accepts `RRGGBB` and `AARRGGBB`.

`always_on_top`, `click_through`, and `no_focus` configure the native host
window at startup.

`KC_WVW_TITLE_MAX` is 4096 bytes and `KC_WVW_SIZE_MAX` is 16384 pixels.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/`.

```bash
make
```

A plain `make` builds the current host target.

### Tests

Build project artifacts first, then run the native contract suite:

```bash
make
make test
```

The test suite validates the reusable public API and one grouped CLI case.

To run the Windows contract suite through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

`make all` builds all configured targets:

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make aarch64/macos
```

WebAssembly is not supported because this library provides a native desktop
window backed by a platform WebView.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` with C11 support

### Linux

- GTK 3
- WebKitGTK 4.1
- `pkg-config`

### Windows

- MinGW for cross-compilation on Linux.
- Microsoft Edge WebView2 Runtime for execution.
- `wine` for Windows tests on Linux.

The Windows build places `WebView2Loader.dll` beside the generated executable
and shared library.

### macOS

- AppKit
- WebKit
- Foundation
- `osxcross` with a compatible Apple SDK when cross-compiling from Linux

macOS window operations must run from the main thread.

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
