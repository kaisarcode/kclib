# REDP2P index server (PHP)

A PHP 8 implementation of the REDP2P index protocol, faithful to the C reference server (`redp2p idx <port>`) and to [INDEX-PROTOCOL-SPECIFICATION.md](../../INDEX-PROTOCOL-SPECIFICATION.md).

It is a single class, `KaisarCode\Redp2pIndex`, using PHP core, PDO, and ext-sodium. Index state is stored in any PDO database using only portable SQL: SQLite for local deployments, MySQL for shared hosting.

The index coordinates peers over HTTP and carries no application traffic. Publisher records expire by TTL. Publisher-generated session secrets are encrypted during registration; heartbeat, punch_poll, and deregister use sequenced HMAC proofs.

## Requirements

* PHP 8.0 or newer (strict types, `match`, `str_ends_with`).
* ext-sodium, required for X25519 and XChaCha20-Poly1305 registration-secret protection.
* PDO with the `pdo_sqlite` driver, or `pdo_mysql` for MySQL.
* MySQL deployments must use a transactional row-locking engine such as InnoDB.
* A web SAPI for `serve()` (`php -S`, Apache, nginx+FPM). The CLI SAPI does not populate `php://input`.

## Usage

Construct the index with a PDO connection and pass each HTTP request to `handle()`:

```php
<?php
declare(strict_types=1);

use KaisarCode\Redp2pIndex;

$pdo = new PDO('sqlite:var/redp2p.sqlite');
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$index = new Redp2pIndex($pdo, ['seats' => 128, 'pow' => 16]);

$res = $index->handle(
    $_SERVER['REQUEST_METHOD'],
    file_get_contents('php://input'),
    function_exists('getallheaders') ? getallheaders() : []
);

foreach ($res['headers'] as $name => $value) {
    header("$name: $value");
}
header('HTTP/1.1 ' . $res['status'] . ' ' . $res['reason'], true, $res['status']);
echo $res['body'];
```

`handle()` returns an array with `status` (int), `reason` (string), `headers` (string map), and `body` (string). The request path is ignored; only the JSON `op` field dispatches the operation.

`serve()` is a standalone front-controller helper that reads the current request and emits the response:

```php
<?php
use KaisarCode\Redp2pIndex;

Redp2pIndex::serve(
    ['dsn' => 'sqlite:var/redp2p.sqlite'],
    ['seats' => 128]
);
```

A full deployment route example lives in the `www` project as `src/controllers/redp2p.php`.

## Configuration

Options are applied in this order: constructor options, then `REDP2P_*` environment variables, then protocol defaults.

| Option | Environment | Default | Meaning |
| :----- | :---------- | :------ | :------ |
| `pass` | `REDP2P_PASS` | empty | Global publisher-registration admission credential |
| `vip` | `REDP2P_VIP` | none | Reserved IDs, seat reservations, and per-ID admission credentials |
| `seats` | `REDP2P_SEATS` | none | Total publisher seats; each VIP occupies one |
| `pow` | `REDP2P_POW` | `0` | Proof-of-work difficulty in leading zero bits |
| `ttl` | `REDP2P_ETIMEOUT_SEC` | `120` | Publisher eviction TTL in seconds |
| `max_consumers_per_publisher` | `REDP2P_MAX_CONSUMERS_PER_PUBLISHER` | `32` | Per-publisher consumer safety window; `0` restores 32 |

The class also exposes public setters: `setPass()`, `setVips()`, `setSeats()`, `setPow()`, `setTtl()`, and `setMaxConsumersPerPublisher()`. `setSeats()` accepts zero (rejects all publishers) and treats an unset value as no limit. Each VIP reservation occupies one seat even while inactive; non-VIP publishers share the remainder.

## Operations

The class implements all eight protocol operations:

* `challenge` - issues a stateless authenticated 32-byte nonce, timestamps, MAC, index X25519 public key, and difficulty.
* `register` - decrypts the publisher's encrypted control secret with X25519/XChaCha20-Poly1305, then verifies the authenticated challenge, candidates, SHA-256 PoW, publisher-secret HMAC, and, when configured, the password HMAC `REDP2P-ADMISSION-v1 || canonical-registration-message` before state checks. Passwords are case-sensitive opaque 1..255-byte tokens with no whitespace, control bytes, or DEL; they are never transmitted. `access_proof` is exactly 64 lowercase hexadecimal characters. An empty global password leaves the index open, and VIP passwords override the global password for their IDs. Passwords are admission HMAC keys only; the publisher session secret continues protecting control operations.
* `heartbeat` - atomically consumes a sequenced HMAC proof before refreshing `last_seen` and endpoint fields.
* `lookup` and `list` - return fresh records, filtering expired ones.
* `deregister` - atomically removes a record only when its sequenced HMAC proof is valid.
* `punch_req` - stores a bounded pending call for a currently active target publisher; an absent or expired target is rejected with `not_found` and enqueues nothing.
* `punch_poll` - returns and consumes pending calls addressed to a publisher, only after authenticating the publisher's sequenced HMAC proof.

Successful responses use the envelope `{"ok": true, ...}`. Errors use `{"ok": false, "error": "<code>"}`. HTTP-level failures before JSON parsing (non-`POST`, `Transfer-Encoding`, oversized body) return a plain-text body, matching the reference server.

## Limits

The class enforces the protocol bounds:

* id and session tokens: 1..63 ASCII alphanumeric characters.
* publisher session secret: 16 hex characters, generated locally for one active registration. The ID may be registered again only after deregistration or expiry.
* nonce and challenge MAC: 64 hexadecimal characters each; `issued_at` and `expires_at` have a 60-second lifetime; `pow_solution` is a fixed 16-character lowercase hexadecimal uint64; proof is 64 hexadecimal characters.
* candidate list: up to 8 normalized candidates of type `host` or `observed`, literal IPv4/IPv6 address up to 47 characters, port 1..65535.
* request body: up to 4096 bytes, else HTTP 413.
* pending punch calls: mandatory global maximum 4096 and per-publisher bound `max_consumers_per_publisher` (default 32; 0 restores 32), with a 30-second TTL. Expired calls are removed before limits are measured; publisher exhaustion returns HTTP 429 `pending_limit_publisher`, global exhaustion returns HTTP 429 `pending_limit_global`, and rejections leave active calls intact.
* punch poll batch: at most 4 pending calls returned per `punch_poll`; the remainder stay pending for a later poll.
* duplicate top-level JSON keys are rejected; nested duplicates are accepted.

Header count/size limits and the 5-second request timeout are enforced by the web server, not by this class.

## Storage

The schema is created automatically on construction and uses portable SQL (no `AUTO_INCREMENT`, no driver-specific syntax):

* `redp2p_publishers` - id, key, proto, udp_port, candidates, last_seen.
* `redp2p_pending_calls` - id, self_id, target_id, session, candidates, ts.
* `redp2p_meta` - internal metadata, including the pending-call write lock and persistent PHP registration challenge key rows.

Expired records are filtered from `lookup` and `list` without writing, and physically removed lazily on register, heartbeat, deregister, and punch_req.

`prune()` is the standalone cleanup entry point. It removes expired publisher records and pending punch calls. The class never schedules it by itself; the deployment operator runs it from their own script, for example a cron job:

```php
<?php
use KaisarCode\Redp2pIndex;

$pdo = new PDO('sqlite:var/redp2p.sqlite');
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$index = new Redp2pIndex($pdo);
$index->prune();
```

## License

GPL-3.0, matching the `redp2p.c` project.
