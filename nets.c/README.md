# nets.c - Network Sender

`nets.c` provides a native asynchronous library and a one-shot CLI for sending raw bytes to one TCP, UDP, or optional TLS destination.

---

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

---

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

/* Close the transfer after the terminal callback has completed. */
    return 0;
}
```

### Transfer semantics

`kc_nets_send()` starts one asynchronous transfer and returns after the operation has been launched. The library copies the host string and input bytes before returning, so the caller may immediately reuse or release its input storage.

A successfully launched transfer invokes its handler exactly once with the terminal result.

For TCP and TLS, `KC_NETS_OK` may include response bytes. The callback owns neither the response pointer nor its storage. Response bytes remain valid only for the callback duration.

UDP sends the complete input as one datagram and reports success with no response bytes.

`kc_nets_stop()` is an interruption command. It requests graceful termination of the active transfer and interrupts the active socket where the platform permits it. A stopped transfer completes through the normal terminal callback with `KC_NETS_ESTOP`. DNS resolution may not be immediately interruptible on every platform.

`kc_nets_close()` releases the transfer. If the transfer is still active, close requests stop and waits for the worker to finish. Call it after the terminal callback has returned.

`kc_nets_strerror()` returns static descriptions for public status codes.

`kc_nets_tls_available()` reports whether TLS support is compiled into the current build.

`kc_nets_version()` returns the generated build version.

### Protocol behavior

TCP connects to one resolved address, sends all input bytes, closes the write side, and reads response bytes until peer EOF.

UDP sends exactly one datagram and does not wait for a reply.

TLS applies the same stream-oriented send/response model over the optional OpenSSL transport.

The library resolves destinations using the platform resolver and tries resolved addresses in order until one succeeds, the operation is stopped, or all addresses fail.

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
- Other cross-compilation toolchains are required only by enabled targets.

### Dependencies

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
