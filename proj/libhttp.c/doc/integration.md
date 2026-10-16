# HTTP integration guide

`libhttp.c` separates HTTP protocol handling from network transport. This page
describes how parsers and builders fit into an application without duplicating
the public C interface.

## Parser lifecycle

One parser belongs to one HTTP byte stream.

Feed received chunks to `kc_http_parser_write()` in stream order. The parser
buffers incomplete input internally. One write can complete zero, one, or
multiple messages, and the corresponding callback runs once for each complete
message.

Call `kc_http_parser_close()` when that stream ends.

Header names are normalized to lowercase. HTTP/1 chunked input is dechunked and
trailers are reported separately.

## Request and response builders

Request and response builders produce allocated HTTP wire bytes from structured
build values. Release returned wire buffers with `kc_http_free()`.

The current defaults are:

- request method: `GET`;
- request target: `/`;
- request or response version: `1.1`;
- response status: `200` when no status is supplied;
- response reason: derived from status when no reason is supplied;
- chunk size: `8192` when chunking is enabled without an explicit size.

An explicitly supplied response status must be between 100 and 599. An explicit
chunk size of zero is invalid.

## Transport composition

`libhttp.c` does not listen on ports or own sockets.

For an inbound TCP server, the application's transport layer owns the
connection. Keep one HTTP parser per accepted connection and pass that
connection's bytes to its parser.

An outbound transport can return bytes that are then passed to an HTTP parser
when the transfer carries HTTP.

This separation keeps HTTP framing independent from connection management.

## Protocol support

The current implementation parses HTTP/0.9, HTTP/1.0, and HTTP/1.1. HTTP/1.0
and HTTP/1.1 request and response building is supported, including
Content-Length, chunked transfer encoding, and trailers where applicable.

The library also retains its existing HTTP/2 and HTTP/3 HEADERS/DATA frame
capabilities. Consult the public header before depending on a specific
frame-level operation.

## Ownership

Parsed request and response values are callback-time views owned by the parser.
Builder output is caller-owned and must be released with `kc_http_free()`.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libhttp.h) for the complete C interface.
