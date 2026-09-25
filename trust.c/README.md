# trust.c - Portable scoped trust and message cryptography

`trust.c` manages persistent cryptographic trust on the local machine. It
establishes scoped identities and protects messages, but it does not implement
networking or any other transport.

The library can be composed with `redp2p`, `netl.c`, `nets.c`, HTTP,
pipes, files, or any application-specific delivery mechanism.

## Local model

There is no Bob object, Alice object, connection object, or session object in
the public model.

Each machine initializes its own local trust state:

```javascript
trust.init()
```

That shorthand represents the local library instance used by a binding. In C,
`kc_trust_init()` returns an opaque `kc_trust_t *` handle to the local
persistent store, and that handle is passed to the remaining C functions.

Bob and Alice are only roles used below to explain two different machines.

The public semantic surface is:

```text
init
invite
join
confirm
seal
unseal
revoke
```

Noise handshakes, static keys, ephemeral keys, PSKs, transcript hashes, files,
and persistence details remain internal to `trust.c`.

## Establishing trust

Assume Bob wants to trust Alice.

On Bob's machine:

```javascript
trust.init()

invite = trust.invite()
alice_uid = invite.uid
code = invite.code
```

Bob gives `code` to Alice through an out-of-band mechanism such as a QR code.

`alice_uid` identifies Alice. Bob keeps that UID and will later use it when
sending messages to Alice.

On Alice's machine:

```javascript
trust.init()

joined = trust.join(code)
bob_uid = joined.uid
confirmation = joined.confirmation
```

`bob_uid` identifies Bob. Alice keeps that UID and will later use it when
sending messages to Bob.

The application transports `confirmation` back to Bob using any mechanism it
chooses.

Back on Bob's machine:

```javascript
confirmed_uid = trust.confirm(confirmation)

// confirmed_uid == alice_uid
```

At this point the relationship is established.

The two endpoint UIDs are different:

```text
alice_uid != bob_uid
```

Their meaning is directional only from the caller's point of view:

```text
alice_uid
    Bob:   remote UID
    Alice: local UID

bob_uid
    Alice: remote UID
    Bob:   local UID
```

No human-readable name such as `"alice"` or `"bob"` participates in the
cryptographic protocol.

## Sending a message

After trust is established, Bob sends a message to Alice like this:

```javascript
// Bob
enc = trust.seal(alice_uid, message)

// The application transports:
request.uid = alice_uid
request.message = enc
```

Alice receives that application envelope:

```javascript
// Alice
dec = trust.unseal(request.uid, request.message)
```

Here `request.uid` is `alice_uid`.

The same UID is therefore used in two different roles:

```text
Bob:
    seal(remote_uid, plaintext)

Alice:
    unseal(local_uid, ciphertext)
```

The reverse direction is symmetric:

```javascript
// Alice
enc = trust.seal(bob_uid, reply)

request.uid = bob_uid
request.message = enc

// Bob
dec = trust.unseal(request.uid, request.message)
```

`seal()` and `unseal()` only transform bytes. They do not send or receive
anything.

## Revocation

`revoke(uid)` receives the remote UID known by the local application.

Bob revokes Alice with:

```javascript
trust.revoke(alice_uid)
```

Alice revokes Bob with:

```javascript
trust.revoke(bob_uid)
```

Revocation is an explicit local administrative action. A failed
`unseal()` does not automatically revoke anything.

## Responsibility boundary

`trust.c` provides:

- scoped cryptographic identity;
- one-use out-of-band invitations;
- confirmation of an invited endpoint;
- authenticated encryption for established relationships;
- persistent local relationship state;
- explicit local revocation.

`trust.c` does not provide or inspect:

- TCP, UDP, KCP, HTTP, WebSocket, or another transport;
- hosts, ports, listeners, requests, or responses;
- message ordering;
- retries or timeouts;
- counters or sequence numbers;
- replay windows;
- clocks or temporal validity;
- human usernames or application identities.

If the caller supplies the same valid protected blob more than once,
`kc_trust_unseal()` may successfully decrypt it more than once.

Replay policy belongs to the protocol or transport that has temporal context.
For example, `redp2p` can enforce replay protection with its own counters
without involving `trust.c`.

## Persistent state

Normal callers do not choose a storage directory.

`kc_trust_init()` resolves the local per-user data directory and ensures the
trust store exists.

On POSIX, when `XDG_DATA_HOME` is set:

```text
$XDG_DATA_HOME/kaisarcode/trust.c
```

Otherwise:

```text
$HOME/.local/share/kaisarcode/trust.c
```

On Windows:

```text
%LOCALAPPDATA%\kaisarcode\trust.c
```

`APPDATA` is used as fallback.

`KC_TRUST_DIR` is an advanced process-level override used for tests and
controlled deployments. It is not a normal API argument.

The current implementation keeps relationship credentials scoped to each
relationship. They are generated internally by `invite()` and `join()` and
persisted by `trust.c`.

