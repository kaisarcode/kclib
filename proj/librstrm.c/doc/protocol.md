# rstrm wire protocol

The current protocol is version 1 and uses KCP as its reliable ordered record
transport.

## Channel

Both endpoints are symmetric. There is no client/server role and no rstrm
handshake. The application must establish an exclusive bidirectional datagram
channel before opening the stream.

KCP conversation value `0x3153524b` identifies protocol version 1. On the wire
its little-endian bytes are `KRS1`.

The default emitted datagram MTU is 1200 bytes. Applications may select another
MTU accepted by the public API; peers should use compatible channel limits.

KCP uses a 64-segment send window, 128-segment receive window, a 20 ms update
interval, fast-resend value 2, and normal congestion control. These are protocol
implementation parameters, not public API settings.

## Records

KCP message mode carries ordered rstrm records. The first byte identifies the
record type.

```text
DATA = 0x01 || non-empty bytes
FIN  = 0x02
```

A public send can be split into multiple DATA records. Multiple public sends can
therefore be delivered through any receive-callback boundaries; only byte order
is significant.

FIN is queued after every DATA record accepted before local shutdown. Because it
uses the same reliable ordered channel, the peer observes all preceding stream
bytes before eof.

A DATA record after FIN, a repeated FIN, an empty DATA record, an unknown record
type, or malformed KCP input is a protocol error.

## Scope

The protocol provides reliability, ordering, retransmission, congestion/window
behavior, and graceful write shutdown. It does not provide path discovery,
keepalive policy, peer identity, encryption, authentication, compression, or
multiplexing.
