# AGENTS.md

## Project Context

`wvw.c` is a small C library and CLI for opening one native window containing
the platform WebView.

It is an application-surface wrapper, not a browser, Electron replacement,
desktop framework, remote application platform, or enterprise UI runtime.

`wvw` is a concrete kclib project and should remain structurally consistent
with the kclib blueprint while preserving the API, lifecycle, platform behavior,
and constraints required by its actual WebView purpose.

Read `README.md` before modifying the project.

## Required Mindset

Treat `wvw` as one small, independent kclib with a concrete native-window
purpose.

Prefer:

* reusable behavior in the library;
* a thin CLI over the public API;
* explicit options and context ownership;
* deterministic cleanup;
* context-local lifecycle state;
* direct platform-specific implementation;
* portable and inspectable public contracts;
* project-local code over generic shared infrastructure;
* small, stable fixes over theoretical redesign.

Do not optimize it toward:

* browser parity;
* enterprise application development;
* framework adoption;
* managed services;
* cloud deployment;
* centralized administration;
* ecosystem growth;
* generic extensibility;
* runtime orchestration;
* common kclib infrastructure.

Do not turn a concrete remediation into a purity exercise.

If existing code is understandable, bounded, compatible, and fixes the concrete
bug without introducing another security or lifecycle defect, prefer preserving
it over rewriting it merely to satisfy an abstract implementation preference.

## Kclib Blueprint Consistency

`libr.c` is the structural blueprint for small kclib projects.

`wvw` should remain consistent with that form where the form applies.

Preserve these common kclib properties:

* reusable behavior lives in the library;
* the CLI remains a thin adapter over the public C API;
* the public header remains the compatibility contract;
* each opened context owns independent state;
* options and context ownership remain explicit;
* open copies effective caller configuration into context-owned state;
* stop remains context-local and cooperative;
* close remains the ownership-destruction boundary;
* cleanup remains deterministic;
* built-in defaults are overridden by environment values and then CLI flags;
* diagnostics remain explicit;
* tests validate shipped public behavior and built artifacts where practical;
* no mandatory network service, hosted service, or shared runtime is introduced;
* platform-specific behavior remains project-local;
* one operator should be able to inspect the complete execution path.

Consistency is architectural, not syntactic.

Do not change `wvw` API shapes, option representation, lifecycle names,
platform-specific design, or native event-loop model merely to make them
textually match the blueprint.

In particular:

* do not replace `kc_wvw_options_t` with opaque blueprint-style options merely
  for consistency;
* do not invent `kc_wvw_exec()` or a generic core operation merely because the
  blueprint has one;
* do not add resident stdin framing where the window/event-loop model does not
  require it;
* do not turn the blueprint into a shared dependency or common ABI;
* do not generalize behavior into another repository before there is a concrete
  cross-project requirement.

The blueprint demonstrates project form.

`wvw` owns its own concrete behavior.

## Core Invariants

* One context owns one native window and one embedded WebView.
* Linux uses GTK 3 and WebKitGTK.
* Windows uses Win32 and the WebView2 Evergreen Runtime.
* macOS uses Cocoa and WKWebView through the existing Objective-C backend.
* The initial URL, title, size, background, and window modes remain explicit.
* The native event loop remains platform-owned and blocking.
* NativeBridge is disabled by default.
* Bridge methods are copied from a fixed explicit whitelist.
* Parson (`lib/parson/`) remains the authoritative JSON parser and serializer
  for bridge values.
* Bridge navigation is confined to trusted origins and explicit scheme
  allowances.
* Bridge requests and responses remain serialized JSON values.
* Browser installation and application packaging remain outside the library.
* No hosted control plane, user account, shared kclib runtime, or network
  service is required by `wvw`.

## Browser Boundary

The embedded engine owns HTML, CSS, JavaScript, networking, cookies, storage,
certificates, accessibility, and page rendering.

`wvw` owns only:

* the host window;
* navigation requests;
* selected window modes;
* bridge enforcement.

Do not add:

* tabs;
* address bars;
* bookmarks;
* downloads;
* password storage;
* browser extensions;
* profile synchronization;
* ad blocking;
* developer platforms;
* update services;
* generic browser policy.

