# REDP2P Unified Transport Refactor Plan

## Scope

This plan describes the refactor of `proj/redp2p.c` on the `slave` branch of `kclib`.

The goal is to merge the current native REDP2P implementation and the current browser/WebRTC implementation into one REDP2P protocol and capability model while preserving the existing behavior of both implementations.

This is not a rewrite of REDP2P.

The refactor must preserve:

- the existing native TCP and UDP behavior;
- the existing browser WebRTC behavior;
- the existing native CLI contract;
- the current REDP2P admission, registration, lifetime, and index policies;
- the current kclib public API philosophy: expose intent, not transport plumbing.

The current `redp2p-web` project becomes a JavaScript/browser implementation of REDP2P rather than a separate REDP2P variant.

## Project Layout

```text
proj/redp2p.c/
├── src/
│   ├── libredp2p-*.c
│   ├── libredp2p*.h
│   └── redp2p.c
├── imp/
│   ├── redp2p-idx.php
│   ├── redp2p-pub.js
│   └── redp2p-con.js
├── tests/
│   ├── test.c
│   ├── test.php
│   └── test.html
└── doc/
    └── protocol.md
```

The `imp/` directory is intentionally flat. The JavaScript pub/con files are
standalone implementations; WebRTC plumbing remains private inside them rather
than being exposed as an additional public implementation file.

## Design Principles

### One REDP2P protocol

REDP2P has three capability roles:

```text
idx
pub
con
```

Those roles are independent from the implementation language and transport.

The intended implementation matrix is:

```text
idx
    C
    PHP

pub
    C   -> TCP / UDP
    JS  -> RTC

con
    C   -> TCP / UDP
    JS  -> RTC
```

All implementations must speak the same REDP2P index protocol.

### Public APIs express intent

Public consumers must not be exposed to transport plumbing such as hole punching, candidate exchange, SDP, ICE, KCP, poll loops, socket readiness, or internal ports.

Public concepts remain capability-oriented:

```text
idx
pub
con
receive
respond
send
close
```

### Publisher selects the published transport

Native publishers explicitly select TCP or UDP. Browser publishers use RTC because that is the browser implementation's available peer transport.

The consumer does not select a transport. It expresses only: connect to publisher X.

### The index coordinates; it never relays application data

The index owns registry, admission, lookup, temporary connection state, publisher lifetime, and policy enforcement. Application payloads remain peer-to-peer.

## Compatibility

The native CLI remains unchanged:

```bash
redp2p idx 9876
redp2p pub web@idx.example.com:9876 --tcp 8080
redp2p pub game@idx.example.com:9876 --udp 7777
redp2p con web@idx.example.com:9876 9000
```

Existing native TCP/UDP behavior and existing browser RTC behavior are compatibility requirements.

## Unified publisher record

The index records the publisher transport:

```text
publisher foo -> TCP
publisher bar -> UDP
publisher baz -> RTC
```

The transport belongs to the publisher record, not the consumer request.

## Unified connection model

Native punching and RTC signaling are two implementations of the same connection lifecycle:

```text
consumer requests publisher
        ↓
index creates temporary connection state
        ↓
publisher receives pending connection
        ↓
publisher and consumer exchange transport-specific setup data
        ↓
direct peer channel becomes usable
        ↓
index leaves the application data path
```

The common lifecycle is unified. Transport-specific payloads remain transport-specific.

## Unified index implementations

The end state must have interchangeable:

```text
libredp2p-idx.c
redp2p-idx.php
```

Both coordinate TCP, UDP, and RTC publishers and implement the same wire protocol.

## JavaScript implementation

The current browser implementation becomes the JavaScript implementation of REDP2P:

```text
imp/redp2p-pub.js
imp/redp2p-con.js
```

Both files are standalone. Shared WebRTC/index plumbing is internal to those
implementations and does not appear as a separate public `core` file.

The normal API keeps `receive`, `respond`, `send`, and `close`, without requiring WebRTC vocabulary.

## Native direct data model

Native `pub` and `con` gain the same capability-level data model:

```text
pub
    receive
    respond

con
    receive
    send
```

The exact C ABI must be designed from the natural JS/Lua shape first. TCP stream semantics and UDP datagram boundaries must be preserved.

Internal transport ports should be ephemeral/private where they are not part of user intent.

The existing CLI can adapt this direct data API back to the current local-port behavior.

## STUN

STUN remains an optional REDP2P configuration concept. Native maps it to native candidate discovery; browser maps it to WebRTC ICE configuration. ICE remains an implementation detail.

## Unsupported cross-family connections

Initial unification does not require native peers to implement RTC or browsers to implement raw TCP/UDP.

Initially supported:

```text
C con  -> TCP pub
C con  -> UDP pub
JS con -> RTC pub
```

Initially unsupported:

```text
C con  -> RTC pub
JS con -> TCP pub
JS con -> UDP pub
```

These must fail cleanly without asking the consumer to choose a transport.

## Implementation Phases

1. Freeze current native, PHP, browser, and CLI behavior in tests.
2. Compare native and Web protocols operation by operation.
3. Write one `doc/protocol.md` as the source of truth.
4. Add publisher transport to the common registry model.
5. Introduce one common temporary connection lifecycle with transport-specific payloads.
6. Update `libredp2p-idx.c` to coordinate native and RTC connection state.
7. Merge native PHP index and Web PHP index into one `redp2p-idx.php`.
8. Move JS pub/con onto the unified protocol.
9. Add native direct `receive/respond/send` data behavior.
10. Adapt the existing CLI over the refactored native library without changing CLI syntax.
11. Validate mechanical JS/Lua projections.
12. Run the full interoperability matrix.
13. Retire `redp2p-web` as a separate protocol only after parity is complete.

## Interoperability matrix

The following combinations must pass:

```text
native pub + native con + C index
native pub + native con + PHP index
JS pub + JS con + C index
JS pub + JS con + PHP index
```

A single index must simultaneously hold TCP, UDP, and RTC publisher records.

## Non-goals

This refactor does not initially:

- add native WebRTC to `libredp2p.c`;
- add raw TCP/UDP sockets to browsers;
- add relay behavior to the index;
- expose signaling or punching publicly;
- replace the CLI contract;
- introduce a generic transport framework;
- create dependencies between kclibs.

## Completion Criteria

The refactor is complete when:

1. REDP2P has one documented index protocol.
2. C and PHP indexes implement it.
3. Native TCP/UDP pub/con work through either index.
4. Browser RTC pub/con work through either index.
5. One index can host TCP, UDP, and RTC publishers simultaneously.
6. Consumers still select only publisher IDs.
7. Native applications can consume REDP2P data through capability-level receive/send/respond operations.
8. The native CLI remains compatible.
9. PoW, VIPs, seats, admission, heartbeat, expiry, lookup, list, deregistration, and limits are unified protocol behavior.
10. `redp2p-web` no longer needs to exist as a separate protocol.

## Core Rule

```text
The publisher describes what it publishes.
The consumer says who it wants to reach.
The index coordinates the connection.
The implementation owns the transport mechanics.
```

TCP, UDP, and RTC are implementation paths inside REDP2P, not separate REDP2P products.
