# http.c Design

## Purpose

`http.c` separates HTTP protocol framing from byte transport. It parses wire
messages into explicit metadata and logical body bytes, and performs the inverse
build operation.

## Architecture

The CLI chooses parse, request build, or response build and configures one
context. `libhttp.c` contains shared state and all version-specific parsing,
header compression, frame handling, and building.

No source file owns sockets. stdin and configured output streams are the
transport boundary.

## Normalized Format

Metadata uses stable `key=value` lines, followed by one empty line and exactly
`body.length` raw bytes. Requests expose method, target, path, and query;
responses expose status and reason. Repeated headers retain order. Trailers are
distinct from headers.

Parse-all separates complete normalized messages with a `\n\n` line; consumers use
body length rather than delimiter scanning inside arbitrary body bytes.

## HTTP/1

HTTP/0.9 simple requests and HTTP/1.0/1.1 start lines and headers are parsed
directly. Body framing follows protocol rules for length, chunked transfer, and
message type. Chunk syntax is removed from logical output; trailers are retained.

Builders generate start lines, validated fields, CRLF, Content-Length or chunks,
trailers, and terminal chunk framing.

## HTTP/2 and HTTP/3

Supported binary modes parse and build HEADERS and DATA framing with their
corresponding header compression representations. These are message/frame
operations only, not complete connection implementations.

TCP/TLS negotiation, HTTP/2 stream scheduling, QUIC, HTTP/3 transport, flow
control, and connection lifecycle remain outside the project.

## Context and Ownership

Options own method, target, and version strings. Opening deep-copies effective
configuration. Context setters replace owned values; added fields are copied.
The output stream is borrowed.

Execution reads wire or body bytes from the configured input buffer and writes
the configured result. The CLI owns stdin and stdout: it reads stdin into a
buffer, passes it with `kc_http_set_input()`, and emits the captured output.
Close releases only context-owned storage.

## Composition

`netl` or another listener supplies incoming bytes; `nets` or another sender
carries built bytes. Application commands consume normalized metadata and body.

This composition avoids embedding networking and application policy into the
protocol implementation.

## Resource and Failure Model

Parsing is bounded by protocol field and buffer limits with checked lengths.
Malformed, incomplete, ambiguous, or unsupported framing fails explicitly.
Builders reject invalid configuration or field capacity exhaustion.

There is no persistent state, connection pool, cache, thread pool, or network
dependency.

## Non-Goals

The project does not provide clients, servers, sockets, TLS, QUIC, routers,
middleware, proxies, caching, cookies, sessions, authentication, authorization,
WebSockets, application decoding, telemetry, or hosted infrastructure.

## Change Criteria

Changes must identify exact wire bytes and expected normalized or built output,
define partial and malformed behavior, preserve framing security and limits,
maintain parse-all boundaries, and avoid transport or application scope.

## Core Invariants

The project is defined by explicit HTTP wire ownership, normalized metadata plus
raw logical bodies, strict bounded parsing, deterministic building, supported
version dispatch in one library source, stream-tool composition, and no network
transport implementation.

## CLI Adapter

The CLI is a thin adapter over the public C API. It owns argument parsing,
`stdin`, `stdout`, `stderr`, and exit status. For each command it opens a
context, configures it with the public setters, passes `stdin` bytes with
`kc_http_set_input()`, runs `kc_http_exec()`, captures the result with
`kc_http_get_output()`, writes it to stdout, and closes the context.

There is no runner and no JSON layer. The public API is pure C; `libhttp.h`
exports only `kc_http_*` functions. Parsing, building, and all version handling
live in `libhttp.c` and are exercised directly through the public contract.
