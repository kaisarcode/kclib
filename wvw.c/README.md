# wvw.c - native WebView window

`libwvw` exposes one persistent native WebView window on Windows, Linux and
macOS. The public API is designed as the direct programmatic surface used by C
and FFI consumers: `kc_wvw_open()` returns an operational WebView and there is
no caller-visible run, loop, wait or stop step.

The standalone `wvw` executable is a small consumer of the same public API.
As a terminal program it blocks until its window closes; that waiting behavior
is private to the CLI and is not part of `libwvw.h`.

Backends are WebView2 on Windows, WebKitGTK on Linux and WKWebView on macOS.

## Public API

A minimal window can be opened with borrowed input options:

```c
#include "libwvw.h"

kc_wvw_t *wvw = NULL;
kc_wvw_options_t options = {
    .url = "https://example.com",
    .title = "Example"
};

if (kc_wvw_open(&wvw, &options) != KC_WVW_OK) {
    const char *error = kc_wvw_get_error(wvw);
    /* handle error */
}

/* The WebView is already operational here. */
kc_wvw_set_title(wvw, "Ready");
kc_wvw_navigate(wvw, "https://kaisarcode.com");

kc_wvw_close(wvw);
```

Input strings and scalar option values needed after `open` returns are copied
by the library. `url` is required. Omitted `width` and `height` use
1280x720. Omitted positions use native placement. Omitted boolean options are
false.

Scalar option pointers make omission different from an explicit zero:

```c
int width = 800;
int fullscreen = 0;

kc_wvw_options_t options = {
    .url = "file:///tmp/app.html",
    .width = &width,
    .fullscreen = &fullscreen
};
```

The public window operations are:

```c
int kc_wvw_navigate(kc_wvw_t *wvw, const char *url);

int kc_wvw_show(kc_wvw_t *wvw);
int kc_wvw_hide(kc_wvw_t *wvw);
int kc_wvw_minimize(kc_wvw_t *wvw);
int kc_wvw_maximize(kc_wvw_t *wvw);
int kc_wvw_restore(kc_wvw_t *wvw);

int kc_wvw_set_title(kc_wvw_t *wvw, const char *title);
const char *kc_wvw_get_title(const kc_wvw_t *wvw);

int kc_wvw_set_size(kc_wvw_t *wvw, int width, int height);
int kc_wvw_get_size(const kc_wvw_t *wvw, int *out_width, int *out_height);

int kc_wvw_set_position(kc_wvw_t *wvw, int x, int y);
int kc_wvw_get_position(const kc_wvw_t *wvw, int *out_x, int *out_y);

int kc_wvw_is_visible(const kc_wvw_t *wvw);
int kc_wvw_is_minimized(const kc_wvw_t *wvw);
int kc_wvw_is_maximized(const kc_wvw_t *wvw);
int kc_wvw_is_fullscreen(const kc_wvw_t *wvw);

void kc_wvw_close(kc_wvw_t *wvw);
```

Actions remain actions. Named value properties use `set_*` and `get_*`.
This includes both window size and window position. Boolean properties use `is_*`. There is no aggregate `get_state()`: each
query says directly what it returns.

`kc_wvw_get_title()` and `kc_wvw_get_error()` return borrowed strings.
The title is invalidated by the next successful title change or close. The
error is contextual and invalidated by close.

`kc_wvw_close(NULL)` is safe and ends the public lifetime of a non-NULL
WebView.

## FFI shape

The public header is intentionally the FFI surface. Generated LuaJIT cdefs use
the same C names, so the natural binding shape is mechanically equivalent to:

```lua
local view = wvw.open({
    url = "file:///tmp/app.html",
    title = "My app"
})

view:set_title("Ready")

local title = view:get_title()
local width, height = view:get_size()

if view:is_visible() then
    view:minimize()
    view:restore()
end

view:close()
```

There is no `loop()`, `run()`, `wait()`, `start()` or `stop()` operation
for programmatic consumers.

## Native bridge

The bridge is disabled by default. Applications explicitly install trusted
document-start JavaScript and may enable a fixed method whitelist:

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

The callback receives the method name and serialized JSON parameters exactly as
strings. `out_result_json` is borrowed from the callback: `wvw` validates and
uses it synchronously and never frees it. A NULL success result means JSON
`null`. A non-NULL result must contain exactly one complete serialized JSON
value.

The method array is copied when the bridge is enabled. Callback and userdata
remain retained logically until `kc_wvw_close()`; the caller keeps userdata
valid for that lifetime.

When a bridge is active, remote navigation is blocked. `file:` and `data:`
access require their corresponding bridge flags and localhost requires
`allow_localhost`.

The page-side bridge keeps its existing `window.NativeBridge` surface and the
`nativebridge` event. `kc_wvw_post_bridge_event()` sends one serialized JSON
value as that event's detail.

## CLI

```sh
./bin/x86_64/linux/wvw --url https://example.com
./bin/x86_64/linux/wvw --url https://example.com --title Example --width 1280 --height 720
```

Supported options:

```text
--url <url>
--title <title>
--background <hex>
--width <px>
--height <px>
--posx <px>
--posy <px>
--fullscreen
--borderless
--always-on-top
--click-through
--no-focus
-h, --help
-v, --version
```

The environment provides CLI defaults and command-line arguments override them:

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

`KC_WVW_BROWSER_ARGS` remains available on Windows for explicit WebView2
runtime diagnostics and experiments.

The CLI intentionally blocks until its window closes. That behavior is
implemented through a private CLI/library contract and is not exported by the
public header or generated cdef.

## Window options

`background` accepts `RRGGBB` and `AARRGGBB`. Transparent
`AARRGGBB` requests a best-effort transparent host surface. On Windows,
WebView2 supports startup alpha values 00 or FF; Linux and macOS use their
native compositor/WebView facilities.

`always_on_top`, `click_through` and `no_focus` are initial host-window
properties intended for overlay-style applications.

`KC_WVW_TITLE_MAX` is 4096 bytes and `KC_WVW_SIZE_MAX` is 16384 pixels.

## Build and tests

```sh
make
make test
```

Windows-through-Wine tests use:

```sh
make x86_64/windows
make test wine
```

The grouped contract includes `kc_wvw_cli`, which verifies help, version,
missing URL, unknown options, missing values and invalid integer values without
opening a GUI window.

WASM is not applicable because this library's capability is a native desktop
host window containing a platform WebView.

Artifacts are written under `bin/{arch}/{platform}/`.

## Platform notes

Windows uses Microsoft Edge WebView2 and requires the Evergreen Runtime.
Distributions place `WebView2Loader.dll` beside `wvw.exe` and
`libwvw.dll`.

Linux requires GTK 3 and WebKitGTK 4.1.

macOS uses AppKit and WKWebView and requires calls that touch AppKit to originate
from the main thread. The standalone CLI owns its normal AppKit application
loop itself.

## License

This project is distributed under the GNU General Public License version 3.
