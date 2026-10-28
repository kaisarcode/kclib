# Native bridge and window integration

`libwvw.c` can expose native capabilities to trusted WebView content. This page
describes the bridge boundary, origin controls, executable registration, and
window behavior that should be considered together.

## Bridge model

`NativeBridge` is infrastructure provided to WebView content.

The root names reserved by `wvw` are `Window` and `exec`.

Built-in window control is available through `NativeBridge.Window.*`.

The executable facility is available through `NativeBridge.exec(args)`.

Application-defined methods are exposed as `NativeBridge.<method>` through an
explicit whitelist supplied with `kc_wvw_set_bridge_methods()`.

Bridge callbacks receive the requested method and JSON parameters and return a
JSON result.

Keep the exposed method set small and treat bridge inputs as untrusted
application data.

## Origin controls

Access from `file:`, `data:`, and localhost content is controlled by the
corresponding options on `kc_wvw_options_t`.

Origin restrictions apply to built-in and application-defined bridge methods.

Do not enable origins that the application does not need.

## Initialization scripts

`kc_wvw_add_init_script()` installs JavaScript that runs for WebView documents
according to the native backend's initialization behavior.

Use initialization scripts for stable application bootstrap state rather than
for dynamically evaluating untrusted input.

## Executables

Native executable access is opt-in. An application registers arbitrary
desktop executable paths, not KaisarCode-specific commands, using
`kc_wvw_set_executables()`. The executable is identified to JavaScript by
its basename. Duplicate basenames are rejected, even when paths differ.

```c
const char *paths[] = {"/usr/bin/git", "/usr/bin/printf"};
kc_wvw_exec_options_t execs = {paths, 2};
kc_wvw_set_executables(window, &execs);
```

The paths are chosen by the host program; the WebView cannot select another
filesystem path or look up a program from `PATH`.

```js
try {
    const output = await NativeBridge.exec(["git", "--version"]);
    console.log(output);
} catch (error) {
    console.error(error.code, error.message);
}
```

The call is asynchronous. It runs the exact registered native executable
directly in a worker thread without a shell, preserving each array element
as one process argument. Standard output and standard error are merged into
the resolved string for a zero exit code. A nonzero exit rejects with an
`Error` containing `code: "EXEC_FAILED"`, `exitCode`, and `output`.
Unregistered basenames reject with `EXEC_NOT_EXPOSED`; invalid arrays reject
with `INVALID_ARGUMENT`. An execution that cannot start rejects with
`EXEC_FAILED`. The implementation limits each call to 64 arguments and
48,000 bytes of captured output; oversized output rejects with
`OUTPUT_TOO_LARGE`.

Results from earlier document instances cannot satisfy promises created
by a newly loaded page. Closing the host discards pending UI replies; it
does not turn the child into a daemon or guarantee termination of an already
running process.

No shell is registered implicitly. Applications must expose only binaries
whose capabilities they intentionally trust, even if they live elsewhere on
the desktop system.

## Window state

Visibility and window-list membership are separate properties.

A hidden window remains alive and can be shown again. Unlisting a window removes
it from the platform's normal window list without changing its visibility.

Minimize, maximize, restore, size, position, title, and icon operations modify
the same live native host window.

```js
await NativeBridge.Window.minimize();

await NativeBridge.Window.setTitle("Connected");

const state = await NativeBridge.Window.getState();
```

Boolean startup options such as always-on-top, click-through, and no-focus are
applied to the native host at creation time.

## Platform backends

Windows uses WebView2. Linux uses WebKitGTK. macOS uses WKWebView.

Backend-specific runtime requirements are listed in the project README. The
portable C contract is defined by the public header.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libwvw.h) for exact signatures and options.
