# STUN Binding lifecycle

`libstun.c` has two operations: `client` and `server`. Both work over owned UDP
or a caller-provided transport. No other kclib is required.

## Client

`kc_stun_client()` starts an RFC 8489 Binding transaction and returns a client
handle. Use `kc_stun_client_poll()` to send the request, pace retransmissions,
receive on an owned socket and report completion. `KC_STUN_PENDING` means the
transaction is running. A successful transaction yields `client_mapped()`.
The client retains its UDP mapping until `client_close()`.

`client_refresh()` starts another transaction on the same mapping, with a new
transaction identifier and integrity values. `client_send()` sends application
UDP datagrams; `client_read()` receives them on owned transports after the
transaction completes.

The client validates the response transaction ID, source endpoint, message
integrity and fingerprint. Authentication and `487` error responses are returned
to the caller. Unknown extension attributes in responses remain available via
`client_attributes()`.

## Server

`kc_stun_server()` listens for Binding requests. For owned UDP the library
accepts requests in its own worker, responds with `XOR-MAPPED-ADDRESS` and closes
its sockets after `server_close()`. For borrowed UDP the caller delivers each
incoming request to `server_receive()` and owns all socket lifetime and I/O.

The optional credentials callback provides the key matching a request's
`USERNAME`. The binding callback sees parsed STUN attributes and can accept or
reject the Binding request with an error code. Authenticated success and
application-rejected error responses carry verified message integrity.

## Borrowed UDP and concurrent ICE compositions

The application or composition owns **exactly one UDP receive loop**. Its
STUN-response datagrams are delivered to the matching active client transaction,
and incoming Binding requests are delivered to the server. The transport send
callback is borrowed; neither role reads it or closes it.

Client objects can be driven independently by a higher-level checklist
scheduler. STUN provides its own per-transaction retry timing; ICE supplies
candidates, credential values, policy and extension attribute values.

## Protocol

STUN Binding uses the RFC 8489 message envelope, `XOR-MAPPED-ADDRESS`, short-term
`USERNAME`, SHA-1 `MESSAGE-INTEGRITY` and SHA-256 `MESSAGE-INTEGRITY-SHA256`,
`FINGERPRINT`, `ERROR-CODE` and opaque extensions. Both IPv4 and IPv6 are
supported. The implementation does not perform ICE scheduling, nomination,
role determination or candidate selection.