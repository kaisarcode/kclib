# nets.c - Network Sender

`nets.c` provides a native asynchronous library and a one-shot CLI for sending raw bytes to one TCP, UDP, or optional TLS destination.

## CLI

The CLI reads all input from standard input, sends it to one target, and prints stream response bytes to standard output.

```bash
echo 'hello' | nets 127.0.0.1:8080
echo 'hello' | nets 127.0.0.1:8080 --udp
nets https://example.com <<< 'payload'
```

Usage:

```text
nets <target> [--tcp|--udp|--tls]
```

Targets may be a host, `host:port`, bracketed IPv6 address, or a URL-shaped `http://`, `https://`, `tcp://`, or `udp://` target.

URL-shaped targets select transport and authority defaults only. They do not add HTTP framing, headers, redirects, parsing, or other application-protocol behavior.

`http://` selects TCP port 80, `https://` selects TLS port 443, and `tcp://` or `udp://` select their corresponding transport with port 80 when no explicit port is present.

TLS is available only when the library is compiled with OpenSSL. The TLS transport sets SNI but does not verify the server certificate or hostname, so it must not be treated as authenticated transport.

## Public API

A `kc_nets_t` represents one active transfer.

```c
#include "libnets.h"

static void complete(
    int status,
    const void *data,
    size_t size,
    void *userdata
) {
    (void)userdata;

    if (status == KC_NETS_OK && data != NULL) {
        /* data[0..size) is the borrowed response */
    }
}

int main(void) {
    const unsigned char message[] = "hello";
    kc_nets_t *transfer = NULL;

    if (kc_nets_send(
            &transfer,
            "127.0.0.1",
            8080,
            KC_NETS_TCP,
            message,
            sizeof(message) - 1,
            complete,
            NULL
        ) != KC_NETS_OK) {
        return 1;
    }

    /*
     * The transfer may continue asynchronously.
     * Call kc_nets_stop(transfer) when graceful interruption is required.
     */

    /* Close from the terminal callback, or stop explicitly when needed. */
    return 0;
}
```

## Transfer semantics

`kc_nets_send()` starts one asynchronous transfer and returns after the operation has been launched. The library copies the host string and input bytes before returning, so the caller may immediately reuse or release its input storage.

A successfully launched transfer invokes its handler exactly once with the terminal result.

For TCP and TLS, `KC_NETS_OK` may include response bytes. The callback owns neither the response pointer nor its storage. Response bytes remain valid only for the callback duration.

UDP sends the complete input as one datagram and reports success with no response bytes.

`kc_nets_stop()` is an interruption command. It requests graceful termination of the active transfer and interrupts the active socket where the platform permits it. A stopped transfer completes through the normal terminal callback with `KC_NETS_ESTOP`. DNS resolution may not be immediately interruptible on every platform.

`kc_nets_close()` releases the transfer. If the transfer is still active, close requests stop and waits for the worker to finish. It is also safe to call `kc_nets_close()` from the transfer callback.

`kc_nets_strerror()` returns static descriptions for public status codes.

`kc_nets_tls_available()` reports whether TLS support is compiled into the current build.

`kc_nets_version()` returns the generated build version.

## Protocol behavior

TCP connects to one resolved address, sends all input bytes, closes the write side, and reads response bytes until peer EOF.

UDP sends exactly one datagram and does not wait for a reply.

TLS applies the same stream-oriented send/response model over the optional OpenSSL transport.

The library resolves destinations using the platform resolver and tries resolved addresses in order until one succeeds, the operation is stopped, or all addresses fail.

## WASM

WASM support is not provided for this library. The reusable capability is arbitrary raw TCP, UDP, and TLS networking, which standard browser WASM environments do not expose. Substituting WebSocket, WebTransport, or another browser transport would change the capability contract.

## Build

A plain build targets the current host architecture.

```bash
make
```

Multi-architecture artifacts are written under `bin/{arch}/{platform}/`.

```bash
make all
```

## Tests

Build project artifacts before running the contract suite.

```bash
make
make test
```

Windows artifacts can be validated through Wine:

```bash
make x86_64/windows
make test wine
```

The test suite covers the reusable asynchronous API and one grouped `kc_nets_cli` case for the shipped command-line interface.

WASM tests are not applicable because the browser runtime cannot represent the raw socket capability without changing its semantics.

## Dependencies

Build tools:

- GNU Make
- CMake 3.14 or newer
- Ninja
- a C11 compiler

Platform networking:

- Windows: `ws2_32`
- POSIX platforms: native socket APIs and threading support

Optional:

- OpenSSL for TLS
- MinGW and Wine for Windows cross-compilation and validation
- osxcross SDKs for macOS and iOS cross-compilation
- Android NDK for Android cross-compilation
