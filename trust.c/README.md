# trust.c

Persistent scoped trust and authenticated message protection.

trust.c creates trust relationships between endpoints and uses those
relationships to protect arbitrary binary messages. It does not send or
receive data. The application can move invitations, confirmations, UIDs, and
protected messages through any transport.

## Public model

Each process opens its local trust state once:

```c
kc_trust_t *trust = NULL;

if (kc_trust_init(&trust) != KC_TRUST_OK) {
    return 1;
}
```

The public operations are:

```text
invite / join / confirm = establish trust
seal / unseal           = protect and recover messages
revoke                  = remove local trust
close                   = release the local handle
```

A trust relationship has two endpoint UIDs.

For a relationship between Bob and Alice:

```text
alice_uid identifies Alice
bob_uid   identifies Bob
```

The same UID changes perspective depending on which machine is using it:

```text
alice_uid
    remote on Bob
    local on Alice

bob_uid
    remote on Alice
    local on Bob
```

Applications can associate these UIDs with their own users, devices, scopes,
or other domain objects. trust.c does not define human-readable identities.

## C API

```c
#include "libtrust.h"

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

Strings and buffers returned through `out_*` are owned by the caller and
must be released with `kc_trust_free()`.

`kc_trust_close()` releases the local handle. It does not remove persistent
trust relationships.

## Establishing trust

One endpoint creates an invitation:

```c
char *alice_uid = NULL;
char *code = NULL;

if (kc_trust_invite(trust, &alice_uid, &code) != KC_TRUST_OK) {
    return 1;
}
```

The application sends `code` to the other endpoint out of band.

The invited endpoint joins:

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

The application sends `confirmation` back to the endpoint that created the
invitation.

That endpoint confirms it:

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

`confirmed_uid` is the same endpoint UID originally returned by
`kc_trust_invite()`:

```text
confirmed_uid == alice_uid
```

The UID returned by `kc_trust_join()` identifies the other endpoint:

```text
alice_uid != bob_uid
```

After confirmation, both sides can protect messages in either direction.

## Messages

To send a message to a trusted endpoint, call `kc_trust_seal()` with that
endpoint's UID.

For Bob sending to Alice:

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

The application transports:

```text
uid     = alice_uid
message = protected
```

On Alice's machine, that UID is local. Alice opens the message with the UID
received beside it:

```c
void *message = NULL;
size_t message_size = 0;

if (kc_trust_unseal(
    trust,
    request_uid,
    protected,
    protected_size,
    &message,
    &message_size
) != KC_TRUST_OK) {
    return 1;
}
```

For the reverse direction, Alice seals with `bob_uid` and Bob unseals with
that same UID.

`kc_trust_seal()` and `kc_trust_unseal()` do not perform transport,
ordering, retries, timeouts, replay detection, or request lifecycle handling.
Those concerns belong to the surrounding application or protocol.

A valid protected message may therefore be successfully passed to
`kc_trust_unseal()` more than once.

Messages may contain arbitrary binary data up to `KC_TRUST_MAX_MESSAGE`.

## Revocation

`kc_trust_revoke()` receives the remote UID known by the local endpoint.

Bob revokes Alice with `alice_uid`:

```c
kc_trust_revoke(trust, alice_uid);
```

Alice revokes Bob with `bob_uid`:

```c
kc_trust_revoke(trust, bob_uid);
```

Revocation is local. A failed `kc_trust_unseal()` does not revoke a
relationship automatically.

## Persistent state

Normal callers do not select a storage directory.

On XDG environments, trust.c uses:

```text
$XDG_DATA_HOME/kaisarcode/trust.c
```

When `XDG_DATA_HOME` is not set:

```text
$HOME/.local/share/kaisarcode/trust.c
```

On Windows, trust.c uses the corresponding per-user application-data
directory under:

```text
kaisarcode\trust.c
```

`KC_TRUST_DIR` is an advanced process-level override for tests and controlled
deployments.

## CLI

```text
Usage: trust <command> [arguments]

Commands:
    init
    invite
    join <code>
    confirm <confirmation>
    seal <remote_uid>
    unseal <local_uid>
    revoke <remote_uid>
```

`invite` prints:

```json
{"uid":"<invited_uid>","code":"<invitation>"}
```

`join <code>` prints:

```json
{"uid":"<inviter_uid>","confirmation":"<confirmation>"}
```

`confirm <confirmation>` prints:

```json
{"uid":"<invited_uid>"}
```

`seal <remote_uid>` reads plaintext bytes from stdin and writes the protected
binary message to stdout.

`unseal <local_uid>` reads a protected binary message from stdin and writes
the plaintext bytes to stdout.

The CLI does not transport any of these values.

## Build

```bash
make
```

Build all configured targets:

```bash
make all
```

## Tests

Native contract tests:

```bash
make test
```

Windows validation through Wine:

```bash
make test wine
```

WebAssembly validation:

```bash
make test wasm
```
