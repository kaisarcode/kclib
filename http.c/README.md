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

## CLI

### Parse

\`http parse\` reads stdin incrementally and exits as soon as one complete HTTP
message is available. It does not wait for EOF after the message has completed.

\`\`\`bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse
\`\`\`

Default request output contains the application-facing fields and headers:

\`\`\`text
request.method=GET
request.target=/
request.path=/
request.query=
header.host=localhost
\`\`\`

If a logical body is present, one blank line is followed by its raw bytes.

\`--all\` prints the same message plus protocol metadata, logical body length,
and trailers:

\`\`\`bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse --all
\`\`\`

Additional fields include:

\`\`\`text
http.type=request
http.version=1.1
body.length=0
\`\`\`

### Request

\`\`\`bash
printf 'hello' | http request \
    --method POST \
    --target /api \
    --header 'host: localhost' \
    --header 'content-type: text/plain'
\`\`\`

### Response

\`\`\`bash
printf 'hello' | http response \
    --status 200 \
    --header 'content-type: text/plain'
\`\`\`

Request and response builders support:

- \`--version <version>\`
- repeatable \`--header <name: value>\`
- \`--chunked\`
- \`--chunk-size <n>\`
- repeatable \`--trailer <name: value>\`

Requests additionally support \`--method\` and \`--target\`. Responses support
\`--status\` and \`--reason\`.

For compatibility with existing shell consumers, \`http build request\` and
\`http build response\` are accepted as aliases.

Common options:

- \`-h\`, \`--help\`
- \`-v\`, \`--version\`

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
\`\`\`

The parser buffers incomplete messages internally. When one write completes
multiple messages, the corresponding callback is invoked once per complete
message.

Header names are normalized lowercase. HTTP/1 chunked input is dechunked and
trailers are reported separately.

Requests and responses passed to callbacks are borrowed. They and all nested
strings, fields, and body bytes remain valid only until that callback returns.

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
are caller-owned and released with \`kc_http_free()\`.

Defaults:

- request method: \`GET\`
- request target: \`/\`
- request/response version: \`1.1\`
- response status: \`200\`
- response reason: derived from status
- chunk size when chunked: \`8192\`

## Transport composition

\`http.c\` deliberately has no client/server socket API.

For an HTTP server, \`netl.c\` owns listening, accepted TCP connections, and
connection identity. Each accepted TCP connection gets its own HTTP parser:

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

\`nets.c\` remains an independent outbound transfer API. Returned bytes may also
be fed to an HTTP parser when that transfer carries HTTP.

## Protocol support

| Version | Status |
| :--- | :--- |
| HTTP/0.9 | Parsed |
| HTTP/1.0 | Parse and build |
| HTTP/1.1 | Incremental parse/build, Content-Length, chunked, trailers |
| HTTP/2 | Existing HEADERS/DATA frame parse/build capability retained |
| HTTP/3 | Existing HEADERS/DATA frame parse/build capability retained |

## Source layout

| File | Role |
| :--- | :--- |
| \`src/http.c\` | CLI projection of the public API |
| \`src/libhttp.c\` | Reusable parser and builders |
| \`src/libhttp.h\` | Public C contract |
| \`src/test.c\` | Public contract tests |

## Build

A plain \`make\` builds the current host target into
\`bin/{arch}/{platform}/\`.

\`\`\`bash
make
\`\`\`

Build every configured target:

\`\`\`bash
make all
\`\`\`

Individual targets include Linux, Windows, macOS, iOS, Android, and WebAssembly
variants defined by the project Makefile.

## Tests

Build artifacts first, then run the native public-contract tests:

\`\`\`bash
make
make test
\`\`\`

Windows-through-Wine:

\`\`\`bash
make x86_64/windows
make test wine
\`\`\`

WebAssembly:

\`\`\`bash
make wasm32/wasm
make test wasm
\`\`\`

The WebAssembly module contains the reusable parser/builders only; the CLI is
not compiled into the module.

WASM exports:

\`\`\`text
kc_http_parser_open
kc_http_parser_write
kc_http_parser_close
kc_http_request
kc_http_response
kc_http_free
kc_http_strerror
kc_http_version
\`\`\`

## Development requirements

Core build tools:

- GNU Make
- CMake >= 3.14
- Ninja
- a C11 compiler

Optional toolchains are required only for their matching cross-build targets,
including MinGW/Wine, Emscripten, osxcross, and the Android NDK.
