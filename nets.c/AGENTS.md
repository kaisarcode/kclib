# AGENTS.md

## Project Context

`nets.c` is a small C library and CLI for sending one byte buffer to one TCP,
UDP, or optional TLS target. The CLI reads stdin and prints stream responses to
stdout.

It is a one-shot network composition tool, not an incomplete HTTP client,
service SDK, transfer manager, or enterprise networking platform.

Read `README.md` before modifying the project.

## Core Invariants

- Input and output remain raw binary byte streams.
- One invocation addresses one host and one port.
- TCP sends all input on one connection, closes its write side, and reads until
    peer EOF where the platform implementation supports response reading.
- UDP sends the complete input as one datagram and does not wait for a response.
- TLS is optional, TCP-based, and available only when compiled with OpenSSL.
- URL-shaped targets select host, port, and transport defaults only.
- URL paths, queries, fragments, user information, redirects, and HTTP semantics
    do not belong to the sender.
- DNS resolution remains an explicit local system operation.
- The caller retains ownership of the input buffer.
- No persistent sender state, connection pool, background worker, or remote
    service is required.

## Scope Boundary

`nets` owns target parsing in the CLI and one direct resolve, connect, send, and
optional response-read operation in the library.

Do not add HTTP requests, response parsing, headers, cookies, redirects,
authentication flows, sessions, caching, retries, proxy discovery, connection
pooling, service discovery, file transfer policy, multipart handling, QUIC, or
application payload interpretation.

Compose protocol builders and parsers such as `http` through stdin and stdout.
Prefer another small tool over absorbing its protocol or policy.

## Architecture

The CLI reads stdin, selects TCP, UDP, or optional TLS through flags or URL
scheme, and delegates one send operation to the library. The library resolves
the target through the local resolver, tries resolved addresses in order,
creates one socket per attempt, performs the selected transport operation,
returns response bytes through output parameters, and releases all per-call
network state before returning.

The host and input buffer are borrowed for the duration of the call. The
library does not write to stdout; the CLI decides how to present the returned
response bytes.

## Transport Contract

Preserve TCP as a byte stream and UDP as exactly one datagram. Never split UDP
input silently, merge multiple sends, invent acknowledgement behavior, or turn
UDP into a session protocol.

Target syntax accepts a host, `host:port`, bracketed IPv6, or a URL-shaped
`http`, `https`, `tcp`, or `udp` target. Preserve default ports and explicit
CLI transport override behavior. A URL is not fetched; only its authority and
transport defaults are used.

Treat `KC_NETS_TCP`, `KC_NETS_UDP`, and `KC_NETS_TLS` as concrete modes, not an
invitation to build a generic transport plugin system.

## TLS Boundary

OpenSSL is an optional build-time dependency. Plain TCP and UDP must remain
usable without it. Do not add mandatory certificate services, hosted trust
systems, automatic enrollment, or opaque platform networking frameworks.

The current TLS path sets SNI and encrypts the TCP stream but does not configure
certificate or hostname verification. Do not describe it as authenticated.
Security changes must define trust inputs, verification behavior, failure
output, portability, and tests explicitly rather than silently changing the
contract.

## Resource and Stop Model

The CLI currently buffers all stdin in growing memory before sending. TCP and
TLS response reads may wait for peer EOF without a timeout. DNS, connect, and
some read operations are not interrupted by a context stop request. These are
known implementation limits, not claims of bounded or cancellable operation.

Changes in these areas require a concrete use case and explicit limits. Do not
introduce hidden temporary files, background queues, retry loops, resident
agents, or remote coordination as default remedies.

Preserve partial-send handling, exact byte counts, address iteration, explicit
socket cleanup, and visible negative error codes.

Invalid arguments return `KC_NETS_EINVAL`; resolution, socket, connect, send,
TLS, and unavailable-TLS failures return `KC_NETS_ENET`; observed stop state
returns `KC_NETS_ESTOP`. Resolution, connect, and response-read attempts
continue until one succeeds or all fail.

## Public API and Ownership

Treat `src/libnets.h` as a compatibility boundary. Preserve protocol constants,
error codes, context lifecycle, stop state, caller-owned input, stdout response
behavior, and build-version reporting unless explicitly instructed otherwise.

The library does not own or close stdin or stdout. It does not retain the host
or input buffer after `kc_nets_send()` returns.

## Portability

Keep POSIX and Windows socket behavior explicit. Do not hide platform omissions
behind portability claims. In particular, current plain TCP response reading is
POSIX-only and must not be documented as tested Windows behavior.

Do not claim runtime support based only on successful cross-compilation.

## Source Layout

Preserve exactly:

- `src/nets.c` for CLI parsing and stdin buffering;
- `src/libnets.c` for TCP, UDP, TLS, socket, and stop behavior;
- `src/libnets.h` for the public API;
- `src/test.c` for all tests, including network, TLS, stress, platform, and
    integration cases.

Do not create additional source, header, protocol, transport, TLS, platform, or
test files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add HTTP client frameworks, libcurl-style feature sets, browser behavior,
API gateways, service meshes, cloud SDKs, proxy platforms, credential stores,
OAuth, SSO, account systems, tenant models, telemetry, analytics, distributed
tracing, dashboards, fleet management, plugin systems, daemon modes, or generic
transport abstractions.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover target parsing,
IPv4 and IPv6 forms, invalid ports and schemes, exact binary payloads, partial
TCP writes, peer EOF, UDP datagram size and boundaries, address fallback, stop
checks, socket cleanup, stdout failures, TLS availability, certificate behavior,
and relevant POSIX and Windows differences.

Prefer loopback servers and public API behavior. Do not require public network
access. Do not weaken tests to accommodate an implementation change.

## Build and Completion

For documentation-only changes run `kcs .`. For behavior
changes use the repository build and tests without cleaning unless authorized.

A change is complete when target parsing, exact byte transport, stream and
datagram semantics, TLS truthfulness, ownership, failure behavior, tests, and
documentation agree.

The goal is one sharp network sender that composes through standard streams.
