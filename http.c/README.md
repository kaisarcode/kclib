# http.c - HTTP Protocol Parser and Builder

`http.c` is an HTTP protocol layer. It incrementally parses HTTP byte streams
into complete structured requests/responses and builds HTTP wire bytes from
structured requests/responses.

It does not listen on ports, own sockets, route requests, or perform outgoing
network transfers. Those responsibilities belong to transport libraries such as
`netl.c` and `nets.c`.

One parser belongs to one HTTP byte stream. A write may complete zero, one, or
multiple HTTP messages.

---

## CLI

### Parse

`http parse` reads stdin incrementally and exits as soon as one complete HTTP
message is available. It does not wait for EOF after the message has completed.

```bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse
```

Default request output contains the application-facing fields and headers:

```text
request.method=GET
request.target=/
request.path=/
request.query=
header.host=localhost
```

If a logical body is present, one blank line is followed by its raw bytes.

`--all` prints the same message plus protocol metadata, logical body length,
and trailers:

```bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse --all
```

Additional fields include:

```text
http.type=request
http.version=1.1
body.length=0
```

### Build request

```bash
printf 'hello' | http build request \
    --method POST \
    --target /api \
    --header 'host: localhost' \
    --header 'content-type: text/plain'
```

### Build response

```bash
printf 'hello' | http build response \
    --status 200 \
    --header 'content-type: text/plain'
```

Request and response builders support:

- `--version <version>`
- repeatable `--header <name: value>`
- `--chunked`
- `--chunk-size <n>`

Requests additionally support `--method` and `--target`. Responses support
`--status`, `--reason`, and repeatable `--trailer <name: value>`.

Common options:

- `-h`, `--help`
- `-v`, `--version`

---

## Public API

The public surface has three semantic capabilities:

```text
parser
request
response
```

### Parser lifecycle

```c
static void on_request(const kc_http_request_t *request, void *userdata) {
    /* request and nested data are borrowed for this callback only */
}

static void on_error(int status, void *userdata) {
    fprintf(stderr, "%s\n", kc_http_strerror(status));
}

kc_http_parser_t *parser = NULL;

if (kc_http_parser_open(
        &parser,
        on_request,
        NULL,
        on_error,
        userdata) != KC_HTTP_OK) {
    /* handle allocation/argument failure */
}

/* Feed chunks belonging to this one byte stream. */
kc_http_parser_write(parser, chunk, chunk_size);

/* Finalize when that stream closes. */
kc_http_parser_close(parser);
```

The parser buffers incomplete messages internally. When one write completes
multiple messages, the corresponding callback is invoked once per complete
message.

Header names are normalized lowercase. HTTP/1 chunked input is dechunked and
trailers are reported separately.

Requests and responses passed to callbacks are borrowed. They and all nested
strings, fields, and body bytes remain valid only until that callback returns.

### Build a request

```c
const kc_http_field_t headers[] = {
    { "Host", "example.com" }
};

kc_http_request_build_t request = {0};
request.method = "GET";
request.target = "/";
request.headers = headers;
request.header_count = 1;

void *wire = NULL;
size_t wire_size = 0;

if (kc_http_request(&request, &wire, &wire_size) == KC_HTTP_OK) {
    /* send wire bytes */
    kc_http_free(wire);
}
```

### Build a response

```c
int status = 200;
kc_http_response_build_t response = {0};
response.status = &status;
response.body = "hello";
response.body_size = 5;

void *wire = NULL;
size_t wire_size = 0;

if (kc_http_response(&response, &wire, &wire_size) == KC_HTTP_OK) {
    /* send wire bytes */
    kc_http_free(wire);
}
```

`kc_http_request_t` and `kc_http_response_t` are parser output types.
Builders use the separate `kc_http_request_build_t` and
`kc_http_response_build_t` input types so parsed scalar fields remain direct
values while optional builder scalars can represent absence with NULL.

Builder inputs are borrowed for the duration of the call. Returned wire bytes
are caller-owned and released with `kc_http_free()`.

Defaults:

- request method: `GET`
- request target: `/`
- request/response version: `1.1`
- response status: `200` when `status == NULL`
- response reason: derived from status when `reason == NULL`
- chunk size when chunked: `8192` when `chunk_size == NULL`

Non-NULL scalar pointers are explicit. An explicit response status outside
`100..599` is invalid, including `0`. An explicit chunk size of `0` is
invalid.

### Transport composition

`http.c` deliberately has no client/server socket API.

For an HTTP server, `netl.c` owns listening, accepted TCP connections, and
connection identity. Each accepted TCP connection gets its own HTTP parser:

`nets.c` remains an independent outbound transfer API. Returned bytes may also
be fed to an HTTP parser when that transfer carries HTTP.

### Protocol support

| Version | Status |
| :--- | :--- |
| HTTP/0.9 | Parsed |
| HTTP/1.0 | Parse and build |
| HTTP/1.1 | Incremental parse/build, Content-Length, chunked, trailers |
| HTTP/2 | Existing HEADERS/DATA frame parse/build capability retained |
| HTTP/3 | Existing HEADERS/DATA frame parse/build capability retained |

### Source layout

| File | Role |
| :--- | :--- |
| `src/http.c` | CLI projection of the public API |
| `src/libhttp.c` | Reusable parser and builders |
| `src/libhttp.h` | Public C contract |
| `src/test.c` | Public contract tests |

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/http.wasm`
- Exports: `kc_http_parser_open`, `kc_http_parser_write`, `kc_http_parser_close`, `kc_http_free`, `kc_http_request`, `kc_http_response`, `kc_http_strerror`, `kc_http_version`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains are required only by enabled targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
