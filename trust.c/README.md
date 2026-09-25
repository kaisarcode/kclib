# trust.c - Portable scoped trust and message cryptography

\`trust.c\` establishes small, persistent trust relationships and protects
messages between the two established endpoints. It deliberately does not
implement networking.

The library is intended to compose with transports such as \`redp2p\`,
\`netl.c\`, \`nets.c\`, \`http.c\`, pipes, files, or application-specific
message delivery. A transport moves blobs; \`trust.c\` decides whether a blob
belongs to an established cryptographic identity and encrypts or decrypts its
contents.

## Consumer model

The public model is intentionally small:

\`\`\`text
init
invite -> join -> confirm
seal <-> unseal
revoke
\`\`\`

All Noise handshake machinery, static and ephemeral keys, PSKs, transcript
hashes, nonces, and persistent key files are internal implementation details.

A typical application flow is:

\`\`\`javascript
// Bob's machine
const bob = trust.init();
const invitation = bob.invite();

// Bob gives invitation.code to Alice out of band.
// Bob stores invitation.uid as the application UID for this scope.

// Alice's machine
const alice = trust.init();
const joined = alice.join(invitationCode);

// Alice stores joined.uid as the application UID for Bob.
// The application sends joined.confirmation back to Bob by any transport.

// Bob's machine
const aliceUid = bob.confirm(confirmation);

// Later:
const protectedMessage = bob.seal(aliceUid, "Hello Alice!");
\`\`\`

Alice receives the surrounding application's UID and protected blob:

\`\`\`javascript
const plaintext = alice.unseal(request.uid, request.message);
\`\`\`

The UID is an application-visible UUIDv4 identifying one scoped trust
relationship. Both endpoints use the same UID bytes; each application is free
to label or associate that UID with its own domain objects.

No human or application name such as \`"alice"\` participates in the
cryptographic protocol.

## Trust establishment

\`invite()\` creates a one-use invitation. It returns:

- a canonical UUIDv4 string used by the application as the scope UID;
- an opaque Base64 invitation code suitable for a QR code, text, file, or any
  other out-of-band transfer selected by the application.

The invitation contains the scope UID, a 32-byte one-use PSK, and the inviter's
static public key. The inviter persists the matching local static secret and
PSK as pending state.

\`join(code)\` is performed on the invited endpoint. It:

- imports the UID and inviter public key;
- generates a static key pair scoped to this relationship;
- persists the local static secret plus the inviter public key;
- returns the same UID and one opaque Base64 confirmation.

The confirmation is generated with:

\`\`\`text
Noise_Xpsk1_25519_ChaChaPoly_BLAKE2b
\`\`\`

The scope UID is mixed into the Noise prologue. The \`Xpsk1\` pattern lets the
joining endpoint transmit its static public key to the inviter while proving
knowledge of the one-use PSK.

\`confirm(confirmation)\` identifies the matching pending invitation from the
confirmation UID, verifies the Noise message and PSK, records the joiner's
static public key, destroys the pending invitation secret, and returns the
confirmed UID.

A confirmation is deterministic from the application's point of view: a valid
confirmation establishes the relationship; an invalid confirmation is rejected.
There is no public handshake object or protocol-step API.

The Noise protocol specification is at <https://noiseprotocol.org/noise.html>.

## Messages

After confirmation, each endpoint persists, for that UID:

\`\`\`text
local static secret key
remote static public key
\`\`\`

\`seal(uid, message)\` and \`unseal(uid, protected)\` use:

\`\`\`text
Noise_K_25519_ChaChaPoly_BLAKE2b
\`\`\`

The Noise \`K\` one-way pattern is appropriate because both static public keys
are already known after trust establishment. Every protected blob starts a
fresh one-way Noise handshake with a fresh ephemeral key. The scope UID is
mixed into the Noise prologue.

The handshake itself carries an empty payload. After \`Split()\`, the first
one-way transport \`CipherState\` protects an encrypted logical length followed
by bounded Noise transport records. This allows one logical message up to
64 MiB while every individual Noise message remains within Noise's 65535-byte
limit.

\`seal()\` and \`unseal()\` are blob transforms only:

\`\`\`text
plaintext + UID -> protected blob
protected blob + UID -> plaintext
\`\`\`

They do not send or receive anything.

## Responsibility boundary

\`trust.c\` provides:

- scoped cryptographic identity;
- one-use out-of-band trust invitations;
- authentication of the invited endpoint;
- confidentiality and integrity for established UIDs;
- local persistence of cryptographic relationship state;
- explicit revocation.

\`trust.c\` does **not** provide or inspect:

- TCP, UDP, KCP, HTTP, WebSocket, or any other transport;
- hosts, ports, listeners, connections, requests, or responses;
- ordering, retries, timeouts, counters, sequence numbers, or replay windows;
- application usernames or human identity;
- a clock or temporal validity policy.

If a caller supplies the same valid protected blob repeatedly,
\`kc_trust_unseal()\` may authenticate and decrypt it repeatedly. Replay policy
belongs to the composing protocol, such as the counters already maintained by a
transport.

## Persistent state

Normal callers never select a storage directory.

\`kc_trust_init()\` follows the repository's per-user data convention:

On POSIX, when \`XDG_DATA_HOME\` is set:

\`\`\`text
$XDG_DATA_HOME/kaisarcode/trust.c
\`\`\`

otherwise:

\`\`\`text
$HOME/.local/share/kaisarcode/trust.c
\`\`\`

On Windows the corresponding per-user application-data directory is used:

\`\`\`text
%LOCALAPPDATA%\kaisarcode\trust.c
\`\`\`

with \`APPDATA\` as fallback.

\`KC_TRUST_DIR\` is an advanced process-level override for contract tests and
controlled deployments. It is not a normal API argument.

State is split internally into pending invitations and established peers.
Secret-bearing files are written as private per-user files on POSIX. Existing
trust directories must resolve as real directories rather than symlinked final
paths and must not be group/world writable.

The file format is private to \`trust.c\`. Applications persist only their own
UID associations; they do not store or manipulate trust keys.

## Public C API

\`\`\`c
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
\`\`\`

Strings and blobs returned through \`out_*\` are owned by the caller and are
released with \`kc_trust_free()\`. The context is released with
\`kc_trust_close()\`.

## CLI

The CLI exposes the same semantic operations:

\`\`\`text
trust init
trust invite
trust join <code>
trust confirm <confirmation>
trust seal <uid>
trust unseal <uid>
trust revoke <uid>
\`\`\`

\`invite\`, \`join\`, and \`confirm\` print compact JSON containing their
application-visible UID and portable code/confirmation.

\`seal\` reads plaintext bytes from stdin and writes only the protected binary
blob to stdout.

\`unseal\` reads a protected binary blob from stdin and writes only plaintext
bytes to stdout.

The application is responsible for transporting those values between the two
machines.

## Cryptographic implementation

The protocol profile is fixed:

\`\`\`text
Trust establishment:
    Noise_Xpsk1_25519_ChaChaPoly_BLAKE2b

Established messages:
    Noise_K_25519_ChaChaPoly_BLAKE2b
\`\`\`

The implementation uses the vendored Monocypher sources for X25519,
ChaCha20-Poly1305, BLAKE2b, constant-time comparison, and secret wiping.
There is no OpenSSL, OpenSSH, GPG, libsodium runtime, daemon, or system crypto
command dependency.

Platform entropy comes from:

- \`BCryptGenRandom\` on Windows;
- \`getentropy()\` under Emscripten;
- \`/dev/urandom\` on other POSIX targets.

Low-order X25519 results are rejected.

## WASM

The reusable API is built for Emscripten. Noise and cryptographic operations
are identical to native builds. State uses the filesystem visible to the
Emscripten runtime; durable browser persistence, when desired, is supplied by
the host's filesystem integration rather than by \`trust.c\`.

The WASM module exports:

\`\`\`text
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
\`\`\`

The native CLI is not compiled into the WASM reusable contract tests.

## Build and test

\`\`\`sh
make all
make test
make test wine
make test wasm
\`\`\`

Native and Wine contract runs exercise the reusable API plus exactly one
grouped \`kc_trust_cli\` case. WASM exercises the reusable API only.
