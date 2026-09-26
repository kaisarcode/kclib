# REDP2P index server (PHP)

This directory provides the PHP implementation of the REDP2P `idx` capability for deployments such as shared hosting.

The class coordinates peers over HTTP and stores temporary index state in PDO. It never carries application traffic. The REDP2P wire protocol remains internal to the implementation: applications do not call registration, heartbeat, lookup, punching, challenge, or deregistration operations themselves.

## Requirements

- PHP 8.0 or newer.
- ext-sodium.
- PDO with SQLite or MySQL.
- MySQL deployments must use a transactional row-locking engine such as InnoDB.
- A web SAPI for `serve()` or `handle()`.

## Application-facing API

Construct the index with operator policy:

```php
use KaisarCode\Redp2pIndex;

$pdo = new PDO('sqlite:var/redp2p.sqlite');
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$idx = new Redp2pIndex($pdo, [
    'seats' => 128,
    'pow' => 16,
]);
```

Inspect currently announced publisher IDs:

```php
$ids = $idx->list();
```

`list()` returns fresh IDs only. Expired records are filtered without requiring the application to understand publisher heartbeats or TTL bookkeeping.

Run periodic storage cleanup when the deployment needs it:

```php
$idx->prune();
```

PHP has no resident background runtime, so `prune()` is an explicit maintenance entry point suitable for cron or another deployment scheduler.

## Serving the index

The simplest front controller is:

```php
use KaisarCode\Redp2pIndex;

Redp2pIndex::serve(
    ['dsn' => 'sqlite:var/redp2p.sqlite'],
    ['seats' => 128, 'pow' => 16]
);
```

For an application that already owns HTTP response handling, `handle()` accepts one request and returns:

```text
status
reason
headers
body
```

Example:

```php
$res = $idx->handle(
    $_SERVER['REQUEST_METHOD'],
    file_get_contents('php://input'),
    function_exists('getallheaders') ? getallheaders() : [],
    $_SERVER['REMOTE_ADDR'] ?? null
);
```

`handle()` is an HTTP adapter for the REDP2P index protocol. The protocol operations it dispatches are not separate application-facing methods.

## Configuration

Application/operator configuration is deliberately small.

| Option | Environment | Default | Meaning |
| :-- | :-- | :-- | :-- |
| `pass` | `REDP2P_PASS` | empty | Shared publisher-registration admission credential |
| `vip` | `REDP2P_VIP` | none | Reserved IDs and per-ID admission credentials |
| `seats` | `REDP2P_SEATS` | unlimited | Total publisher capacity; VIP reservations consume seats |
| `pow` | `REDP2P_POW` | `0` | Registration proof-of-work difficulty |
| `max_consumers_per_publisher` | `REDP2P_MAX_CONSUMERS_PER_PUBLISHER` | `32` | Per-publisher pending-consumer safety bound |

Protocol timing, challenge lifetime, pending-call TTL, punch-poll batching, sequence handling, cryptographic fields, and candidate machinery are internal protocol policy rather than application API.

## Internal protocol and security

The PHP implementation remains wire-compatible with the native REDP2P index.

Internally it handles:

- authenticated stateless registration challenges;
- X25519/XChaCha20-Poly1305 protection of the publisher control secret;
- registration PoW;
- global and VIP admission proofs;
- strictly increasing authenticated publisher control sequences;
- publisher heartbeat/expiry;
- fresh publisher lookup and list responses;
- authenticated deregistration;
- bounded pending punch requests;
- per-source and per-target punch-request rate control;
- candidate validation and normalization;
- bounded request bodies and punch-poll batches;
- duplicate top-level JSON-key rejection.

Those mechanisms are implementation details. A normal application embedding `Redp2pIndex` configures policy, optionally calls `list()`, serves the endpoint, and otherwise leaves coordination to REDP2P.

Successful wire responses use `{"ok":true,...}`; protocol errors use `{"ok":false,"error":"<code>"}`. These envelopes matter to compatible REDP2P peers, not to application code using the public capability API.

## Storage

The schema is created automatically.

- `redp2p_publishers`: temporary announced publisher state.
- `redp2p_pending_calls`: bounded short-lived punch coordination.
- `redp2p_rate_sources`: abuse-control accounting.
- `redp2p_meta`: internal coordination metadata and challenge key material.

SQLite and MySQL are supported through portable PDO SQL.

Expired records are filtered from reads immediately and removed lazily by state-changing operations or explicitly by `prune()`.

## Security boundaries

The index is a rendezvous service, not an application server.

It does not:

- relay application payloads;
- authenticate application users;
- authorize application actions;
- inspect tunneled application protocols;
- provide accounts or global identity;
- replace application-level encryption.

Its security mechanisms protect REDP2P coordination and index resources.

## License

GPL-3.0, matching `redp2p.c`.
