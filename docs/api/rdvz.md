# rdvz scripting API

An in-memory registry of independent peers and their direct, bidirectional
links. IDs are unique within an open registry and are not persistent. The
registry does not know what a peer represents or how it communicates.

## Lua

```lua
local rendezvous = rdvz.open({
    onConnect = function(peer, other)
        -- The application handles the encounter.
    end,
    onDisconnect = function(peer, other)
        -- The application handles the departure.
    end
})

local peer1 = rendezvous:connect()
local peer2 = rendezvous:connect(peer1.id)

local all = rendezvous:list()
local linked = peer1:list()

peer1:disconnect()
peer1 = rendezvous:connect(peer2.id)

peer1:disconnect()
peer2:disconnect()
rendezvous:close()
```

## JavaScript

```js
const rendezvous = rdvz.open({
    onConnect(peer, other) {
        // The application handles the encounter.
    },
    onDisconnect(peer, other) {
        // The application handles the departure.
    }
});

let peer1 = rendezvous.connect();
const peer2 = rendezvous.connect(peer1.id);

const all = rendezvous.list();
const linked = peer1.list();

peer1.disconnect();
peer1 = rendezvous.connect(peer2.id);

peer1.disconnect();
peer2.disconnect();
rendezvous.close();
```

`connect()` creates an independent peer. `connect(id)` creates a new peer
with one direct link to the specified existing peer. Unknown IDs fail rather
than creating a pending room or a deferred encounter. A peer's `id` remains
stable during its lifetime, and its `list()` returns direct neighbors only.

The callbacks run synchronously when a direct link is created or removed;
there is one notification per link, not one per endpoint. A callback can
inspect peers and links but cannot mutate or close its registry. Closing the
registry drops all remaining peers without emitting notifications.

The Lua and JavaScript arrays are mechanical projections of the native C
visitor operations. Neither projection imposes asynchronous execution,
network transport, roles, rooms, timers, or application metadata.
