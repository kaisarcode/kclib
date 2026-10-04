# REDP2P Index Protocol

## Purpose

REDP2P has one native index protocol for its implementations.

The index coordinates three roles:

```text
idx
pub
con
```

The index never relays application payloads. After connection establishment,
application data travels through the peer path selected by REDP2P.

Native publishers provide TCP or UDP application semantics. The peer transport
is datagram based in both cases; TCP mode uses KCP and endpoint stream adapters.

The protocol does not require a particular implementation language for the
index. Any index implementation is valid if it preserves the wire contract and
behavior defined in this document.

Consumers select a publisher by `id`. They do not select TCP or UDP. The
publisher record determines the service protocol.

## Publisher registry

Every active publisher record has:

```text
id
control secret
control sequence
last_seen
proto
udp_port
candidates
rate-limit state
```

Protocol values are:

```text
1 = TCP
2 = UDP
```

The index stores the endpoint metadata required for native peer establishment.

## Common policy

The following behavior is part of the REDP2P index contract:

- publisher IDs;
- challenge lifetime;
- registration PoW;
- admission passwords;
- VIP reservations;
- seats;
- active-ID replacement protection;
- publisher control secrets;
- monotonic control sequences;
- heartbeats;
- publisher TTL;
- lookup;
- list;
- deregistration;
- connection rate limiting;
- per-publisher pending limits;
- global pending limits.

## Challenge

`challenge` starts native publisher registration.

The response provides the challenge nonce, timestamps, PoW difficulty, MAC, and
registration key-agreement material required by the native registration flow.

The canonical challenge and PoW constructions are part of the REDP2P wire
contract and must be reproduced exactly by alternate index implementations.

## Registration

`register` creates one native publisher record.

Registration protects the initial control secret and includes:

```text
proto
udp_port
candidates
```

PoW, admission password, VIP, seat, active-ID, TTL, and control-session policies
are applied before the publisher becomes visible.

## Heartbeat

`heartbeat` refreshes publisher lifetime and updates the native endpoint data.

The request is authenticated with the publisher control secret and a strictly
increasing control sequence. The heartbeat carries the current protocol, UDP
port, and candidate set used by peer establishment.

## Lookup

`lookup` returns the active publisher and its native endpoint metadata.

A successful response includes the publisher identifier together with `proto`,
`udp_port`, `candidates`, and freshness information required by consumers.

Unknown response fields are ignored by clients.

## List

`list` returns active publisher IDs.

## Connection lifecycle

Native TCP and UDP use the same introduction lifecycle:

```text
consumer gathers candidates
        ↓
punch_req
        ↓
index stores bounded pending introduction
        ↓
publisher punch_poll
        ↓
index exchanges peer candidates
        ↓
peers perform hole punching / relay selection
        ↓
REDP2P session becomes usable
```

The index carries only control metadata. It never relays application payloads.

### Native TCP / UDP

Native implementations use:

```text
punch_req
punch_poll
candidate exchange
hole punching
native session establishment
```

TCP is an application-facing stream mode. REDP2P peer traffic remains UDP, and
KCP reconstructs the ordered byte stream at the endpoint adapters. UDP mode
preserves datagram boundaries.

## Pending limits and rate limiting

Pending introductions are bounded globally and per publisher.

Connection attempts share the source and target token-bucket policy. Limits
protect the index from unbounded pending state without changing normal peer
traffic after introduction.

## Deregistration

`deregister` removes one publisher after validating its monotonic sequence and
control proof.

Temporary native punch state addressed to an expired or removed publisher is
cleaned according to the index lifecycle rules.

## STUN and TURN

STUN and TURN are optional connection infrastructure, not index semantics.

Native implementations use STUN for candidate discovery and may advertise a
TURN `relay` candidate. Direct native UDP connectivity remains preferred; TURN
is a transparent fallback when the native peer path cannot be established.

The native TCP capability keeps application stream semantics only at the
endpoints. Peer transport is always datagram based: REDP2P uses UDP directly or
through TURN, and KCP reconstructs an ordered byte stream for the local stream
adapters. TCP sockets, TCP FIN, half-close, and EOF are not peer transport
semantics.

Stream CLOSE/CLOSE_ACK frames are REDP2P session-lifecycle control messages.
They do not represent TCP packets and are never encoded into application data.
A CLOSE ends only the sender's stream direction after queued KCP data has been
acknowledged; the opposite direction may remain active. Native endpoint adapters
translate that control state to local socket shutdown and expose it separately
from data callbacks.

Native candidate objects use:

```text
host
observed
relay
srflx
```

Client-supplied candidate lists may contain `host`, `srflx`, and `relay`. An
`observed` candidate is index-derived only and must not be accepted as a
client-supplied replacement.

A `srflx` candidate is the server-reflexive UDP endpoint discovered by the peer
through STUN. An `observed` candidate is derived by the index from the trusted
request source address together with the declared UDP port. They are not
equivalent: the index-observed address does not prove the public UDP port
mapping that STUN discovered.

The index exchanges relay candidates but never carries application payloads.
TURN relays the same REDP2P session datagrams that would otherwise travel
directly between peers.

## Compatibility

REDP2P publishers and consumers use only the native TCP/UDP protocol defined in
this document. Implementations may include an optional `version` field where
supported; unknown fields are ignored unless a field is explicitly required by
an operation.
