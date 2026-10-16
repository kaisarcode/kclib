# Native bridge and window integration

`libwvw.c` can expose native capabilities to trusted WebView content. This page
describes the bridge boundary, origin controls, executable registration, and
window behavior that should be considered together.

## Bridge model

`NativeBridge` is infrastructure provided to WebView content.

Built-in window methods and `NativeBridge.exec(args)` are available through the
bridge. Application-defined methods are exposed only through an explicit
whitelist supplied with `kc_wvw_set_bridge_methods()`.

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

Native executable access is opt-in.

Register allowed executable paths with `kc_wvw_set_executables()`. WebView
content addresses an executable by its registered basename through
`NativeBridge.exec(args)`.

Do not expose a general shell. Register only executables that are part of the
application's intended capability set.

## Window state

Visibility and window-list membership are separate properties.

A hidden window remains alive and can be shown again. Unlisting a window removes
it from the platform's normal window list without changing its visibility.

Minimize, maximize, restore, size, position, title, and icon operations modify
the same live native host window.

Boolean startup options such as always-on-top, click-through, and no-focus are
applied to the native host at creation time.

## Platform backends

Windows uses WebView2. Linux uses WebKitGTK. macOS uses WKWebView.

Backend-specific runtime requirements are listed in the project README. The
portable C contract is defined by the public header.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libwvw.h) for exact signatures and options.
