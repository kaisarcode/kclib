# librtc.c - Native WebRTC DataChannel Library

`librtc.c` provides a compact native WebRTC DataChannel capability through a
kclib-compatible C API.

The implementation uses
[`libdatachannel`](https://github.com/paullouisageneau/libdatachannel) internally
and uses Mbed TLS as the private DTLS/TLS backend. Both are build-time
implementation dependencies and are not exposed as part of the public
`librtc.c` API.

The scope is native WebRTC peer connections and DataChannels. Media transport
and WebSocket support are disabled in the bundled libdatachannel build.

## Public API

```c
#include "librtc.h"

kc_rtc_peer_t *peer = NULL;

if (kc_rtc_peer(&peer, NULL) == KC_RTC_OK) {
    kc_rtc_peer_close(peer);
}
```

The current public API contains peer lifecycle, status helpers, and build
version support:

```c
int kc_rtc_peer(kc_rtc_peer_t **out,
    const kc_rtc_peer_options_t *options);
void kc_rtc_peer_close(kc_rtc_peer_t *peer);
const char *kc_rtc_strerror(int status);
uint64_t kc_rtc_version(void);
```

ICE servers are supplied as ordinary URI strings through
`kc_rtc_peer_options_t`. STUN and TURN URIs can therefore be composed by higher
level consumers without coupling `librtc.c` to a signaling protocol.

## Build

`librtc.c` requires local source checkouts of `libdatachannel` and Mbed TLS.
The sources are not vendored in kclib, downloaded automatically, or included as
Git submodules of kclib.

Clone the dependencies wherever you keep external source trees:

```bash
git clone --recursive https://github.com/paullouisageneau/libdatachannel.git
git clone --recursive https://github.com/Mbed-TLS/mbedtls.git
```

For existing checkouts, initialize all nested submodules if needed:

```bash
cd /path/to/libdatachannel
git submodule update --init --recursive

cd /path/to/mbedtls
git submodule update --init --recursive
```

Set the source checkout paths:

```bash
export LIBDATACHANNEL_DIR="/path/to/libdatachannel"
export MBEDTLS_DIR="/path/to/mbedtls"
```

Then build normally:

```bash
make
```

The variables can also be supplied directly:

```bash
make \
    LIBDATACHANNEL_DIR="/path/to/libdatachannel" \
    MBEDTLS_DIR="/path/to/mbedtls"
```

The build compiles the required DataChannel-only libdatachannel implementation,
Mbed TLS, libjuice, and usrsctp with the target toolchain and links them
privately into the `librtc` artifact. No separate libdatachannel or Mbed TLS
runtime library is required by consumers.

### Tests

Build the native artifact first, then run the public API contract tests:

```bash
make
make test
```

Build the Windows artifact and run the contract tests through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch builds

A plain `make` builds the current supported host target. `make all` builds all
currently configured targets.

```bash
make all
make x86_64/linux
make aarch64/linux
make x86_64/windows
make x86_64/macos
make aarch64/macos
```

## Development requirements

### Build tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- C11 and C++17 compilers for the target
- a local `libdatachannel` source checkout referenced by `LIBDATACHANNEL_DIR`
- a local Mbed TLS source checkout referenced by `MBEDTLS_DIR`

### Optional cross-compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS.
- Cross GCC/G++ toolchains for non-native Linux architectures.

## Beta notice

This is a beta project tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You are free to test it, use it,
and modify it.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
