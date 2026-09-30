# REDP2P Index Protocol

## Purpose

REDP2P has one index protocol for all implementations.

The index coordinates three roles:

```text
idx
pub
con
```

The index never relays application payloads. After connection establishment,
application data travels directly between peers.

Publisher implementations currently provide:

```text
native C       TCP / UDP
JavaScript     WebRTC
```

The protocol does not require a particular implementation language for the
index. Any index implementation is valid if it preserves the wire contract and
transport-independent behavior defined in this document.

Consumer implementations select a publisher by `id`. They do not select the
transport. The publisher record determines the connection path.

## Common registry model

Every active publisher record has:

```text
id
transport
control secret
control sequence
last_seen
rate-limit state
```

Transport values are:

```text
tcp
udp
rtc
```

Native records additionally carry the endpoint metadata required for native
peer establishment. RTC records carry no persistent SDP/ICE state; RTC
signaling belongs to temporary connection state.

For compatibility, native requests continue to use the existing `proto`
encoding:

```text
1 = TCP
2 = UDP
```

Indexes normalize those values internally to `tcp` and `udp`.

## Common policy

The following behavior is transport-independent and must be equivalent in every
index implementation:

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

`challenge` is common to native and RTC publishers.

The canonical challenge and PoW constructions remain the existing REDP2P
constructions.

A native-capable index may include native registration key-agreement material
such as `pkey` in the response. Implementations that do not need a field ignore
it.

## Registration

`register` creates one publisher record.

Native publishers keep the existing REDP2P registration mechanism, including
the protected initial control secret, `proto`, `udp_port`, and candidates.

RTC publishers identify the registration with:

```json
{
  "transport": "rtc"
}
```

RTC uses the JavaScript/WebRTC-compatible registration proof and sends the
initial control secret only over the HTTPS-protected index request.

Both registration forms apply the same PoW, admission, VIP, seat, active-ID,
TTL, and control-session policies.

## Heartbeat

Heartbeat refreshes publisher lifetime.

Native heartbeat retains the native endpoint update and its canonical proof.

RTC heartbeat uses:

```text
heartbeat
<id>
<seq>
```

with HMAC-SHA256 under the publisher control secret.

The index determines which canonical heartbeat form applies from the stored
publisher transport. The caller does not negotiate it.

## Lookup

`lookup` returns the active publisher and its transport.

Common response fields:

```json
{
  "ok": true,
  "id": "site",
  "transport": "rtc",
  "last_seen": 1700000000
}
```

Native publishers additionally return the existing `proto`, `udp_port`, and
`candidates` fields.

Unknown response fields are ignored by clients.

## List

`list` returns active publisher IDs exactly as before.

## Connection lifecycle

All transports follow the same logical lifecycle:

```text
consumer requests publisher
        ↓
index allocates temporary connection state
        ↓
publisher receives pending connection
        ↓
publisher and consumer exchange transport-specific setup data
        ↓
direct peer channel becomes usable
        ↓
temporary index state expires or is consumed
```

Transport-specific setup data is private protocol plumbing.

### Native TCP / UDP

Native implementations retain the existing:

```text
punch_req
punch_poll
candidate exchange
hole punching
native session establishment
```

### RTC

RTC implementations retain the WebRTC signaling flow:

```text
connect
publisher poll
answer
consumer poll
```

`connect` is valid only for an RTC publisher.

The publisher-authenticated `poll` and `answer` operations are valid only for
RTC publisher records.

Consumer poll uses the temporary unguessable capability associated with one RTC
connection.

## Pending limits and rate limiting

Native pending calls and RTC pending signaling sessions share the same logical
capacity policy.

Per-publisher and global pending limits count pending work regardless of
transport.

Connection attempts share the same source and target token-bucket policy.

## Deregistration

`deregister` is common to all publisher transports.

A successful deregistration removes the publisher and all temporary connection
state addressed to it, including both native punch state and RTC signaling
state.

## STUN and TURN

STUN and TURN are optional connection infrastructure, not index semantics.

Native implementations use STUN for candidate discovery and may advertise a
TURN `relay` candidate. Direct native UDP connectivity remains preferred;
TURN is a transparent fallback when the native peer path cannot be established.
The native TCP capability keeps application stream semantics only at the
endpoints. Peer transport is always datagram based: REDP2P uses UDP directly
or through TURN, and KCP reconstructs an ordered byte stream for the local
stream adapters. TCP sockets, TCP FIN, half-close, and EOF are not peer
transport semantics.

Stream CLOSE/CLOSE_ACK frames are REDP2P session-lifecycle control messages.
They do not represent TCP packets and are not exposed as application data or
receive-callback EOF events.

Native candidate objects use:

```text
host
observed
relay
```

The index exchanges relay candidates but never carries application payloads.
TURN relays the same REDP2P session datagrams that would otherwise travel
directly between peers.

JavaScript/WebRTC implementations pass STUN/TURN infrastructure to WebRTC ICE
configuration. ICE, SDP, direct-vs-relay selection, and native TURN state remain
internal details; applications continue to use the same REDP2P capability API.

## Compatibility

Native TCP/UDP protocol operations remain accepted while the unified protocol is
introduced.

JavaScript/WebRTC requests may continue to include `version: 0`; indexes
ignore unknown fields unless a field is explicitly required by an operation.

## Unsupported cross-family connections

Initial implementations support:

```text
C con  -> TCP pub
C con  -> UDP pub
JS con -> RTC pub
```

A consumer that cannot implement the publisher transport fails explicitly. It
does not ask the user to select a different transport.
