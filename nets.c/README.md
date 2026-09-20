# nets.c - Network Sender

`nets.c` is a small C library and CLI for sending standard input to one TCP, UDP, or optional TLS target and printing the response to standard output.

---

## CLI

Send bytes from standard input to a network address and print the response to standard output.

### Examples

Send to a TCP endpoint - stdin is sent, then the response is printed to stdout:

```bash
echo 'hello' | nets 127.0.0.1:8080
```

Send to port 80 by default:

```bash
echo 'hello' | nets 127.0.0.1
```

Pass a URL-shaped target:

```bash
nets https://example.com <<< 'payload'
```

URL-shaped targets select transport and authority defaults only: `http://` uses TCP port `80`, `https://` uses TLS and port `443`, `tcp://` uses TCP, and `udp://` uses UDP. `nets` remains a raw byte sender; `https://` does not create an HTTP request or add HTTP framing. TLS encryption is available only when TLS support is compiled in. The current TLS path does not verify the server certificate or hostname and must not be treated as authenticated.

Send a UDP datagram:

```bash
echo 'hello' | nets 127.0.0.1:8080 --udp
```

---

### Parameters

| Parameter | Description |
| :--- | :--- |
| `<target>` | Host, `host:port`, or `http://`, `https://`, `tcp://`, `udp://` URL-shaped target. |
| `--tcp` | Use TCP. This is the default. |
| `--udp` | Use UDP. |
| `--tls` | Use TLS over TCP. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

```c
#include "libnets.h"

const unsigned char msg[] = "hello\n";
kc_nets_t *ctx = NULL;
void *response = NULL;
size_t response_size = 0;

if (kc_nets_open(&ctx) == KC_NETS_OK) {
    int rc = kc_nets_send(
        ctx,
        "127.0.0.1",
        8080,
        KC_NETS_TCP,
        msg,
        sizeof(msg) - 1,
        &response,
        &response_size
    );

    if (rc == KC_NETS_OK) {
        /* response[0..response_size) is binary data */
    }

    kc_nets_free(response);
    kc_nets_close(ctx);
}
```

---

## Lifecycle

- `kc_nets_open()` allocates a sender context.
- `kc_nets_send()` borrows the input bytes for the duration of the call and never retains them. It opens a socket, sends the bytes, returns any response through the output parameters, and closes the socket before returning.
- A returned response is owned binary memory. `response_size` is authoritative, and no NUL terminator is promised.
- Release response memory with `kc_nets_free()`. Calling `kc_nets_free(NULL)` is valid.
- TCP sends all bytes over one connection, then reads the response until EOF.
- UDP sends the provided bytes as one datagram and normally returns no response data.
- `kc_nets_stop()` requests a cooperative stop for one context. DNS resolution, connection attempts, and some reads may not be interrupted, and TCP or TLS response reads may wait for peer EOF.
- `kc_nets_strerror()` maps categorical status codes to static messages.
- TLS is optional and available only when compiled with OpenSSL.
- `kc_nets_version()` returns the build version.
- `kc_nets_close()` releases the context.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

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

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

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
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- `ws2_32`

macOS / iOS:
- No additional system libraries required.

### Optional Dependencies

- `OpenSSL` - required for TLS over TCP (`--tls`, `https://` URLs).
    If missing, TLS features are unavailable but TCP and UDP work normally.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
