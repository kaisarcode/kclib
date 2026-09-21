# http.c - HTTP Protocol Parser and Builder

`http.c` is an HTTP protocol parser and builder library and CLI. It reads
HTTP wire bytes from `stdin`, emits normalized message metadata plus the raw
logical body, and builds HTTP wire messages from raw payload bytes and
metadata.

It composes with stream tools such as [`netl`](../netl.c) and
[`nets`](../nets.c):

```bash
netl 127.0.0.1:8080 'http parse | app'

app | http build response --status 200 | nets 127.0.0.1:8080
```

---

## CLI

### Examples

Parse one HTTP request from stdin:

```bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | http parse
```

Parse all messages from a keep-alive stream:

```bash
cat stream.bin | http parse --all
```

Build a POST request:

```bash
printf 'hello' | http build request \
    --method POST \
    --target /api \
    --header 'host: localhost' \
    --header 'content-type: text/plain'
```

Build a 200 response:

```bash
cat index.html | http build response \
    --status 200 \
    --header 'content-type: text/html'
```

Build a chunked response:

```bash
cat large.bin | http build response \
    --status 200 \
    --header 'content-type: application/octet-stream' \
    --chunked
```

---

### Commands

| Command | Description |
| :--- | :--- |
| `parse` | Parse one HTTP message from stdin and emit normalized output. |
| `parse --all` | Parse all HTTP messages from stdin until EOF. |
| `build request` | Build an HTTP request from stdin body. |
| `build response` | Build an HTTP response from stdin body. |

### Parse options

| Option | Description |
| :--- | :--- |
| `--all` | Parse all messages until EOF, separated by `---`. |

### Build request options

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
