# redp2p.c - Peer-to-Peer Connectivity

`redp2p.c` is a library for direct peer-to-peer communication.

It is organized around three roles:

- the **index**, which keeps publisher registrations and coordinates connection
  establishment;
- the **publisher**, which registers an ID and accepts consumers;
- the **consumer**, which resolves that ID and connects to the publisher.

```text
           index
          /     \
   publisher - consumer
```

The index participates in discovery and connection setup. Once a connection is
established, application data flows directly between publisher and consumer.

The same model is used by the native C library and the browser implementation.
Native peers use the REDP2P native transport; browser peers use WebRTC data
channels. The native C index and the PHP index implement the same REDP2P index
protocol, including WebRTC signaling.

---

## Public C API

The library is the primary interface to REDP2P.

A publisher receives data from connected consumers through a callback. A
consumer sends data with `kc_redp2p_con_send()` and may receive replies through
its own callback.

No application-facing TCP or UDP port is required in this mode.

### Publisher

```c
#include "libredp2p.h"

static void receive(const kc_redp2p_pub_input_t *input, void *userdata)
{
    (void)userdata;

    /* input->data / input->size came from one consumer. */

    kc_redp2p_client_respond(
        input->client,
        "pong",
        4
    );
}

int main(void)
{
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_pub_options_t options = {0};

    options.id = "echo";
    options.index = "index.example.com:9876";
    options.protocol = KC_REDP2P_TCP;
    options.receive = receive;

    if (kc_redp2p_pub(&pub, &options) != KC_REDP2P_OK)
        return 1;

    /* Keep the application alive while the publisher is in use. */

    kc_redp2p_pub_close(pub);
    return 0;
}
```

With `port = 0`, which is the default above, the publisher works directly
through the library callback. REDP2P owns the transport-side adapter needed to
carry application data.

Each received `kc_redp2p_pub_input_t` identifies the consumer that sent the
data. The publisher can answer that consumer with
`kc_redp2p_client_respond()` or close it with
`kc_redp2p_client_close()`.

### Consumer

```c
#include "libredp2p.h"

static void receive(const void *data, size_t size, void *userdata)
{
    (void)userdata;

    /* data / size came from the publisher. */
}

int main(void)
{
    kc_redp2p_con_t *con = NULL;
    kc_redp2p_con_options_t options = {0};

    options.id = "echo";
    options.index = "index.example.com:9876";
    options.receive = receive;

    if (kc_redp2p_con(&con, &options) != KC_REDP2P_OK)
        return 1;

    kc_redp2p_con_send(con, "ping", 4);

    /* Keep the application alive while the connection is in use. */

    kc_redp2p_con_close(con);
    return 0;
}
```

The consumer does not choose TCP or UDP. It learns the publisher transport from
the index.

### Port adapters

The public API can also adapt an existing local TCP or UDP service instead of
handling application data directly.

For a publisher, setting `options.port` publishes an existing local service:

```c
kc_redp2p_pub_options_t options = {0};

options.id = "web";
options.index = "index.example.com:9876";
options.protocol = KC_REDP2P_TCP;
options.port = 8080;
```

For a consumer, setting `options.port` exposes the remote publisher through a
local loopback port:

```c
kc_redp2p_con_options_t options = {0};

options.id = "web";
options.index = "index.example.com:9876";
options.port = 9000;
```

The remote service is then available locally at `127.0.0.1:9000`.

This port-backed form is an adapter for existing socket-based applications. It
is also the mode exposed by the CLI.

### Index

The C library can run an index directly:

```c
#include "libredp2p.h"

kc_redp2p_idx_t *idx = NULL;
kc_redp2p_idx_options_t options = {0};

options.port = 9876;

if (kc_redp2p_idx(&idx, &options) == KC_REDP2P_OK) {
    /* Keep the index alive while it is in use. */
    kc_redp2p_idx_close(idx);
}
```

The index handles native peer coordination as well as the WebRTC signaling used
by browser peers.

Publisher IDs currently known by a live C index can be obtained with
`kc_redp2p_idx_list()`.

