# redp2p.c - Peer-to-Peer Transport

`redp2p.c` connects two machines directly so one can access a service running
on the other.

---

## CLI

Start an index:

```bash
redp2p idx 9876
redp2p idx 9876 --seats 128 --pow 16
```

List active publisher IDs from a local index:

```bash
redp2p idx 9876 --list
```

Publish a local TCP or UDP port:

```bash
redp2p pub web@idx.example.com:9876 --tcp 8080
redp2p pub game@idx.example.com:9876 --udp 7777
```

Expose one announced publisher through a local port:

```bash
redp2p con web@idx.example.com:9876 9000
```

The consumer derives TCP or UDP from the publisher record. It therefore does
not accept `--tcp` or `--udp`.

The CLI accepts `REDP2P_SEATS`, `REDP2P_POW`, `REDP2P_PASS`,
`REDP2P_VIP`, `REDP2P_MAX_CONSUMERS_PER_PUBLISHER`, and `REDP2P_STUN`
as operator configuration and translates them into the public API.

`idx`, `pub`, and `con` processes run until SIGINT or SIGTERM and then
close their handle.

Common options are `-h` / `--help` and `-v` / `--version`.

---

## Library

Use the library when you want to connect two applications directly through
REDP2P instead of driving the command-line program.

A REDP2P setup has three parts:

- an index, which helps the two sides find each other;
- a publisher, which makes a service available;
- a consumer, which connects to that published service.

The index is only used to introduce the peers. It does not carry the
application traffic.

### Example

Suppose a machine has a web server listening on port 8080 and you want to reach
it from another machine.

On a reachable host, start an index:

```bash
redp2p idx 9876
```

On the machine running the web server, publish it as `web`:

```bash
redp2p pub web@idx.example.com:9876 --tcp 8080
```

On the other machine, open that published service on local port 9000:

```bash
redp2p con web@idx.example.com:9876 9000
```

Now the remote web server is available locally:

```bash
curl http://127.0.0.1:9000/
```

REDP2P only carries the bytes between the two applications. It does not need to
know whether those bytes are HTTP, a database protocol, a game protocol, or
anything else.

### Using the C library

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
socket. The `redp2p` CLI implements that last option itself, which is why its
`pub` and `con` commands still accept local ports.

The exact C declarations, option fields, return codes, and callback types are
documented in `src/libredp2p.h`.

### TCP and UDP

The native publisher selects TCP or UDP when it registers. The consumer selects
only the publisher ID and learns the transport from the index.

The public data model is the same for both transports: established peer
channels, send, receive, and publisher responses. TCP behaves as a stream. UDP
keeps datagram boundaries.

### Browser use

Native applications use the C library and native TCP/UDP networking.

The browser implementation under `imp/` uses WebRTC, but it connects through
the same REDP2P index and uses the same publisher IDs. Its application-facing
model is the same: publishers observe established clients and receive their
data, while consumers return connected channels that can send and receive.

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
