# redp2p.c - Peer-to-Peer Transport

`redp2p.c` creates direct peer-to-peer tunnels for TCP and UDP services.

One machine publishes a local service under an ID, another machine connects to
that ID and gets a local port that reaches the published service. An index helps
the two peers find each other and establish the connection; application traffic
then travels directly between the peers.

---

## CLI

REDP2P has three commands:

- `idx` runs an index used by peers to find each other.
- `pub` publishes a local TCP or UDP service.
- `con` connects to a published service and exposes it on localhost.

### Example

Suppose a TCP service is already running on port `8080` of the machine that
will publish it.

Run an index on a reachable machine:

```bash
redp2p idx 9876
```

Publish the service as `web`:

```bash
redp2p pub web@index.example.com:9876 --tcp 8080
```

On another machine, connect to `web` and expose it locally on port `9000`:

```bash
redp2p con web@index.example.com:9876 9000
```

The remote service is now available through:

```text
127.0.0.1:9000
```

The same flow works for UDP by publishing with `--udp`.

The index address may omit the port; the default is `9876`.

### Index

Start an index:

```bash
redp2p idx 9876
```

Limit the number of publishers:

```bash
redp2p idx 9876 --seats 100
```

Require proof of work when publishers register:

```bash
redp2p idx 9876 --pow 16
```

Limit simultaneous consumers per publisher:

```bash
redp2p idx 9876 --max-consumers 20
```

List the publishers currently announced on a local index:

```bash
redp2p idx 9876 --list
```

### Publish

Publish a local TCP service:

```bash
redp2p pub myservice@index.example.com --tcp 8080
```

Publish a local UDP service:

```bash
redp2p pub myservice@index.example.com --udp 5353
```

### Connect

Connect to a publisher and expose the tunnel on a local port:

```bash
redp2p con myservice@index.example.com 9000
```

The protocol does not need to be specified by the consumer. It is learned from
the publisher registration.

### Parameters

| Command / option | Description |
| :--- | :--- |
| `idx <port>` | Run an index on the given port |
| `idx <port> --list` | List publishers announced on the local index |
| `--seats <N>` | Maximum number of publishers; unset means unlimited |
| `--pow <N>` | Registration proof-of-work bits, from 0 to 32 |
| `--max-consumers <N>` | Maximum simultaneous consumers per publisher |
| `pub <id>@<index[:port]> --tcp <port>` | Publish a local TCP service |
| `pub <id>@<index[:port]> --udp <port>` | Publish a local UDP service |
| `con <id>@<index[:port]> <local-port>` | Connect to a publisher |
| `--stun <url>` | Use a STUN server for peer discovery |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

### Environment

| Variable | Description |
| :--- | :--- |
| `REDP2P_SEATS` | Default publisher capacity for `idx` |
| `REDP2P_POW` | Default registration proof-of-work bits |
| `REDP2P_PASS` | Registration password used by the index and publisher |
| `REDP2P_VIP` | Reserved IDs as `<id> <pass> ...` pairs |
| `REDP2P_MAX_CONSUMERS_PER_PUBLISHER` | Default consumer limit per publisher |
| `REDP2P_STUN` | Default STUN URL for `pub` and `con` |

Command-line options override the corresponding environment values.

`idx`, `pub`, and `con` keep running until interrupted with SIGINT or
SIGTERM.

---

## Public API

The library exposes the same three main capabilities as the CLI: start an index,
publish a service, and connect to a publisher.

Publish an existing local TCP service:

```c
#include "libredp2p.h"

kc_redp2p_pub_options_t options = {0};
kc_redp2p_pub_t *pub = NULL;

options.id = "web";
options.index = "index.example.com:9876";
options.protocol = KC_REDP2P_TCP;
options.port = 8080;

if (kc_redp2p_pub(&pub, &options) == KC_REDP2P_OK) {
    /* "web" is available while pub remains open. */
    kc_redp2p_pub_close(pub);
}
```

Connect to that publisher and expose it on local port `9000`:

```c
#include "libredp2p.h"

kc_redp2p_con_options_t options = {0};
kc_redp2p_con_t *con = NULL;

options.id = "web";
options.index = "index.example.com:9876";
options.port = 9000;

if (kc_redp2p_con(&con, &options) == KC_REDP2P_OK) {
    /* The service is available on 127.0.0.1:9000. */
    kc_redp2p_con_close(con);
}
```

The main public functions are:

```c
int kc_redp2p_idx(kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options);
int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options);
int kc_redp2p_con(kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options);

int kc_redp2p_idx_list(kc_redp2p_idx_t *idx,
    kc_redp2p_idx_entry_t **out_entries, size_t *out_count);

void kc_redp2p_idx_close(kc_redp2p_idx_t *idx);
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub);
void kc_redp2p_con_close(kc_redp2p_con_t *con);

const char *kc_redp2p_strerror(int status);
uint64_t kc_redp2p_version(void);
```

The public API also supports direct application data without opening a local
service port through `kc_redp2p_con_send()`, publisher receive callbacks, and
`kc_redp2p_client_respond()`.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

Build project artifacts first, then run the test suite:

```bash
make
make test
```

To run the Windows build through Wine:

```bash
make x86_64/windows
make test wine
```

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

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
- `gcc` or `clang` (C11 compatible)

The protocol dependencies used by the library are included in the project
source tree.

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS and iOS.
- Android NDK for Android targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
