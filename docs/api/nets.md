# nets scripting API

`nets` maintains one persistent outbound TCP or UDP endpoint and exchanges raw
bytes with the remote destination.

## Lua

```lua
local network = nets.open({
    host = "example.com",
    port = 443,
    protocol = nets.TCP,

    receive = function(data)
    end,

    disconnect = function()
    end,

    error = function(status)
    end
})

network:send(bytes)
network:close()
```

`receive(data)` receives TCP stream chunks or one complete UDP datagram.
`disconnect()` is used only when a TCP peer closes the connection.
`error(status)` reports an asynchronous endpoint failure.

`send(bytes)` may be called repeatedly. TCP calls contribute bytes to one
ordered stream. Each UDP call remains one datagram.

## JavaScript

```js
const network = await nets.open({
  host: "example.com",
  port: 443,
  protocol: nets.TCP,

  receive(data) {
  },

  disconnect() {
  },

  error(status) {
  }
});

await network.send(bytes);
await network.close();
```

`open()` establishes the endpoint before returning it. `receive` is required.
`disconnect` and `error` are optional.

`strerror(status)` and `version()` remain namespace functions.
