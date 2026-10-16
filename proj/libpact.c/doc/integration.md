# Pact integration guide

`libpact.c` establishes persistent endpoint relationships and protects binary
messages. This guide explains the identifiers and transport responsibilities
that are easy to misuse when looking only at function signatures.

## Establish trust

The relationship is established in three application-mediated steps.

The inviting endpoint calls `kc_pact_invite()` and receives:

- its local relationship UID;
- an invitation code.

The application transports the invitation code to the invited endpoint.

The invited endpoint calls `kc_pact_join()` and receives:

- its own local relationship UID;
- a confirmation value.

The application transports the confirmation back to the inviting endpoint.

The inviting endpoint calls `kc_pact_confirm()` to complete the relationship.

The UID returned by `kc_pact_invite()` and the UID returned by
`kc_pact_join()` are different because each identifies the remote endpoint from
the other side of the relationship.

## Protect a message

`kc_pact_seal()` receives the remote UID known by the sending endpoint and
returns allocated protected bytes.

The application transports that UID together with the protected bytes.

On the receiving endpoint, the same UID is local and is supplied to
`kc_pact_size()` or `kc_pact_unseal()`.

The reverse direction uses the other endpoint UID in the same way.

## Stream framing

Protected messages are self-sized, but the library does not own transport
buffers.

When data arrives through a byte stream, accumulate leading bytes and call
`kc_pact_size()`. `KC_PACT_INCOMPLETE` means more leading bytes are required.

When the call returns `KC_PACT_OK`, the returned size is the number of bytes
that form one complete protected message. Consume exactly that amount before
processing the next message.

`kc_pact_size()` authenticates and decrypts Pact's encrypted logical-length
record. It does not parse a transport header.

## Transport responsibilities

Pact does not implement:

- networking or delivery;
- message ordering;
- retries or timeouts;
- buffering;
- replay detection;
- application authorization.

A valid protected message can be supplied to `kc_pact_unseal()` more than once.
Applications that require replay prevention must provide that policy.

## Persistence and revocation

The trust store is persistent. Closing a `kc_pact_t` handle does not delete
relationships.

`kc_pact_revoke()` receives the remote UID known by the local endpoint and
removes that relationship.

Normal callers do not choose a storage directory. `KC_PACT_DIR` is an advanced
process-level override intended for tests and controlled deployments.

## Ownership

Strings and buffers returned by Pact are caller-owned. Release them with
`kc_pact_free()`.

Messages can contain arbitrary binary data up to `KC_PACT_MAX_MESSAGE`.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libpact.h) for the complete C interface.
