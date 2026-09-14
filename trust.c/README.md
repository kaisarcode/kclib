# trust.c - Message Cryptography with TOFU Peer Identity

`trust.c` is a small C11 library and CLI for protecting messages with the Noise protocol `Noise_X_25519_ChaChaPoly_BLAKE2b` and its one-way `X` handshake pattern.
Each sealed message carries a fresh ephemeral public key and an encrypted sender static public key.
Incoming messages are authenticated before being evaluated against an in-memory Trust-On-First-Use store.

---

## CLI

### Examples

Alice creates an identity and exports its public key:

```bash
trust init
trust pk > alice.pub
```

Bob does the same:

```bash
trust init
trust pk > bob.pub
```

Alice sends `alice.pub` to Bob, and Bob sends `bob.pub` to Alice. These binary
32-byte files are the canonical public-key exchange format and need no
conversion. Alice can use Bob's file directly:

```bash
trust seal bob.pub < message > payload
trust trust bob bob.pub
```

Bob can use Alice's file directly:

```bash
trust seal alice.pub < message > payload
trust trust alice alice.pub
```

Open a sealed message from stdin (TOFU with peer_id):

```bash
trust open alice < payload
```

Open a sealed message without trust evaluation:

```bash
trust open < payload
```

Open with a custom key path:

```bash
trust --key ~/.bob/id open alice < payload
```

Explicitly trust a peer binding:

```bash
trust trust alice alice.pub
```

Remove a peer binding:

```bash
trust forget alice
```

List persisted binding hashes:

```bash
trust peers
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `init` | Generate a new identity keypair |
| `pk` | Write the local 32-byte public key |
| `seal <key_file>` | Seal stdin for a recipient |
| `open [peer_id]` | Open a sealed message from stdin |
| `trust <peer_id> <key_file>` | Trust a peer binding |
| `forget <peer_id>` | Remove a peer binding |
| `peers` | List persisted binding hashes |
| `--key <path>` | Override the identity key path |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

CLI flags override environment variables, which override built-in defaults.

Identity files are exactly 64 bytes: `[sk:32][pk:32]`. Recipient and `trust`
key files must be exactly 32-byte public keys or exactly 64-byte identity
files; for identity files, the public key is derived from the secret half.
`trust pk` writes exactly 32 raw bytes with no newline or encoding, so its output
can be used directly as a recipient or trust key file.

`seal` accepts at most 64 MiB (67,108,864 bytes) from stdin. `open` accepts at
most `KC_TRUST_MAX_PAYLOAD` (67,125,384 bytes). Both reject input containing
an additional byte.

`trust seal` reads message bytes from stdin and writes the encrypted binary
payload to stdout. `trust open` reads that payload and
writes exactly one JSON document to stdout. Successful authenticated results
use this shape:

```json
{"ok":true,"status":"ok","peer_pk":"<64 lowercase hex>","message":"<base64>"}
```

`status` is `ok`, `peer_new`, or `peer_changed`. The message is standard
Base64 so arbitrary binary and empty messages remain representable. All three
statuses exit zero because authentication and decryption succeeded; the
calling application decides trust policy. Normal failures exit nonzero and
return JSON such as:

```json
{"ok":false,"error":"authentication_failed"}
```

The stable error values are `invalid_input`, `message_too_large`,
`identity_error`, `state_error`, and `authentication_failed`. stderr is
reserved for exceptional failures that prevent writing a valid JSON response.

`TRUST_STATE_DIR` sets the state directory. Its POSIX default follows the
XDG Base Directory convention: `$XDG_DATA_HOME/trust` when that variable is
set, otherwise `$HOME/.local/share/trust`. Its Windows default is
`%LOCALAPPDATA%\trust`, falling back to `%USERPROFILE%\.trust` when
`LOCALAPPDATA` is unavailable. Resolution fails if the required platform
environment variables are unavailable; trust never uses `.` as an implicit
persistent state root.

The default identity is `<state>/id` and persisted CLI bindings are under
`<state>/trust/`. `TRUST_KEY` overrides only the identity file and never moves
the trust directory. Explicit values in `kc_trust_options_t` take precedence
over environment values.

---

## Public API

```c
#include "libtrust.h"

