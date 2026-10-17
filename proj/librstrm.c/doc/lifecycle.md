# Reliable stream lifecycle

`librstrm.c` turns one exclusive bidirectional datagram channel into one
reliable ordered byte stream.

## Transport contract

The caller owns the underlying transport for the entire rstrm lifetime.
Datagram boundaries must be preserved in both directions.

Outgoing datagrams are delivered to the callback supplied to `kc_rstrm_open()`.
Each callback invocation is one complete datagram and must be consumed or copied
before the callback returns. Returning nonzero reports a terminal transport
failure.

Each incoming transport datagram is supplied once with `kc_rstrm_input()`.
The library copies accepted datagrams before returning. The channel may lose,
duplicate, delay, or reorder datagrams.

One datagram channel carries one rstrm session. Multiplexing several rstrm
sessions over one channel is outside this API.

## Byte stream

`kc_rstrm_send()` accepts bytes, not messages. Send-call boundaries are not
preserved. The receive callback observes the same byte sequence in the same
order, but callback boundaries may differ from send boundaries.

The library performs protocol scheduling and retransmission internally; callers
do not run a timer or update loop.

## Backpressure

`kc_rstrm_send()` returns `KC_RSTRM_EFULL` when accepting more bytes would exceed
the local send capacity. This is not a terminal error. The caller should pause
its producer and resume after the optional drain callback fires.

`kc_rstrm_input()` can also return `KC_RSTRM_EFULL` when its input queue is
saturated. The caller may drop that datagram; reliable delivery causes missing
stream data to be retransmitted.

## Graceful shutdown

`kc_rstrm_shutdown()` ends the local write side after all bytes previously
accepted by `kc_rstrm_send()`. Repeated shutdown calls are harmless. Further
send calls return `KC_RSTRM_ECLOSED`.

Shutdown is half-duplex. Incoming peer bytes remain valid after local shutdown.
When the peer performs its own graceful write shutdown, all preceding bytes are
delivered before the eof callback.

`kc_rstrm_close()` is local and abortive. It releases the session without
waiting for queued bytes or shutdown to reach the peer. Use shutdown first when
graceful write completion matters.

## Failure

Malformed protocol input, local allocation failure, or rejection by the
underlying output transport puts the session into a terminal failure state. The
optional error callback receives one public rstrm status. Further send and input
operations return `KC_RSTRM_ECLOSED`.

## Threading

Send, input, shutdown, and close may be invoked while the internal protocol
worker is active. User callbacks are serialized by that worker. Close may be
called from a callback; destruction is deferred until the callback returns.
