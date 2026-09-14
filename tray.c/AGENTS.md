# AGENTS.md

## Project Context

`tray.c` is a small, independent kclib that provides native system tray /
notification-area / status-bar integration for Windows, Linux, and macOS.

It consists of:

- `libtray`, a public C library that opens a tray icon, holds a lightweight
  menu, and runs the platform event loop;
- `tray`, a thin CLI that builds one tray icon/menu from arguments and maps
  menu activation to the direct execution of one explicitly configured local
  program.

`tray.c` is completely independent of `wvw.c`, WebViews, NativeBridge, browser
content, and every other kclib API or shared runtime. It owns its native
resources and does not attach to another application's window.

Read `README.md` before modifying the project.

## Required Mindset

Treat `tray.c` as one small, independent kclib with a concrete native-tray
purpose.

Prefer:

- reusable behavior in `libtray`;
- a thin CLI over the public API;
- explicit options and context ownership;
- deterministic cleanup;
- context-local lifecycle state;
- direct platform-specific implementation;
- portable and inspectable public contracts;
- project-local code;
- small, stable fixes over theoretical redesign.

Do not optimize it toward:

- desktop frameworks;
- application launchers;
- notification systems;
- process supervision;
- remote control planes;
- ecosystem growth;
- generic extensibility;
- managed services.

## Capability Boundary

`libtray` owns tray resources and the event loop.

It does not own:

- browsing, JavaScript, or WebView behavior;
- window creation or window content;
- process execution. The CLI owns process execution;
- notification popups, messaging, or applets;
- ledger, IPC, socket, or command-channel behavior.

Do not grow `libtray` into a generic process, notification, or IPC layer.

## Tray Options

Support only real tray properties required by the implementation:

- `"icon"` - icon path or, on Linux, icon name;
- `"tooltip"` - hover text.

Use established configuration precedence:

1. built-in defaults;
2. documented `KC_TRAY_*` environment variables;
3. CLI flags.

Use `KC_TRAY_ICON` and `KC_TRAY_TOOLTIP` environment naming.

Options are opaque caller-owned storage returned by
`kc_tray_options_default()` and released by `kc_tray_options_free()`.
`kc_tray_open()` copies effective option values into context-owned storage.

## Menu Model

Menu configuration is explicit and caller-supplied.

Item rules:

- a `kc_tray_item_t` with both label and action NULL is a separator;
- a label requires its paired action and vice versa;
- empty strings are invalid;
- action names are opaque caller-defined values.

Do not special-case action values such as `show`, `hide`, `quit`, `open`, or
`reload`. The standalone CLI's `kc:tray:quit` reserved action is tray-process
lifecycle behavior only and must remain clearly distinct from caller-defined
program actions.

`kc_tray_set_menu()` copies every retained string.

Menu replacement is atomic from the caller's perspective:

1. construct the full replacement first;
2. on allocation failure free the incomplete replacement, leave the active
   menu unchanged, and return the error status;
3. only on success replace the active menu and free its previous contents.

Menu clearing uses `items == NULL` with count `0` or a zero count.

## Execution Authority

Menu activation runs only an explicitly configured local executable.

The CLI separates executable identity from arguments:

- `--exec <path>` configures the executable path;
- `--arg <value>` configures one exact argument; multiple `--arg` values are
  allowed and ordered.

Execution: 

- passes the configured argv exactly;
- preserves executable/argument separation;
- preserves spaces within one argument;
- treats shell metacharacters literally;
- never routes execution through a shell;
- never reinterprets an action string as shell syntax;
- requires no executable whitelist beyond the explicit configuration;
- is one-shot: activation spawns the configured executable with its configured
  arguments and the tray remains resident.

Do not add supervision, restart, child registries, command channels, IPC,
stdout/stderr protocols, or service discovery.

## Event-Loop Lifecycle

Keep stop and close semantics separate:

- `kc_tray_stop()` requests event-loop termination for one context;
- `kc_tray_close()` is the ownership-destruction boundary.

Stop state is context-local. A stop on one context must not destroy another
context.

Run enters the platform event loop. Run honors a stop request that happened
before run, and must return promptly instead of blocking on the loop.

Contexts are caller-owned. Close releases all native and owned resources.

