# redp2p.c - Peer-to-Peer Connectivity

`redp2p.c` is a library designed to provide peer-to-peer connectivity.

It's based on three roles: an index, a publisher, and a consumer.

- The publisher registers an identifier in an index and exposes a service or data
endpoint under that identifier.
- The consumer resolves that identifier through the same index and requests a connection.
- The index coordinates discovery and connection establishment; once the
peers are connected, application traffic flows directly between publisher and consumer.

```text
           index
          /     \
   publisher - consumer
```

The same model is used by the native and browser implementations. Native peers
publish and consume TCP or UDP services, while browser peers use WebRTC data
channels. The project provides two compatible index implementations: the native
C index and the PHP index. Both implement the REDP2P index protocol, including
the signaling required by WebRTC peers.

---

## Native CLI

The native command follows the three REDP2P roles:

```text
redp2p idx ...
redp2p pub ...
redp2p con ...
```

### Example

Suppose machine A has a TCP service running on port `8080`.

First run an index somewhere reachable by both peers:

```bash
redp2p idx 9876
```

On machine A, publish the local service under the name `web`:

```bash
redp2p pub web@index.example.com:9876 --tcp 8080
```

On machine B, connect to `web` and make it available locally on port `9000`:

```bash
redp2p con web@index.example.com:9876 9000
```

Machine B can now use:

```text
127.0.0.1:9000
```

as if the service were local.

The index coordinates the connection, but service traffic travels directly
between the peers.

The same works for UDP:

```bash
redp2p pub dns@index.example.com:9876 --udp 5353
redp2p con dns@index.example.com:9876 9001
```

The consumer does not need to specify TCP or UDP. It learns that from the
publisher registration.

If the index port is omitted, REDP2P uses port `9876`.

### Index

Start an index:

```bash
redp2p idx 9876
```

The native index coordinates TCP/UDP peers and also implements the WebRTC
signaling used by browser peers.

List the publishers currently known by a local index:

```bash
redp2p idx 9876 --list
```

Optional limits can be applied when running an index:

```bash
redp2p idx 9876 --seats 100
redp2p idx 9876 --pow 16
redp2p idx 9876 --max-consumers 20
```

### Publish

Publish a TCP service:

```bash
redp2p pub myservice@index.example.com --tcp 8080
```

Publish a UDP service:

```bash
redp2p pub myservice@index.example.com --udp 5353
```

### Connect

Connect to a published service:

```bash
redp2p con myservice@index.example.com 9000
```

The service becomes available on `127.0.0.1:9000`.

### Parameters

| Command / option | Description |
| :--- | :--- |
| `idx <port>` | Run an index |
| `idx <port> --list` | List publishers known by the local index |
| `--seats <N>` | Maximum number of registered publishers |
| `--pow <N>` | Registration proof-of-work bits, from 0 to 32 |
| `--max-consumers <N>` | Maximum pending consumers per publisher |
| `pub <id>@<index[:port]> --tcp <port>` | Publish a local TCP service |
| `pub <id>@<index[:port]> --udp <port>` | Publish a local UDP service |
| `con <id>@<index[:port]> <local-port>` | Connect to a publisher |
| `--stun <url>` | STUN server used by native peers |
| `-h`, `--help` | Show help |
| `-v`, `--version` | Show version |

### Environment

| Variable | Description |
| :--- | :--- |
| `REDP2P_SEATS` | Default publisher capacity for the index |
| `REDP2P_POW` | Default registration proof-of-work bits |
| `REDP2P_PASS` | Registration password |
| `REDP2P_VIP` | Reserved IDs as `<id> <pass> ...` pairs |
| `REDP2P_MAX_CONSUMERS_PER_PUBLISHER` | Default pending-consumer limit |
| `REDP2P_STUN` | Default STUN URL for native peers |

CLI values override environment defaults.

`idx`, `pub`, and `con` keep running until interrupted with SIGINT or
SIGTERM.

---

## Browser / WebRTC

The browser implementation uses the same index/publisher/consumer model, with
WebRTC as the peer transport.

A browser publisher registers an ID in the index. A browser consumer resolves
that ID and submits a WebRTC offer through the index. The publisher receives the
offer through the same index and returns an answer. After negotiation completes,
the resulting data channel carries application traffic directly between the two
browsers.

The browser implementation is under:

```text
imp/redp2p-pub.js
imp/redp2p-con.js
```

### Publish from a browser

```html
<script src="redp2p-pub.js"></script>
```

```js
const pub = await RedP2P.pub({
    id: "chat",
    index: "https://example.com/index",

    receive(input) {
        console.log(input.data);
        input.client.respond("hello back");
    }
});
```

The publisher remains registered while the returned publisher object is open.

When a consumer sends data, `receive()` receives the data together with the
connected client. The publisher can answer that client with:

