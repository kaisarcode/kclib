# redp2p.c - Peer-to-Peer Transport

`redp2p.c` provides REDP2P transport for native applications and the CLI.
Native peers use TCP or UDP; the browser implementation under `imp/` uses
WebRTC. Both share the same index protocol and capability model.

Applications describe the tunnel they want. REDP2P owns registration,
heartbeats, lookup, candidate exchange, hole punching, session setup, retries,
and publisher deregistration. The index coordinates peers over HTTP but never
relays application traffic.

TCP uses vendored KCP over the direct peer UDP path. UDP preserves application
datagram boundaries. Application bytes are opaque to REDP2P.

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

## Public API

The library exposes the same three roles as the CLI:

```text
idx  -> coordinates peers
pub  -> publishes one service
con  -> connects to one published service
```

An index maintains the control plane. Publishers register an ID and keep that
registration alive. Consumers request an ID and establish a direct path to the
matching publisher. Application traffic does not pass through the index.

### Publisher and consumer modes

A publisher can expose an existing local TCP or UDP service, or receive traffic
directly through the library.

A consumer can expose the remote publisher as a local port, or exchange bytes
directly through the library.

```text
local service mode

application <-> local port <-> REDP2P <==== direct peer path ====> REDP2P
                                                                   |
                                                                   v
                                                             local service


direct mode

application <-> REDP2P <==== direct peer path ====> REDP2P <-> application
```

The transport protocol belongs to the publisher. A consumer selects only the
publisher ID; REDP2P derives TCP or UDP from the publisher record.

TCP is a byte stream. UDP preserves datagram boundaries. REDP2P does not
interpret application payloads.

### Capabilities

`kc_redp2p_idx_t` represents a running index. It can list the publisher IDs
currently known by that index and is closed when coordination is no longer
needed.

`kc_redp2p_pub_t` represents one published service. REDP2P owns registration,
heartbeats, lookup state, peer setup, retries, and deregistration for the
lifetime of that capability.

`kc_redp2p_con_t` represents one consumer connection to a publisher ID. In
local-port mode it exposes the tunnel on loopback. In direct mode the
application sends through the capability and may receive through a callback.

`kc_redp2p_client_t` identifies one direct publisher-side client. It exists so
a publisher callback can respond to or close the peer that delivered the input.

### Configuration

Index configuration controls the listening address, port, publisher capacity,
proof-of-work admission, passwords, VIP reservations, and pending-consumer
limits.

Publisher configuration selects an ID, index endpoint, TCP or UDP, and either a
local service port or a direct receive callback. Optional password and STUN
settings apply to registration and peer discovery.

Consumer configuration selects a publisher ID and index endpoint. A nonzero
local port enables the loopback adapter. With no local port, the consumer uses
the direct send/receive interface.

### Ownership and lifecycle

REDP2P owns the internal coordination state, sockets, adapter threads, session
state, and peer-negotiation details associated with each capability.

Applications own only the public handles and data they explicitly allocate.
Memory returned by REDP2P is released with `kc_redp2p_free()`. Capability
handles are released with their corresponding close function.

The public contract intentionally does not expose registration messages,
heartbeats, lookup, hole punching, candidates, KCP state, session keys, control
sequences, SDP, ICE, or private adapter ports.

The exact C types, option fields, status codes, callbacks, and function
signatures are defined in `src/libredp2p.h`.

### Runtime model

```text
                 control plane
    pub  ------------------------------>  idx
     ^                                     ^
     |                                     |
     |                                     |
     +=========== direct path ==========  con

                 application data
```

The index coordinates discovery and session setup. Once peers establish the
direct path, application traffic flows between publisher and consumer without
being relayed by the index.

### Platform scope

REDP2P depends on native TCP/UDP sockets and direct UDP hole punching. The
browser implementation under `imp/` uses WebRTC while sharing the same index
protocol and publisher/consumer model.


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
