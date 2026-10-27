# nets scripting API

`libnets` is a general-purpose outbound TCP/UDP transport. JavaScript below
is an **API projection** to describe the native C contract; no JavaScript
implementation or bindings are required.

## JavaScript projection

```js
const network = nets.open({
    host: "192.168.1.10",
    port: 5000,
    protocol: nets.UDP,
    localHost: "0.0.0.0",
    localPort: 4000
}, function(data, peer) {
    // Called with a received datagram and its source.
    console.log(peer.host, peer.port);
});

network.send(data);                        // Default destination.
network.sendTo("192.168.1.20", 6000, data); // Same UDP socket.
network.sendTo("192.168.1.30", 7000, data);

console.log(network.localPort());
network.close();
```

The configuration contains data only. All callbacks are **arguments to the
opening function**, not properties of the options object. TCP retains its
connection and ordered-stream semantics; `sendTo` is UDP-only.

## Native C contract

Existing callers continue using `kc_nets_open()`, `kc_nets_send()` and
`kc_nets_close()`. Extended UDP configuration is available through
`kc_nets_open_udp()` with `kc_nets_udp_options_t`. The receive callback is
still an argument of the opening function for regular use; for source-aware UDP,
the native `kc_nets_udp_options_t.receive_from` callback receives
`(host, port, bytes, size, userdata)`. This native detail differs from the
logical projection shown above.

`kc_nets_send_to()` sends to arbitrary same-family UDP destinations through
the existing socket; `kc_nets_local_port()` reports the bound local port.
The default `kc_nets_send()` continues sending to the configured destination.

## CLI

```sh
# Existing stdin/stdout relay remains available.
nets 192.168.1.10 5000 -t udp
nets 192.168.1.10 5000 -t udp --local-port 4000

# Register a persistent UDP sender, optionally with a receive command.
nets 192.168.1.10 5000 -t udp --id canal1 --local-port 4000 --receive 'handler'

# Use the same resident socket, including for other destinations.
printf hola | nets canal1 -s
printf hola | nets canal1 -s 192.168.1.20 6000

nets -l
nets -l canal1
nets canal1 -d
```

In resident mode, `handler <host> <port>` receives the complete binary UDP
datagram on stdin. A send command reads one binary UDP datagram from stdin,
submits it through a private local IPC request, and exits without creating an
additional UDP socket. The worker remains the socket owner.

Resident UDP endpoints are currently limited to one socket address family,
chosen by the default host. For IPv4 and IPv6 destinations use separate
instances when the families differ. Stream TCP mode does not support
`sendTo`. Background callbacks are optional; the CLI is useful as a send-only
endpoint without `--receive`.
