# REDP2P Protocol

## Purpose

This document defines the interoperable REDP2P coordination contract between
three roles:

```text
index
publisher
consumer
```

The protocol defines how publishers become reachable through an index, how
consumers locate or request a connection to a publisher, and how the index
coordinates peer establishment.

It does not define an implementation language, storage backend, thread model,
process model, public API, or application payload format.

The index never relays application payloads. It only carries registration,
discovery, authentication, candidate, and signaling metadata. After peer
establishment, application data travels over the selected peer transport or an
external relay such as TURN.

## Implementations and compatibility

REDP2P currently has two peer transport families:

```text
Native   tcp / udp
WebRTC   rtc
```

They share the same index namespace and the same common publisher lifecycle,
but they use different peer-establishment protocols.

The current implementations are compatible as follows:

| Implementation | Index | Publisher | Consumer |
| --- | --- | --- | --- |
| C | Native + WebRTC | Native | Native |
| PHP | Native + WebRTC | - | - |
| JavaScript/browser | - | WebRTC | WebRTC |

Compatibility means that a Native C publisher or consumer can use either the C
or PHP index, and a browser WebRTC publisher or consumer can also use either the
C or PHP index.

It does not mean that Native and WebRTC peers are interchangeable. A Native
consumer connects to a Native publisher. A WebRTC consumer connects to a WebRTC
publisher. A transport-specific connection request against the wrong publisher
family must fail with `unsupported_transport`.

One index may contain Native and WebRTC publishers at the same time. Publisher
IDs occupy one shared namespace, so one active ID cannot simultaneously refer
to different transport families.

## Index endpoint

The protocol does not define a fixed URL path such as `/redp2p`.

A client sends protocol requests to the configured index endpoint. A native
index listener may use the listener root directly; a hosted implementation may
expose the same protocol through any configured URL.

Requests use HTTP `POST` with a JSON object body. Browser-facing index
implementations also support `OPTIONS` for CORS.

Successful JSON responses contain:

```json
{"ok":true}
```

and may contain operation-specific fields.

Protocol errors use:

```json
{"ok":false,"error":"error_code"}
```

with an appropriate non-success HTTP status.

Duplicate top-level JSON fields are invalid. Implementations may ignore unknown
fields, but all required fields and transport-specific constraints still apply.

## Identifiers and control state

Publisher and consumer identifiers are ASCII alphanumeric strings from 1 to 63
characters.

Every registered publisher has transport-independent control state:

```text
id
transport
control secret
control sequence
last_seen
```

The control sequence starts at zero after a new registration. Authenticated
publisher operations increment it monotonically. Values sent on the wire must
be positive integers representable exactly by JavaScript, up to
`9007199254740991`.

A proof with a repeated or older sequence is invalid. This prevents replay of
publisher control operations.

The index may keep registry state in memory, a database, or another storage
backend. Persistence across index restarts is not required by the wire
protocol. A publisher whose previous registration no longer exists must perform
a fresh registration.

## Common publisher lifecycle

Both Native and WebRTC publishers use the same high-level lifecycle:

```text
challenge
    ↓
register
    ↓
heartbeat + transport-specific publisher control
    ↓
deregister
```

The common policy includes:

- publisher ID validation;
- registration challenge lifetime;
- proof of work;
- optional admission passwords;
- VIP reservations;
- seat limits;
- active-ID replacement protection;
- publisher control secrets;
- monotonic control sequences;
- publisher TTL;
- lookup and list;
- deregistration;
- source and target connection rate limiting;
- per-publisher pending limits;
- global pending limits.

These policies apply independently of the peer transport family.

## Challenge

`challenge` begins publisher registration.

Request:

```text
op = challenge
id
```

A successful response provides:

```text
nonce
issued_at
expires_at
mac
pkey
bits
```

The challenge is valid for 60 seconds. `bits` is the required registration
proof-of-work difficulty and is in the range 0 through 32.

The challenge MAC authenticates the nonce and timestamps as index-issued data.
The proof of work binds the challenge to the requested publisher ID.

`pkey` is registration key-agreement material used by Native registration.
WebRTC registration shares the same challenge response but does not require the
Native key-agreement step.

All interoperable implementations use the same challenge, proof-of-work, and
admission-proof domains. Their canonical transcripts are wire protocol, even
though each implementation is free to construct them in its own way.

## Registration

`register` creates an active publisher record after validating the challenge,
proof of work, registration proof, admission policy, capacity, and active-ID
state.

The registration establishes a publisher control secret. Subsequent publisher
control operations prove possession of that secret and use the monotonic
sequence.

The optional index password or VIP password is never the publisher control
secret. When admission is enabled, the publisher sends an admission proof bound
to the registration transcript rather than using the password as ongoing
session state.

