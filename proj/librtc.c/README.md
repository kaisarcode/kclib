# librtc.c - Native WebRTC DataChannel Library

`librtc.c` provides a compact native WebRTC DataChannel capability through a
kclib-compatible C API.

The implementation uses
[`libdatachannel`](https://github.com/paullouisageneau/libdatachannel) internally.
`libdatachannel` is a build-time implementation dependency and is not exposed as
part of the public `librtc.c` API.

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

`librtc.c` requires a local checkout of the `libdatachannel` source tree.
The sources are not vendored in kclib, downloaded automatically, or included as
a Git submodule.

Clone `libdatachannel` with its submodules in a vendor directory:

```bash
mkdir -p ~/work/vendor
cd ~/work/vendor
git clone --recursive https://github.com/paullouisageneau/libdatachannel.git
```

For an existing checkout, initialize its submodules if needed:

```bash
cd ~/work/vendor/libdatachannel
git submodule update --init --recursive
```

Set `LIBDATACHANNEL_DIR` to the source checkout:

```bash
export LIBDATACHANNEL_DIR="$HOME/work/vendor/libdatachannel"
```

Then build normally:

```bash
make
```

The variable can also be supplied directly:

```bash
make LIBDATACHANNEL_DIR="$HOME/work/vendor/libdatachannel"
```

The build compiles the required DataChannel-only libdatachannel implementation
from that source tree and links it privately into the `librtc` artifact.

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
