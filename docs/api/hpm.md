# hpm scripting API

Direct bidirectional UDP path establishment against peer endpoints supplied by
the application.

## Lua

```lua
local path = hpm.connect {
    host = "0.0.0.0",
    port = 5000,
    peers = {
        { host = "203.0.113.20", port = 5000 },
        { host = "192.168.1.20", port = 5000 }
    },
    timeout_ms = 5000
}

path:send("hello")
local data = path:receive(1000)

local local_endpoint = path.local
local peer_endpoint = path.peer

path:close()
```

`host` is optional. `port` may be zero to request an operating-system-selected
local port. `timeout_ms` zero uses the default connection timeout.

`path:send(bytes)` sends one UDP datagram. `path:receive(timeout_ms)` returns one
UDP datagram; a zero receive timeout waits indefinitely.

`path.local` and `path.peer` project as `{ host = ..., port = ... }` values.

## JavaScript

```js
const path = await hpm.connect({
  host: "0.0.0.0",
  port: 5000,
  peers: [
    { host: "203.0.113.20", port: 5000 },
    { host: "192.168.1.20", port: 5000 }
  ],
  timeout_ms: 5000
});

await path.send(new TextEncoder().encode("hello"));
const data = await path.receive(1000);

const localEndpoint = path.local;
const peerEndpoint = path.peer;

await path.close();
```

Endpoint discovery is outside HPM. The supplied peer endpoints may come from
STUN, rendezvous, configuration, local discovery, or any other application
mechanism without changing the HPM capability model.

`strerror(status)` and `version()` remain namespace functions.