Persistent browser state created by WebView2 or WebKit is native engine state,
not a `wvw` database.

Do not silently relocate, upload, synchronize, inspect, or reinterpret that
state.

## Bridge Security Boundary

Treat all page messages as untrusted.

Preserve:

* method-name validation;
* explicit whitelisting;
* the 65,536-byte message bound where implemented;
* callback return ownership;
* valid serialized JSON requirements;
* exact origin trust;
* explicit local-scheme allowances.

The built-in window methods:

* `minimize`
* `maximize`
* `restore`
* `close`
* `setTitle`
* `setSize`
* `getState`

are available whenever the bridge is active, outside the application method
whitelist, directly under `window.NativeBridge`.

Changes must account for their authority explicitly.

Custom application methods must never override actual built-in NativeBridge
surfaces.

Reserve the top-level names that the bootstrap creates directly:

* `minimize`
* `maximize`
* `restore`
* `close`
* `setTitle`
* `setSize`
* `getState`

Do not reserve hypothetical names that are not actually implemented.

## Bridge Close Semantics

Bridge dispatch must not destroy the active `kc_wvw_t`.

`close` requests native shutdown through the context-local stop/close
lifecycle.

It must not call the ownership-destroying cleanup path while the bridge callback
stack still uses the context.

Preserve this distinction:

* stop requests termination;
* close releases ownership and resources.

No bridge handler, navigation callback, COM callback, Cocoa
delegate, or WebKit callback may continue using a context after it has been
freed.

## Trusted Navigation

Trusted navigation includes only:

* the initial URL origin;
* origins listed explicitly in `TRUSTED_ORIGINS`;
* file URLs when `allow_file` is enabled;
* data URLs when `allow_data` is enabled;
* localhost HTTP/HTTPS URLs when `allow_localhost` is enabled.

Do not broaden trust through:

* unsafe prefix matching;
* suffix matching;
* wildcard origins;
* redirect-derived trust;
* malformed authority acceptance;
* remote configuration;
* convenience defaults.

For localhost:

* the host must be exactly `localhost`;
* localhost subdomains must not be trusted;
* an optional port must be syntactically valid;
* malformed authorities must not become trusted;
* userinfo must not turn a remote host into localhost.

For trusted HTTP/HTTPS origins:

* path, query, and fragment must not broaden trust;
* explicit ports remain significant unless existing documented behavior states
  otherwise;
* comparison must not rely on suffix or wildcard rules.

Keep URL trust parsing small.

Do not build a complete RFC URL parser unless a concrete failing case requires
it.

A compact local authority parser or scanner is acceptable when it directly
enforces these trust rules and remains easy to inspect.

Do not add a URL parsing dependency.

## Bridge JSON

Parson is the authoritative JSON parser and serializer for bridge values.

Use Parson for:

* bridge request parsing;
* object and array parsing;
* member extraction;
* number/string/value interpretation;
* bridge-generated JSON serialization.

Do not replace Parson with:

* another JSON library;
* ad-hoc object parsing;
* `strstr`-based member extraction;
* hand-written interpretation of JSON object fields.

Callback `result_json` must be valid serialized JSON whenever a result is
supplied.

Malformed callback output must never be silently converted into a JSON string.

Success and failure callback output must follow the same validity rule.

### Supplemental Boundary Checks

The vendored Parson API does not expose every parser-internal detail needed for
all framing checks.

A small local structural or boundary scanner is acceptable when it only
supplements Parson for a narrow purpose such as:

* finding the end of the first serialized value;
* rejecting trailing non-whitespace data;
* enforcing framing around a Parson-accepted value.

Such a helper must not:

* replace Parson as the authoritative parser;
* independently interpret object members;
* independently produce application values;
* become a second general-purpose JSON parser;
* disagree with Parson about the semantic value;
* introduce another dependency.

Do not rewrite a compact, working boundary helper solely to achieve theoretical
"Parson-only" purity.

Do not validate JSON by requiring byte-equivalence with Parson's reserialized
form. Serialization normalization is not equivalent to syntactic validity.

If a stricter property cannot be implemented cleanly with the vendored Parson
API and the existing small helper is safe and understandable, preserve the
small helper rather than expanding the dependency or parser surface.

