# rdvz scripting API

Named independent peers with direct, symmetric relationships. Every peer
remains registered until it is explicitly removed. Either endpoint may
establish or remove the same relationship. Neither peer owns the other.

## JavaScript

```js
const peer1 = rdvz.create("peer1", function(peer) {
    console.log(peer); // "peer2"
});

const peer2 = rdvz.create("peer2", function(peer) {
    console.log(peer); // "peer1"
});

rdvz.list(); // ["peer1", "peer2"]

peer2.connect("peer1", function(peer) {
    console.log(peer); // "peer1"
});

peer1.list(); // ["peer2"]
peer2.list(); // ["peer1"]

peer1.disconnect("peer2", function(peer) {
    console.log(peer); // "peer2"
});

rdvz.disconnect("peer1");
rdvz.disconnect("peer2");
```

## Lua

```lua
local peer1 = rdvz.create("peer1", function(peer)
    print(peer) -- "peer2"
end)

local peer2 = rdvz.create("peer2", function(peer)
    print(peer) -- "peer1"
end)

local peers = rdvz.list()

peer2:connect("peer1", function(peer)
    print(peer) -- "peer1"
end)

local linked = peer1:list()

peer1:disconnect("peer2", function(peer)
    print(peer) -- "peer2"
end)

rdvz.disconnect("peer1")
rdvz.disconnect("peer2")
```

The callback given to `rdvz.create(id, callback)` receives the **other
peer's ID as a string** when another peer establishes a new relationship.
The callback remains active while its local handle is alive. Connections
established by another process may be reported asynchronously.

The optional callbacks passed to `peer.connect(id, callback)` and
`peer.disconnect(id, callback)` also receive the other peer's ID and
run after the requested operation succeeds. They are not subscriptions.

`connect()` never creates another peer: both IDs must already be
registered. A link is undirected, not transitive, and unique per pair.
`peer1.connect("peer2")` and `peer2.connect("peer1")` refer to the
same relation. Likewise, either endpoint can disconnect the pair.

`peer.disconnect(id)` removes a single direct relation while keeping both
peers registered. `rdvz.disconnect(id)` removes a peer completely,
including all its remaining relations. Closing a local native handle does
not remove the peer.

The CLI additionally supports a persistent executable command associated
with each named peer. It receives the other peer's identifier whenever
that peer gains a direct relation. No network connection, message transport,
hierarchy or separate application event system is implied.