---

## Browser / WebRTC

The browser implementation follows the same publisher/consumer model, but uses
WebRTC data channels as the direct peer transport.

A browser publisher registers an ID in an index. A browser consumer connects to
that ID. The index carries the WebRTC signaling needed to establish the peer
connection; once the data channel is open, application data flows directly
between the browsers.

The browser implementation is provided by:

```text
imp/redp2p-pub.js
imp/redp2p-con.js
```

### Publisher

```html
<script src="redp2p-pub.js"></script>
```

```js
const pub = await RedP2P.pub({
    id: "echo",
    index: "https://example.com/index",

    receive(input) {
        input.client.respond("pong");
    }
});
```

The callback receives the data together with the connected client. The client
can be answered with `respond()` or closed with `close()`.

Stop publishing with:

```js
await pub.close();
```

### Consumer

```html
<script src="redp2p-pub.js"></script>
<script src="redp2p-con.js"></script>
```

```js
const con = await RedP2P.con({
    id: "echo",
    index: "https://example.com/index",

    receive(data) {
        console.log(data);
    }
});

con.send("ping");
```

Close the connection with:

```js
con.close();
```

WebRTC ICE servers can be supplied through `iceServers`:

```js
iceServers: [
    {urls: "stun:stun.example.com:3478"}
]
```

Remote browser indexes must use HTTPS. Plain HTTP is accepted for localhost
development.

---

## Index implementations

REDP2P includes two compatible index implementations.

### Native C index

The native index is part of `libredp2p` and can be created with
`kc_redp2p_idx()` or started through the CLI.

It maintains publisher registrations, coordinates native peer connections, and
handles the signaling operations used by WebRTC publishers and consumers.

### PHP index

`imp/redp2p-idx.php` implements the same REDP2P index protocol over HTTP/JSON.

A minimal SQLite endpoint:

```php
<?php
require 'redp2p-idx.php';

\KaisarCode\Redp2pIndex::serve([
    'dsn' => 'sqlite:/var/lib/redp2p/index.sqlite',
]);
```

The PHP implementation uses PDO and supports SQLite or MySQL.

Requirements:

- PHP
- PDO
- Sodium
- the PDO driver for the selected database

Index policy can be configured with `REDP2P_SEATS`, `REDP2P_POW`,
`REDP2P_PASS`, `REDP2P_VIP`, and
`REDP2P_MAX_CONSUMERS_PER_PUBLISHER`.

---

## CLI

The CLI is a convenience interface for exposing existing local TCP or UDP
services through REDP2P.

It maps the same three roles to commands:

```text
redp2p idx ...
redp2p pub ...
redp2p con ...
```

### Example

Run an index:

```bash
redp2p idx 9876
```

Publish an existing TCP service running on local port `8080`:

```bash
redp2p pub web@index.example.com:9876 --tcp 8080
```

On another machine, expose that publisher on local port `9000`:

```bash
redp2p con web@index.example.com:9876 9000
```

The service is then available through:

```text
127.0.0.1:9000
```

For UDP:

```bash
redp2p pub dns@index.example.com:9876 --udp 5353
redp2p con dns@index.example.com:9876 9001
```

The consumer learns the publisher protocol from the index.

If the index port is omitted, REDP2P uses port `9876`.

### Index options

```bash
redp2p idx 9876 --seats 100
redp2p idx 9876 --pow 16
redp2p idx 9876 --max-consumers 20
```

List publishers known by a local index:

```bash
redp2p idx 9876 --list
```

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
| `con <id>@<index[:port]> <local-port>` | Expose a publisher on a local port |
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

`idx`, `pub`, and `con` run until interrupted with SIGINT or SIGTERM.

---

## Protocol

The REDP2P index protocol is documented in:

```text
doc/protocol.md
```

The native C index, PHP index, and browser implementations use the same
coordination contract.

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

- MinGW for Windows
- `wine` for Windows tests on Linux
- `osxcross` with Apple SDKs for macOS and iOS
- Android NDK for Android

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