## Security Implementation Rules

Bridge capabilities are authority boundaries, not categories to prohibit by
default.

Keep dangerous authority explicit, opt-in, and bounded. In particular:

* do not expose arbitrary shell command strings;
* do not execute through a shell merely for convenience when direct process
  execution is sufficient;
* do not permit process execution outside an explicit executable whitelist;
* do not silently broaden an executable whitelist through environment,
  navigation, remote content, or fallback behavior;
* keep executable identity and arguments distinct when process execution is
  implemented;
* keep filesystem access, clipboard access, screen capture, input injection,
  window enumeration, external-window control, and dynamic native invocation
  behind explicit application-controlled capabilities when they are needed.

Process execution is a legitimate bridge capability when the project owner
chooses to expose it and the authority is concrete and inspectable. A command
or executable whitelist may be supplied through public configuration and, when
the effective configuration model supports it, environment variables. An empty
or absent process whitelist must not imply unrestricted execution.

`wvw` may start an explicitly authorized process, including a local application
server, when that is the application's chosen composition. This does not make
`wvw` responsible for inventing a process supervisor, service-discovery layer,
authentication system, application framework, or generic runtime.

If process lifetime, stdin/stdout streaming, restart behavior, or termination
is exposed, define and document those semantics explicitly instead of
implicitly turning one-shot execution into supervision.

Keep `wvw` independent of server language and application framework. Do not
couple process execution to kclib-specific knowledge or to a common library
runtime.

Do not weaken normal platform WebView security unless a change is explicitly
required, narrowly scoped, and documented.

Keep `NativeBridge` built-in methods limited to the current `kc_wvw_t` instance.

Keep application bridge methods restricted to explicitly registered direct
methods.

Treat the addition of any new built-in bridge capability as a
security-sensitive architectural change. This means its authority, enabling
condition, whitelist/configuration semantics, lifecycle, and failure behavior
must be explicit; it does not mean the capability is forbidden.

Do not implement a documented-but-missing bridge capability merely to reconcile
stale documentation.

If documentation describes a bridge surface that is not implemented, first
correct the documentation unless the project owner explicitly chooses to add
that capability.

## Platform Truth

Do not hide material platform differences behind the common API.

`wvw` is a desktop application wrapper. Supported targets are Linux, Windows,
and macOS; iOS and mobile targets are not supported.

Linux, Windows, and macOS implement the primary bridge, navigation restriction,
and window paths in `libwvw.c` behind platform guards.

Windows requires:

* a matching `WebView2Loader.dll` beside the executable;
* an installed WebView2 Runtime.

The library does not download either dependency.

macOS implements bridge, navigation restriction, and window paths in
`src/libwvw.c` using Cocoa and WKWebView behind the platform guard.

The macOS bridge uses:

* `WKScriptMessageHandler`;
* `WKNavigationDelegate`.

macOS does not require localhost bridge allowance by default.

Linux uses `libdl` for dynamic loading where needed.

Windows uses:

* `LoadLibrary`;
* `GetProcAddress`;
* `FreeLibrary`.

Transparent host mode is a best-effort platform feature, not a portable
rendering guarantee.

Platform-specific code should remain direct and explicit rather than being
forced through abstractions that conceal materially different ownership or
runtime behavior.

## macOS Native Ownership

Objective-C bridge helpers and delegates must have explicit lifetime tied to
their owning `kc_wvw_t`.

The context must explicitly own any native helper object required to remain
alive, including as applicable:

* script message handlers;
* navigation delegates;
* window delegates.

Do not rely on accidental retention or leak-based lifetime.

During replacement or close:

* detach delegates before freeing their context;
* remove script message handlers before freeing their context;
* clear helper pointers back to `kc_wvw_t`;
* release retained helper objects deterministically.

At most one active NativeBridge installation may exist per context.

Repeated bridge enablement must either:

* replace the previous bridge installation safely; or
* fail explicitly.

It must not intentionally accumulate handlers, delegates, bootstrap scripts, or
duplicate message delivery.

## macOS Validation Policy

Native macOS runtime testing is not required in the current development
environment.

For macOS, require the strongest available local evidence:

