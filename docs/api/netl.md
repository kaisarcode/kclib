# netl scripting API

`netl` maintains TCP or UDP listeners and exchanges raw bytes with incoming
peers.

## Lua

```lua
local listener = netl.open({
    host = "0.0.0.0",
    port = 8080,
    protocol = netl.TCP,

    receive = function(input)
        input.peer:respond("ok")
    end,

    disconnect = function(peer)
    end,

    error = function(status)
    end
})

local port = listener:port()
listener:close()
```

`receive` is required. `disconnect` and `error` are optional. A TCP peer remains
the same object across receives until it closes. The peer can respond multiple
times, including after a receive callback has returned, and responses preserve
call order.

A UDP peer is receive-local and each `respond(bytes)` call produces one datagram
to that datagram's origin.

Peer methods:

```lua
peer:respond(bytes)
peer:close()
```

## JavaScript

```js
const listener = await netl.open({
  host: "0.0.0.0",
  port: 8080,
  protocol: netl.TCP,

  receive(input) {
    input.peer.respond(new TextEncoder().encode("ok"));
  },

  disconnect(peer) {
  },

  error(status) {
  }
});

const port = await listener.port();
await listener.close();
```

A TCP peer is usable outside the receive callback while its connection remains
live. `disconnect(peer)` is terminal for that peer.

`strerror(status)` and `version()` remain namespace functions.