kc_trust_options_t opts = kc_trust_options_default();
kc_trust_t *ctx = NULL;

if (kc_trust_create(&ctx, &opts) == KC_TRUST_OK) {
    unsigned char recipient_pk[32];
    /* ... load recipient_pk ... */
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    kc_trust_seal(ctx, recipient_pk, message, message_len,
        &payload, &payload_len);
    /* payload contains the sealed message */
    free(payload);
    kc_trust_close(ctx);
}

kc_trust_options_free(&opts);
```

---

## Lifecycle

- `kc_trust_options_default()` - creates caller-owned default options.
- `kc_trust_options_load_env()` - applies documented environment overrides.
- `kc_trust_options_free()` - releases option-owned strings.
- `kc_trust_generate()` - generates a new identity keypair and writes it to disk.
- `kc_trust_create()` - allocates a caller-owned context and loads the identity.
- `kc_trust_public_key()` - returns the context-owned 32-byte public key.
- `kc_trust_seal()` - protects an outgoing message for a recipient.
- `kc_trust_open()` - verifies and opens an incoming payload.
- `kc_trust_trust()` - adds or replaces a peer trust binding.
- `kc_trust_forget()` - removes a peer trust binding.
- `kc_trust_result_free()` - releases a result and wipes sensitive data.
- `kc_trust_close()` - releases the context and wipes all sensitive material.

## Trust Model

`libtrust` trust is context-local, in-memory only, and managed through explicit
caller operations. The library performs no filesystem persistence.

When `kc_trust_open()` is called with a `peer_id`:

- No binding exists for the peer identifier: `KC_TRUST_PEER_NEW` (1)
- Binding exists and matches: `KC_TRUST_OK` (0)
- Binding exists and differs: `KC_TRUST_PEER_CHANGED` (2)

When `peer_id` is NULL, trust evaluation is skipped and the result is `KC_TRUST_OK`.

Incoming processing never modifies trust state. The caller decides what to do with trust-related results.

The CLI separately owns optional persistence under `<state>/trust/`. It stores exactly 32-byte public-key bindings under BLAKE2b-256 hashes of `peer_id`,
explicitly loads a binding into a library context for `open`, and discards that in-memory binding afterward. Malformed binding files are ignored and therefore evaluate as new peers.
Incoming processing never creates or updates the persisted binding.

On POSIX, trust-created state and trust directories use mode 0700. Existing
state and trust directories must be directories, must not be symlinks, and
must not be writable by group or others. On Windows, both paths must be
directories and reparse points are rejected. Existing directories are not
silently rewritten.

Library peer identifiers are opaque sequences from 1 through 256 bytes. CLI
peer identifiers are non-empty command-line strings of at most 256 bytes and
are hashed byte-for-byte without normalization.

`trust peers` prints valid persisted filename identifiers: one 64-character
lowercase BLAKE2b-256 hash per line. The original `peer_id` is not stored or
printed directly; guessable identifiers can still be tested against the hash.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

```bash
make test
```

To run tests in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

### Multiarch Builds

Cross-compiled artifacts are placed under `bin/{arch}/{platform}/` for the requested target.

```bash
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

Builds a single target only. Cross-compilation verifies compilation only; runtime validation requires the target environment.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Dependencies

- [Monocypher](https://monocypher.org) (BSD-2-Clause / CC0) - vendored under `lib/monocypher/`

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- osxcross for macOS cross-compilation from Linux.
- Android NDK for Android cross-compilation.

### Test Dependencies

- No additional test dependencies required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support.
You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub,
and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
