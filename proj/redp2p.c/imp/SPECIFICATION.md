# REDP2P Index Protocol Specification

## Introduction

The REDP2P Index is a stateless HTTP coordination service. It acts as a rendezvous point where publishers register their reachability metadata and consumers discover publishers and initiate NAT traversal.

The protocol is language-agnostic: any HTTP server that can accept JSON POST requests and keep records with a TTL can implement it. The reference implementation is the `redp2p idx <port>` command in C; deployments may use PHP, Python, Go, or any other language. This document is the behavioral contract an index must follow to interoperate with the REDP2P reference client.

**Key properties:**
- Transport: HTTP/1.0 or HTTP/1.1, one JSON POST request per connection
- One logical endpoint, dispatched by the `op` field; the request path is ignored
- No persistent connections; publisher lifetime = TTL from `last_seen`
- Proof-of-work only at registration; publisher controls use sequenced HMAC proofs
- Direct punch coordination via `punch_req` / `punch_poll` (no relay)
- The index does not carry application traffic and runs no UDP or tunnel logic

---

## Common Conventions

### Transport

- Requests use HTTP/1.0 or HTTP/1.1, method `POST`. Any other method is rejected with HTTP `405`.
- The request path is ignored by the server (the reference client sends `POST /redp2p/`; `POST /` also works).
- The request body is a JSON object. The server does not validate the `Content-Type` header, but clients should send `Content-Type: application/json`.
- Transfer-Encoding is not supported and is rejected with HTTP `501`.
- The server closes the connection after replying (`Connection: close`); each request uses a fresh connection.
- Implementations that own accepted sockets enforce a total 5-second request deadline from socket acceptance; zero-byte and partial requests are dropped without a reply when it expires. Hosted implementations may delegate this transport timeout to their HTTP server.

### Request Limits

- Maximum request body: 4096 bytes. A `Content-Length` larger than 4095 is rejected with HTTP `413`.
- Maximum 32 header lines, each at most 256 bytes. Larger headers are rejected with HTTP `431`.
- Request timeout: 5 seconds.

### JSON Rules

- The request body must be a JSON object with a string `op` field.
- Duplicate field names are rejected with `bad_request`.
- Unknown fields are ignored (they are not rejected).
- Requests that parse as non-objects, have a missing `op`, or use an unknown `op` are rejected with `bad_request`.
- All string values are UTF-8. Identifiers, session tokens, and hex tokens are ASCII.
- `udp_port` and candidate `port` must be finite numeric values whose value is an exact integer in `1..65535`. Validity is based on numeric value, not parser or runtime type: `123` and `123.0` are valid, while `123.9` is rejected. JSON token spelling does not otherwise affect this rule.

### Response Envelope

Every JSON response is an object with an `ok` boolean:

- Success: `{ "ok": true, ... }` with operation-specific fields.
- Error: `{ "ok": false, "error": "<code>" }`.

There is no `status`, `code`, or `message` field. Clients must dispatch on HTTP status and on the `error` code.

### HTTP-Level Errors

Requests that fail before JSON parsing (bad request line, wrong method, oversized headers or body, Transfer-Encoding) receive a plain-text body, not JSON. Relevant statuses: `400` (malformed request line), `405` (method not `POST`), `413` (body too large), `431` (headers too large), `501` (Transfer-Encoding). The JSON error bodies below apply only to well-formed HTTP requests with a parseable JSON body.

### Field Definitions

| Field | Type | Description |
|-------|------|-------------|
| `id` | string (1..63) | Publisher/consumer identifier, ASCII alphanumeric only |
| `self_id` | string (1..63) | Consumer identifier in `punch_req`, same rules as `id` |
| `target_id` | string (1..63) | Publisher identifier in `punch_req`, same rules as `id` |
| `session` | string (1..63) | Session identifier, ASCII alphanumeric only |
| `nonce` | hex string (64 chars) | 32-byte random challenge nonce |
| `issued_at` | integer | Challenge issue timestamp in Unix seconds |
| `expires_at` | integer | Challenge expiry timestamp, exactly 60 seconds after issuance |
| `mac` | hex string (64 chars) | HMAC-SHA256(challenge-key, canonical challenge bytes) |
| `pow_solution` | hex string (16 chars) | Exact uint64 PoW solution encoded as lowercase hexadecimal |
| `proof` | hex string (64 chars) | HMAC-SHA256(publisher secret, canonical registration bytes) |
| `access_proof` | hex string (64 chars) | Required admission HMAC when an index password applies |
| `pkey` | hex string (64 chars) | Challenge index X25519 public key or register publisher ephemeral X25519 public key |
| `secret` | hex string (64 chars) | Register ciphertext (16 bytes) followed by XChaCha20-Poly1305 tag (16 bytes) |
| `seq` | integer (1..9007199254740991) | Strictly increasing publisher-control sequence |
| `bits` | integer (0..32) | PoW difficulty, returned by `challenge` |
| `proto` | integer | 1 = TCP (KCP), 2 = UDP |
| `udp_port` | integer (1..65535) | Local UDP port for direct traffic |
| `candidates` | array | Up to 8 normalized candidate objects; optional in `register`, `heartbeat`, and `punch_req` |
| `last_seen` | integer | Unix timestamp (seconds) of the last successful `register` or `heartbeat` |

