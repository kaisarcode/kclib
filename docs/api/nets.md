# nets scripting API

One asynchronous raw-byte transfer over TCP, UDP, or optional TLS.

## Lua

```lua
local transfer = nets.send({
    host = "127.0.0.1",
    port = 8080,
    protocol = nets.TCP,
    data = bytes,

    complete = function(status, response)
        -- called exactly once after a successful launch
    end
})

transfer:stop()
transfer:close()

local tls = nets.tlsAvailable()
```

A successful launch returns a transfer object and eventually invokes `complete(status, response)` exactly once. TCP/TLS completion may include response bytes. UDP succeeds with no response bytes.

`stop()` requests graceful interruption and terminal completion reports `nets.ESTOP`.

## JavaScript

```js
const transfer = await nets.send({
  host: "127.0.0.1",
  port: 8080,
  protocol: nets.TCP,
  data: bytes,

  complete(status, response) {
    // called exactly once after a successful launch
  }
});

await transfer.stop();
await transfer.close();

const tls = await nets.tlsAvailable();
```

`strerror(status)` and `version()` remain namespace functions.
