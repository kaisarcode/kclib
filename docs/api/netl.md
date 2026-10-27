# libnetl — Public API projection

`libnetl` manages persistent, bidirectional TCP and UDP sockets. The snippets
below are **conceptual projections** of the native C API, not JavaScript or Lua
implementations.

## Listener

```js
const server = netl.open({
    host: "0.0.0.0",
    port: 5000,
    protocol: netl.UDP
}, function(peer, data) {
    console.log(data);
    peer.send("Received OK");
});

server.close();
```

The incoming `peer` identifies the remote endpoint and uses the same underlying
socket to send a reply. The reply may differ entirely from the received data.
Responding is optional. TCP peers remain valid until disconnected; a UDP
receive peer is valid only during its callback.

## Persistent outgoing endpoint

```js
const client = netl.connect({
    host: "192.168.1.20",
    port: 6000,
    protocol: netl.UDP,
    localPort: 4000
}, function(peer, data) {
    console.log(data);
});

client.send("message 1");
client.send("message 2");
client.close();
```

Connecting creates a persistent socket once. Every `send()` reuses it until
the instance is explicitly closed. TCP establishes a persistent stream
connection; UDP binds one persistent datagram socket.

All callbacks are positional arguments, never options fields.

## Unified native interface

`kc_netl_open()` creates a persistent address-oriented interface
without a client/server parameter. A TCP listener is created when the address
can be bound locally; outbound TCP is established on the first
`kc_netl_send()`. The instance remains available when the remote
destination is not listening, allowing a later send attempt. UDP uses one
persistent bound datagram socket for both send and receive. Incoming traffic
is always delivered as `peer, data, size, userdata`.

```c
kc_netl_t *channel = NULL;
kc_netl_options_t options = {
    .host = "127.0.0.1",
    .port = 8080,
    .protocol = KC_NETL_TCP
};

int status = kc_netl_open(
    &channel, &options, receive, disconnect, error, userdata
);
if (status == KC_NETL_OK) {
    (void)kc_netl_send(channel, "Hello", 5U, NULL);
    kc_netl_close(channel);
}
```

The low-level socket functions used inside the implementation still exist;
CLI callers do not select between them.

## C contract

- `kc_netl_open(out, options, receive, disconnect, error, userdata)` opens
  a persistent TCP or UDP listener.
- `kc_netl_connect(out, peer, options, receive, disconnect, error, userdata)`
  creates one persistent outgoing endpoint.
- `kc_netl_send(peer, bytes, size)` sends or replies using that peer's
  existing socket.
- `kc_netl_peer_host(peer)`, `kc_netl_peer_port(peer)`, and
  `kc_netl_peer_protocol(peer)` describe the remote endpoint.
- `kc_netl_close(instance)` ends a persistent instance.

There is no `respond()`, `sendTo()`, or `replyTo()`.

## CLI

```sh
# Create a persistent interface at the specified address:
netl servidor 0.0.0.0:5000 handler -t udp

# The same syntax works for another address:
netl cliente 192.168.1.20:6000 -t udp

# Send stdin without opening another socket:
echo "message 1" | netl cliente
echo "message 2" | netl cliente

netl -l
netl cliente -d
netl servidor -d
```

An external UDP receive command is invoked with the source host and port
as arguments, and receives the datagram on stdin. TCP streams are delivered
to the registered command's stdin; its stdout is sent to the peer.

The CLI maintains sockets in resident processes; ordinary `netl <id>`
invocations communicate with the registered process via private local IPC.
IPv6 destination syntax uses brackets: `[::1]:6000`.

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
