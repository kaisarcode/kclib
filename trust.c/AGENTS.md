# AGENTS.md

## Project Context

`trust.c` is a small C11 library and CLI for message cryptography with TOFU
peer identity.

It provides:

- a reusable cryptographic library;
- a public header as the contract;
- a thin CLI over the public API;
- ephemeral-static X25519 DH key agreement;
- Noise X with X25519, ChaChaPoly, and BLAKE2b;
- in-memory trust-on-first-use peer identity management;
- a fixed binary payload representation;
- static and shared library artifacts;
- portable public-contract tests;
- native and cross-platform build targets.

Read `README.md` for the effective interface before modifying the project.

## Required Mindset

Treat this as a focused cryptographic message-transformation component.

Do not optimize it toward:

- enterprise application development;
- framework adoption;
- managed services;
- cloud deployment;
- hyperscale operation;
- organization-wide standardization;
- generic extensibility;
- centralized administration;
- runtime orchestration;
- ecosystem growth.

Do not assume that the project becomes more useful by accumulating common
features. Every added facility increases the code inherited by future
projects that fork this repository.

Prefer deletion and direct adaptation over expanding the scope.

## Core Invariants

Preserve these properties unless the project owner explicitly instructs
otherwise:

- the reusable library owns core cryptographic behavior;
- the CLI only adapts arguments, stdin, stdout, stderr, and process exit status;
- the public header is the library contract;
- each opened context is independently caller-owned;
- options and context allocations have explicit cleanup;
- built-in defaults are overridden by environment values and then CLI flags;
- `trust pk` writes exactly the 32 raw bytes of the local public key to stdout;
- `trust seal` reads application message bytes from stdin and writes an opaque
    encrypted payload to stdout;
- `trust open` reads an opaque encrypted payload from stdin and writes exactly
    one JSON result to stdout for every normal success or failure;
- successful authenticated `trust open` results have `ok` set to `true` and a
    status of `ok`, `peer_new`, or `peer_changed`;
- normal `trust open` failures have `ok` set to `false` and a stable error of
    `invalid_input`, `message_too_large`, `identity_error`, `state_error`, or
    `authentication_failed`;
- `trust open` stdout is the authoritative machine-readable result channel;
    stderr is reserved for exceptional failures that prevent producing or
    writing the JSON response;
- commands other than `open` may write human diagnostics to stderr as
    appropriate;
- the CLI, static library, and shared library remain normal build artifacts;
- tests exercise the public library and CLI contracts;
- the implementation remains portable C11;
- network access and external services are not required;
- the implementation remains small enough for one person to inspect;
- trust state changes only through explicit caller operations;
- incoming processing never modifies trust state;
- the payload representation uses one fixed protocol profile and suite.

## Terminology

Use `identity` for the persistent local keypair and `public key` or `pk` for
its shareable 32-byte public half. Use `peer_id` only for an
application-defined trust identifier. `id` is not a synonym for a public key.

`peer_id` and `peer_pk` are distinct. The former selects a TOFU binding; the
latter is a cryptographic public key. Do not conflate or interchange them.

## Cryptographic Contract

The protocol is fixed:

- `Noise_X_25519_ChaChaPoly_BLAKE2b`;
- HMAC-BLAKE2b Noise HKDF;
- Noise CipherState, SymmetricState, and X token processing;
- X25519 DH with all-zero output rejection;
- ChaCha20-Poly1305 with the Noise 64-bit nonce encoding;
- a 96-byte Noise handshake with an empty handshake payload;
- an authenticated 8-byte logical-message length record after `Split()`;
- initiator-to-responder transport data records with at most 65,519 plaintext
    bytes and one 16-byte tag per record;
- a 120-byte payload representation base before transport data records;
- a 64 MiB maximum application message.

Do not add algorithm negotiation, cipher selection, or protocol-version
frameworks. The protocol is intentionally singular.

Each seal operation generates a fresh ephemeral keypair. The ephemeral
secret key and Noise states are wiped after use. The sender's persistent
public key is encrypted by the `s` token after `es` establishes a cipher key.
The application plaintext is encrypted only after `Split()` with the first
initiator-to-responder transport CipherState. The same CipherState is reused
across records so its nonce increments once per record and is never
transmitted.

## Trust Model

Library trust state is in-memory only, bounded to 256 entries per context.

Outgoing operations never modify trust state. Incoming operations report
trust status but never modify it. Trust changes happen only through
explicit `kc_trust_trust()` and `kc_trust_forget()` calls.

The caller owns all trust decisions. The library does not decide who
deserves trust.

The CLI owns its existing optional filesystem persistence under the configured
state directory and explicitly loads a binding into each library context. Do
not add persistence to `libtrust`, trust file watching, automatic trust
acceptance, interactive trust prompts, or external trust resolution.

## Source Layout

