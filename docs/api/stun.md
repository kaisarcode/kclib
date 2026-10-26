# stun scripting API

The canonical operations are `client` and `server`. A client performs a Binding
transaction; a server answers Binding requests. Either may borrow UDP owned by a
higher-level composition. Bindings project the C transaction state into an
asynchronous language operation without exposing C buffer or pointer mechanics.

## JavaScript

```js
const client = await stun.client({
  peer: "stun.example.com:3478",
  port: 5000
});

console.log(client.local);   // { host, port }
console.log(client.mapped);  // { host, port }

await client.send({ host: "203.0.113.20", port: 5000 }, bytes);
const datagram = await client.receive(1000);  // { from: { host, port }, data }
await client.refresh();
await client.keepalive();
await client.close();

const server = await stun.server({ port: 3478 });
console.log(server.port);
await server.close();
```

Borrowing a UDP transport for ICE-style composition:

```js
const server = await stun.server({
  transport: udp,
  credentials: username => localCredentials(username),
  onBinding: ({ peer, attributes }) => validateBinding(peer, attributes)
});

const client = await stun.client({
  transport: udp,
  peer: remoteEndpoint,
  credentials: remoteCredentials,
  attributes: checkAttributes
});
```

The host binding maps datagrams from `udp` to pending client transactions and the
server's Binding handler. The UDP transport has a single receive owner. `stun`
borrows it and never closes it. User applications do not construct STUN packets,
transaction identifiers, HMACs or CRCs. `keepalive()` emits an
unauthenticated STUN Binding Indication, not an ICE policy decision. The `credentials` and `attributes`
values in the second example are produced by the higher-level composition.

## Lua

```lua
local client = stun.client {
    peer = "stun.example.com:3478",
    port = 5000
}

print(client.mapped.host, client.mapped.port)
client:refresh()
client:keepalive()
client:close()

local server = stun.server { port = 3478 }
print(server.port)
server:close()
```

Borrowed UDP and credential/binding callback semantics are the same as in
JavaScript. Native STUN statuses become ordinary binding errors, except where a
higher-level protocol needs to inspect a returned STUN error (such as `487`).

Use `stun.version()` to query the linked build version.
