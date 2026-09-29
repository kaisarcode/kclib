# libredp2p.c - Peer-to-Peer Transport

`libredp2p.c` connects applications directly through peer-to-peer transport.

---

## Library

A REDP2P setup has three entities:

- an index, which helps the two sides find each other;
- a publisher, which makes a service available;
- a consumer, which connects to that published service.

The index is only used to introduce the peers. It does not carry the
application traffic.

REDP2P includes a PHP index implementation because PHP is readily available on
many shared hosting environments. PHP is not required by the protocol. The
index may be implemented in any language or runtime that implements the REDP2P
Index Protocol documented in `doc/protocol.md`.

### Public API

The C library exposes peer channels directly. It does not expose application
service ports or perform local port forwarding. The index listener is the only
library capability with an explicit port.

A publisher registers an ID and is notified when a peer channel is established.
Incoming bytes are delivered to its receive callback together with the client
that sent them. The publisher can send bytes back with
`kc_redp2p_client_respond()`.

A consumer connects by publisher ID. A successful `kc_redp2p_con()` returns an
established peer channel; the application can immediately send with
`kc_redp2p_con_send()`, and incoming bytes are delivered to its receive
callback.

What those bytes mean is an application decision. An application can process
them directly, store them, feed another protocol, or bridge them to a local
socket.

The exact C declarations, option fields, return codes, and callback types are
documented in `src/libredp2p.h`.

### TCP and UDP

The native publisher selects TCP or UDP when it registers. The consumer selects
only the publisher ID and learns the transport from the index.

The public data model is the same for both transports: established peer
channels, send, receive, and publisher responses. TCP behaves as a stream. UDP
keeps datagram boundaries.

### WebRTC use

Native applications use the C library and native TCP/UDP networking.

The JavaScript implementation under `imp/` uses WebRTC, but it connects through
the same REDP2P index and uses the same publisher IDs. Its application-facing
model is the same: publishers observe established clients and receive their
data, while consumers return connected channels that can send and receive.

Browsers provide the Web APIs expected by this implementation directly. Other
JavaScript runtimes can use the same files when their host environment exposes
compatible Web APIs.

For Node.js or QuickJS-ng, provide a small runtime adapter before loading the
REDP2P JavaScript implementation. The adapter should supply any missing host
capabilities, such as WebRTC peer connections, `fetch`, Web Crypto,
`TextEncoder`/`TextDecoder`, and timers. REDP2P itself does not depend on a
specific external module; only a compatible host interface is required.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

Native C tests are run with:

```bash
make
make test
```

The browser RTC integration test is independent from the native build:

```bash
./tests/test.sh
```

The script first runs the PHP index contract tests, then starts a temporary
SQLite-backed PHP index on `127.0.0.1:8088` and serves the browser integration
page at `http://127.0.0.1:8088/tests/test.html`. Set `TEST_HOST` or `TEST_PORT`
to override the local endpoint. Stop it with Ctrl+C.

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

Vendored KCP, Monocypher, and Parson retain their respective upstream licenses.