Preserve the existing `src/` structure:

- `src/libtrust.c` contains all reusable implementation;
- `src/libtrust.h` contains the public contract;
- `src/trust.c` contains the CLI;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Add new CLI
behavior to `trust.c`, reusable behavior to `libtrust.c`, public declarations
to `libtrust.h`, and all new test cases to `test.c`.

## Configuration

Configuration must remain explicit and legible.

State-directory precedence is:

1. Explicit `state_path` configuration.
2. Documented `TRUST_STATE_DIR` environment variable.
3. Platform default following the XDG Base Directory convention:
    `$XDG_DATA_HOME/trust`, falling back to `$HOME/.local/share/trust` on
    POSIX; `%LOCALAPPDATA%\trust` with `%USERPROFILE%\.trust` as fallback
    on Windows.

`TRUST_KEY` and `key_path` independently override only the identity file. They
must not change the CLI trust directory at `<state>/trust/`.

Malformed CLI input outside `open` must fail directly and may report a human
diagnostic on stderr. Malformed `open` input is a normal failure and must write
exactly one JSON error to stdout.
Unknown options must not be silently ignored.

## Forbidden Default Recommendations

Do not recommend or implement these without explicit instruction:

- cloud or SaaS integration;
- hosted APIs;
- remote configuration;
- telemetry or analytics;
- structured logging stacks;
- metrics exporters;
- tracing systems;
- account or organization models;
- authentication or authorization frameworks beyond TOFU;
- plugin systems;
- generic backends;
- dynamic module loading;
- dependency injection;
- event buses;
- message brokers;
- databases;
- service discovery;
- API gateways;
- daemon management;
- container orchestration;
- automatic updates;
- a generic control socket.

Do not justify changes through enterprise readiness, ecosystem adoption,
standardization, market expectations, or hypothetical future scale.

## Change Evaluation

Before changing the project, determine:

- which concrete problem exists;
- whether the problem appears in the current code or tests;
- whether the change belongs in a derived project instead;
- whether existing code can be removed or simplified;
- what every future derived project would inherit;
- whether public API or CLI compatibility changes;
- whether ownership remains explicit;
- whether trust semantics change;
- whether the payload representation changes;
- whether hidden state or background activity is introduced;
- whether a new dependency or network requirement is introduced;
- whether the result remains inspectable by one person.

Reject additions whose only purpose to support possible future needs.

Do not add extension points without an existing caller.

Payload representation changes are breaking changes. They require concrete
justification, operator approval, version increment, and updated tests.

## Testing

Every behavioral change must update or add tests.

Tests cover the shipped public API contract, including:

- option defaults, environment loading, and cleanup;
- state path resolution across platforms;
- identity generation and loading;
- public key extraction;
- seal and open round-trips;
- wrong-key rejection;
- trust store operations (known, new, changed, forget, implicit);
- independent contexts;
- result ownership and cleanup;
- malformed, truncated, and oversized input handling;
- the fixed noise conformance vector;
- X25519 low-order and DM rejection;
- the 64 MiB message limit.

Every public API function has one dedicated case named
`case_kc_trust_<function>`; aggregate cases cover only cross-cutting library
concerns (TOFU state machine, noise conformance, crypto rejection, multiple
independent contexts, message limit).

Tests link against the generated shared library when validating the
distributed public API. Do not weaken tests to accommodate an implementation
change.

## Build and Validation

Use the repository build system.

Typical validation is:

```bash
kcs .
make
make test
```

When Windows behavior is affected:

```bash
make x86_64/windows
make test wine
```

Treat compiler warnings as failures.

Do not run `make clean` or delete build artifacts without authorization.

Cross-compilation verifies compilation only. Do not claim runtime portability
for a target that was not run.

## Documentation

Keep documentation operational and truthful.

Use:

- `README.md` for the effective CLI, API, build, test, requirements, status,
    and license contract;
- `AGENTS.md` for implementation constraints and agent behavior.

This project has no `DESIGN.md`; architecture, crypto protocol, ownership,
composition, invariants, limitations, and non-goals are documented here and
in `README.md`.

Do not turn documentation into a product roadmap.

Do not describe the absence of enterprise facilities as incompleteness.

## Completion Standard

A change is complete when:

- the concrete problem is solved;
- the library and CLI boundary remains correct;
- ownership and cleanup remain explicit;
- trust semantics remain unchanged unless intentionally modified;
- the payload representation is stable or intentionally versioned;
- public and CLI contracts are tested;
- relevant builds and tests pass;
- documentation matches actual behavior;
- no unrelated enterprise machinery was introduced;
- the project remains small, direct, and inspectable by one person.

The goal is not to make `trust.c` a general-purpose security framework.

The goal is to preserve a focused cryptographic message-transformation
component with explicit trust semantics and a fixed protocol.