Hex tokens accept lowercase or uppercase digits; the server echoes and compares hex text exactly.

### Candidate Object

Input and output candidates use the same shape:

```json
{
  "type": "host|observed",
  "addr": "x.x.x.x|xxxx:...",
  "port": 12345
}
```

- `type`: one of `host`, `observed`. Any other value is rejected.
- `addr`: literal IPv4 or IPv6 address (up to 47 characters), validated with `inet_pton`.
- `port`: finite numeric UDP port whose value is an exact integer in `1..65535`; `123` and `123.0` are valid, while `123.9` is rejected regardless of parser or runtime numeric type.

There is no `priority` field on the wire. The server never reads a client-supplied priority. Candidates are normalized with `inet_pton`, deduplicated by type, binary family/address, and port, then sorted by family, binary address, port, and type.

`candidates` is optional. When it is omitted, or present with a value that is not an array, the request is accepted and the stored candidate set becomes empty (on `register` and `heartbeat` this replaces any previously stored set). A present-but-malformed array is rejected with `bad_request`.

---

## Registration Authentication

Each C index context creates one random 32-byte registration challenge key at startup and wipes it on close. PHP stores its equivalent as a validated 64-character lowercase hexadecimal value in `redp2p_meta`, so it survives independent request objects. Challenges remain stateless: no nonce records are stored.

The challenge MAC input is the exact bytes `REDP2P-CHALLENGE` (16 ASCII bytes), raw 32-byte nonce, `issued_at` uint64 big-endian, and `expires_at` uint64 big-endian. The MAC is HMAC-SHA-256 with the challenge key. A challenge is valid only when its MAC compares in constant time, `expires_at > issued_at`, lifetime is exactly 60 seconds, `issued_at <= now + 5`, and `now <= expires_at`.

The challenge `pkey` is a stateless index ephemeral X25519 public key. Its secret key is `HMAC-SHA256(challenge_key, REDP2P-INDEX-KEY || nonce || uint64_be(issued_at) || uint64_be(expires_at))`; its public key is X25519_public of that result. The key pair is recomputed from validated challenge fields and is never stored.

For each registration the publisher creates a fresh random X25519 secret key and sends its public key as register `pkey`. Both sides reject an all-zero raw X25519 shared secret. They derive `encryption_key = HMAC-SHA256(raw_shared_secret, REDP2P-REGISTER-SECRET || nonce || index_pkey || publisher_pkey)`, in that public-key order. The AEAD nonce is the first 24 bytes of the challenge nonce; associated data is empty. The publisher encrypts exactly its existing 16 ASCII hexadecimal control-secret bytes with XChaCha20-Poly1305 and sends `secret = hex(ciphertext[16] || tag[16])`. The plaintext secret never appears in registration JSON. The index decrypts before constructing the existing canonical registration message, proofs, storage, and later control paths.

PoW is `SHA-256(REDP2P-POW || nonce || issued_at || expires_at || uint16(id length) || id || pow_solution)`, with integer fields big-endian. The digest must have at least `bits` leading zero bits. `bits` remains 0..32 and the registration proof remains mandatory at zero difficulty.

The registration proof is HMAC-SHA-256 keyed by the exact 16 ASCII hexadecimal bytes of the publisher `secret`. Its canonical message is `REDP2P-REGISTER`, raw nonce, issue and expiry uint64 values, uint16-length-prefixed id and secret, canonical protocol byte (UDP `0x01`, TCP/KCP `0x02`), UDP port uint16, candidate count uint8, sorted binary candidates, and pow_solution uint64. A candidate is type byte (`host` `0x01`, `observed` `0x02`), family byte (IPv4 `0x04`, IPv6 `0x06`), raw address bytes, and port uint16.