## Platform Truth

Use the native event-loop and tray mechanisms proven by the repository family:

- Windows: own all required window/message/tray resources inside the tray
  context (a message-only window plus `Shell_NotifyIconW`); do not depend on
  another application's HWND.
- Linux: use GTK 3 and `GtkStatusIcon`; document the desktop/tray runtime
  requirement (a running system tray/notification area host).
- macOS: use the Cocoa status-item approach and tie Objective-C helper and
  native-object lifetime explicitly to the tray context.

Do not hide material platform differences behind the common API.

Windows implementation lives in `src/libtray.c`.

Linux implementation lives in `src/libtray.c`.

macOS implementation lives in `src/libtray.c` and is compiled as
Objective-C on macOS so the Cocoa status-item backend stays native alongside
the Windows and Linux backends in the same file.

Deprecated-but-available platform APIs used where unavoidable (for example
`GtkStatusIcon`) are wrapped with the minimal suppression needed to build
cleanly under the repository's warning policy. Keep suppression narrow and
local.

## Library/CLI Boundary

`libtray` owns all reusable tray behavior:

- option handling;
- open, close, stop, run;
- menu copy, replacement, clearing;
- error inspection.

`src/tray.c` is a thin process adapter:

- initializes options;
- loads environment configuration;
- parses CLI flags;
- reports CLI diagnostics;
- opens the library context;
- configures icon, tooltip, and menu;
- maps menu activation to one explicit local execution;
- enters the library/native loop;
- maps library failure to process exit status;
- releases caller-owned resources.

Reusable tray behavior must not exist only in the CLI, and process execution
must not be duplicated inside the library.

Unknown or malformed CLI input must fail directly on stderr. Do not silently
ignore unsupported flags or invalid values.

## Independence

Do not couple `tray.c` to other kclib repositories or APIs:

- no dependency on `wvw.c`, its tray code, or its headers;
- no shared kclib runtime;
- no cross-project ABI;
- no changes to wvw.c in this project.

Tray state belongs to the current `kc_tray_t`.

## Native Ownership

Native objects are context-owned:

- icon handles and tooltips;
- notification-area entries;
- helper Objective-C objects retained for their owning context;
- window/message resources on Windows.

Release all owned menu, option, context, and native resources exactly once on
clearing, replacement, and close as applicable.

Do not rely on accidental retention or leak-based lifetime.

The native UI thread owns all native objects.

## Testing

All tests remain in `src/test.c`.

Cover shipped behavior through the existing public and integration test
architecture:

- option defaults, environment loading, and cleanup;
- invalid open inputs and context lifecycle;
- loop/stop behavior;
- menu copying, separators, stable action identity, replacement, clearing,
  invalid definitions, and cleanup;
- ownership-sensitive replacement failure behavior where reachable through
  normal test mechanisms;
- error inspection;
- CLI malformed input;
- process argument preservation, including executable/argument separation,
  spaces within one argument, and shell metacharacters remaining literal;
- direct process execution without shell interpretation;
- expected build artifacts.

Do not:

- expose internal static functions solely for tests;
- create test-only public API;
- redesign production code solely to make every branch directly testable;
- create new test files.

Native tray behavior requires a desktop session. Distinguish runtime-tested
platforms from compile-only or cross-compiled platforms truthfully.

Cross-compilation proves compilation only.

## Documentation

Keep documentation operational and truthful:

- `README.md` documents the effective CLI, API, menu model, process
  execution, configuration, build, test, requirements, status, limitations,
  and license;
- `AGENTS.md` documents implementation constraints and agent behavior.

Do not document intended behavior as though it exists.

## Completion Standard

A change is complete when:

- the tray icon/menu and event loop implement the frozen public contract;
- native and menu state are owned deterministically and released once;
- stop/loop/close lifecycle is correct on the supported backends;
- the CLI executes the explicitly configured local program with exact
  structured arguments and no shell;
- the CLI stays thin and rejects malformed input;
- no dependency on another kclib exists;
- supported targets build with the strongest validation practical in the
  current environment;
- runnable tests pass;
- documentation matches actual behavior;
- runtime platforms are distinguished from compile-only platforms truthfully.

Report what changed, how it was verified, and what remains unverified.
