# wvw.c - Native WebView Window Wrapper

`wvw.c` is a C library and CLI for hosting a platform WebView in a controlled
native window with optional application bridge.

Backends:

- Linux uses WebKitGTK.
- Windows uses Microsoft Edge WebView2.
- macOS uses WKWebView.

---

## CLI

### Examples

Open one URL with default size:

```bash
./bin/x86_64/linux/wvw --url https://example.com
```

Open one URL with explicit title and size:

```bash
./bin/x86_64/linux/wvw --url https://example.com --title "Example" --width 1280 --height 720
```

Open one URL with a non-white startup background:

```bash
./bin/x86_64/linux/wvw --url https://example.com --background 111111
```

Start in borderless mode:

```bash
./bin/x86_64/linux/wvw --url https://example.com --borderless
```

Start in fullscreen mode:

```bash
./bin/x86_64/linux/wvw --url https://example.com --fullscreen
```

Open one transparent overlay:

```bash
./bin/x86_64/linux/wvw --url "file:///tmp/overlay.html" --borderless --background 00000000 --always-on-top
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `--url <url>` | Set the initial URL. |
| `--title <title>` | Set the native window title. |
| `--background <hex>` | Set the WebView background color using `RRGGBB` or `AARRGGBB`. |
| `--width <px>` | Set the initial window width. |
| `--height <px>` | Set the initial window height. |
| `--posx <px>` | Set the initial horizontal window position. |
| `--posy <px>` | Set the initial vertical window position. |
| `--fullscreen` | Open the window in fullscreen mode. |
| `--borderless` | Open the window without native decorations. |
| `--always-on-top` | Keep the window above normal windows. |
| `--click-through` | Ignore mouse input on the host window. |
| `--no-focus` | Prevent the window from activating for keyboard focus. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show build version. |

---

## Environment

| Variable | Description |
| :--- | :--- |
| `KC_WVW_URL` | Default URL when `--url` is omitted. |
| `KC_WVW_TITLE` | Default window title. |
| `KC_WVW_BACKGROUND` | Default WebView background color in `RRGGBB` or `AARRGGBB`. |
| `KC_WVW_WIDTH` | Default window width. |
| `KC_WVW_HEIGHT` | Default window height. |
| `KC_WVW_POSX` | Default horizontal window position. |
| `KC_WVW_POSY` | Default vertical window position. |
| `KC_WVW_FULLSCREEN` | Default fullscreen flag (`0` or `1`). |
| `KC_WVW_BORDERLESS` | Default borderless flag (`0` or `1`). |
| `KC_WVW_ALWAYS_ON_TOP` | Default topmost flag (`0` or `1`). |
| `KC_WVW_CLICK_THROUGH` | Default click-through flag (`0` or `1`). |
| `KC_WVW_NO_FOCUS` | Default no-focus flag (`0` or `1`). |

| `KC_WVW_BROWSER_ARGS` | Windows WebView2 browser arguments. |

---

## Public API

```c
#include "libwvw.h"

kc_wvw_options_t opts = kc_wvw_options_default();
opts.url = strdup("https://example.com");

kc_wvw_t *ctx = NULL;
if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
    const char *error = kc_wvw_get_error(ctx);
    fprintf(stderr, "wvw: %s\n", error ? error : "open failed");
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return 1;
}
kc_wvw_loop(ctx);
kc_wvw_close(ctx);
kc_wvw_options_free(&opts);
```

## NativeBridge

`wvw` can inject one native application bridge into trusted local content.

JavaScript request surface (all calls return a Promise):

```js
NativeBridge.MethodName(params)
    .then(function (result) { })
    .catch(function (err) { });
