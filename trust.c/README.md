# trust.c - Persistent Scoped Trust

`trust.c` is a small C library and CLI for establishing persistent scoped
trust relationships and protecting binary messages with the Noise Protocol
Framework. It does not implement transport; applications move invitations,
confirmations, UIDs, and protected messages themselves.

---

## CLI

Initialize the local trust store:

```bash
trust init
```

Create a one-use invitation:

```bash
trust invite
{"uid":"<invited_uid>","code":"<invitation>"}
```

Join from an invitation:

```bash
trust join "<invitation>"
{"uid":"<inviter_uid>","confirmation":"<confirmation>"}
```

Confirm the relationship:

```bash
trust confirm "<confirmation>"
{"uid":"<invited_uid>"}
```

Seal stdin for a trusted remote UID:

```bash
printf "hello" | trust seal "<remote_uid>" > message.bin
```

Unseal stdin addressed to a local UID:

```bash
trust unseal "<local_uid>" < message.bin
```

Revoke a trusted remote UID:

```bash
trust revoke "<remote_uid>"
```

### Parameters

| Command | Description |
| :--- | :--- |
| `init` | Ensure the local trust store exists |
| `invite` | Create a one-use trust invitation |
| `join <code>` | Join from an invitation code |
| `confirm <confirmation>` | Confirm a joined invitation |
| `seal <remote_uid>` | Protect stdin for a trusted remote UID |
| `unseal <local_uid>` | Open stdin addressed to a local UID |
| `revoke <remote_uid>` | Revoke an established or pending remote UID |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |


---

## Public API

Initialize the local trust store:

```c
#include "libtrust.h"

kc_trust_t *trust = NULL;

if (kc_trust_init(&trust) != KC_TRUST_OK) {
    return 1;
}
```

Create an invitation:

```c
char *alice_uid = NULL;
char *code = NULL;

if (kc_trust_invite(trust, &alice_uid, &code) != KC_TRUST_OK) {
    return 1;
}
```

On the invited endpoint, join with the invitation code:

```c
char *bob_uid = NULL;
char *confirmation = NULL;

if (kc_trust_join(
    trust,
    code,
    &bob_uid,
    &confirmation
) != KC_TRUST_OK) {
    return 1;
}
```

The application moves `confirmation` back to the endpoint that created the
invitation, which confirms it:

```c
char *confirmed_uid = NULL;

if (kc_trust_confirm(
    trust,
    confirmation,
    &confirmed_uid
) != KC_TRUST_OK) {
    return 1;
}
```

`confirmed_uid` is the same UID returned by `kc_trust_invite()`.

To protect a message for Alice:

```c
void *protected = NULL;
size_t protected_size = 0;

if (kc_trust_seal(
    trust,
    alice_uid,
    message,
    message_size,
    &protected,
    &protected_size
) != KC_TRUST_OK) {
    return 1;
}
```

The application transports `alice_uid` beside the protected bytes. On
Alice's endpoint, that same UID is local and is passed to
`kc_trust_unseal()`:

```c
void *message = NULL;
size_t message_size = 0;

if (kc_trust_unseal(
    trust,
    alice_uid,
    protected,
    protected_size,
    &message,
    &message_size
) != KC_TRUST_OK) {
    return 1;
}
```

The reverse direction uses `bob_uid` in the same way.

The public API is:

```c
typedef struct kc_trust kc_trust_t;

#define KC_TRUST_OK 0
#define KC_TRUST_ERROR -1

#define KC_TRUST_UID_SIZE 36
#define KC_TRUST_MAX_MESSAGE (64 * 1024 * 1024)

int kc_trust_init(kc_trust_t **out);

int kc_trust_invite(
    kc_trust_t *trust,
    char **out_uid,
    char **out_code
);

int kc_trust_join(
    kc_trust_t *trust,
    const char *code,
    char **out_uid,
    char **out_confirmation
);

int kc_trust_confirm(
    kc_trust_t *trust,
    const char *confirmation,
    char **out_uid
);

int kc_trust_seal(
    kc_trust_t *trust,
    const char *uid,
    const void *message,
    size_t message_size,
    void **out_data,
    size_t *out_size
);

int kc_trust_unseal(
    kc_trust_t *trust,
    const char *uid,
    const void *data,
    size_t data_size,
    void **out_message,
    size_t *out_message_size
);

int kc_trust_revoke(
    kc_trust_t *trust,
    const char *uid
);

void kc_trust_close(kc_trust_t *trust);
void kc_trust_free(void *ptr);
uint64_t kc_trust_version(void);
```

A relationship has two endpoint UIDs. The UID returned by
`kc_trust_invite()` identifies the invited endpoint. The UID returned by
`kc_trust_join()` identifies the inviter. They are distinct.

`kc_trust_seal()` receives a remote UID. The application transports that UID
with the protected message. On the receiving endpoint, the same UID is local
and is passed to `kc_trust_unseal()`.

`kc_trust_revoke()` receives the remote UID known by the local endpoint.

`kc_trust_seal()` and `kc_trust_unseal()` do not implement transport,
ordering, retries, timeouts, or replay detection. A valid protected message may
be passed to `kc_trust_unseal()` more than once.

Messages may contain arbitrary binary data up to `KC_TRUST_MAX_MESSAGE`.

Strings and buffers returned through `out_*` are owned by the caller and must
be released with `kc_trust_free()`. `kc_trust_free()` accepts `NULL`.

`kc_trust_close()` releases the local handle. It does not delete persistent
trust relationships.

Normal callers do not select a storage directory. On XDG environments,
trust.c uses `$XDG_DATA_HOME/kaisarcode/trust.c`, falling back to
`$HOME/.local/share/kaisarcode/trust.c`. Windows uses the corresponding
per-user application-data directory under `kaisarcode\trust.c`.

`KC_TRUST_DIR` is an advanced process-level override for tests and controlled
deployments.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

Native and Wine runs validate the reusable public API plus one grouped CLI
case. WebAssembly validates only the reusable public API; native process
helpers and the CLI case are excluded from the WASM test build at compile
time.

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/trust.wasm`
- Exports: `kc_trust_init`, `kc_trust_invite`, `kc_trust_join`,
    `kc_trust_confirm`, `kc_trust_seal`, `kc_trust_unseal`,
    `kc_trust_revoke`, `kc_trust_close`, `kc_trust_free`,
    `kc_trust_version`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

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

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS and iOS.
- Android NDK for Android targets.
- Emscripten SDK and Node.js for WebAssembly builds and tests.

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