When a global password or VIP password applies, `register` also requires `access_proof`: exactly 64 lowercase hexadecimal characters encoding `HMAC-SHA256(password, REDP2P-ADMISSION-v1 || canonical-registration-message)`. Passwords are case-sensitive opaque tokens of 1..255 bytes; all bytes except ASCII whitespace, controls, and DEL are valid. The password is never transmitted. An empty global password means no global admission password is configured. A VIP password overrides the global password for its ID. Missing or incorrect admission proofs return `403 auth_failed`; malformed present proofs return `400 bad_request`; neither response reveals which password was selected.

Registration verifies challenge, candidate normalization, PoW, publisher-session proof, and any required admission proof before capacity, ownership, or state changes. Authentication failures do not create or modify publisher state. VIP reservations still occupy seats.

---

## Operations

### `challenge` - Request PoW challenge

**Request:**
```json
{ "op": "challenge", "id": "publisher1" }
```

**Response (HTTP 200):**
```json
{
  "ok": true,
  "nonce": "a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890",
  "issued_at": 1700000000,
  "expires_at": 1700000060,
  "mac": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "pkey": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "bits": 16
}
```

The server stores no nonce records. The client solves the canonical PoW input
defined in [Registration Authentication](#registration-authentication), then
signs the canonical registration bytes with its newly generated publisher
secret.

**Errors:** `bad_request`, `invalid_id`.

---

### `register` - Register publisher

**Request:**
```json
{
  "op": "register",
  "id": "publisher1",
  "nonce": "a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890",
  "issued_at": 1700000000,
  "expires_at": 1700000060,
  "mac": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "pow_solution": "0000000000000001",
  "proof": "deadbeef...",
  "access_proof": "deadbeef...",
  "pkey": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "secret": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "proto": 1,
  "udp_port": 40000,
  "candidates": [
    {"type": "host", "addr": "10.0.0.5", "port": 40000}
  ]
}
```

`pow_solution` is the uint64 counter encoded as exactly 16 lowercase hex
characters. `access_proof` is required exactly when a global or VIP admission
password applies; it never transmits that password.

The request contains the publisher ephemeral `pkey` and encrypted 64-hex-character
`secret`; the recovered original 16-byte control secret is accepted only for this
initial registration and is never returned by the index. IDs are arbitrary,
temporary registration identifiers: winning an available ID is valid. This protects
the active registration control secret from passive observation only; it provides no
persistent identity, index identity, active-MITM protection, peer authentication,
or peer-traffic encryption. While a record is active, every further registration
attempt for the same ID is rejected: it does not refresh the TTL, modify endpoints
or protocol fields, replace or disclose the session secret, or create a duplicate record. This rule
also applies to reserved VIP IDs after their initial authorized registration.

Ownership ends when the record is deregistered or expires. A later valid
registration of the released ID creates a new session using a newly generated
publisher session secret. VIP IDs retain their seat-reservation rules and use
their configured admission password instead of the global password.

**Error responses:**
- `bad_request` - malformed, oversized, or duplicate input; bad hex fields; bad `proto`/`udp_port`; malformed `candidates` array
- `invalid_id` - id not alphanumeric or longer than 63 chars
- `auth_failed` - challenge, PoW, publisher-session proof, or admission proof failure
- `already_registered` - the ID has an active publisher registration
- `table_full` - seats capacity exhausted (including inactive VIP reservations)

On success the server creates a new record with `last_seen = now`.

---

### `heartbeat` - Refresh publisher TTL and endpoints

**Request:**
```json
{
  "op": "heartbeat",
  "id": "publisher1",
  "seq": 1,
  "proof": "9d4c...",
  "proto": 1,
  "udp_port": 40000,
  "candidates": [...]
}
```

**Response (HTTP 200):**
```json
{ "ok": true }
```

**Error responses:**
- `not_found` - unknown or expired id
- `invalid_proof` - proof mismatch or replayed sequence

No PoW and no raw secret. The canonical heartbeat message is the UTF-8/ASCII
text `heartbeat\n<id>\n<seq>\n<proto>\n<udp_port>\n<count>` followed, in request
candidate order, by `\n<type>\n<addr>\n<port>` for each candidate. `proof` is
lowercase hexadecimal `HMAC-SHA256(secret, canonical-message)`. The server
accepts only a sequence greater than the stored sequence, then updates
`last_seen`, `proto`, `udp_port`, and `candidates`.
Sequence consumption and the endpoint update are one atomic index mutation:
concurrent requests using the same sequence produce at most one success, and a
rejected request does not alter the record or refresh its TTL.

### Publisher restart lifecycle

The reference C publisher persists its locally generated session secret and
the next control sequence before each authenticated request. On restart, it
loads that scoped local state and first sends a `heartbeat` to prove ownership
of the still-active record. A successful proof continues the same session; it
does not send another `register` request or rotate the secret.

If the scoped local state cannot be parsed, the reference C publisher removes
it and follows the normal registration flow as though no session state existed.
This local recovery does not apply to a successfully parsed session whose
heartbeat returns `invalid_proof`.

If that heartbeat returns `not_found`, the record has expired or been removed,
so the publisher creates and persists a new session through the normal
registration flow. If it returns `invalid_proof`, the local state cannot prove
ownership of an active record. The publisher does not replace that record or
create a recovery path; it must wait for expiry before a new registration can
succeed. The publisher restart lifecycle is publisher-side behavior. Index
implementations, including the PHP index, only preserve the server-side session
semantics: valid resumed proofs are accepted, invalid proofs cannot replace an
active session, and an expired session becomes `not_found`.

---

### `lookup` - Get one publisher record

**Request:**
```json
{ "op": "lookup", "id": "publisher1" }
```

**Response (HTTP 200):**
```json
{
  "ok": true,
  "id": "publisher1",
  "proto": 1,
  "udp_port": 40000,
  "candidates": [...],
  "last_seen": 1700000000
}
```

**Error (HTTP 404):**
```json
{ "ok": false, "error": "not_found" }
```

Expired records are filtered without writing.

---

### `list` - List all active publisher IDs

**Request:**
```json
{ "op": "list" }
```

**Response (HTTP 200):**
```json
{ "ok": true, "ids": ["publisher1", "publisher2"] }
```

Returns only non-expired IDs, in no particular order.

---

### `deregister` - Remove publisher record

**Request:**
```json
{ "op": "deregister", "id": "publisher1", "seq": 2, "proof": "9d4c..." }
```

**Response (HTTP 200):**
```json
{ "ok": true }
```

**Error:**
- `invalid_proof` - proof mismatch, replayed sequence, or unknown/expired id

The canonical deregistration message is the UTF-8/ASCII text
`deregister\n<id>\n<seq>`. `proof` is lowercase hexadecimal
`HMAC-SHA256(secret, canonical-message)`. The record is removed only when that
proof is valid and the sequence is still greater than the stored sequence at
the delete operation. Concurrent copies of one proof have at most one success.

---

### `punch_req` - Consumer announces itself to publisher

**Request:**
```json
{
  "op": "punch_req",
  "self_id": "consumer1",
  "target_id": "publisher1",
  "session": "abc123",
  "udp_port": 41080,
  "candidates": [...]
}
```

**Response (HTTP 200):**
```json
{ "ok": true }
```

Stores one pending call (TTL 30 seconds by default, configurable through
`REDP2P_PENDING_CALL_TTL_S`, range 1..86400) addressed to a currently active
publisher. The `target_id` MUST refer to a currently active publisher: a publisher
that exists and has not expired by its registration TTL. A punch request for a
target that is absent or expired is rejected and enqueues nothing. The consumer
typically sends this after a successful `lookup`.

Every valid `punch_req` passes two token buckets before pending-call cleanup
or capacity checks:

| Bucket | Identity | Capacity | Refill |
|--------|----------|----------|--------|
| Source | Trusted HTTP transport source IP | 20 tokens | 5 tokens/second |
| Target | Active publisher `target_id` | 40 tokens | 10 tokens/second |

Both buckets start full. One request consumes one token from each bucket only
when both have at least one token. If either is insufficient, neither loses a
token and the response is HTTP 429 `Too Many Requests`:

```json
{ "ok": false, "error": "rate_limited" }
```

A rate-limited request neither enqueues nor removes pending calls, including
expired calls. Refilling, source activity accounting, and source-table expiry
or eviction may still occur. A request that passes both buckets consumes both
tokens even if a subsequent pending-capacity check rejects it.

Source identity comes from the accepted socket address in C and the trusted
transport address supplied to the PHP handler (`REMOTE_ADDR` in `serve`).
`X-Forwarded-For` and other client-controlled headers do not affect identity.
Addresses are compared by binary IP identity; IPv4-mapped IPv6 and the matching
IPv4 address share a bucket. Ports are not part of source identity.

The source table holds at most 4096 entries. Entries expire after 120 seconds
without a valid request reaching rate limiting, including rate rejections.
Expired entries are removed before insertion. If the table is still full,
the least recently used entry is replaced; equal accounting timestamps use a
deterministic tie break. A new entry starts full.

Target credit belongs to the publisher record, not a separate target table.
It is discarded with the publisher on expiry/removal or deregistration; a new
registration starts full. Expired publishers cannot use their retained record
while awaiting ordinary lazy cleanup.

C uses monotonic milliseconds and integer thousandths of a token. PHP persists
integer credit and wall-clock milliseconds in its existing PDO storage across
requests, with refill capped at bucket capacity and no refill for a backward
clock interval. Its existing serialized write transaction protects both bucket
checks and debits as well as source-table insertion and pending admission.

The processing order is HTTP parsing, JSON parsing, active-target validation,
candidate and trusted observed-endpoint validation/normalization, source bucket,
target bucket, expired pending-call removal, per-publisher pending limit,
global pending limit, and enqueue. Failed validation does not spend tokens.

The pending-call store has a mandatory global limit of 4096 entries and a
per-publisher consumer window of `MAX_CONSUMERS_PER_PUBLISHER` (default 32):

```
pending_consumers_for_publisher <= MAX_CONSUMERS_PER_PUBLISHER
```

`MAX_CONSUMERS_PER_PUBLISHER` defaults to 32. A configuration value of 0
restores that default; positive values set a finite window. The global limit is
mandatory and cannot be disabled. Expired calls are removed before either limit
is measured. The backing store never reserves capacity for more than 4096
entries, and active calls are never evicted to accept a new one.

Consumed slots are freed immediately when a successful `punch_poll` returns the
calls; expired pending calls also stop counting toward the window. Reaching the
window for one publisher does not affect any other publisher while global
capacity remains. When the publisher window is reached, `punch_req` returns
HTTP 429 with `pending_limit_publisher`; when the global limit is reached it
returns HTTP 429 with `pending_limit_global`. These capacity rejections do not
modify active pending calls.

**Errors:** `bad_request`, `invalid_id` (bad `self_id`/`target_id`/`session`),
`not_found` when `target_id` does not refer to a currently active publisher,
`rate_limited` when either bucket lacks a token,
`pending_limit_publisher` when the target publisher's consumer window has no
room left, and `pending_limit_global` when the global pending-call limit has no
room left.

---

### `punch_poll` - Publisher retrieves pending calls

`punch_poll` is an authenticated publisher-control operation. It reads and
consumes the pending calls addressed to the owning publisher. It is treated by
the same session contract as `heartbeat` and `deregister`: it proves possession
of the active publisher session secret and consumes a strictly increasing
sequence.

**Request:**
```json
{
  "op": "punch_poll",
  "id": "publisher1",
  "seq": 1,
  "proof": "9d4c..."
}
```

**Response (HTTP 200):**
```json
{
  "ok": true,
  "calls": [
    {
      "self_id": "consumer1",
      "session": "abc123",
      "candidates": [...]
    }
  ]
}
```

Call objects contain `self_id`, `session`, and `candidates` only (no `target_id`). With no pending calls the response is `{ "ok": true, "calls": [] }`. The reference publisher polls at about 500 ms cadence.

A single `punch_poll` returns **at most `PUNCH_POLL_MAX` calls**, where

```
PUNCH_POLL_MAX = 4
```

`PUNCH_POLL_MAX` is derived from the protocol's worst-case response size so that
the returned call set always fits the supported C index response budget
(`REDP2P_BUF` = 4096 bytes, serialized body limit 4094 bytes). Each worst-case
call carries the maximum 8 candidates, each with the maximum textual
representation that the protocol actually accepts: a `host`-type candidate,
the longest IPv6 literal `inet_pton` accepts, and port 65535. The longest
accepted address is the mixed IPv6/IPv4 form
`ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255` (45 characters); it exceeds the
maximum all-hex IPv6 literal (39 characters) because the trailing
`255.255.255.255` quad encodes more text than the equivalent hex fields.
Serialized with the shortest valid JSON form, one such candidate is 85 bytes and
one such call is 856 bytes;
a response with 5 such calls is 4306 bytes, which exceeds the 4094-byte body
limit. The bound is therefore set to **4**, whose worst-case response is 3449
bytes, leaving an explicit 645-byte (≈15.7%) margin below the 4094-byte body
limit. This guarantees the selected maximum always fits the supported response
buffer.

The canonical `punch_poll` message is the UTF-8/ASCII text
`punch_poll\n<id>\n<seq>`. `proof` is lowercase hexadecimal
`HMAC-SHA256(secret, canonical-message)`, where `secret` is the stored publisher
session secret. This is the same canonical construction used by `heartbeat` and
`deregister`, without the extra endpoint fields those operations carry.

The server authenticates the request **before** selecting, serializing, or
removing any pending call. A request whose session does not exist, whose proof
does not match, or whose sequence is not strictly greater than the stored
sequence consumes no pending calls. Only a valid proof with a valid next
sequence advances the stored sequence and consumes the addressed pending calls;
the publisher's `last_seen` is refreshed only on such a valid request.

A valid request selects up to `PUNCH_POLL_MAX` pending calls addressed to the
publisher, constructs a valid response containing those calls, and consumes
**only** the calls that appear in the returned response. When more than
`PUNCH_POLL_MAX` calls are pending, the poll returns at most `PUNCH_POLL_MAX`
of them and the remainder stay pending; a later authenticated `punch_poll` may
retrieve the remainder. Pending calls not included in the returned response
remain pending. If a valid response cannot be constructed (for example its
serialized size exceeds the response bound), nothing is consumed and the
preceding pending calls stay pending. Transport failure that occurs after the
operation has committed, so that a generated response is lost before it reaches
the publisher, is outside the delivery guarantee: the protocol does not
implement acknowledgement-based or exactly-once delivery.

**Error responses:**
- `bad_request` - missing or malformed `seq` or `proof` field
- `not_found` - unknown or expired publisher id
- `invalid_proof` - wrong proof, or replayed/out-of-order sequence

No PoW and no raw secret are involved in `punch_poll`; the publisher does not
retransmit its session secret after registration and instead proves possession
with the canonical HMAC proof.

---

## Error Codes

| Code | Meaning | HTTP Status |
|------|---------|-------------|
| `bad_request` | Malformed JSON, missing or wrong-typed fields, duplicate fields, bad hex/token limits, malformed `candidates`, unknown `op` | 400 |
| `invalid_id` | Identifier rejected (not alphanumeric, too long) | 400 |
| `auth_failed` | Challenge, proof-of-work, publisher-session proof, or admission-proof failure | 403 |
| `invalid_proof` | Publisher control proof mismatch or replayed sequence | 403 |
| `not_found` | Unknown or expired id (lookup, heartbeat, punch_req target) | 404 |
| `already_registered` | Requested publisher ID already has an active registration | 409 |
| `rate_limited` | Source or target punch token bucket exhausted | 429 |
| `pending_limit_publisher` | Publisher pending-call window full | 429 |
| `pending_limit_global` | Global pending-call limit full | 429 |
| `table_full` | Seats capacity exhausted | 503 |
| `internal` | Server failure | 500 |

All JSON errors: `{ "ok": false, "error": "<code>" }`.

---

## Server Behavior

### Record Schema

```
PublisherRecord {
  id: string (1..63, alnum)
  key: hex string (16 chars, publisher-generated session secret)
  sequence: integer
  proto: 1|2
  udp_port: uint16
  candidates: Candidate[8]
  last_seen: unix_timestamp
}
```

### TTL

- Default TTL: 120 seconds, configurable through `REDP2P_ETIMEOUT_SEC` (range 1..86400).
- A record is expired when `now - last_seen > TTL`.
- Expired records are filtered from `lookup` and `list` and physically removed from the store on `register`, `heartbeat`, `deregister`, and `punch_req`, and periodically in the idle loop (default 60 s, `REDP2P_PRUNE_INTERVAL_S`).
- The protocol does not expose a prune endpoint; physical removal is an implementation detail of each deployment.

### Seats / Capacity

- Optional total publisher capacity configured through `--seats` or `REDP2P_SEATS`.
- Each VIP reservation (configured via `REDP2P_VIP` as `<id> <pass> ...`) occupies one seat even while inactive.
- Non-VIP publishers share the remaining seats.
- `table_full` is returned when no seat is available. Zero seats accepts no publishers.
- Without configured seats there is no application-level publisher limit.

### Proof of Work

- See the "Proof of Work" section above.
- The server recomputes the digest from the echoed request fields; no challenge state is stored.

### Pending Punch Calls

- Pending-call limits: at most 4096 calls globally and
    `MAX_CONSUMERS_PER_PUBLISHER` calls per publisher (default 32; 0 restores
    that default). The global limit is mandatory and the backing store cannot
    reserve more than 4096 entries.
- TTL: 30 seconds from creation by default, configurable through
    `REDP2P_PENDING_CALL_TTL_S` (range 1..86400).
- `punch_poll` consumes the entries addressed to one id, only after
    authenticating the owning publisher's session. A failed authentication
    consumes no entries.
- A single `punch_poll` returns at most `PUNCH_POLL_MAX` (4) entries.
- Only the entries represented in the returned response are consumed. Entries
    not included in a returned response remain pending. If no valid response
    can be constructed, nothing is consumed.
- Consumed entries free their consumer slots immediately; expired entries are
    removed before limits are applied and free global and publisher capacity.
- When one publisher's consumer window has no room left, `punch_req` returns
    `pending_limit_publisher` (HTTP 429). When the global limit has no room,
    it returns `pending_limit_global` (HTTP 429). Rejections do not evict or
    modify active calls.

### Punch Coordination

1. Consumer `lookup` → gets publisher candidates.
2. Consumer `punch_req` with its own candidates + session id.
3. Publisher `punch_poll` (authenticated with a sequenced HMAC proof) retrieves the pending call.
4. Both peers run direct UDP probes against each other's candidates.
5. On success, they establish KCP (TCP mode) or a UDP stream.

None of this traffic touches the index.

---

## Implementation Notes

- **No background daemons required**: pruning runs in the request path and the idle loop.
- **Storage**: any in-memory or TTL-capable K/V store (the reference index is fully in-memory).
- **No UDP/KCP/socket logic** in the index.
- **Secret stability**: the publisher secret is established once and never rotated.
- **Control auth**: heartbeat, punch_poll, and deregister use a monotonic
    sequence and canonical HMAC proof over the publisher session secret; the raw
    secret is never retransmitted after registration.
- **Candidate validation**: strict `type`/`addr`/`port` parsing, binary
    normalization, deduplication, and canonical sorting.
- **Bounds**: all fields bounded; oversized, truncated, duplicate, or wrong-typed input is rejected with `bad_request`.
- **One request per connection**: no session or keep-alive support.

---

## Example Flow

```bash
# 1. Get challenge
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"challenge","id":"alice"}' http://index:8080/
# -> {"ok":true,"nonce":"...","bits":16}

# 2. Solve PoW locally, then register (pow_solution = 16 hex digits)
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"register","id":"alice","nonce":"...","issued_at":1700000000,"expires_at":1700000060,"mac":"...","pow_solution":"0000000000000001","proof":"...","pkey":"<64 hex chars>","secret":"<ciphertext-and-tag:64-hex-chars>","proto":1,"udp_port":40000,"candidates":[{"type":"host","addr":"203.0.113.10","port":40000}]}' \
  http://index:8080/
# -> {"ok":true}  (persist the locally generated session secret and sequence)

# 3. Heartbeat every 15s (seq and proof are computed from the local secret)
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"heartbeat","id":"alice","seq":1,"proof":"<HMAC-SHA256>","proto":1,"udp_port":40000,"candidates":[...]}' \
  http://index:8080/
# -> {"ok":true}

# 4. Consumer looks up publisher
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"lookup","id":"alice"}' http://index:8080/

# 5. Consumer requests punch
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"punch_req","self_id":"bob","target_id":"alice","session":"s1","candidates":[...]}' \
  http://index:8080/

# 6. Publisher polls (500ms), authenticated with the same canonical HMAC proof
curl -X POST -H "Content-Type: application/json" \
  -d '{"op":"punch_poll","id":"alice","seq":1,"proof":"<HMAC-SHA256>"}' http://index:8080/
# -> {"ok":true,"calls":[{"self_id":"bob","session":"s1","candidates":[...]}]}

# 7. Both punch directly via UDP, establish KCP/TCP or UDP tunnel
```
