# redp2p.c - Peer-to-Peer Port Tunneling

`redp2p.c` is a small C library and CLI for creating direct peer-to-peer TCP
or UDP tunnels between local ports.

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

Start an index:

```c
#include "libredp2p.h"

kc_redp2p_idx_t *idx = NULL;
kc_redp2p_idx_options_t options = {0};

options.port = 9876;
options.pow = 16;

if (kc_redp2p_idx(&idx, &options) != KC_REDP2P_OK) {
    return 1;
}
```

Publish a local TCP service:

```c
kc_redp2p_pub_t *pub = NULL;
kc_redp2p_pub_options_t options = {0};

options.id = "web";
options.index = "idx.example.com:9876";
options.protocol = KC_REDP2P_TCP;
options.port = 8080;

if (kc_redp2p_pub(&pub, &options) != KC_REDP2P_OK) {
    return 1;
}
```

Expose that publisher locally:

```c
kc_redp2p_con_t *con = NULL;
kc_redp2p_con_options_t options = {0};

options.id = "web";
options.index = "idx.example.com:9876";
options.port = 9000;

if (kc_redp2p_con(&con, &options) != KC_REDP2P_OK) {
    return 1;
}
```

After success, `127.0.0.1:9000` is the application-facing tunnel endpoint.
REDP2P does not interpret the bytes sent through that port. Any process or
socket library may use it.

For example:

```bash
curl http://127.0.0.1:9000/
nc 127.0.0.1 9000
```

Inspect publisher IDs known by a live index:

```c
kc_redp2p_idx_entry_t *entries = NULL;
size_t count = 0;

if (kc_redp2p_idx_list(idx, &entries, &count) == KC_REDP2P_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("%s\n", entries[i].id);
    }
}
kc_redp2p_free(entries);
```

Close handles when the application no longer needs them:

```c
kc_redp2p_con_close(con);
kc_redp2p_pub_close(pub);
kc_redp2p_idx_close(idx);
```

The public API is:

```c
typedef struct kc_redp2p_idx kc_redp2p_idx_t;
typedef struct kc_redp2p_pub kc_redp2p_pub_t;
typedef struct kc_redp2p_con kc_redp2p_con_t;

int kc_redp2p_idx(
    kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options
);

int kc_redp2p_pub(
    kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options
);

int kc_redp2p_con(
    kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options
);

int kc_redp2p_idx_list(
    kc_redp2p_idx_t *idx,
    kc_redp2p_idx_entry_t **out_entries,
    size_t *out_count
);

void kc_redp2p_idx_close(kc_redp2p_idx_t *idx);
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub);
void kc_redp2p_con_close(kc_redp2p_con_t *con);
void kc_redp2p_free(void *ptr);

const char *kc_redp2p_strerror(int status);
uint64_t kc_redp2p_version(void);
```

All close functions accept `NULL`. `kc_redp2p_free(NULL)` is also safe.

The public API intentionally does not expose registration, heartbeat, lookup,
punching, candidates, KCP state, session keys, control sequences, or
application-data I/O.

### Model

REDP2P exposes three persistent capability objects:

```text
idx
  +-- coordinates publishers and consumers
  +-- applies admission and abuse-control policy
  +-- lists active publisher IDs

pub
  +-- publishes one local TCP or UDP port
  +-- maintains its registration automatically

con
  +-- exposes one announced publisher through one local port
  +-- derives TCP or UDP from the publisher record
```

The tunnel is only a byte or datagram path. REDP2P does not provide
`onData`, `read`, `write`, or application-stream callbacks.

### Options

For `kc_redp2p_idx_options_t`:

- `host == NULL` listens on all interfaces.
- `port == 0` uses port 9876.
- `seats == NULL` means no publisher-capacity limit.
- A non-NULL `seats` pointer whose value is zero rejects every publisher.
- `pow == 0` disables registration proof of work.
- `pass == NULL` or an empty string leaves global registration admission open.
- VIP entries reserve IDs and may provide per-ID admission passwords.
- `max_consumers == 0` uses the protocol safety default of 32 pending
    consumers per publisher.

For `kc_redp2p_pub_options_t`, `id`, `index`, `protocol`, and
`port` are required. `protocol` is `KC_REDP2P_TCP` or
`KC_REDP2P_UDP`. `pass` is optional registration admission material.
`stun` is an optional STUN URL for deployments that need external candidate
discovery.

For `kc_redp2p_con_options_t`, `id`, `index`, and local `port` are
required. `stun` is optional. The consumer derives TCP or UDP from the
publisher record.

Timing, punch cadence, pending-call TTL, candidate limits, KCP parameters, and
other protocol mechanics are internal implementation policy rather than public
application configuration.

### Runtime model

Each `idx`, `pub`, and `con` handle owns its runtime until its matching
close function is called.

The native event loops use `poll()` on POSIX and `WSAPoll()` on Windows.
Closing a handle wakes its event loop immediately.

TCP-over-KCP forwarding uses bounded per-session local output buffering.
A slow local TCP endpoint does not cause REDP2P to drain unlimited KCP data or
block in an unbounded write loop. Peer and session errors are isolated whenever
possible.

The index protocol remains an internal implementation detail. It provides
authenticated registration challenges, encrypted publisher control secrets,
proof-of-work admission, password and VIP admission proofs, strictly increasing
authenticated control sequences, heartbeat expiry, bounded pending punch calls,
per-source and per-target abuse controls, candidate validation, bounded HTTP
bodies, bounded session state, and explicit protocol validation.

The index never relays application payloads. Successful peer traffic remains
direct. REDP2P does not add application authentication, authorization, accounts,
content semantics, or application-layer encryption.

The PHP implementation in `imp/php/` provides the same index wire protocol
for deployments where a persistent native index process is inconvenient.

A publisher keeps the minimum local session material required to prove ownership
of its active index registration across process restarts. The platform storage
location and control material are implementation details.

### Platform scope

REDP2P requires native TCP/UDP listeners and direct UDP hole punching. It is
intended for the native platforms supported by the project, including POSIX
systems and Windows.

Browser WebAssembly is not applicable to this capability because the browser
runtime does not expose the native listener and direct UDP networking semantics
represented by REDP2P.

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

The native test run executes the private protocol/security regression suite and
then the reusable public API plus one grouped CLI case.

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
