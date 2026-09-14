# AGENTS.md

## Project Context

`http.c` is a protocol parser and builder. It converts HTTP wire bytes into
normalized metadata plus raw logical body bytes, and builds wire messages from
metadata and payload bytes.

It handles protocol framing, not transport, serving, routing, TLS, proxies, or
application behavior. It composes with `netl.c` and `nets.c`.

Read `README.md` and `DESIGN.md` before modifying the project.

## Core Invariants

- Input and output remain byte-exact streams.
- Parsed metadata is line-oriented and body bytes remain raw.
- `body.length` is logical body length.
- Header names are normalized lowercase; repeated order is preserved.
- HTTP/1 Content-Length and chunked framing are consumed completely.
- Chunked input is dechunked and trailers remain explicit metadata.
- Builders own CRLF, lengths, chunks, and terminal framing.
- Multi-message mode separates normalized messages predictably.
- HTTP/0.9, 1.0, 1.1, and supported HTTP/2/3 frame behavior remain explicit.
- Parsing rejects malformed, ambiguous, incomplete, or oversized protocol data
    rather than guessing.
- No sockets, DNS, TLS, server loop, or network connection state belongs here.
- Memory and field counts remain bounded.

## Protocol Boundary

Do not add HTTP clients, servers, routing, middleware, cookies, sessions,
authentication, caching, reverse proxying, WebSockets, TLS, DNS, connection
pools, retries, redirects, or application payload interpretation.

HTTP/2 and HTTP/3 support concerns frame and header encoding represented by the
tool. It does not absorb TCP, QUIC, stream scheduling, congestion, TLS, or
connection control.

## Security and Compatibility

Treat wire parsing as hostile input. Preserve strict limits, integer overflow
checks, exact partial-input behavior, header validation, chunk validation,
Content-Length consistency, and rejection of framing ambiguity that could cause
request smuggling.

Protocol changes require malformed, truncated, duplicate, oversized, and
multi-message tests. Never silently reinterpret existing normalized output.

## Public API and Ownership

Treat `src/libhttp.h` as a compatibility boundary. Contexts own copied options,
headers, trailers, and parser state. Input defaults to stdin; output is borrowed
`FILE *` state and is never closed by the library.

Keep setters direct and operations explicit. Do not expose internal frame
structures or create a generic event framework without an existing caller.

## Source Layout

Preserve exactly:

- `src/http.c` for CLI parsing;
- `src/libhttp.c` for all HTTP versions and reusable behavior;
- `src/libhttp.h` for the public contract;
- `src/test.c` for all tests.

Do not create `http1.c`, `http2.c`, `http3.c`, parser, builder, HPACK, QPACK,
transport, runner, or additional test files. Those implementations were
deliberately inlined into `libhttp.c`. The library is pure C; the CLI calls the
public API directly. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add web frameworks, API gateways, reverse proxies, servers, clients,
TLS, QUIC stacks, service discovery, OAuth, accounts, rate limiting, telemetry,
distributed tracing, access logs, cloud integrations, plugins, or control
sockets.

Do not justify changes through enterprise HTTP stacks or framework parity.

## Resource Model

Protocol lines, fields, headers, trailers, frames, and bodies must have explicit
limits or checked growth. Parse-all must consume one complete message at a time
without losing bytes belonging to the next message.

Do not introduce unbounded buffering, hidden temporary files, background
threads, or connection state.

## Testing

All tests remain in `src/test.c`. Cover every supported version, request and
response lines, repeated headers, bodyless messages, Content-Length, chunk
extensions and trailers, malformed chunks, conflicting framing, truncation,
oversized fields, binary bodies, multiple messages, HPACK/QPACK boundaries,
frame lengths, exact normalized output, and exact builder wire bytes.

## Build and Completion

For documentation-only changes run `kcs AGENTS.md DESIGN.md`. For behavior
changes use repository build/tests without cleaning unless authorized.

A change is complete when framing, limits, malformed-input behavior, normalized
output, ownership, tests, and docs agree without adding transport or application
infrastructure.

The goal is one sharp HTTP protocol filter.