* compile the existing macOS target;
* do not introduce compiler errors;
* treat warnings according to the repository build policy;
* review Objective-C ownership and delegate lifetime statically;
* review Cocoa/WebKit API usage for obvious invalid calls;
* avoid unresolved symbols introduced by changes.

macOS may be reported as:

* compile-checked;
* statically reviewed;
* runtime not verified.

Do not claim native macOS runtime behavior has been confirmed.

Do not block completion solely because a native macOS machine or runtime is not
available.

Do not introduce additional macOS machinery solely to compensate for the lack
of runtime testing.

## Lifecycle and Threading

Open deep-copies effective options into the context.

Run enters the native platform event loop.

Navigate, bridge event delivery, visibility changes, window
controls, stop, and close interact with native UI objects and must obey the
platform UI thread.

Keep close idempotence and asynchronous backend initialization explicit.

On Windows, WebView2 environment and controller creation use COM callbacks and
may hold context references during initialization.

Do not add:

* hidden worker pools;
* cross-thread UI mutation;
* resident agents;
* generic task dispatchers;
* implicit background services.

Stop state is context-local.

A stop request must not destroy another context.

Do not introduce synchronization machinery without a concrete requirement.

## Public API and Ownership

Treat `src/libwvw.h` as a compatibility boundary.

Preserve:

* option ownership;
* callback signatures;
* bridge result ownership;
* event name;
* return codes;
* native loop behavior;
* caller-owned context lifecycle.

The bridge callback allocates `*result_json`; the library consumes and frees it.

Bridge methods are copied.

Callback `userdata` is borrowed.

Posted event JSON is borrowed for the duration of the call.

`kc_wvw_open()` copies effective option state into context-owned storage.

The caller retains ownership of its own option storage and may release it
according to the documented lifecycle.

Do not convert the public API to another kclib's API shape merely for visual or
naming consistency.

## Source Layout

Preserve the existing source set:

* `src/wvw.c` for CLI parsing and bridge initialization;
* `src/libwvw.c` for the Windows, Linux, and macOS backends plus shared
  behavior, using platform guards;
* `src/libwvw.h` for the public API;
* `src/test.c` for all tests, including bridge, lifecycle, platform, and
  integration cases;
* `lib/parson/` for the vendored Parson JSON library and its license.

Do not create additional:

* source files;
* public headers;
* backend files;
* bridge files;
* platform files;
* test files.

Extend only the existing source files unless the project owner explicitly
changes this contract.

Do not modify or fork vendored Parson for a narrow wvw-only validation concern
without explicit authorization.

The CLI calls the public C API directly.

`libwvw` must not depend on a common kclib runtime.

## CLI Boundary

`src/wvw.c` is a thin process adapter.

It may:

* initialize options;
* load environment configuration;
* parse CLI flags;
* report CLI diagnostics;
* open the library context;
* configure explicitly requested bridge behavior;
* enter the library/native loop;
* map library failure to process exit status;
* release caller-owned resources.

Reusable WebView behavior must not exist only in the CLI.

Do not implement a second window, navigation, bridge, or lifecycle model
inside `wvw.c`.

Unknown or malformed CLI input must fail directly.

Do not silently ignore unsupported flags or invalid values.

## Configuration

Configuration must remain explicit and legible.

Normal precedence is:

1. built-in defaults;
2. documented `KC_WVW_*` environment variables;
3. CLI flags.

Do not introduce:

* remote configuration;
* account configuration;
* hidden configuration stores;
* configuration discovery services;
* generic configuration frameworks.

Environment or CLI parsing must not silently broaden bridge authority.

## Change Evaluation

Before changing `wvw`, determine:

* what concrete problem exists;
* whether the problem is present in current code, tests, or documentation;
* whether the behavior belongs specifically to `wvw`;
* whether existing local code already solves most of it;
* whether the public API changes;
* whether ownership changes;
* whether lifecycle authority changes;
* whether NativeBridge authority changes;
* whether navigation trust changes;
* whether platform behavior changes;
* whether a dependency or service would be introduced;
* whether the CLI remains thin;
* whether the result remains inspectable by one person.

Prefer the smallest correct change.

