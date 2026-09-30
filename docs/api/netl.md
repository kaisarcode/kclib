# netl scripting API

Incoming TCP or UDP listener with peer-oriented responses.

## Lua

```lua
local listener = netl.open({
    host = "0.0.0.0",
    port = 8080,
    protocol = netl.TCP,

    receive = function(input)
        -- input.peer, input.protocol, input.host, input.port, input.data
        input.peer:respond("ok")
    end,

    disconnect = function(peer) end,
    error = function(status) end
})

local port = listener:port()
listener:close()
```

`receive` is required. `disconnect` and `error` are optional. A TCP peer remains the same object across receives until closed. A UDP peer is receive-local.

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
    // input.peer, input.protocol, input.host, input.port, input.data
    input.peer.respond(new TextEncoder().encode("ok"));
  },

  disconnect(peer) {},
  error(status) {}
});

const port = await listener.port();
await listener.close();
```

`strerror(status)` and `version()` remain namespace functions.