```

JavaScript window control surface (built-in, always available when bridge is active):

```js
NativeBridge.minimize()
NativeBridge.maximize()
NativeBridge.restore()
NativeBridge.close()
NativeBridge.setTitle("New Title")
NativeBridge.setSize(width, height)
NativeBridge.getState()  // returns {width, height, minimized, maximized, fullscreen, visible}
```

JavaScript event surface:

```js
window.addEventListener("nativebridge", function (e) {
    const message = e.detail;
});
```

Bridge rules:

- `window.NativeBridge` is injected by native code.
- Only whitelisted methods exist.
- The bridge is disabled by default.
- Remote navigation is blocked when the bridge is active.
- `localhost` is denied unless the application enables it explicitly.
- The standard CLI registers no custom methods; unregistered methods are unavailable.

`wvw` only hosts the provided URL and does not secure or audit the loaded
application or server. See `AGENTS.md` for the full security boundary.

### Bridge built-in window methods

These methods are handled by the bridge core itself and are always available (no whitelist needed). All return Promises.

| Method | Parameters | Description |
| :--- | :--- | :--- |
| `minimize` | `{}` | Minimizes (iconifies) the native window. |
| `maximize` | `{}` | Maximizes the native window. |
| `restore` | `{}` | Restores from maximized or minimized state. |
| `close` | `{}` | Closes the window and terminates the process. |
| `setTitle` | `{"title":"..."}` | Sets the native window title. Max 4096 chars. |
| `setSize` | `{"width":1024,"height":768}` | Sets the native window size. Max 16384px. |
| `getState` | `{}` | Returns `{width, height, minimized, maximized, fullscreen, visible}`. |

### Bridge API

```c
typedef int (*kc_wvw_bridge_callback_t)(
kc_wvw_t *ctx,
const char *method,
const char *params_json,
char **result_json,
void *userdata
);

typedef struct {
    const char **methods;
    int method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
} kc_wvw_bridge_options_t;

int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts);
int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json);
int kc_wvw_add_init_script(kc_wvw_t *ctx, const char *javascript);
```

Callback contract:

- `method` is one of the registered bridge methods.
- `params_json` is the serialized JSON fragment received from JavaScript.
- Return `KC_WVW_OK` for success.
- Set `*result_json` to one serialized JSON value when needed.
- Return `KC_WVW_ERROR` for failure.
- On failure, `*result_json` may contain one serialized JSON error object.

For both callback success and callback failure, `*result_json` must be exactly
one complete, serialized JSON value. The library parses it with Parson and
checks that the entire input is consumed (leading whitespace allowed; trailing
non-whitespace data rejected). Malformed output is never silently converted
into a JSON string. On success, a `NULL` result means JSON `null`. On failure,
a `NULL` result produces the built-in `OPERATION_FAILED` error.

`kc_wvw_post_bridge_event()` sends one serialized JSON payload to the current page as the `nativebridge` event detail.

`kc_wvw_add_init_script()` installs trusted host-provided JavaScript for the
start of documents in the specified WebView. It is a native host API and is
not exposed through `NativeBridge`.

### Window State

```c
typedef struct {
    int width;
    int height;
    int minimized;
    int maximized;
    int fullscreen;
    int visible;
} kc_wvw_window_state_t;

int kc_wvw_maximize(kc_wvw_t *ctx);
int kc_wvw_restore(kc_wvw_t *ctx);
int kc_wvw_set_title(kc_wvw_t *ctx, const char *title);
int kc_wvw_set_size(kc_wvw_t *ctx, int width, int height);
int kc_wvw_get_state(kc_wvw_t *ctx, kc_wvw_window_state_t *state);
```

Limits: `KC_WVW_TITLE_MAX = 4096`, `KC_WVW_SIZE_MAX = 16384`.

### Example

```c
static int app_bridge(
kc_wvw_t *ctx,
const char *method,
const char *params_json,
char **result_json,
void *userdata
) {
    (void)ctx;
    (void)params_json;
    (void)userdata;

    if (strcmp(method, "GetVersion") == 0) {
        *result_json = strdup("{\"version\":1}");
        return KC_WVW_OK;
    }

    *result_json = strdup("{\"code\":\"UNKNOWN_METHOD\",\"message\":\"Bridge method is not implemented.\"}");
    return KC_WVW_ERROR;
}

