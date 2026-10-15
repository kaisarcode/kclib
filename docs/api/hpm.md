# hpm scripting API

Direct bidirectional UDP channel establishment against peer endpoints supplied
by the application. HPM can create its own local UDP mapping or use a live
mapping supplied by the caller, then maintains the selected channel until it is
closed or peer liveness is lost.

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

A live UDP mapping may be supplied instead of `host` and `port`:

```lua
local path = hpm.connect {
    udp = mapping,
    peers = {
        { host = "203.0.113.20", port = 5000 }
    },
    timeout_ms = 5000
}
```

`host` is optional. `port` may be zero to request an operating-system-selected
local port. When `udp` is supplied, HPM uses that mapping directly and does not
open or close another local mapping. The mapping must remain alive until
`path:close()` and must have one active receive owner while the path is active.
`timeout_ms` zero uses the default connection timeout.

The returned path object represents a live bidirectional channel. HPM maintains
peer liveness autonomously, including while application traffic is idle; callers
do not send maintenance traffic themselves. `path:send(bytes)` writes one UDP
datagram onto the channel and `path:receive(timeout_ms)` returns one datagram
from it. A zero receive timeout waits indefinitely.

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

With an existing live UDP mapping:

```js
const path = await hpm.connect({
  udp: mapping,
  peers: [
    { host: "203.0.113.20", port: 5000 }
  ],
  timeout_ms: 5000
});
```

The scripting `udp` object projects the native borrowed-UDP contract as a live
mapping with a numeric local endpoint and complete-datagram `send` and `receive`
operations. Binding layers translate that object mechanically to the native
adapter; HPM does not take ownership of it.

Peer endpoint discovery and exchange remain outside HPM. The application
supplies remote endpoints and HPM attempts a direct UDP path using them.

`strerror(status)` and `version()` remain namespace functions.
