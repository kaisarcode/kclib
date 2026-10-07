<?php
/**
 * Redp2pIndex.php - REDP2P index server.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);
namespace KaisarCode;

use PDO;

require_once __DIR__ . '/Redp2pNative.php';
require_once __DIR__ . '/Redp2pWebRtc.php';

if (!extension_loaded('sodium')) {
    throw new \RuntimeException('REDP2P PHP index requires ext-sodium');
}

final class Redp2pIndex
{
    public const ID_MAX = 63;
    public const KEY_SZ = 16;
    public const PASS_MAX = 255;
    public const PENDING_POLL_MAX = 4;
    public const MAX_PENDING_CALLS_GLOBAL = 4096;
    public const MAX_PENDING_CALLS_PER_PUBLISHER = 32;
    public const PENDING_TTL_S = 30;
    public const BODY_MAX = 65536;
    public const DEFAULT_TTL_S = 120;

    private const RATE_SOURCES_MAX = 4096;
    private const RATE_SOURCE_IDLE_MS = 120000;

    private PDO $db;
    private string $pass = '';
    private array $vips = [];
    private ?int $seats = null;
    private int $pow = 0;
    private int $ttl = self::DEFAULT_TTL_S;
    private int $pendingTtlS = self::PENDING_TTL_S;
    private int $maxConsumers = self::MAX_PENDING_CALLS_PER_PUBLISHER;
    private string $challengeKey;

    /** @var array<string, object> */
    private array $transports = [];

    public function __construct(PDO|array $db, array $options = [])
    {
        if (is_array($db)) {
            $config = $db;
            $dsn = $config['dsn'] ?? null;
            if (!is_string($dsn) || $dsn === '') {
                throw new \InvalidArgumentException('dsn is required');
            }
            $db = new PDO(
                $dsn,
                $config['user'] ?? null,
                $config['db_pass'] ?? null
            );
            $options = $config;
        }
        $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $this->db = $db;
        $this->applyOptions($options);
        $this->ensureSchema();
        $this->challengeKey = $this->loadChallengeKey();
        $this->registerTransport(new Redp2pNative());
        $this->registerTransport(new Redp2pWebRtc());
    }

    public function serve(): void
    {
        $headers = function_exists('getallheaders') ? getallheaders() : [];
        $res = $this->handle(
            $_SERVER['REQUEST_METHOD'] ?? 'GET',
            file_get_contents('php://input'),
            $headers,
            $_SERVER['REMOTE_ADDR'] ?? null
        );
        header(sprintf('HTTP/1.1 %d %s', $res['status'], $res['reason']), true,
            $res['status']);
        foreach ($res['headers'] as $name => $value) {
            header($name . ': ' . $value);
        }
        echo $res['body'];
    }

    public function handle(
        string $method,
        string $body,
        array $headers = [],
        ?string $peerAddress = null
    ): array {
        if ($method === 'OPTIONS') {
            return $this->response(204, 'No Content', 'text/plain', '');
        }
        if ($method !== 'POST') {
            return $this->httpError(405, 'Method Not Allowed');
        }
        if ($this->hasHeader($headers, 'Transfer-Encoding')) {
            return $this->httpError(501, 'Not Implemented');
        }
        $bodySize = strlen($body);
        if ($bodySize > self::BODY_MAX) {
            return $this->httpError(413, 'Payload Too Large');
        }
        $request = json_decode($body);
        if (!($request instanceof \stdClass)) {
            return $bodySize > 4096
                ? $this->httpError(413, 'Payload Too Large')
                : $this->jsonError(400, 'bad_request');
        }
        if ($this->hasDuplicateTopKeys($body) || !$this->hasString($request, 'op')) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($bodySize > 4096 && $request->op !== 'connect' &&
            $request->op !== 'answer') {
            return $this->httpError(413, 'Payload Too Large');
        }

        try {
            return match ($request->op) {
                'challenge' => $this->handleChallenge($request),
                'register' => $this->handleRegister($request),
                'heartbeat' => $this->handleHeartbeat($request),
                'lookup' => $this->handleLookup($request),
                'list' => $this->jsonOk(['ids' => $this->list()], false),
                'deregister' => $this->handleDeregister($request),
                default => $this->dispatchTransportOperation(
                    (string)$request->op,
                    $request,
                    $peerAddress
                ),
            };
        } catch (\PDOException $e) {
            return $this->jsonError(500, 'internal');
        }
    }

    public function prune(): void
    {
        $this->evictStale();
        $this->evictPending();
    }

    public function list(): array
    {
        $cutoff = time() - $this->ttl;
        $st = $this->db->prepare(
            'SELECT id FROM redp2p_publishers WHERE last_seen >= ?'
        );
        $st->execute([$cutoff]);
        return array_map('strval', $st->fetchAll(PDO::FETCH_COLUMN));
    }

    private function registerTransport(object $transport): void
    {
        $this->transports[$transport->key()] = $transport;
    }

    private function transportByKey(string $key): ?object
    {
        return $this->transports[$key] ?? null;
    }

    private function transportForPublisher(string $transport): ?object
    {
        foreach ($this->transports as $module) {
            if ($module->acceptsPublisherTransport($transport)) {
                return $module;
            }
        }
        return null;
    }

    private function dispatchTransportOperation(
        string $operation,
        \stdClass $request,
        ?string $peerAddress
    ): array {
        foreach ($this->transports as $module) {
            if ($module->handles($operation)) {
                return $module->handle($this, $operation, $request, $peerAddress);
            }
        }
        return $this->jsonError(400, 'bad_request');
    }

    private function handleChallenge(\stdClass $request): array
    {
        [$result, $id] = $this->requireId($request, 'id');
        if ($result === 0) return $this->jsonError(400, 'bad_request');
        if ($result < 0) return $this->jsonError(400, 'invalid_id');

        $nonce = random_bytes(32);
        $issuedAt = time();
        $expiresAt = $issuedAt + 60;
        $mac = hash_hmac(
            'sha256',
            $this->challengeMacInput($nonce, $issuedAt, $expiresAt),
            $this->challengeKey,
            true
        );
        $indexSecret = $this->indexSecretKey($nonce, $issuedAt, $expiresAt);
        $indexPublic = sodium_crypto_scalarmult_base($indexSecret);
        return $this->jsonOk([
            'nonce' => bin2hex($nonce),
            'issued_at' => $issuedAt,
            'expires_at' => $expiresAt,
            'mac' => bin2hex($mac),
            'pkey' => bin2hex($indexPublic),
            'bits' => $this->pow,
        ]);
    }

    private function handleRegister(\stdClass $request): array
    {
        [$result, $id] = $this->requireId($request, 'id');
        if ($result === 0) return $this->jsonError(400, 'bad_request');
        if ($result < 0) return $this->jsonError(400, 'invalid_id');

        $key = property_exists($request, 'transport') &&
            $request->transport === 'rtc' ? 'rtc' : 'native';
        $transport = $this->transportByKey($key);
        if ($transport === null) {
            return $this->jsonError(409, 'unsupported_transport');
        }
        return $transport->register($this, $request, $id);
    }

    private function handleHeartbeat(\stdClass $request): array
    {
        [$result, $id] = $this->requireId($request, 'id');
        if ($result === 0) return $this->jsonError(400, 'bad_request');
        if ($result < 0) return $this->jsonError(400, 'invalid_id');
        $this->evictStale();
        $publisher = $this->publisher($id);
        if ($publisher === null) return $this->jsonError(404, 'not_found');
        $transport = $this->transportForPublisher((string)$publisher['transport']);
        if ($transport === null) {
            return $this->jsonError(409, 'unsupported_transport');
        }
        return $transport->heartbeat($this, $request, $id, $publisher);
    }

    private function handleLookup(\stdClass $request): array
    {
        [$result, $id] = $this->requireId($request, 'id');
        if ($result === 0) return $this->jsonError(400, 'bad_request');
        if ($result < 0) return $this->jsonError(400, 'invalid_id');
        $publisher = $this->publisher($id);
        if ($publisher === null ||
            (int)$publisher['last_seen'] < time() - $this->ttl) {
            return $this->jsonError(404, 'not_found');
        }
        $transport = $this->transportForPublisher((string)$publisher['transport']);
        if ($transport === null) {
            return $this->jsonError(409, 'unsupported_transport');
        }
        $fields = [
            'id' => $id,
            'transport' => (string)$publisher['transport'],
            'last_seen' => (int)$publisher['last_seen'],
        ];
        return $this->jsonOk(array_merge(
            $fields,
            $transport->lookup($this, $publisher)
        ));
    }

    private function handleDeregister(\stdClass $request): array
    {
        [$result, $id] = $this->requireId($request, 'id');
        if ($result === 0) return $this->jsonError(400, 'bad_request');
        if ($result < 0) return $this->jsonError(400, 'invalid_id');
        $seq = $this->requireSequence($request);
        $proof = $this->requireHex($request, 'proof', 64);
        if ($seq === null || $proof === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $this->evictStale();
        $publisher = $this->publisher($id);
        if ($publisher === null || $seq <= (int)$publisher['seq']) {
            return $this->jsonError(403, 'invalid_proof');
        }
        $expected = $this->simpleControlProof(
            (string)$publisher['key'],
            'deregister',
            $id,
            $seq
        );
        if (!hash_equals($expected, strtolower($proof))) {
            return $this->jsonError(403, 'invalid_proof');
        }
        $lock = $this->beginPendingWrite();
        try {
            $this->pendingDeleteByPublisher($id);
            $st = $this->db->prepare(
                'DELETE FROM redp2p_publishers WHERE id = ? AND seq < ?'
            );
            $st->execute([$id, $seq]);
            if ($st->rowCount() !== 1) {
                $this->endPendingWrite($lock, false);
                return $this->jsonError(403, 'invalid_proof');
            }
            $this->endPendingWrite($lock, true);
            return $this->jsonOk();
        } catch (\Throwable $e) {
            $this->endPendingWrite($lock, false);
            throw $e;
        }
    }

    public function commitPublisherRegistration(
        string $id,
        string $secret,
        string $transport,
        array $transportData,
        string $registrationMessage,
        ?string $accessProof
    ): array {
        $password = $this->getPass($id);
        if ($password !== '') {
            if ($accessProof === null || !hash_equals(
                $this->admissionProof($password, $registrationMessage),
                $accessProof
            )) {
                return $this->jsonError(403, 'auth_failed');
            }
        }
        $this->evictStale();
        $added = $this->addPublisher($id, $secret, $transport, $transportData);
        return match ($added) {
            'ok' => $this->jsonOk(),
            'exists' => $this->jsonError(409, 'already_registered'),
            'full' => $this->jsonError(503, 'table_full'),
            default => $this->jsonError(500, 'internal'),
        };
    }

    private function addPublisher(
        string $id,
        string $secret,
        string $transport,
        array $transportData
    ): string {
        $st = $this->db->prepare(
            'SELECT 1 FROM redp2p_publishers WHERE id = ?'
        );
        $st->execute([$id]);
        if ($st->fetch()) return 'exists';
        if ($this->seats !== null) {
            $count = (int)$this->db->query(
                'SELECT COUNT(*) FROM redp2p_publishers'
            )->fetchColumn();
            if ($count >= $this->seats) return 'full';
            if (!isset($this->vips[$id])) {
                $cap = max(0, $this->seats - count($this->vips));
                if ($this->countNonVip() >= $cap) return 'full';
            }
        }
        $json = json_encode($transportData, JSON_UNESCAPED_SLASHES);
        if ($json === false) return 'error';
        $ins = $this->db->prepare(
            'INSERT INTO redp2p_publishers'
            . ' (id, key, seq, transport, transport_data, connect_credit,'
            . ' connect_ms, last_seen) VALUES (?, ?, 0, ?, ?, 40000, 0, ?)'
        );
        try {
            $ins->execute([$id, $secret, $transport, $json, time()]);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0])
                ? (string)$e->errorInfo[0] : (string)$e->getCode();
            if (strncmp($state, '23', 2) === 0) return 'exists';
            throw $e;
        }
        return 'ok';
    }

    public function publisher(string $id, bool $locked = false): ?array
    {
        $sql = 'SELECT id, key, seq, transport, transport_data, connect_credit,'
            . ' connect_ms, last_seen FROM redp2p_publishers WHERE id = ?';
        if ($locked && $this->db->getAttribute(PDO::ATTR_DRIVER_NAME) ===
            'mysql') {
            $sql .= ' FOR UPDATE';
        }
        $st = $this->db->prepare($sql);
        $st->execute([$id]);
        $row = $st->fetch(PDO::FETCH_ASSOC);
        if ($row === false) return null;
        $data = json_decode((string)$row['transport_data'], true);
        $row['transport_data'] = is_array($data) ? $data : [];
        return $row;
    }

    public function updatePublisher(
        string $id,
        int $seq,
        string $transport,
        array $transportData
    ): bool {
        $json = json_encode($transportData, JSON_UNESCAPED_SLASHES);
        if ($json === false) return false;
        $st = $this->db->prepare(
            'UPDATE redp2p_publishers SET seq = ?, transport = ?,'
            . ' transport_data = ?, last_seen = ? WHERE id = ? AND seq < ?'
        );
        $st->execute([$seq, $transport, $json, time(), $id, $seq]);
        return $st->rowCount() === 1;
    }

    public function touchPublisher(string $id, int $seq): bool
    {
        $st = $this->db->prepare(
            'UPDATE redp2p_publishers SET seq = ?, last_seen = ?'
            . ' WHERE id = ? AND seq < ?'
        );
        $st->execute([$seq, time(), $id, $seq]);
        return $st->rowCount() === 1;
    }

    public function pendingCreate(
        string $id,
        string $publisherId,
        string $transport,
        array $request,
        ?string $consumerHash = null
    ): bool {
        $json = json_encode($request, JSON_UNESCAPED_SLASHES);
        if ($json === false) return false;
        $st = $this->db->prepare(
            'INSERT INTO redp2p_pending'
            . ' (id, publisher_id, transport, consumer_hash, request_json,'
            . ' response_json, created_at, expires_at)'
            . ' VALUES (?, ?, ?, ?, ?, NULL, ?, ?)'
        );
        $now = time();
        $st->execute([
            $id,
            $publisherId,
            $transport,
            $consumerHash,
            $json,
            $now,
            $now + $this->pendingTtlS,
        ]);
        return true;
    }

    public function pendingForPublisher(
        string $publisherId,
        string $transport,
        bool $onlyUnanswered,
        int $limit = self::PENDING_POLL_MAX
    ): array {
        $this->evictPending();
        $sql = 'SELECT id, consumer_hash, request_json, response_json, created_at,'
            . ' expires_at FROM redp2p_pending WHERE publisher_id = ?'
            . ' AND transport = ? AND expires_at >= ?';
        if ($onlyUnanswered) $sql .= ' AND response_json IS NULL';
        $sql .= ' ORDER BY created_at LIMIT ' . max(1, $limit);
        $st = $this->db->prepare($sql);
        $st->execute([$publisherId, $transport, time()]);
        $rows = [];
        foreach ($st->fetchAll(PDO::FETCH_ASSOC) as $row) {
            $request = json_decode((string)$row['request_json'], true);
            $response = $row['response_json'] === null
                ? null : json_decode((string)$row['response_json'], true);
            $row['request'] = is_array($request) ? $request : [];
            $row['response'] = is_array($response) ? $response : null;
            unset($row['request_json'], $row['response_json']);
            $rows[] = $row;
        }
        return $rows;
    }

    public function pending(string $id): ?array
    {
        $this->evictPending();
        $st = $this->db->prepare(
            'SELECT id, publisher_id, transport, consumer_hash, request_json,'
            . ' response_json, created_at, expires_at FROM redp2p_pending'
            . ' WHERE id = ? AND expires_at >= ?'
        );
        $st->execute([$id, time()]);
        $row = $st->fetch(PDO::FETCH_ASSOC);
        if ($row === false) return null;
        $request = json_decode((string)$row['request_json'], true);
        $response = $row['response_json'] === null
            ? null : json_decode((string)$row['response_json'], true);
        $row['request'] = is_array($request) ? $request : [];
        $row['response'] = is_array($response) ? $response : null;
        unset($row['request_json'], $row['response_json']);
        return $row;
    }

    public function pendingRespond(
        string $id,
        string $publisherId,
        string $transport,
        array $response
    ): bool {
        $json = json_encode($response, JSON_UNESCAPED_SLASHES);
        if ($json === false) return false;
        $st = $this->db->prepare(
            'UPDATE redp2p_pending SET response_json = ? WHERE id = ?'
            . ' AND publisher_id = ? AND transport = ?'
            . ' AND response_json IS NULL AND expires_at >= ?'
        );
        $st->execute([$json, $id, $publisherId, $transport, time()]);
        return $st->rowCount() === 1;
    }

    public function pendingDeleteMany(array $ids): void
    {
        if ($ids === []) return;
        $ph = implode(',', array_fill(0, count($ids), '?'));
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE id IN (' . $ph . ')'
        );
        $st->execute($ids);
    }

    public function pendingDelete(string $id): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE id = ?'
        );
        $st->execute([$id]);
    }

    public function pendingDeleteByPublisher(string $publisherId): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE publisher_id = ?'
        );
        $st->execute([$publisherId]);
    }

    public function pendingCapacityAvailable(string $publisherId): ?array
    {
        $this->evictPending();
        $st = $this->db->prepare(
            'SELECT COUNT(*) FROM redp2p_pending'
            . ' WHERE publisher_id = ? AND expires_at >= ?'
        );
        $st->execute([$publisherId, time()]);
        if ((int)$st->fetchColumn() >= $this->maxConsumers) {
            return $this->jsonError(429, 'pending_limit_publisher');
        }
        $st = $this->db->prepare(
            'SELECT COUNT(*) FROM redp2p_pending WHERE expires_at >= ?'
        );
        $st->execute([time()]);
        if ((int)$st->fetchColumn() >= self::MAX_PENDING_CALLS_GLOBAL) {
            return $this->jsonError(429, 'pending_limit_global');
        }
        return null;
    }

    public function allowConnectRate(
        string $peerAddress,
        string $publisherId,
        array $publisher
    ): bool {
        $raw = @inet_pton($peerAddress);
        if ($raw === false) return false;
        if (strlen($raw) === 16 && substr($raw, 0, 12) ===
            str_repeat("\x00", 10) . "\xff\xff") {
            $raw = substr($raw, 12);
        }
        $address = bin2hex($raw);
        $now = (int)floor(microtime(true) * 1000);
        $expired = $this->db->prepare(
            'DELETE FROM redp2p_rate_sources WHERE updated_ms <= ?'
        );
        $expired->execute([$now - self::RATE_SOURCE_IDLE_MS]);
        $get = $this->db->prepare(
            'SELECT credit, updated_ms FROM redp2p_rate_sources'
            . ' WHERE address = ?'
        );
        $get->execute([$address]);
        $source = $get->fetch(PDO::FETCH_ASSOC);
        if ($source === false) {
            $count = (int)$this->db->query(
                'SELECT COUNT(*) FROM redp2p_rate_sources'
            )->fetchColumn();
            if ($count >= self::RATE_SOURCES_MAX) {
                $oldest = $this->db->query(
                    'SELECT address FROM redp2p_rate_sources'
                    . ' ORDER BY updated_ms, address LIMIT 1'
                )->fetchColumn();
                $remove = $this->db->prepare(
                    'DELETE FROM redp2p_rate_sources WHERE address = ?'
                );
                $remove->execute([$oldest]);
            }
            $insert = $this->db->prepare(
                'INSERT INTO redp2p_rate_sources (address, credit, updated_ms)'
                . ' VALUES (?, 20000, ?)'
            );
            $insert->execute([$address, $now]);
            $source = ['credit' => 20000, 'updated_ms' => $now];
        }
        $sourceNow = max($now, (int)$source['updated_ms']);
        $targetNow = max($now, (int)$publisher['connect_ms']);
        $sourceCredit = $this->refillCredit(
            (int)$source['credit'],
            (int)$source['updated_ms'],
            $sourceNow,
            20000,
            5
        );
        $targetCredit = $this->refillCredit(
            (int)$publisher['connect_credit'],
            (int)$publisher['connect_ms'],
            $targetNow,
            40000,
            10
        );
        $allowed = $sourceCredit >= 1000 && $targetCredit >= 1000;
        if ($allowed) {
            $sourceCredit -= 1000;
            $targetCredit -= 1000;
        }
        $update = $this->db->prepare(
            'UPDATE redp2p_rate_sources SET credit = ?, updated_ms = ?'
            . ' WHERE address = ?'
        );
        $update->execute([$sourceCredit, $sourceNow, $address]);
        $update = $this->db->prepare(
            'UPDATE redp2p_publishers SET connect_credit = ?, connect_ms = ?'
            . ' WHERE id = ?'
        );
        $update->execute([$targetCredit, $targetNow, $publisherId]);
        return $allowed;
    }

    private function refillCredit(
        int $credit,
        int $updated,
        int $now,
        int $capacity,
        int $rate
    ): int {
        $credit = max(0, min($capacity, $credit));
        $elapsed = min(4000, max(0, $now - $updated));
        return min($capacity, $credit + $elapsed * $rate);
    }

    public function beginPendingWrite(): string
    {
        $driver = $this->db->getAttribute(PDO::ATTR_DRIVER_NAME);
        if ($driver === 'sqlite') {
            $this->db->exec('BEGIN IMMEDIATE');
            return 'sqlite';
        }
        if ($driver === 'mysql') {
            try {
                $this->db->beginTransaction();
                $lock = $this->db->prepare(
                    'SELECT value FROM redp2p_meta'
                    . ' WHERE name = ? FOR UPDATE'
                );
                $lock->execute(['pending_write_lock']);
                if ($lock->fetchColumn() === false) {
                    throw new \PDOException('pending write lock is missing');
                }
                return 'mysql';
            } catch (\Throwable $e) {
                if ($this->db->inTransaction()) $this->db->rollBack();
                throw $e;
            }
        }
        throw new \PDOException('unsupported PDO driver');
    }

    public function endPendingWrite(string $lock, bool $commit): void
    {
        if ($lock === 'sqlite') {
            $this->db->exec($commit ? 'COMMIT' : 'ROLLBACK');
            return;
        }
        if ($lock === 'mysql') {
            $commit ? $this->db->commit() : $this->db->rollBack();
            return;
        }
        throw new \PDOException('unsupported pending write lock');
    }

    public function requireId(\stdClass $o, string $field): array
    {
        if (!$this->hasString($o, $field)) return [0, null];
        return $this->isValidId($o->$field) ? [1, $o->$field] : [-1, null];
    }

    public function requireHex(\stdClass $o, string $field, int $len): ?string
    {
        if (!$this->hasString($o, $field)) return null;
        return $this->isHexToken($o->$field, $len) ? $o->$field : null;
    }

    public function requireLowerHex(
        \stdClass $o,
        string $field,
        int $len
    ): ?string {
        if (!$this->hasString($o, $field)) return null;
        return preg_match('/^[0-9a-f]{' . $len . '}$/D', $o->$field) === 1
            ? $o->$field : null;
    }

    public function requireSequence(\stdClass $o): ?int
    {
        if (!property_exists($o, 'seq') || !is_int($o->seq) ||
            $o->seq < 1 || $o->seq > 9007199254740991) {
            return null;
        }
        return $o->seq;
    }

    public function getString(\stdClass $o, string $field): ?string
    {
        return $this->hasString($o, $field) ? $o->$field : null;
    }

    public function hexBytes(string $hex, int $length): ?string
    {
        if (strlen($hex) !== $length * 2 || !ctype_xdigit($hex)) return null;
        $bytes = hex2bin($hex);
        return $bytes !== false && strlen($bytes) === $length ? $bytes : null;
    }

    public function requireTimestamp(\stdClass $request, string $field): ?int
    {
        if (!property_exists($request, $field) ||
            !is_int($request->$field) || $request->$field < 0) {
            return null;
        }
        return $request->$field;
    }

    public function verifyChallenge(
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $mac
    ): bool {
        $now = time();
        if (strlen($nonce) !== 32 || strlen($mac) !== 32 ||
            $expiresAt <= $issuedAt || $expiresAt - $issuedAt !== 60 ||
            $issuedAt > $now + 5 || $now > $expiresAt) {
            return false;
        }
        $expected = hash_hmac(
            'sha256',
            $this->challengeMacInput($nonce, $issuedAt, $expiresAt),
            $this->challengeKey,
            true
        );
        return hash_equals($expected, $mac);
    }

    public function verifyPow(
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $id,
        string $solution
    ): bool {
        $digest = hash(
            'sha256',
            $this->powInput($nonce, $issuedAt, $expiresAt, $id, $solution),
            true
        );
        return $this->leadingZeroBits($digest) >= $this->pow;
    }

    public function admissionProof(string $password, string $message): string
    {
        return hash_hmac(
            'sha256',
            'REDP2P-ADMISSION-v1' . $message,
            $password,
            true
        );
    }

    public function simpleControlProof(
        string $secret,
        string $op,
        string $id,
        int $sequence,
        array $extra = []
    ): string {
        return hash_hmac(
            'sha256',
            implode("\n", [$op, $id, (string)$sequence, ...$extra]),
            $secret
        );
    }

    public function u64be(int $value): string
    {
        return pack('N2', intdiv($value, 4294967296), $value % 4294967296);
    }

    public function indexSecretKey(
        string $nonce,
        int $issuedAt,
        int $expiresAt
    ): string {
        return hash_hmac(
            'sha256',
            'REDP2P-INDEX-KEY' . $nonce . $this->u64be($issuedAt)
                . $this->u64be($expiresAt),
            $this->challengeKey,
            true
        );
    }

    public function jsonOk(array $fields = [], bool $okFirst = true): array
    {
        $payload = $okFirst ? array_merge(['ok' => true], $fields)
            : array_merge($fields, ['ok' => true]);
        $body = json_encode($payload, JSON_UNESCAPED_SLASHES);
        if ($body === false) return $this->jsonError(500, 'internal');
        return $this->response(200, 'OK', 'application/json', $body);
    }

    public function jsonError(int $status, string $code): array
    {
        $body = json_encode(['ok' => false, 'error' => $code]);
        if ($body === false) $body = '{"ok":false,"error":"internal"}';
        return $this->response(
            $status,
            $this->statusReason($status),
            'application/json',
            $body
        );
    }

    private function challengeMacInput(
        string $nonce,
        int $issuedAt,
        int $expiresAt
    ): string {
        return 'REDP2P-CHALLENGE' . $nonce . $this->u64be($issuedAt)
            . $this->u64be($expiresAt);
    }

    private function powInput(
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $id,
        string $solution
    ): string {
        return 'REDP2P-POW' . $nonce . $this->u64be($issuedAt)
            . $this->u64be($expiresAt) . pack('n', strlen($id)) . $id
            . $solution;
    }

    private function leadingZeroBits(string $bin): int
    {
        $total = 0;
        for ($i = 0, $len = strlen($bin); $i < $len; $i++) {
            $value = ord($bin[$i]);
            if ($value === 0) {
                $total += 8;
                continue;
            }
            for ($bit = 7;
                $bit >= 0 && (($value >> $bit) & 1) === 0;
                $bit--
            ) {
                $total++;
            }
            break;
        }
        return $total;
    }

    private function setPass(string $pass): void
    {
        if ($pass !== '' && !$this->isValidPassToken($pass)) {
            throw new \InvalidArgumentException('pass contains invalid bytes');
        }
        $this->pass = $pass;
    }

    private function setVips($vips): void
    {
        $map = [];
        if (is_string($vips)) {
            $tokens = preg_split('/\s+/', trim($vips), -1,
                PREG_SPLIT_NO_EMPTY) ?: [];
            if (count($tokens) % 2 !== 0) {
                throw new \InvalidArgumentException('vip has odd token count');
            }
            for ($i = 0; $i < count($tokens); $i += 2) {
                if (isset($map[$tokens[$i]])) {
                    throw new \InvalidArgumentException(
                        'vip redefines reserved id'
                    );
                }
                $map[$tokens[$i]] = $tokens[$i + 1];
            }
        } elseif (is_array($vips)) {
            foreach ($vips as $key => $value) {
                if (is_int($key)) {
                    if (!is_array($value) ||
                        !isset($value['id'], $value['pass'])) {
                        throw new \InvalidArgumentException(
                            'vip pair must provide id and pass'
                        );
                    }
                    $map[(string)$value['id']] = (string)$value['pass'];
                } else {
                    $map[(string)$key] = (string)$value;
                }
            }
        } elseif ($vips !== null) {
            throw new \InvalidArgumentException(
                'vip must be a string or an array'
            );
        }
        foreach ($map as $id => $password) {
            if (!$this->isValidId($id) ||
                !$this->isValidPassToken($password)) {
                throw new \InvalidArgumentException('vip contains invalid data');
            }
        }
        $this->vips = $map;
    }

    private function applyOptions(array $options): void
    {
        $pass = $options['pass'] ?? '';
        $vip = $options['vip'] ?? null;
        $seats = $options['seats'] ?? null;
        $pow = $options['pow'] ?? 0;
        $max = $options['max_consumers_per_publisher'] ?? null;
        $this->setPass((string)$pass);
        if ($vip !== null) $this->setVips($vip);
        if ($seats !== null) {
            if ((int)$seats < 0) {
                throw new \InvalidArgumentException(
                    'seats must be zero or positive'
                );
            }
            $this->seats = (int)$seats;
        }
        if ($max !== null) {
            if ((int)$max < 0) {
                throw new \InvalidArgumentException(
                    'max_consumers_per_publisher must be zero or positive'
                );
            }
            $this->maxConsumers = (int)$max === 0
                ? self::MAX_PENDING_CALLS_PER_PUBLISHER : (int)$max;
        }
        $pow = (int)$pow;
        if ($pow < 0 || $pow > 32) {
            throw new \InvalidArgumentException(
                'pow must be between 0 and 32'
            );
        }
        $this->pow = $pow;
    }

    private function countNonVip(): int
    {
        if ($this->vips === []) {
            return (int)$this->db->query(
                'SELECT COUNT(*) FROM redp2p_publishers'
            )->fetchColumn();
        }
        $ids = array_keys($this->vips);
        $ph = implode(',', array_fill(0, count($ids), '?'));
        $st = $this->db->prepare(
            'SELECT COUNT(*) FROM redp2p_publishers WHERE id NOT IN ('
            . $ph . ')'
        );
        $st->execute($ids);
        return (int)$st->fetchColumn();
    }

    private function getPass(string $id): string
    {
        return $this->vips[$id] ?? $this->pass;
    }

    private function evictStale(): void
    {
        $cutoff = time() - $this->ttl;
        $ids = $this->db->prepare(
            'SELECT id FROM redp2p_publishers WHERE last_seen < ?'
        );
        $ids->execute([$cutoff]);
        $expired = array_map('strval', $ids->fetchAll(PDO::FETCH_COLUMN));
        foreach ($expired as $id) $this->pendingDeleteByPublisher($id);
        $st = $this->db->prepare(
            'DELETE FROM redp2p_publishers WHERE last_seen < ?'
        );
        $st->execute([$cutoff]);
    }

    private function evictPending(): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE expires_at < ?'
        );
        $st->execute([time()]);
    }

    private function ensureSchema(): void
    {
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_publishers ('
            . ' id VARCHAR(63) NOT NULL PRIMARY KEY'
            . ', key CHAR(16) NOT NULL'
            . ', seq INTEGER NOT NULL DEFAULT 0'
            . ', transport VARCHAR(8) NOT NULL'
            . ', transport_data TEXT NOT NULL'
            . ', connect_credit INTEGER NOT NULL DEFAULT 40000'
            . ', connect_ms BIGINT NOT NULL DEFAULT 0'
            . ', last_seen INTEGER NOT NULL)'
        );
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_rate_sources ('
            . ' address VARCHAR(32) NOT NULL PRIMARY KEY'
            . ', credit INTEGER NOT NULL'
            . ', updated_ms BIGINT NOT NULL)'
        );
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_pending ('
            . ' id VARCHAR(64) NOT NULL PRIMARY KEY'
            . ', publisher_id VARCHAR(63) NOT NULL'
            . ', transport VARCHAR(8) NOT NULL'
            . ', consumer_hash CHAR(64) NULL'
            . ', request_json TEXT NOT NULL'
            . ', response_json TEXT NULL'
            . ', created_at INTEGER NOT NULL'
            . ', expires_at INTEGER NOT NULL)'
        );
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_meta ('
            . ' name VARCHAR(32) NOT NULL PRIMARY KEY'
            . ', value TEXT NULL)'
        );
        try {
            $st = $this->db->prepare(
                'INSERT INTO redp2p_meta (name, value) VALUES (?, ?)'
            );
            $st->execute(['pending_write_lock', '']);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0])
                ? (string)$e->errorInfo[0] : (string)$e->getCode();
            if (strncmp($state, '23', 2) !== 0) throw $e;
        }
        try {
            $this->db->exec(
                'CREATE INDEX IF NOT EXISTS redp2p_pending_publisher'
                . ' ON redp2p_pending (publisher_id)'
            );
            $this->db->exec(
                'CREATE INDEX IF NOT EXISTS redp2p_pending_expiry'
                . ' ON redp2p_pending (expires_at)'
            );
        } catch (\PDOException $e) {
        }
    }

    private function loadChallengeKey(): string
    {
        $candidate = bin2hex(random_bytes(32));
        try {
            $st = $this->db->prepare(
                'INSERT INTO redp2p_meta (name, value) VALUES (?, ?)'
            );
            $st->execute(['registration_challenge_key', $candidate]);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0])
                ? (string)$e->errorInfo[0] : (string)$e->getCode();
            if (strncmp($state, '23', 2) !== 0) throw $e;
        }
        $st = $this->db->prepare(
            'SELECT value FROM redp2p_meta WHERE name = ?'
        );
        $st->execute(['registration_challenge_key']);
        $value = $st->fetchColumn();
        if (!is_string($value) ||
            !preg_match('/^[0-9a-f]{64}$/D', $value)) {
            throw new \PDOException(
                'registration challenge key is malformed'
            );
        }
        $key = hex2bin($value);
        if ($key === false || strlen($key) !== 32) {
            throw new \PDOException(
                'registration challenge key is malformed'
            );
        }
        return $key;
    }

    private function isValidId(string $id): bool
    {
        return strlen($id) >= 1 && strlen($id) <= self::ID_MAX
            && preg_match('/^[A-Za-z0-9]+$/D', $id) === 1;
    }

    private function isHexToken(string $token, int $len): bool
    {
        return strlen($token) === $len && ctype_xdigit($token);
    }

    private function isValidPassToken(string $pass): bool
    {
        if (strlen($pass) > self::PASS_MAX) return false;
        for ($i = 0, $len = strlen($pass); $i < $len; $i++) {
            $c = ord($pass[$i]);
            if ($c < 0x21 || $c > 0x7e) return false;
        }
        return true;
    }

    private function hasString(\stdClass $o, string $field): bool
    {
        return property_exists($o, $field) && is_string($o->$field);
    }

    private function hasHeader(array $headers, string $name): bool
    {
        foreach ($headers as $key => $_) {
            if (strcasecmp((string)$key, $name) === 0) return true;
        }
        return false;
    }

    private function hasDuplicateTopKeys(string $json): bool
    {
        $depth = 0;
        $inString = false;
        $escape = false;
        $keys = [];
        $len = strlen($json);
        for ($i = 0; $i < $len; $i++) {
            $c = $json[$i];
            if ($inString) {
                if ($escape) {
                    $escape = false;
                } elseif ($c === '\\') {
                    $escape = true;
                } elseif ($c === '"') {
                    $inString = false;
                }
                continue;
            }
            if ($c === '"') {
                if ($depth === 1) {
                    $start = ++$i;
                    $raw = '';
                    $esc = false;
                    for (; $i < $len; $i++) {
                        $ch = $json[$i];
                        if ($esc) {
                            $raw .= '\\' . $ch;
                            $esc = false;
                            continue;
                        }
                        if ($ch === '\\') {
                            $esc = true;
                            continue;
                        }
                        if ($ch === '"') break;
                        $raw .= $ch;
                    }
                    $j = $i + 1;
                    while ($j < $len && ctype_space($json[$j])) $j++;
                    if ($j < $len && $json[$j] === ':') {
                        $decoded = json_decode('"' . $raw . '"');
                        if (is_string($decoded)) {
                            if (isset($keys[$decoded])) return true;
                            $keys[$decoded] = true;
                        }
                    }
                    continue;
                }
                $inString = true;
            } elseif ($c === '{' || $c === '[') {
                $depth++;
            } elseif ($c === '}' || $c === ']') {
                $depth--;
            }
        }
        return false;
    }

    private function httpError(int $status, string $reason): array
    {
        return $this->response($status, $reason, 'text/plain', '');
    }

    private function response(
        int $status,
        string $reason,
        string $contentType,
        string $body
    ): array {
        return [
            'status' => $status,
            'reason' => $reason,
            'headers' => [
                'Content-Type' => $contentType,
                'Content-Length' => (string)strlen($body),
                'Connection' => 'close',
                'Access-Control-Allow-Origin' => '*',
                'Access-Control-Allow-Headers' => 'Content-Type',
                'Access-Control-Allow-Methods' => 'POST, OPTIONS',
                'Cache-Control' => 'no-store',
            ],
            'body' => $body,
        ];
    }

    private function statusReason(int $status): string
    {
        return match ($status) {
            200 => 'OK',
            400 => 'Bad Request',
            403 => 'Forbidden',
            404 => 'Not Found',
            405 => 'Method Not Allowed',
            409 => 'Conflict',
            413 => 'Payload Too Large',
            429 => 'Too Many Requests',
            500 => 'Internal Server Error',
            501 => 'Not Implemented',
            503 => 'Service Unavailable',
            default => 'Error',
        };
    }
}

if (!\class_exists('Redp2pIndex', false)) {
    \class_alias(Redp2pIndex::class, 'Redp2pIndex');
}
