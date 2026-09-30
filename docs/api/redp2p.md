# redp2p scripting API

Direct peer-to-peer publication and consumption through a REDP2P index.

## Lua

```lua
local index = redp2p.idx({
    host = "0.0.0.0",
    port = 9876,
    seats = 100,
    pow = 0,
    pass = nil,
    max_consumers = 0,
    vips = {
        { id = "peer-a", pass = "secret" }
    }
})

local publishers = index:list()

local publisher = redp2p.pub({
    id = "service-a",
    index = "index.example.com:9876",
    protocol = redp2p.TCP,
    pass = nil,
    stun = nil,
    turn = nil,
    turn_user = nil,
    turn_pass = nil,

    connect = function(client)
    end,

    receive = function(input)
        input.client:respond(input.data)
    end
})

local consumer = redp2p.con({
    id = "service-a",
    index = "index.example.com:9876",
    stun = nil,
    turn = nil,
    turn_user = nil,
    turn_pass = nil,

    receive = function(data)
    end
})

consumer:send(bytes)

consumer:close()
publisher:close()
index:close()
```

The three capability objects are index, publisher, and consumer.

An index exposes `list()` and `close()`. A publisher exposes `close()`; clients received by publisher callbacks expose `respond(bytes)` and `close()`. A consumer exposes `send(bytes)` and `close()`.

The consumer does not select TCP or UDP. It learns the publisher transport through the index. TCP preserves stream semantics; UDP preserves datagram boundaries.

Optional STUN/TURN settings only affect peer path establishment. Direct
connectivity remains preferred; TURN is used as a relay fallback. These
options do not change the publisher, consumer, client, callback, or
send/respond API.

## JavaScript

```js
const index = await redp2p.idx({
  host: "0.0.0.0",
  port: 9876,
  seats: 100,
  pow: 0,
  pass: null,
  maxConsumers: 0,
  vips: [{ id: "peer-a", pass: "secret" }]
});

const publishers = await index.list();

const publisher = await redp2p.pub({
  id: "service-a",
  index: "index.example.com:9876",
  protocol: redp2p.TCP,
  pass: null,
  stun: null,
  turn: null,
  turn_user: null,
  turn_pass: null,

  connect(client) {},

  receive(input) {
    input.client.respond(input.data);
  }
});

const consumer = await redp2p.con({
  id: "service-a",
  index: "index.example.com:9876",
  stun: null,
  turn: null,
  turn_user: null,
  turn_pass: null,
  receive(data) {}
});

await consumer.send(bytes);

await consumer.close();
await publisher.close();
await index.close();
```

`strerror(status)` and `version()` remain namespace functions.
