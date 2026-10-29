# rstrm scripting API

Reliable ordered byte streams over caller-supplied datagram channels.

## Lua

```lua
local stream = rstrm.open({
    mtu = 1200,

    receive = function(data)
    end,

    datagram = function(data)
        transport.send(data)
        return true
    end,

    eof = function()
    end,

    drain = function()
    end,

    error = function(status)
    end
})

stream:send(bytes)
stream:input(datagram)
stream:shutdown()
stream:close()
```

## JavaScript

```js
const stream = await rstrm.open({
  mtu: 1200,

  receive(data) {},

  datagram(data) {
    transport.send(data);
    return true;
  },

  eof() {},
  drain() {},
  error(status) {}
});

await stream.send(bytes);
await stream.input(datagram);
await stream.shutdown();
await stream.close();
```

`send()` operates on a byte stream, not messages. A binding should surface
`KC_RSTRM_EFULL` as a recoverable send-capacity condition so the producer can
resume after `drain()`.

`input()` accepts exactly one transport datagram at a time. The datagram callback
emits exactly one complete transport datagram at a time.

`shutdown()` gracefully ends only the local write side. `eof()` reports the
peer's graceful write shutdown after all preceding bytes have been delivered.
`close()` releases the local stream immediately.

`strerror(status)` and `version()` remain namespace functions.

## Next step

Continue with [stun](stun.md).
