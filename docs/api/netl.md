# netl scripting API

`libnetl` is a general-purpose TCP/UDP listener. JavaScript below is an
**API projection**, not a binding or implementation.

## JavaScript projection

```js
const listener = netl.open({
    host: "0.0.0.0",
    port: 5000,
    protocol: netl.UDP
}, function(input) {
    // Respond during this callback:
    input.peer.respond(input.data);

    // Keep the numeric address (not the temporary UDP peer object).
    peer = {host: input.host, port: input.port};
});

// Respond later from the same bound UDP listener socket:
listener.replyTo(peer.host, peer.port, data);

listener.port();
listener.close();
```

Callbacks are arguments to `open()`, not options. For TCP, the peer can live
across receives until disconnect; for UDP, `input.peer` is scoped to the
receive callback. Saving only `input.host`/`input.port` supports delayed
`replyTo` calls.

## Native C contract

`kc_netl_open()`, `kc_netl_respond()`, `kc_netl_port()` and
`kc_netl_close()` retain their public semantics.
`kc_netl_reply_to(listener, host, port, bytes, size)` is UDP-only,
copies a datagram into the listener's outgoing queue and uses its bound socket.

## CLI

```sh
# Register a resident UDP listener and a callback command.
netl servidor 5000 'handler' -t udp

# Incoming UDP invokes: handler <host> <port>
# Its stdin contains the binary datagram.

# Later, reply using the existing listener:
printf respuesta | netl servidor -r 192.168.1.20 6000

netl -l
netl servidor -d
```

The new `-r/--reply` command delivers one binary UDP datagram via a local
IPC request to the resident listener. It does not open another UDP listener.
The registered command receives source host and port as arguments and the
incoming datagram on stdin. The old listener registration, list and delete
commands are unchanged.
