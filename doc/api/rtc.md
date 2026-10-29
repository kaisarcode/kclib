# rtc scripting API

Native WebRTC peer connections and DataChannels.

## Lua

```lua
local peer = rtc.peer({
    ice_servers = {
        "stun:stun.example.com:3478",
        "turn:user:pass@turn.example.com:3478?transport=udp"
    }
})

peer:close()
```

The peer object represents one native WebRTC peer connection. ICE server URIs
may contain STUN or TURN configuration. Higher-level signaling and transport
composition remain outside `librtc.c`.

## JavaScript

```js
const peer = await rtc.peer({
  iceServers: [
    "stun:stun.example.com:3478",
    "turn:user:pass@turn.example.com:3478?transport=udp"
  ]
});

await peer.close();
```

`strerror(status)` and `version()` remain namespace functions.
