# Network interface behavior

The CLI exposes one address-based interface model: `netl <id> <addr:port> [command...]`.
Creating an interface does not imply a client or server role, nor does it
transmit any bytes. `netl <id>` sends stdin over its resident socket; the
optional command processes received data asynchronously.

For UDP, the runtime first attempts to bind the configured address locally.
If the address is not available on this machine, the runtime uses an ephemeral
local port and retains the configured address as its default send target.
Failed UDP datagrams do not close the persistent socket. For TCP the native
runtime must still use different listening and connected sockets internally:
it tries local listening first and remote connection otherwise. TCP connection
establishment and disconnection continue to follow OS socket semantics.

The native public API exposes `kc_netl_open`, `kc_netl_send`,
`kc_netl_peer_send`, and `kc_netl_close`. An optional
`kc_netl_endpoint_t` argument to `kc_netl_send` selects a UDP destination
for an individual datagram; NULL uses the configured address. It does not
alter the local bind or create another socket. A wildcard address is never
rewritten to loopback. The low-level socket machinery is private.

## Next step

Return to the [project README](../README.md).