Applications do not read, write, or transport private keys.

`kc_trust_close()` releases only the in-memory C context. It does not delete
the persistent trust store.

## Invitation semantics

`kc_trust_invite()` creates a pending one-use relationship and returns:

```text
uid   = UID assigned to the invited endpoint
code  = opaque Base64 invitation
```

For Bob inviting Alice, the returned UID is `alice_uid`.

The invitation code contains the protocol data required by Alice, including
both endpoint UIDs, Bob's scoped public key, and a one-use invitation secret.

`kc_trust_join()` consumes that code on Alice's machine and returns:

```text
uid           = UID assigned to the inviter
confirmation  = opaque Base64 confirmation
```

For Alice joining Bob, the returned UID is `bob_uid`.

`kc_trust_confirm()` consumes the confirmation on Bob's machine. On success,
it establishes the pending relationship, destroys the one-use invitation
secret, and returns the same `alice_uid` originally returned by
`kc_trust_invite()`.

The application is responsible only for moving `code` and `confirmation`
between the machines.

## Message semantics

`kc_trust_seal()` receives a remote UID:

```text
seal(remote_uid, plaintext) -> protected blob
```

The application transports the protected blob together with that UID.

On the receiving endpoint, that same UID is local:

```text
unseal(local_uid, protected blob) -> plaintext
```

A UID is therefore an endpoint identifier within one scoped relationship, not
a connection identifier, request identifier, username, or transport address.

Messages may contain arbitrary binary data up to `KC_TRUST_MAX_MESSAGE`.

## Public C API

The real reusable C API is:

```c
typedef struct kc_trust kc_trust_t;

#define KC_TRUST_OK 0
#define KC_TRUST_ERROR -1

#define KC_TRUST_UID_SIZE 36
#define KC_TRUST_MAX_MESSAGE (64 * 1024 * 1024)

uint64_t kc_trust_version(void);

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
```

The `kc_trust_t *` parameter is the local C context returned by
`kc_trust_init()`. It is plumbing required by the C ABI, not a peer object or
network session.

`kc_trust_free()` releases strings and blobs allocated by `trust.c`.

The operation-specific UID rules are:

```text
kc_trust_invite
    returns the invited endpoint UID

kc_trust_join
    returns the inviter UID

kc_trust_confirm
    returns the invited endpoint UID

kc_trust_seal
    receives a remote UID

kc_trust_unseal
    receives the local destination UID from the application envelope

kc_trust_revoke
    receives a remote UID
```

## CLI

The CLI exposes the same semantics:

```text
trust init
trust invite
trust join <code>
trust confirm <confirmation>
trust seal <remote_uid>
trust unseal <local_uid>
trust revoke <remote_uid>
```

`trust invite` prints:

```json
{"uid":"<invited_uid>","code":"<invitation>"}
```

`trust join <code>` prints:

```json
{"uid":"<inviter_uid>","confirmation":"<confirmation>"}
```

`trust confirm <confirmation>` prints:

```json
{"uid":"<invited_uid>"}
```

`trust seal <remote_uid>` reads plaintext bytes from stdin and writes only
the protected binary blob to stdout.

`trust unseal <local_uid>` reads a protected binary blob from stdin and
writes only plaintext bytes to stdout.

The CLI does not transport the UID, invitation, confirmation, ciphertext, or
plaintext anywhere. The caller is responsible for doing that.

## Cryptographic profile

Trust establishment uses:

```text
Noise_Xpsk1_25519_ChaChaPoly_BLAKE2b
```

Established messages use:

```text
Noise_K_25519_ChaChaPoly_BLAKE2b
```

For both protocols, the two directional endpoint UIDs form the Noise prologue
in initiator/responder order.

The implementation uses the vendored Monocypher sources for X25519,
ChaCha20-Poly1305, BLAKE2b, constant-time comparison, and secret wiping.

There is no OpenSSL, OpenSSH, GPG, libsodium runtime, daemon, or system crypto
command dependency.

Platform entropy comes from:

- `BCryptGenRandom` on Windows;
- `getentropy()` under Emscripten;
- `/dev/urandom` on other POSIX targets.

Low-order X25519 shared secrets are rejected.

## WASM

The reusable API is built for Emscripten.

The WASM module exports:

```text
_kc_trust_init
_kc_trust_invite
_kc_trust_join
_kc_trust_confirm
_kc_trust_seal
_kc_trust_unseal
_kc_trust_revoke
_kc_trust_close
_kc_trust_free
_kc_trust_version
```

The reusable WASM contract contains no CLI process machinery.

The filesystem visible to the Emscripten runtime is used for local state.
Durability in a browser depends on the host filesystem integration.

## Build and test

```sh
make all
make test
make test wine
make test wasm
```

Native and Wine contract runs exercise the reusable API plus one grouped
`kc_trust_cli` case. WASM exercises the reusable API only.