Native and WebRTC registration use different transport transcripts.

### Native registration

Native registration carries the Native service metadata:

```text
proto
udp_port
candidates
```

Protocol values are:

```text
1 = TCP
2 = UDP
```

The public publisher transport becomes `tcp` or `udp` from this value.

Native registration uses the challenge key-agreement material to protect the
publisher control secret while registering it with the index. The registration
proof binds the secret and Native endpoint metadata to the challenge.

### WebRTC registration

WebRTC registration explicitly selects:

```text
transport = rtc
```

It does not register Native `proto`, UDP port, or Native candidate metadata.
Its registration proof binds the WebRTC publisher control secret to the common
challenge and proof-of-work result.

## Heartbeat

`heartbeat` refreshes an active publisher and advances its authenticated control
sequence.

For a Native publisher, heartbeat also updates the Native endpoint state:

```text
proto
udp_port
candidates
```

A Native heartbeat may update TCP/UDP Native metadata, but it does not convert
the publisher into WebRTC.

For a WebRTC publisher, heartbeat refreshes the registration and sequence only.
ICE state is not stored as publisher lookup metadata.

A missing publisher returns `not_found`. A stale or invalid sequence/proof is
rejected.

Current interoperable implementations use a publisher TTL of 120 seconds and
heartbeat substantially more frequently than that lifetime.

## Lookup

`lookup` resolves one active publisher by ID.

Every successful lookup identifies the publisher family with:

```text
id
transport
last_seen
```

`transport` is one of:

```text
tcp
udp
rtc
```

Native lookup additionally returns:

```text
proto
udp_port
candidates
```

WebRTC lookup does not return SDP, ICE candidates, offers, or answers. WebRTC
signaling is temporary connection state and uses the RTC connection lifecycle
instead.

A Native consumer uses lookup to learn whether the publisher exposes TCP or UDP
application semantics and to obtain the publisher candidates before starting a
punch request.

A WebRTC consumer may use lookup for discovery or validation, but the WebRTC
`connect` operation also validates that the target publisher is `rtc`.

## List

`list` returns the IDs of active, non-expired publishers.

The list spans the shared publisher namespace and may therefore contain Native
and WebRTC publishers together. Transport metadata is obtained with `lookup`.

## Deregistration

`deregister` removes one publisher after validating a new monotonic sequence and
a control proof.

Pending connection state associated with that publisher is temporary. An index
may reclaim it immediately when the publisher is removed or allow the normal
pending-state lifetime to expire it, but it must not turn that state into an
active connection after the publisher is gone.

## Pending state and connection limits

Both Native introductions and WebRTC signaling use bounded temporary index
state.

The current interoperable limits are:

```text
pending lifetime                 30 seconds
maximum returned per poll         4
maximum pending per publisher    32
maximum pending globally       4096
```

Connection attempts are also subject to source and target rate limiting. The
rate limiter protects the index and publisher from unbounded connection setup;
it has no role in established peer application traffic.

Pending state is coordination metadata only and never contains application
payloads.

## Native transport

## Native model

Native REDP2P exposes either TCP stream semantics or UDP datagram semantics to
the application, while the peer transport remains datagram based in both modes.

For Native TCP:

```text
application stream
    ↓
REDP2P stream adapter
    ↓
KCP
    ↓
UDP peer transport
```

The receiving endpoint reverses the transformation. KCP and stream lifecycle
control belong to the Native peer transport and are not application payload.

Native UDP preserves application datagram boundaries directly over the REDP2P
peer session.

## Native candidates

Native candidate objects have:

```text
type
addr
port
```

Candidate types are:

```text
host
srflx
observed
relay
```

A publisher or consumer may submit `host`, `srflx`, and `relay` candidates.
`observed` is index-derived only and must not be accepted as client-supplied
replacement metadata.

`srflx` is the server-reflexive UDP endpoint learned through STUN.

`observed` is derived by the index from the trusted HTTP request source address
combined with the declared peer UDP port. It is not equivalent to STUN and does
not prove the external UDP port mapping discovered by STUN.

`relay` represents a TURN relay endpoint. The index exchanges relay candidates
but does not operate the TURN relay.

## Native connection lifecycle

The standard Native consumer flow is:

```text
lookup publisher
        ↓
learn tcp/udp and publisher candidates
        ↓
gather consumer candidates
        ↓
punch_req
        ↓
index stores bounded pending introduction
        ↓
publisher punch_poll
        ↓
publisher receives consumer candidates
        ↓
peers perform hole punching / relay path selection
        ↓
Native session becomes usable
```

### punch_req

A Native consumer submits:

