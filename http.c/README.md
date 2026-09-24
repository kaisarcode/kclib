# http.c - HTTP Protocol Parser and Builder

\`http.c\` is an HTTP protocol layer. It incrementally parses HTTP byte streams
into complete structured requests/responses and builds HTTP wire bytes from
structured requests/responses.

It does not listen on ports, own sockets, route requests, or perform outgoing
network transfers. Those responsibilities belong to transport libraries such as
\`netl.c\` and \`nets.c\`.

The intended scripting composition is:

\`\`\`js
const parser = http.parser();

client.on("data", (data) => {
    parser.write(data);
});

parser.on("request", (request) => {
    const response = http.response({
        status: 200,
        headers: [["content-type", "text/plain"]],
        body: "hello"
    });

    client.send(response);
});
\`\`\`

One parser belongs to one HTTP byte stream. A write may complete zero, one, or
multiple HTTP messages.

---

## CLI

### Parse

\`http parse\` reads stdin incrementally and exits as soon as one complete HTTP
message is available. It does not wait for EOF after the message has completed,
which allows it to operate directly on a TCP connection stream.

\`\`\`bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse
\`\`\`

Default output contains application-facing request/response fields, headers,
and raw logical body bytes. \`--all\` prints protocol metadata, body length, and
trailers as well.

\`\`\`bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse --all
\`\`\`

For a request, default metadata is:

\`\`\`text
request.method=GET
request.target=/
request.path=/
request.query=
header.host=localhost
\`\`\`

With \`--all\`, protocol metadata such as \`http.type\`, \`http.version\`,
\`body.length\`, and trailers are also emitted.

### Build

The normalized command names mirror the reusable API:

\`\`\`bash
printf 'hello' | http request \
    --method POST \
    --target /api \
    --header 'host: localhost' \
    --header 'content-type: text/plain'
\`\`\`

\`\`\`bash
printf 'hello' | http response \
    --status 200 \
    --header 'content-type: text/plain'
\`\`\`

\`http build request\` and \`http build response\` remain temporary compatibility
aliases for existing shell consumers.

Request and response builders support:

- \`--version <version>\`
- repeatable \`--header <name: value>\`
- \`--chunked\`
- \`--chunk-size <n>\`
- repeatable \`--trailer <name: value>\`

Requests additionally support \`--method\` and \`--target\`. Responses support
\`--status\` and \`--reason\`.

---

## Public API

The public surface has three semantic capabilities:

\`\`\`text
parser
request
response
\`\`\`

### Parser lifecycle

\`\`\`c
static void on_request(const kc_http_request_t *request, void *userdata) {
    /* request and all nested data are borrowed for this callback only */
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

/* Feed each chunk belonging to this one stream. */
kc_http_parser_write(parser, chunk, chunk_size);

/* Finalize when that stream closes. */
kc_http_parser_close(parser);
\`\`\`

The parser buffers incomplete messages internally. When a write completes more
than one message, the callback is invoked once per complete message. Header names
are normalized lowercase, chunked HTTP/1 bodies are dechunked, and trailers are
reported separately.

Requests and responses delivered to callbacks are borrowed and valid only for
the duration of the callback. A scripting bridge is expected to copy them into
normal language objects before the callback returns.

### Build a request

\`\`\`c
const kc_http_field_t headers[] = {
    { "Host", "example.com" }
};

kc_http_request_t request = {0};
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
\`\`\`

### Build a response

\`\`\`c
kc_http_response_t response = {0};
response.status = 200;
response.body = "hello";
response.body_size = 5;

void *wire = NULL;
size_t wire_size = 0;

if (kc_http_response(&response, &wire, &wire_size) == KC_HTTP_OK) {
    /* send wire bytes */
    kc_http_free(wire);
}
\`\`\`

Builder inputs are borrowed for the duration of the call. Returned wire bytes
are owned by the caller and released with \`kc_http_free()\`.

Defaults are:

- request method: \`GET\`
- request target: \`/\`
- request/response version: \`1.1\`
- response status: \`200\`
- response reason: derived from status
- chunk size when chunked: \`8192\`

---

## Transport composition

\`http.c\` deliberately has no client/server socket API.

For an HTTP server, \`netl.c\` owns listening, accepted TCP connections, and
connection identity. Each accepted connection gets its own HTTP parser:

\`\`\`js
netl.listen({ port: 8080, protocol: "tcp" });

netl.on("connection", (client) => {
    const parser = http.parser();

    parser.on("request", (request) => {
        client.send(http.response({ status: 200 }));
    });

    client.on("data", (data) => {
        parser.write(data);
    });

    client.on("close", () => {
        parser.close();
    });
});
\`\`\`

\`nets.c\` remains an independent outbound transfer API. Its returned bytes may
also be fed to an HTTP parser when the transfer carries HTTP.

---

## Protocol support

| Version | Status |
| :--- | :--- |
| HTTP/0.9 | Parsed |
| HTTP/1.0 | Parse and build |
| HTTP/1.1 | Incremental parse and build, Content-Length, chunked, trailers |
| HTTP/2 | Existing HEADERS/DATA frame parse/build capability retained |
| HTTP/3 | Existing HEADERS/DATA frame parse/build capability retained |

---

## Source Layout

| File | Role |
| :--- | :--- |
| \`src/http.c\` | CLI projection of the public API |
| \`src/libhttp.c\` | Reusable parser and builders |
| \`src/libhttp.h\` | Public C contract |
| \`src/test.c\` | Public contract tests |

---

## Build request options

| Option | Description |
| :--- | :--- |
| `--method <method>` | HTTP method. Default: `GET`. |
| `--target <target>` | Request target. Default: `/`. |
| `--version <version>` | HTTP version. Default: `1.1`. |
| `--header <name: value>` | Add a request header. Repeatable. |
| `--chunked` | Use `Transfer-Encoding: chunked`. |
| `--chunk-size <n>` | Chunk size in bytes. Default: `8192`. |

### Build response options

| Option | Description |
| :--- | :--- |
| `--status <code>` | HTTP status code. Default: `200`. |
| `--reason <phrase>` | Reason phrase. Default: derived from status. |
| `--version <version>` | HTTP version. Default: `1.1`. |
| `--header <name: value>` | Add a response header. Repeatable. |
| `--chunked` | Use `Transfer-Encoding: chunked`. |
| `--chunk-size <n>` | Chunk size in bytes. Default: `8192`. |
| `--trailer <name: value>` | Add a trailer (chunked only). Repeatable. |

### Common options

| Option | Description |
| :--- | :--- |
| `-h`, `--help` | Show help and exit. |
| `-v`, `--version` | Show version and exit. |

Headers and trailers are limited to 256 each; exceeding the limit fails the
build. A failed parse or build writes a diagnostic to `stderr` and exits 1.

---

## Parse output format

`http parse` writes metadata lines, one empty line, then raw body bytes:

```
http.type=request
http.version=1.1
request.method=POST
request.target=/api?a=1
request.path=/api
request.query=a=1
header.host=localhost
header.content-type=text/plain
body.length=4