int main(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bridge;
    kc_wvw_t *ctx;
    const char *methods[] = { "GetVersion" };

    opts = kc_wvw_options_default();
    opts.url = strdup("file:///tmp/app.html");
    ctx = NULL;
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        const char *error = kc_wvw_get_error(ctx);
        fprintf(stderr, "wvw: %s\n", error ? error : "open failed");
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }

    memset(&bridge, 0, sizeof(bridge));
    bridge.methods = methods;
    bridge.method_count = 1;
    bridge.callback = app_bridge;
    bridge.allow_file = 1;
    kc_wvw_enable_bridge(ctx, &bridge);

    kc_wvw_loop(ctx);
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return 0;
}
```

With this configuration, JavaScript in the WebView can call:

```js
// App-specific method (dispatched to app_bridge callback)
var ver = await NativeBridge.GetVersion();
```

---

## Lifecycle

- `kc_wvw_options_default()` creates default window options.
- `kc_wvw_options_load_env()` applies environment overrides.
- `kc_wvw_open()` creates the native window and embedded WebView.
- `kc_wvw_loop()` starts the native event loop.
- `kc_wvw_navigate()` loads a new URL.
- `kc_wvw_enable_bridge()` injects `window.NativeBridge` into trusted pages.
- `kc_wvw_post_bridge_event()` emits one `nativebridge` event into the page.
- `kc_wvw_add_init_script()` installs trusted host JavaScript at document start.
- `kc_wvw_hide(ctx)` hides the native window.
- `kc_wvw_show(ctx)` shows and brings the native window to front.
- `kc_wvw_minimize(ctx)` minimizes (iconifies) the native window.
- `kc_wvw_maximize(ctx)` maximizes the native window.
- `kc_wvw_restore(ctx)` restores from maximized or minimized state.
- `kc_wvw_set_title(ctx, title)` sets the native window title.
- `kc_wvw_set_size(ctx, width, height)` sets the native window size.
- `kc_wvw_get_state(ctx, &state)` retrieves the current window state.
- `kc_wvw_close()` releases the window, WebView, and associated resources.

Color input accepts `RRGGBB` and `AARRGGBB`. On Windows, alpha must be `00` or `FF` because WebView2 does not support semi-transparent startup colors.

When `background` uses `AARRGGBB` with `AA=00`, `wvw` enters one experimental transparent host mode:

- Linux requests one RGBA compositor visual for the host window.
- Windows enables one layered host window and requests one transparent WebView2 background.
- macOS disables web-view background drawing and sets a transparent, non-opaque, shadowless host window.

This mode is best-effort. It depends on the platform compositor and the embedded WebView backend. Borderless windows and transparent page CSS are still the intended setup for overlay-style applications.

Overlay-oriented flags:

- `always_on_top` / `--always-on-top` keeps the host window above normal windows.
- `click_through` / `--click-through` lets mouse input pass through the host window.
- `no_focus` / `--no-focus` keeps the host window from stealing keyboard focus.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the supported targets.

```bash
make all
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables and run directly.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### Supported targets

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make aarch64/macos
```

The Windows target builds the backend as C and uses the official Microsoft Edge WebView2 Win32 SDK headers stored under `lib/webview2/include/`.

`make x86_64/windows` copies the architecture-matched `WebView2Loader.dll` from `lib/webview2/bin/x86_64/` to `bin/x86_64/windows/` beside `wvw.exe`.

The current Windows cross-build path uses MinGW-w64:

```bash
make x86_64/windows
```

Native MSVC builds are supported by the CMake project when the same WebView2 SDK files are available, but this repository's default workflow is the Make target above.

---

## Windows Runtime

Windows 10 and Windows 11 require the Microsoft Edge WebView2 Evergreen Runtime installed on the target machine.

The library does not install or download the runtime. Runtime installation belongs to application packaging.

If the runtime is missing, `kc_wvw_open()` returns `KC_WVW_ERROR` and the backend prints:

```text
wvw: Microsoft Edge WebView2 Runtime is required
```

Windows packages must include these files together:

- `wvw.exe`
- `libwvw.dll`
- `WebView2Loader.dll`

The loader must be the matching architecture and must remain beside `wvw.exe`. The backend does not search arbitrary directories.

WebView2 stores persistent browser state under:

```text
%LOCALAPPDATA%\KaisarCode\wvw\WebView2
```

Wine can run the Windows backend when the prefix provides a working WebView2 rendering path. `KC_WVW_BROWSER_ARGS` is available for explicit WebView2 diagnostics and runtime experiments.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)
- `pkg-config`

### System Libraries

Linux:
- `libpthread`
- `libm`
- `libdl`
- `gtk+-3.0`
- `webkit2gtk-4.1`

Windows (MSVC or MinGW):
- `ole32`
- `uuid`
- `user32`
- `gdi32`
- `shell32`
- WebView2 SDK (bundled in `lib/webview2/`)

macOS:
- `WebKit` framework
- `Cocoa` framework
- `Foundation` framework

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with the macOS SDK for macOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

### Test Dependencies

No additional test dependencies required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
