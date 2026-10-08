# REDP2P alternate implementations

This directory contains non-C implementations of REDP2P roles.

```text
redp2p.js
redp2p-idx.php
```

## Browser implementation

`redp2p.js` is the complete browser REDP2P implementation. It contains the
shared protocol helpers, publisher, and consumer in one file and uses WebRTC
for peer transport.

It is an ES module and exports the complete `RedP2P` API as the default export,
with `pub` and `con` also available as named exports:

```js
import RedP2P, {pub, con} from "./redp2p.js";

const publisher = await pub({
    index: "https://index.example/",
    id: "example",
    receive(event) {
        event.client.respond(event.data);
    }
});

const consumer = await con({
    index: "https://index.example/",
    id: "example",
    receive(data) {
        console.log(data);
    }
});

consumer.send("hello");
```

The same module also assigns the API to `globalThis.RedP2P`, so browser code
that prefers the global namespace may use `RedP2P.pub()` and `RedP2P.con()`
after the module has loaded.

The implementation relies on browser APIs including WebRTC, Fetch, Web Crypto,
and local storage.

## PHP index implementation

`redp2p-idx.php` is the included PHP implementation of the REDP2P index
protocol. It supports the native and WebRTC REDP2P transports handled by the
index protocol.

PHP is used for convenience and broad shared-hosting availability; the index
may be implemented in any language or runtime that implements the same
protocol.

Example:

```php
<?php
require_once __DIR__ . '/redp2p-idx.php';

$index = new \KaisarCode\Redp2pIndex([
    'dsn' => 'sqlite:' . __DIR__ . '/redp2p.sqlite',
    'pass' => 'secret',
]);
$index->serve();
```

The PHP implementation requires `ext-sodium`, PDO, and the PDO driver for the
configured database.

The common wire contract is documented in [doc/protocol.md](../doc/protocol.md).