hello
```

Response output:

```
http.type=response
http.version=1.1
response.status=200
response.reason=OK
header.content-type=text/plain
body.length=4

hello
```

- Header names are lowercased.
- Repeated headers are emitted as separate lines in order.
- Chunked bodies are automatically dechunked; only logical payload bytes appear.
- Trailers are emitted as `trailer.<name>=<value>` lines before `body.length`.
- `body.length` always reflects the logical byte count, not the wire byte count.

For `--all`, messages are separated by a `\n\n` line on its own.
The consumer uses `body.length` to determine the exact end of each body.

---

## Public API

```c
#include "libhttp.h"

kc_http_t *ctx = NULL;
if (kc_http_open(&ctx) != KC_HTTP_OK) {
    /* allocation failed */
}

void *out = NULL;
size_t out_size = 0;
if (kc_http_parse(ctx, data, data_size, 0, &out, &out_size) != KC_HTTP_OK) {
    fprintf(stderr, "%s\n", kc_http_get_error(ctx));
}
kc_http_free(out);

kc_http_close(ctx);
```

Build a response programmatically:

```c
kc_http_set_status(ctx, 200);
kc_http_add_header(ctx, "content-type", "text/plain");
kc_http_build_response(ctx, body, body_size, &out, &out_size);
kc_http_free(out);
```

---

## Lifecycle

- `kc_http_open(&ctx)` creates a reusable configuration context.
- The `kc_http_set_*` and `kc_http_add_*` functions copy their configuration
    into the context. Headers and trailers use separate `name` and `value`
    arguments.
- `kc_http_parse(ctx, data, data_size, all, &out, &out_size)` parses the
    borrowed wire input supplied for that call. Set `all` non-zero to parse all
    complete messages from that input.
- `kc_http_build_request()` and `kc_http_build_response()` build from the
    borrowed body bytes supplied for that call, using the context configuration.
- Successful parse and build calls return caller-owned binary output through
    `out`; release it with `kc_http_free()`.
- `kc_http_get_error(ctx)` returns a borrowed contextual error string. Do not
    free it.
- `kc_http_close(ctx)` releases the context and its copied configuration.

`http.c` owns HTTP framing completely. Callers provide raw payload bytes and receive logical payload bytes. Wire framing details (chunk syntax, Content-Length, CRLF) are never exposed to the caller.

---

## Protocol support

| Version | Status |
| :--- | :--- |
| HTTP/0.9 | Parsed (simple request, no headers, no body) |
| HTTP/1.0 | Full parse and build |
| HTTP/1.1 | Full parse and build, chunked, trailers |
| HTTP/2 | Binary frame parse/build for HEADERS and DATA |
| HTTP/3 | Frame parse/build for HEADERS and DATA |

---

## Source Layout

| File | Role |
| :--- | :--- |
| `src/http.c` | CLI argument parsing and direct calls to the public C API |
| `src/libhttp.c` | Shared context, helpers, and protocol dispatch |
| `src/libhttp.h` | Public C contract (pure C, no JSON types) |
| `src/test.c` | All tests, one case per public API function |

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only the test executable and link dynamically against the generated shared library.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test`, `make test wine`, or `make test wasm`.

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable HTTP protocol parser/builder as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/http.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_http_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `http` CLI: `src/http.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/http.wasm` and reports how to build it when it is absent.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- Emscripten SDK (`emcmake`, `emcc`, Node.js) for the `wasm32/wasm` target.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
