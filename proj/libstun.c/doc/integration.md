# STUN codec and composition

## Message codec

STUN messages can also be handled without creating a client, server, or UDP socket.
`kc_stun_encode()` writes a caller-supplied message (type, 12-byte transaction ID,
and attributes) into a caller-owned buffer, adds requested short-term integrity
attributes, and appends FINGERPRINT. `kc_stun_decode()` parses a complete STUN
datagram and provides attribute views; the caller retains the input buffer until
`kc_stun_message_free()` releases the views.

`kc_stun_verify_integrity()` and `kc_stun_verify_fingerprint()` validate
externally received messages, while `kc_stun_transaction_matches()` checks
transaction IDs. `kc_stun_encode_mapped()` and `kc_stun_decode_mapped()`
encode and decode IPv4/IPv6 XOR-MAPPED-ADDRESS attributes using caller-owned
storage. `kc_stun_encode_error()` and `kc_stun_decode_error()` handle ERROR-CODE
attribute values. Other STUN and extension attributes use the generic attribute array,
including those used by ICE; `libstun` does not interpret ICE roles or policy.

The codec does not own UDP reception, maintain a transaction scheduler, or
automatically route responses among outstanding transactions. A composition
with multiple Binding transactions is responsible for dispatching each received
datagram to its appropriate client. The client API maintains retransmission
timers and matches the configured endpoint and transaction identifier.

## Composition

On an owned UDP socket, datagrams unrelated to the active Binding transaction are
preserved while polling and are returned by `kc_stun_client_read()` afterward.
This includes other STUN traffic, which the consuming application must
identify and dispatch rather than treating it automatically as application data.
The pending datagram queue is bounded to 64 entries.

A composition that owns UDP reception can use `kc_stun_is_message(bytes, size)`
to identify STUN message envelopes before routing packets. This operation works
on caller-owned buffers without opening sockets. It checks the STUN header,
cookie, and datagram length; it does not perform authentication or inspect ICE
semantics. The composition still dispatches messages to the appropriate
client or server instance.

A client can borrow an existing UDP sender. The composition delivers inbound
STUN responses using `kc_stun_client_receive()` and drives retransmissions using
`kc_stun_client_poll()`. `kc_stun_client_keepalive()` emits an unauthenticated
Binding Indication without waiting for a response. A server can borrow the
same sender and process inbound
Binding requests with `kc_stun_server_receive()`. The caller remains the sole
owner of UDP reception; STUN never closes borrowed transports.

A credentials callback on the server supplies a key for each username. A binding
callback may reject a request with a protocol error code (for example, 487).
STUN verifies credentials and signs authenticated responses, while the
composition determines policy and any extension attributes.

The supported transport is UDP. STUN short-term credentials are intended for
protocols such as ICE. Long-term username/realm/nonce authentication and STUN
transport variants over TCP/TLS/DTLS are outside this UDP Binding capability.

See [Binding lifecycle](mapping-lifecycle.md), the
[public header](../src/libstun.h) and the canonical
[scripting projection](../../docs/api/stun.md).

## Next step

Return to the [project README](../README.md).