Do not reopen already-fixed code merely because another implementation could be
more theoretically elegant.

Do not rewrite working validation or parsing solely to satisfy a stronger
internal purity rule unless the existing code has a concrete correctness,
security, ownership, or compatibility defect.

If remediation work begins producing large amounts of churn:

1. stop;
2. identify the original concrete defect;
3. determine whether that defect is already fixed;
4. discard speculative improvements not required by the defect;
5. return to the smallest stable implementation.

Reject additions whose only purpose is hypothetical future reuse.

Do not add abstraction merely to remove small amounts of visible repeated code.

Do not split or restructure code solely because a file is large or
unconventional.

## Implementation Preferences

Prefer:

* direct C and Objective-C;
* small functions with concrete responsibilities;
* explicit allocations and cleanup;
* caller-owned contexts;
* library-first behavior;
* thin CLI adaptation;
* strict input validation;
* exact trust checks;
* Parson-based bridge value parsing;
* compact supplemental validation when needed;
* explicit native ownership;
* direct platform branches;
* public-contract tests;
* project-local implementation.

Avoid:

* framework-style architecture;
* generic registries;
* hidden ownership;
* implicit lifecycle;
* recursive dispatch;
* macro metaprogramming for hypothetical variants;
* generated configuration layers;
* unnecessary callbacks;
* platform abstractions that hide material differences;
* dependencies added only for convenience;
* full parsers for problems requiring only a small bounded check.

## Bridge Transport

Bridge transport must preserve serialized JSON values without corruption or
obvious script-injection paths.

Review relevant behavior for:

* quotes;
* apostrophes;
* backslashes;
* carriage returns;
* line feeds;
* Unicode;
* U+2028;
* U+2029;
* nested values;
* message-size limits.

Use the existing platform transport model unless there is a concrete defect.

Do not redesign the public NativeBridge protocol solely for transport purity.

Do not require exhaustive test coverage for behavior that cannot be reached
without redesigning the implementation or exposing internal helpers.

Use the strongest practical tests available through the current architecture.

## Testing

All tests remain in `src/test.c`.

Behavioral changes should add or update tests when the behavior is practically
reachable through the existing public or integration test architecture.

Prioritize tests for concrete regressions:

* option copying and cleanup;
* open and close;
* stop behavior;
* event-loop exit where runnable;
* bridge enablement;
* method validation;
* reserved NativeBridge names;
* localhost trust;
* obvious origin bypasses;
* malformed bridge requests where reachable;
* oversized messages where reachable;
* callback ownership;
* valid/invalid callback output;
* built-in authority;
* visibility/window state;
* cleanup.

Do not:

* expose internal static functions solely for tests;
* create test-only public API;
* redesign production code solely to make every branch directly unit-testable;
* create new test files.

Some behaviors require a live WebView runtime.

When such behavior cannot be exercised in the current environment:

* document the limitation;
* use static review where appropriate;
* do not mark it runtime-verified;
* do not block unrelated completion unless the untested behavior is the concrete
  change being delivered on an available target.

Headless argument tests do not establish WebView runtime correctness.

Cross-compilation proves compilation only.

## Platform Validation

### Linux

Where locally available:

* build natively;
* run the existing tests;
* perform operator-visible WebView checks for changed runtime behavior when
  practical.

### Windows

Where supported:

* cross-build using the existing repository target;
* run Wine tests where supported.

Do not claim native WebView2 runtime verification from cross-compilation or Wine
when WebView2 cannot actually be exercised.

### macOS

Native runtime testing is not required.

Require:

* existing target compilation;
* static review of changed Objective-C/WebKit code.

Report runtime status truthfully.

## Build and Validation

Use the repository build system.

Before running commands:

* inspect the current build configuration;
* inspect the documented targets;
* preserve existing build artifacts.

Never run:

`make clean`

Do not delete:

* build directories;
* `.build`;
* caches;
* generated files;
* compiled artifacts;

without explicit authorization.

For documentation-only changes, use the existing repository documentation check
when available.

For behavior changes, use the existing build and test targets.

Typical validation where supported:

`make`

`make test`

Windows where supported:

`make x86_64/windows`

`make test wine`

macOS:

use the existing macOS compilation target.

