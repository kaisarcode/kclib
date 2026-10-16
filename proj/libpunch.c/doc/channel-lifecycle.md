# Direct channel lifecycle

`libpunch.c` can establish a channel with a socket that it owns or over a live
UDP mapping supplied by the caller. The ownership distinction matters for
correct integration.

## Library-owned UDP socket

`kc_punch_connect()` binds its own local UDP socket and owns that socket for the
lifetime of the returned path.

The caller supplies candidate peer endpoints. PUNCH performs probing and
selects a reachable path.

Closing the path releases the library-owned channel resources.

## Borrowed UDP mapping

`kc_punch_connect_over()` performs the same connectivity process over a live UDP
mapping supplied by the caller.

The mapping is borrowed. PUNCH does not close it.

The caller must keep the mapping alive until after the PUNCH path is closed.

PUNCH consumes incoming probe and peer datagrams while the path is alive. The
supplied mapping must therefore have one active receive owner during that
period.

## Candidate and path behavior

For either connection mode, the application provides the candidate remote
endpoints and a connectivity timeout.

PUNCH owns:

- candidate probing;
- simultaneous punch behavior;
- selected-path tracking;
- private keepalive traffic;
- peer-liveness detection.

The application does not manage probe phases or candidate-pair state.

## Application datagrams

`kc_punch_send()` sends one UDP datagram over the selected channel.

`kc_punch_receive()` returns one complete application datagram. A receive
timeout of zero waits indefinitely.

UDP does not guarantee delivery, ordering, or retransmission.

## Channel lifetime

The returned path represents a live bidirectional channel. PUNCH can exchange
private control traffic while the application is otherwise idle.

When peer liveness is lost, the channel enters a failure state.

The local and remote endpoint accessors describe the path that was actually
selected. They do not indicate how the application discovered those endpoints.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libpunch.h) for the complete C interface.
