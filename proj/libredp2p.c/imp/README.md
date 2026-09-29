# REDP2P alternate implementations

This directory contains non-C implementations of REDP2P roles.

```text
redp2p-idx.php
redp2p-pub.js
redp2p-con.js
```

`redp2p-idx.php` is the included PHP implementation of the REDP2P index
protocol. PHP is used for convenience and broad shared-hosting availability;
the index may be implemented in any language or runtime that implements the
same protocol.

`redp2p-pub.js` and `redp2p-con.js` are standalone JavaScript
implementations of the publisher and consumer roles over WebRTC.

The common wire contract is documented in [doc/protocol.md](../doc/protocol.md).
