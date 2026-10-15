# libhpm.c - Hole-Punch-Man

---

`libhpm.c` establishes direct bidirectional UDP paths between peers. The caller
supplies one or more remote endpoints; HPM performs the direct connectivity
attempt and returns an operational UDP path when one of the supplied endpoints
is reachable.

HPM can either bind its own local UDP socket with `kc_hpm_connect()` or perform
the same direct-path establishment over an already-open UDP mapping with
`kc_hpm_connect_over()`. Using an existing mapping preserves that mapping while
HPM probes and establishes the path; HPM borrows it and does not close it.

Peer endpoint discovery and exchange are outside the library. Remote endpoints
may come from any source chosen by the application.

## Public API

```c
#include "libhpm.h"

kc_hpm_endpoint_t peers[] = {
    { "203.0.113.20", 5000 },
    { "192.168.1.20", 5000 }
};

kc_hpm_options_t options = {
    .host = NULL,
    .port = 5000,
    .peers = peers,
    .peer_count = sizeof(peers) / sizeof(peers[0]),
    .timeout_ms = 5000
};

kc_hpm_path_t *path = NULL;

if (kc_hpm_connect(&path, &options) == KC_HPM_OK) {
    kc_hpm_send(path, "hello", 5);

    void *data = NULL;
    size_t size = 0;
    if (kc_hpm_receive(path, &data, &size, 1000) == KC_HPM_OK) {
        /* one UDP datagram is available in data/size */
        kc_hpm_free(data);
    }
}

kc_hpm_close(path);
```

The public API contains:

```c
int kc_hpm_connect(kc_hpm_path_t **out,
    const kc_hpm_options_t *options);
int kc_hpm_connect_over(kc_hpm_path_t **out,
    const kc_hpm_options_t *options,
    const kc_hpm_udp_t *udp);
int kc_hpm_send(kc_hpm_path_t *path,
    const void *data, size_t data_size);
int kc_hpm_receive(kc_hpm_path_t *path,
    void **out_data, size_t *out_size, uint64_t timeout_ms);
const char *kc_hpm_local_host(const kc_hpm_path_t *path);
unsigned short kc_hpm_local_port(const kc_hpm_path_t *path);
const char *kc_hpm_peer_host(const kc_hpm_path_t *path);
unsigned short kc_hpm_peer_port(const kc_hpm_path_t *path);
void kc_hpm_free(void *ptr);
void kc_hpm_close(kc_hpm_path_t *path);
const char *kc_hpm_strerror(int status);
uint64_t kc_hpm_version(void);
```

`kc_hpm_connect()` establishes a direct UDP path using endpoints supplied by the
application and a local socket owned by HPM. `host` is the optional local bind
host. `port` is the preferred local UDP port; zero asks the operating system to
select one.

`kc_hpm_connect_over()` performs the same probing and path selection over a live
UDP mapping supplied by the caller. The supplied mapping provides its numeric
local endpoint and complete-datagram send/receive operations. HPM borrows it for
the path lifetime; the caller retains ownership and closes it after the HPM path
is closed. Because HPM consumes incoming probe and peer datagrams, the supplied
mapping must have one active receive owner while the path is alive.

For both entry points, `peers` is the set of remote endpoints that HPM may try
and `timeout_ms` bounds the direct connectivity attempt; zero uses the library
default. HPM owns candidate probing, simultaneous punch behavior, and selection.
The application does not manage candidate pairs, probe phases, retry state, or
socket readiness.

The resulting path preserves UDP datagram semantics. `kc_hpm_send()` sends one
datagram, and `kc_hpm_receive()` returns one datagram. A receive timeout of zero
waits indefinitely. UDP itself does not guarantee delivery, ordering, or
retransmission.

The selected local and remote endpoint can be inspected through the path. These
values describe the path that was actually established; they do not imply how
the application discovered or exchanged endpoint information.

---

## Build

A plain `make` builds only the current host target. Artifacts, including the
public header, are generated under `bin/{arch}/{platform}/`.

```bash
make
```

### Tests

The portable test entry point is `make test`.

```bash
make
make test
make x86_64/windows
make test wine
```

The contract tests establish loopback UDP paths using both HPM-owned sockets and
caller-supplied live UDP mappings. They verify path creation, mapping ownership,
datagram exchange, endpoint reporting, timeout behavior, and cleanup.

### Multiarch Builds

A plain `make` builds the current host target. `make all` builds the configured
Linux, Windows, Android, macOS, and iOS target matrix. HPM deliberately has no
WebAssembly target because its capability requires real UDP sockets.

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

- GNU Linux and MinGW-w64 cross-compilers for the selected targets
- Android NDK `27.2.12479018` for Android
- osxcross plus installed Apple SDKs for macOS and iOS
- Wine to run the x86_64 Windows contract test on Linux

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- `ws2_32`

macOS:
- No additional system libraries required.

### Dependencies

`libhpm.c` has no third-party library dependencies.

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

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
