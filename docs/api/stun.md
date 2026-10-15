# stun scripting API

UDP STUN Binding discovery and serving. Discovery keeps the same local mapping
open so the observed endpoint and later application datagrams refer to the same
UDP mapping. A listener autonomously answers Binding Requests from other
machines.

## Lua

```lua
local server = stun.listen {
    host = nil,
    port = 3478
}

print(server.port)

local mapping = stun.discover {
    server = "stun.example.com:3478",
    host = nil,
    port = 5000,
    timeout_ms = 3000
}

local local_endpoint = mapping.local
local mapped_endpoint = mapping.mapped

mapping:refresh()

mapping:send(
    { host = "203.0.113.20", port = 5000 },
    "hello"
)

local packet = mapping:receive(1000)
local from = packet.from
local data = packet.data

mapping:close()
server:close()
```

`stun.listen { host, port }` starts an autonomous UDP Binding endpoint. A nil or
empty `host` listens on available IPv4 and IPv6 wildcard addresses. Port zero
requests an operating-system-selected port, exposed as `server.port`; use 3478
for the conventional public STUN UDP port.

The discovery `server` option accepts `host:port`, `[IPv6]:port`, or a host
without a port, which uses port 3478. `host` is optional. `port` may be zero to request an
operating-system-selected local port. `timeout_ms` zero uses the default
discovery timeout.

`mapping.local` and `mapping.mapped` project as
`{ host = ..., port = ... }` values.

`mapping:refresh()` sends a fresh STUN Binding Request through the same mapping.
It does not wait for the response. STUN responses are consumed internally and
are not returned by `mapping:receive()`.

`mapping:send(endpoint, bytes)` sends one UDP datagram.
`mapping:receive(timeout_ms)` returns
`{ from = { host = ..., port = ... }, data = ... }`. A zero receive timeout
waits indefinitely.

## JavaScript

```js
const server = await stun.listen({
  host: null,
  port: 3478
});

console.log(server.port);

const mapping = await stun.discover({
  server: "stun.example.com:3478",
  host: null,
  port: 5000,
  timeout_ms: 3000
});

const localEndpoint = mapping.local;
const mappedEndpoint = mapping.mapped;

await mapping.refresh();

await mapping.send(
  { host: "203.0.113.20", port: 5000 },
  new TextEncoder().encode("hello")
);

const packet = await mapping.receive(1000);
const from = packet.from;
const data = packet.data;

await mapping.close();
await server.close();
```

The listener answers UDP STUN Binding Requests with `XOR-MAPPED-ADDRESS`. It
does not expose request parsing or socket polling to the scripting caller. The
current transport is UDP; TCP, TLS, and DTLS STUN transports are outside this
API.

The mapping supports IPv4 and IPv6. The chosen STUN server address family and
explicit local bind host must be compatible.

`strerror(status)` and `version()` remain namespace functions.
