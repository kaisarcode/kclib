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
# Register a TCP or UDP interface on a local wildcard address:
netl servidor 0.0.0.0:5000 handler -t udp

# Register one persistent endpoint for a remote destination:
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

The CLI uses one creation syntax: `netl <id> <addr:port> [command...]`.
A wildcard address (`0.0.0.0` or `::`) binds a local listener;
a concrete destination establishes a remote endpoint. Neither creating the
endpoint nor specifying a callback sends data. Sending happens later by
piping stdin to `netl <id>`; incoming data is delivered asynchronously to
the optional callback. For a wildcard listener, an incoming peer is needed to
identify a destination for its response.
