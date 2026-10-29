# tls scripting API

Authenticated client or server TLS sessions over caller-supplied byte transport.

## Lua

```lua
local session = tls.open({
    role = tls.CLIENT,
    host = "example.com",

    plaintext = function(data)
        -- authenticated application bytes from the peer
    end,

    ciphertext = function(data)
        -- pass TLS wire bytes to the chosen transport
    end
})

session:send(bytes)

-- As TLS wire bytes arrive from the transport:
session:receive(wire_bytes)

session:close()
```

Server sessions use `tls.SERVER` and provide `certificate` and `privateKey`
bytes instead of `host`. Clients may provide `ca` bytes to override the native
trust store.

`send()` may be called before the handshake completes; plaintext is retained
until the peer is authenticated. `receive()` accepts arbitrary stream chunks and
may invoke either callback zero, one, or multiple times.

Authenticated peer shutdown is exposed as the capability-level `tls.CLOSED`
status.

## JavaScript

```js
const session = await tls.open({
  role: tls.CLIENT,
  host: "example.com",

  plaintext(data) {
    // authenticated application bytes from the peer
  },

  ciphertext(data) {
    // pass TLS wire bytes to the chosen transport
  }
});

await session.send(bytes);

// As TLS wire bytes arrive from the transport:
await session.receive(wireBytes);

await session.close();
```

`strerror(status)` and `version()` remain namespace functions.

## Next step

Continue with [tpl](tpl.md).
