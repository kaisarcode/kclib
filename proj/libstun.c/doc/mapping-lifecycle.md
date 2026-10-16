# STUN mapping lifecycle

`libstun.c` exposes both a discoverable live UDP mapping and an autonomous STUN
Binding endpoint. This page describes the lifetime and datagram behavior shared
by those roles.

## Discover a mapping

`kc_stun_discover()` creates a UDP mapping, sends an RFC 5389 Binding Request,
and returns only after the STUN server reports the externally observed
endpoint.

The same local UDP socket remains open until `kc_stun_close()`. Application
traffic therefore uses the same mapping that STUN observed.

The server value can use `host:port`, `[IPv6]:port`, or a host without a port.
A host without a port uses STUN port 3478. An optional `stun:` prefix is
accepted.

The local port can be zero to request an operating-system-selected port.

## Local and mapped endpoints

The local endpoint accessors describe the socket bound by the application
host.

The mapped endpoint accessors describe the address observed by the STUN server.

IPv4 and IPv6 mappings are supported. An explicit local host must be compatible
with the address family selected for the STUN server.

## Refresh

`kc_stun_refresh()` sends another Binding Request over the same mapping without
blocking for the response.

STUN responses from the configured server are consumed internally by
`kc_stun_receive()` and are not exposed as application datagrams.

## Application datagrams

`kc_stun_send()` and `kc_stun_receive()` preserve UDP datagram boundaries on
the discovered mapping.

A receive timeout of zero waits indefinitely.

Release received datagram storage with `kc_stun_datagram_free()`.

## Run a Binding endpoint

`kc_stun_listen()` starts an autonomous UDP Binding endpoint.

A null or empty host listens on available IPv4 and IPv6 wildcard addresses
using the same UDP port. An explicit host restricts the listener to compatible
resolved address families.

Port zero requests an operating-system-selected port. Read the resulting port
with `kc_stun_server_port()`.

The listener answers Binding Requests with the source address and port it
actually received, encoded as `XOR-MAPPED-ADDRESS`.

The endpoint runs until `kc_stun_server_close()`.

## Transport scope

The current public transport is UDP. TCP, TLS, and DTLS STUN transports are not
part of this API.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libstun.h) for the complete C interface.