Do not invent a new build path.

Treat compiler warnings according to the repository's existing policy.

Record which targets actually ran.

Do not report unavailable or skipped runtime targets as passing.

## Documentation

Keep documentation operational and truthful.

Use:

* `README.md` for effective CLI, API, runtime requirements, behavior, build,
  test, status, and license information;
* `AGENTS.md` for implementation constraints, architecture boundaries, security
  rules, and agent behavior.

Documentation must describe actual implemented behavior.

Do not document intended behavior as though it already exists.

When implementation changes:

* reconcile relevant README text;
* keep platform differences explicit;
* keep best-effort behavior labeled as such;
* verify bridge examples against actual implementation;
* remove stale bridge surfaces rather than automatically implementing them.

Do not turn documentation into a roadmap.

## Forbidden Default Recommendations

Do not add or recommend by default:

* Electron;
* bundled Chromium;
* browser frameworks;
* web servers;
* hosted content platforms;
* automatic runtime downloads;
* remote debugging services;
* extension systems;
* OAuth;
* SSO;
* accounts;
* tenant models;
* telemetry;
* analytics;
* distributed tracing;
* dashboards;
* fleet management;
* cloud APIs;
* plugin ecosystems;
* application marketplaces;
* service discovery;
* common kclib runtimes;
* generic command registries;
* shared dependency-injection infrastructure.

Do not justify changes through:

* enterprise readiness;
* browser parity;
* hypothetical scale;
* managed operation;
* platform growth;
* ecosystem consistency alone.

## Completion Standard

Before operator-visible testing, describe behavior only as appropriate:

* implemented;
* locally checked;
* locally verified;
* compile-checked;
* statically reviewed;
* ready for operator testing.

Do not claim runtime verification for a platform that was not actually run.

A remediation does not need to eliminate every theoretical uncertainty.

It is sufficient when:

* the concrete defect is fixed;
* no obvious new defect was introduced;
* public compatibility is preserved;
* ownership and lifecycle remain coherent;
* bridge authority is not broadened unintentionally, and intentional authority changes are explicit and bounded;
* malformed input is handled reasonably within existing dependency capability;
* relevant practical tests pass;
* affected build targets compile;
* documentation matches actual behavior;
* unavailable runtime coverage is stated truthfully.

Do not keep expanding scope after these conditions are met unless a concrete
remaining bug is identified.

For macOS, successful target compilation plus static review is acceptable
completion evidence in the current environment.

## Design Summary

One context owns:

* one platform window;
* one embedded WebView;
* copied effective options;
* context-local lifecycle state;
* bridge state;
* platform-native helper objects required for that context.

The CLI initializes configuration, applies environment and CLI overrides, opens
the context through the public C API, optionally configures bridge behavior,
enters the native event loop, and closes the context.

Linux uses GTK 3 and WebKitGTK.

Windows uses Win32 and WebView2 with asynchronous COM initialization.

macOS uses Cocoa and WKWebView through Objective-C.

Native UI objects remain confined to the owning UI thread and platform event
loop.

NativeBridge is opt-in.

It injects a Promise-based JavaScript surface, validates bounded serialized JSON
messages, dispatches built-in controls for the current window, and restricts
custom methods to an explicit copied whitelist.

Bridge-enabled navigation accepts only:

* the initial origin;
* explicitly trusted origins;
* explicitly enabled local schemes/classes.

Parson remains authoritative for JSON value semantics.

Small bounded helpers may supplement Parson where the vendored API does not
expose enough information for framing or boundary checks, provided those helpers
do not replace it as the JSON parser.

The embedded browser engine owns:

* rendering;
* networking;
* certificates;
* cookies;
* storage;
* accessibility.

`wvw` owns only:

* the host window;
* navigation requests;
* bridge enforcement;
* selected window modes.

It provides no:

* application server;
* browser UI;
* account system;
* remote control plane;
* shared kclib runtime;
* update mechanism.

The project follows the kclib blueprint structurally:

* library-first behavior;
* thin CLI adaptation;
* explicit ownership;
* deterministic cleanup;
* direct local composition;
* portable public contracts;
* no mandatory external service.

The goal remains one small, explicit native WebView window.