```text
self_id
target_id
session
udp_port
candidates
```

The index verifies that the target exists and is Native, applies rate and
pending-state limits, derives the consumer `observed` candidate from the HTTP
source when possible, and queues the introduction.

The consumer already has the publisher candidates from `lookup`; `punch_req`
does not relay application data and does not create the peer path itself.

### punch_poll

A Native publisher polls with its publisher ID, a new control sequence, and a
proof.

The index returns a bounded `calls` array. Each call contains:

```text
self_id
session
candidates
```

The returned candidates belong to the requesting consumer. Returned Native
pending calls are consumed by the poll.

After this exchange the index is no longer part of peer path establishment.
The two Native peers select a direct or TURN path and complete the Native
session handshake directly.

## Native STUN and TURN

STUN and TURN are optional Native connection infrastructure, not index
semantics.

STUN contributes `srflx` candidates. TURN contributes `relay` candidates.
Direct connectivity remains preferred when available; TURN may carry the same
REDP2P datagrams when a direct UDP path cannot be established.

The index never receives TURN credentials and never relays REDP2P application
traffic.

For Native TCP, TURN still carries REDP2P/KCP datagrams. TCP sockets, TCP FIN,
half-close, and EOF are endpoint semantics rather than peer transport packets.

## WebRTC transport

## WebRTC model

WebRTC publishers register with `transport = rtc` and use the index only for
SDP signaling.

ICE, STUN, TURN, DTLS, and the WebRTC data channel are peer/runtime concerns.
The index stores signaling state temporarily but does not act as an ICE, STUN,
TURN, or data relay.

Application payload travels through the established WebRTC data channel.

## WebRTC connection lifecycle

The WebRTC flow is:

```text
consumer creates offer and gathers ICE
        ↓
connect
        ↓
index stores offer and creates connection capability
        ↓
publisher authenticated poll
        ↓
publisher receives offer and creates answer
        ↓
answer
        ↓
consumer capability poll
        ↓
consumer receives answer
        ↓
WebRTC data channel becomes usable
```

### connect

The consumer submits:

```text
id
offer
```

`offer` is a WebRTC session description with:

```text
type = offer
sdp
```

The index verifies that the target exists and is `rtc`, applies the shared rate
and pending-state limits, stores the offer, and returns:

```text
connection
capability
expires_at
```

`connection` identifies the temporary signaling exchange.

`capability` authorizes the consumer to poll that exchange. The index keeps a
hash of the capability rather than needing the raw capability as stored
identity state.

### publisher poll

The WebRTC publisher sends `poll` with:

```text
id
seq
proof
```

The proof belongs to the same publisher control sequence used by heartbeat,
answer, and deregistration.

The index returns a bounded `connections` array containing pending:

```text
connection
offer
```

Only unanswered, non-expired offers are returned.

### answer

The publisher accepts an offer, creates its WebRTC answer, and sends:

```text
id
connection
answer
seq
proof
```

The proof binds the publisher control sequence to the connection identifier and
a digest of the answer description. This prevents substitution of signaling
content under a valid publisher sequence.

`answer` has:

```text
type = answer
sdp
```

Session-description SDP is non-empty and bounded to 49152 bytes.

### consumer poll

The consumer polls with:

```text
connection
capability
```

Before the publisher answers, the successful response contains:

```text
answer = null
```

After a valid answer is stored, the consumer receives the answer and the
completed pending signaling record is consumed.

The consumer capability is independent of the publisher control secret. It
only authorizes access to that one temporary WebRTC connection exchange.

## WebRTC STUN and TURN

WebRTC STUN and TURN configuration is applied to the WebRTC peer connection.
The resulting ICE information is represented inside the SDP exchanged through
`connect` and `answer`.

Unlike Native mode, the index does not expose WebRTC ICE candidates through the
Native candidate object format.

TURN credentials are peer configuration and are not index credentials.

## Interoperability rules

An implementation is REDP2P-compatible when it preserves the observable
protocol behavior for the roles and transport families it implements.

In particular:

- index storage technology is not part of the protocol;
- publisher and consumer public APIs are not part of the protocol;
- Native and WebRTC peer transports do not have to share an implementation;
- the common publisher registry and control lifecycle must behave consistently;
- transport-specific operations must reject publishers from another family;
- the index must never become an application-data relay;
- challenge, registration, admission, and control proofs must remain wire
    compatible;
- publisher sequences must remain strictly monotonic;
- temporary connection state must remain bounded and expire;
- `lookup` must expose enough transport information for a consumer to select or
    validate the correct connection family.

The C and PHP indexes are therefore interchangeable at the index-protocol
boundary, while C Native peers and browser WebRTC peers remain intentionally
separate peer transport implementations.