```js
input.client.respond(data);
```

Stop publishing with:

```js
await pub.close();
```

### Connect from a browser

Load the browser files:

```html
<script src="redp2p-pub.js"></script>
<script src="redp2p-con.js"></script>
```

Then connect to the published ID:

```js
const con = await RedP2P.con({
    id: "chat",
    index: "https://example.com/index",

    receive(data) {
        console.log(data);
    }
});

con.send("hello");
```

Close the connection with:

```js
con.close();
```

Both publisher and consumer may receive WebRTC ICE server configuration through
`iceServers`:

```js
iceServers: [
    {urls: "stun:stun.example.com:3478"}
]
```

For remote browser use, the index URL must use HTTPS. Plain HTTP is accepted for
localhost development.

---

## Index implementations

The index maintains publisher registrations and coordinates connection setup. It
supports both native peer discovery and WebRTC signaling, but it is not a relay
for application data.

The project includes two compatible implementations.

### Native C index

The native index is part of the C library and can be started through the CLI:

```bash
redp2p idx 9876
```

It handles native TCP/UDP coordination and the WebRTC signaling operations used
by browser publishers and consumers.

### PHP index

`imp/redp2p-idx.php` implements the same REDP2P index protocol over HTTP/JSON.

A minimal SQLite endpoint looks like this:

```php
<?php
require 'redp2p-idx.php';

\KaisarCode\Redp2pIndex::serve([
    'dsn' => 'sqlite:/var/lib/redp2p/index.sqlite',
]);
```

The PHP implementation uses PDO and supports SQLite or MySQL.

It requires:

- PHP;
- PDO;
- Sodium;
- the PDO driver for the selected database.

The same index limits and registration settings are available through
`REDP2P_SEATS`, `REDP2P_POW`, `REDP2P_PASS`, `REDP2P_VIP`, and
`REDP2P_MAX_CONSUMERS_PER_PUBLISHER`.

---

## Public C API

The library exposes the same three roles directly:

```c
kc_redp2p_idx(...)
kc_redp2p_pub(...)
kc_redp2p_con(...)
```

### Start an index

```c
#include "libredp2p.h"

kc_redp2p_idx_options_t options = {0};
kc_redp2p_idx_t *idx = NULL;

options.port = 9876;

if (kc_redp2p_idx(&idx, &options) == KC_REDP2P_OK) {
    kc_redp2p_idx_close(idx);
}
```

### Publish a local service

```c
#include "libredp2p.h"

kc_redp2p_pub_options_t options = {0};
kc_redp2p_pub_t *pub = NULL;

options.id = "web";
options.index = "index.example.com:9876";
options.protocol = KC_REDP2P_TCP;
options.port = 8080;

if (kc_redp2p_pub(&pub, &options) == KC_REDP2P_OK) {
    kc_redp2p_pub_close(pub);
}
```

Use `KC_REDP2P_UDP` to publish UDP instead.

### Connect to a publisher

```c
#include "libredp2p.h"

kc_redp2p_con_options_t options = {0};
kc_redp2p_con_t *con = NULL;

options.id = "web";
options.index = "index.example.com:9876";
options.port = 9000;

if (kc_redp2p_con(&con, &options) == KC_REDP2P_OK) {
    /* The published service is available on 127.0.0.1:9000. */
    kc_redp2p_con_close(con);
}
```

The C API can also exchange application data directly without exposing a local
TCP or UDP port. In that form, consumers send with `kc_redp2p_con_send()` and
publishers receive clients through their callback and answer with
`kc_redp2p_client_respond()`.

The public header is `src/libredp2p.h`.

---

## Protocol

The common REDP2P index protocol is documented in:

```text
doc/protocol.md
```

The native C index, the PHP index, and the browser implementations use that same
contract.

Applications normally do not need to implement the protocol themselves.

---

## Build

Build the native library and CLI:

```bash
make
```

Artifacts are generated under `bin/{arch}/{platform}/`.

### Tests

Build first, then run the native tests:

```bash
make
make test
```

Run the Windows build through Wine:

```bash
make x86_64/windows
make test wine
```

The browser/PHP integration test is:

```bash
./tests/test.sh
```

It starts a local PHP index and prints the browser test URL to open.

### Multiarch Builds

A plain `make` builds the current host target. `make all` builds all
configured targets.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` with C11 support

Protocol dependencies used by the C implementation are included in the project.

### Optional Cross-Compilation SDKs

Required only for their respective targets:

- MinGW for Windows;
- `wine` for Windows tests on Linux;
- `osxcross` with Apple SDKs for macOS and iOS;
- Android NDK for Android.

---

## Beta Notice

This is a beta project tested primarily on Debian x86_64. It was created out of
a personal need for these libraries, but no guarantees are provided regarding
its stability or future support. You are free to test it, use it, and modify it.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
