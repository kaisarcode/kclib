# redp2p.c - peer-to-peer port tunneling

`redp2p.c` creates direct peer-to-peer TCP or UDP tunnels between local ports.

Applications describe the tunnel they want. REDP2P owns the coordination protocol: registration, heartbeats, lookup, candidate exchange, hole punching, session setup, retries, and publisher deregistration are internal implementation details.

The index coordinates peers over HTTP but never relays application traffic. TCP uses vendored KCP over the direct peer UDP path. UDP preserves application datagram boundaries. Application bytes are opaque to REDP2P.

## Public capability model

REDP2P exposes three persistent objects:

- `idx`: runs an index with operator-selected admission and capacity policy.
- `pub`: publishes one local TCP or UDP port under an ID.
- `con`: exposes one announced publisher through a local port.

A consumer does not choose the remote protocol. The publisher announces TCP or UDP and `con` learns it from the index.

REDP2P does not expose application-data callbacks, reads, writes, or streams. Once a consumer succeeds, any process or socket library may use the local port.

## Public C API

The public header is `src/libredp2p.h`.

Start an index:

```c
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

After success, `127.0.0.1:9000` is the application-facing tunnel endpoint. REDP2P does not interpret the bytes that another program sends through that port.

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

All close functions accept `NULL`. `kc_redp2p_free(NULL)` is also safe.

The complete public capability surface is:

```c
int kc_redp2p_idx(kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options);
int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options);
int kc_redp2p_con(kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options);

int kc_redp2p_idx_list(kc_redp2p_idx_t *idx,
    kc_redp2p_idx_entry_t **out_entries, size_t *out_count);

void kc_redp2p_idx_close(kc_redp2p_idx_t *idx);
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub);
void kc_redp2p_con_close(kc_redp2p_con_t *con);
void kc_redp2p_free(void *ptr);

const char *kc_redp2p_strerror(int status);
uint64_t kc_redp2p_version(void);
```

The public ABI intentionally does not expose registration, heartbeat, lookup, punching, candidates, KCP state, session keys, control sequences, or transport I/O.

## Lua and JavaScript binding shape

Bindings should project the same capability model without exposing coordination
operations.

Lua:

```lua
local redp2p = require("redp2p")

local idx, err = redp2p.idx({
    port = 9876,
    seats = 128,
    pow = 16
})

local pub, err = redp2p.pub({
    id = "web",
    index = "idx.example.com:9876",
    protocol = redp2p.TCP,
    port = 8080
})

local con, err = redp2p.con({
    id = "web",
    index = "idx.example.com:9876",
    port = 9000
})

local entries = idx:list()

con:close()
pub:close()
idx:close()
```

JavaScript should expose the same objects and options. Binding machinery may adapt
calling convention, ownership, or asynchronous host integration, but it must not
introduce public `lookup`, `heartbeat`, `punch`, session, candidate, or data
I/O concepts.

The application uses the resulting local port with its own networking code or a
separate process. REDP2P does not provide `onData`, `read`, `write`, or
application-stream callbacks.

## Options

### idx

`host == NULL` listens on all interfaces. `port == 0` uses port 9876.

`seats == NULL` means no publisher-capacity limit. A non-NULL pointer whose value is zero rejects every publisher.

`pow == 0` disables registration proof of work.

`pass == NULL` or an empty string leaves global registration admission open. VIP entries reserve IDs and may provide per-ID admission passwords.

`max_consumers == 0` uses the protocol safety default of 32 pending consumers per publisher.

Timing, punch cadence, pending-call TTL, candidate limits, and transport internals are deliberately not public application configuration.

### pub

`id`, `index`, `protocol`, and `port` are required.

`protocol` is `KC_REDP2P_TCP` or `KC_REDP2P_UDP`.

`pass` is optional registration admission material. `stun` is an optional STUN URL for deployments that need external candidate discovery.

### con

`id`, `index`, and local `port` are required. `stun` is optional.

The consumer derives TCP or UDP from the publisher record. Repeating the publisher protocol in consumer configuration is intentionally unnecessary.

## CLI

Start an index:

```bash
redp2p idx 9876
redp2p idx 9876 --seats 128 --pow 16
```

List active IDs from a local index:

```bash
redp2p idx 9876 --list
```

Publish a TCP or UDP local port:

```bash
redp2p pub web@idx.example.com:9876 --tcp 8080
redp2p pub game@idx.example.com:9876 --udp 7777
```

Expose an announced publisher on a local port:

```bash
redp2p con web@idx.example.com:9876 9000
```

The consumer CLI also derives the protocol from the publisher. It therefore has no `--tcp` or `--udp` flag.

The CLI accepts `REDP2P_SEATS`, `REDP2P_POW`, `REDP2P_PASS`, `REDP2P_VIP`, `REDP2P_MAX_CONSUMERS_PER_PUBLISHER`, and `REDP2P_STUN` as operator configuration and translates them into the public capability API.

Processes run until SIGINT or SIGTERM and then close their handle.

## Protocol and security boundaries

The public API is intentionally much smaller than the implementation.

The index protocol still provides authenticated registration challenges, encrypted publisher control secrets, proof-of-work admission, password/VIP admission proofs, strictly increasing authenticated control sequences, heartbeat expiry, bounded pending punch calls, per-source and per-target abuse controls, candidate validation, bounded HTTP bodies, bounded session state, and explicit protocol validation.

These mechanisms are internal to REDP2P. Applications using the library do not reproduce them.

The index never relays application payloads. Successful peer traffic remains direct. REDP2P does not add application authentication, authorization, accounts, content semantics, or application-layer encryption.

The PHP implementation in `imp/php/` implements the same index wire protocol for deployments where a persistent native index process is inconvenient.

## Local publisher state

A publisher maintains the minimum local session material required to prove ownership of its active index registration across process restarts. This state is an implementation concern, not a public application lifecycle.

The library uses its platform default state location and keeps scoped publisher credentials private. Applications do not manage control sequences, registration secrets, or key filenames.

## Concurrency and backpressure

Runtime work is isolated by `idx`, `pub`, and `con` handles.

TCP-over-KCP forwarding uses bounded per-session local output buffering. A slow local TCP endpoint does not cause REDP2P to drain unlimited KCP data or block in an unbounded write loop. Peer/session errors are handled at session scope whenever possible.

The index abuse/resource limits are independent from I/O fairness and remain enforced.

## Build and tests

Build the current host target:

```bash
make
```

Run native tests:

```bash
make test
```

Run Windows tests through Wine after building the Windows target:

```bash
make x86_64/windows
make test wine
```

The test suite is split deliberately:

- `src/test-public.c` validates the reusable public capability API and one grouped CLI case.
- `src/test.c` remains a private protocol/security regression suite and may exercise implementation-only operations without making them public ABI.

WebAssembly is not a reusable REDP2P target: the capability requires native TCP/UDP listeners, direct UDP hole punching, and host networking semantics that are not represented by the existing WASM build model.

## License

GPL-3.0.

Vendored KCP, Monocypher, and Parson retain their respective upstream licenses.
