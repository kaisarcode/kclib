# libstun.c - STUN UDP mappings and endpoints

---

`libstun.c` provides both sides of UDP STUN Binding: it can discover the
externally observed endpoint of a local UDP mapping and keep that mapping alive,
or run an autonomous UDP Binding endpoint that other machines can query.

A discovered mapping remains bound to the same local UDP socket for its
lifetime, so application datagrams use the same mapping that STUN observed. A
listener answers Binding Requests with the source IP address and UDP port it
actually received, encoded as `XOR-MAPPED-ADDRESS`.

The library owns STUN Binding discovery, refresh traffic, and UDP Binding
responses. Its public surface is limited to STUN mapping discovery, the live UDP
mapping produced by discovery, and autonomous STUN Binding service.

## Public API

```c
#include "libstun.h"

kc_stun_server_options_t server_options = {
    .host = NULL,
    .port = 3478
};
kc_stun_server_t *server = NULL;

if (kc_stun_listen(&server, &server_options) == KC_STUN_OK) {
    printf("STUN listening on UDP port %u\n",
        kc_stun_server_port(server));
}

kc_stun_options_t options = {
    .server = "stun.example.com:3478",
    .host = NULL,
    .port = 5000,
    .timeout_ms = 3000
};
kc_stun_mapping_t *mapping = NULL;

if (kc_stun_discover(&mapping, &options) == KC_STUN_OK) {
    printf("mapped as %s:%u\n",
        kc_stun_mapped_host(mapping),
        kc_stun_mapped_port(mapping));
}

kc_stun_close(mapping);
kc_stun_server_close(server);
```

The public API contains:

```c
int kc_stun_listen(kc_stun_server_t **out,
    const kc_stun_server_options_t *options);
unsigned short kc_stun_server_port(const kc_stun_server_t *server);
void kc_stun_server_close(kc_stun_server_t *server);
int kc_stun_discover(kc_stun_mapping_t **out,
    const kc_stun_options_t *options);
int kc_stun_refresh(kc_stun_mapping_t *mapping);
int kc_stun_send(kc_stun_mapping_t *mapping,
    const kc_stun_endpoint_t *peer,
    const void *data, size_t data_size);
int kc_stun_receive(kc_stun_mapping_t *mapping,
    kc_stun_datagram_t *out, uint64_t timeout_ms);
const char *kc_stun_local_host(const kc_stun_mapping_t *mapping);
unsigned short kc_stun_local_port(const kc_stun_mapping_t *mapping);
const char *kc_stun_mapped_host(const kc_stun_mapping_t *mapping);
unsigned short kc_stun_mapped_port(const kc_stun_mapping_t *mapping);
void kc_stun_datagram_free(kc_stun_datagram_t *datagram);
void kc_stun_close(kc_stun_mapping_t *mapping);
const char *kc_stun_strerror(int status);
uint64_t kc_stun_version(void);
```

`kc_stun_listen()` starts an autonomous UDP Binding endpoint. A NULL or empty
`host` listens on available IPv4 and IPv6 wildcard addresses using the same UDP
port. An explicit `host` restricts the listener to address families resolved for
that host. Port zero requests an operating-system-selected port, which can be
read with `kc_stun_server_port()`. For a conventional public endpoint, use port
3478. The listener runs until `kc_stun_server_close()` and answers UDP STUN Binding
Requests with `XOR-MAPPED-ADDRESS`. The current transport is UDP; TCP, TLS, and
DTLS STUN transports are not part of this API.

`kc_stun_discover()` creates a UDP mapping, sends an RFC 5389 Binding Request,
and returns a live mapping only after the STUN server reports its observed
endpoint. The same UDP mapping remains open until `kc_stun_close()`.

`server` accepts `host:port`, `[IPv6]:port`, or a host without a port, which
uses STUN port 3478. An optional `stun:` prefix is accepted. `host` is the
optional local bind host and `port` is the preferred local UDP port; zero asks
the operating system to select one. A zero discovery timeout uses the library
default.

`kc_stun_local_host()` and `kc_stun_local_port()` describe the bound local
endpoint. `kc_stun_mapped_host()` and `kc_stun_mapped_port()` describe the
endpoint observed by the STUN server. IPv4 and IPv6 mappings are supported;
an explicit local host must be compatible with the selected STUN server
address family.

`kc_stun_refresh()` sends a fresh Binding Request through the same mapping
without blocking for its response. STUN responses from the configured server
are consumed internally by `kc_stun_receive()` and are never returned as
application datagrams.

`kc_stun_send()` and `kc_stun_receive()` preserve UDP datagram semantics on
the discovered mapping. A receive timeout of zero waits indefinitely.

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

The contract tests validate the autonomous Binding listener at the wire level,
IPv4 and IPv6 discovery, preservation of the observed local mapping, refresh
traffic, application datagram exchange, timeouts, errors, and cleanup.

### Multiarch Builds

A plain `make` builds the current host target. `make all` builds the configured
Linux, Windows, Android, macOS, and iOS target matrix. STUN deliberately has no
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
- `bcrypt`

macOS:
- No additional system libraries required.

### Dependencies

`libstun.c` has no third-party library dependencies.

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
