<?php
/**
 * redp2p-idx.php - REDP2P index server.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);
namespace KaisarCode;

use PDO;

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

    /**
     * Initializes the index with its database connection and options.
     * @param PDO|array $db Database connection or connection configuration.
     * @param array $options Index configuration.
     * @return void
     */
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

    /**
     * Serves the current HTTP request through the index.
     * @return void
     */
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

    /**
     * Handles an HTTP request for the index protocol.
     * @param string $method HTTP request method.
     * @param string $body HTTP request body.
     * @param array $headers HTTP request headers.
     * @param ?string $peerAddress Client network address.
     * @return array HTTP response.
     */
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

    /**
     * Removes expired publishers and pending requests.
     * @return void
     */
    public function prune(): void
    {
        $this->evictStale();
        $this->evictPending();
    }

    /**
     * Lists currently active publisher identifiers.
     * @return array Active publisher identifiers.
     */
    public function list(): array
    {
        $cutoff = time() - $this->ttl;
        $st = $this->db->prepare(
            'SELECT id FROM redp2p_publishers WHERE last_seen >= ?'
        );
        $st->execute([$cutoff]);
        return array_map('strval', $st->fetchAll(PDO::FETCH_COLUMN));
    }

    /**
     * Registers a protocol transport module.
     * @param object $transport Transport module.
     * @return void
     */
    private function registerTransport(object $transport): void
    {
        $this->transports[$transport->key()] = $transport;
    }

    /**
     * Finds a registered transport by its key.
     * @param string $key Transport key.
     * @return ?object Registered transport or null.
     */
    private function transportByKey(string $key): ?object
    {
        return $this->transports[$key] ?? null;
    }

    /**
     * Finds the transport module for a publisher transport name.
     * @param string $transport Publisher transport name.
     * @return ?object Matching transport or null.
     */
    private function transportForPublisher(string $transport): ?object
    {
        foreach ($this->transports as $module) {
            if ($module->acceptsPublisherTransport($transport)) {
                return $module;
            }
        }
        return null;
    }

    /**
     * Dispatches a transport-specific protocol operation.
     * @param string $operation Requested operation.
     * @param \stdClass $request Decoded request.
     * @param ?string $peerAddress Client network address.
     * @return array HTTP response.
     */
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

    /**
     * Creates a signed challenge for a publisher.
     * @param \stdClass $request Decoded request.
     * @return array HTTP response.
     */
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

    /**
     * Registers a publisher through its selected transport.
     * @return array HTTP response.
     */
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

    /**
     * Refreshes a publisher registration.
     * @return array HTTP response.
     */
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

    /**
     * Looks up an active publisher.
     * @return array HTTP response.
     */
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

    /**
     * Removes a registered publisher.
     * @return array HTTP response.
     */
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

    /**
     * Stores a verified publisher registration.
     * @return array Registration result.
     */
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

    /**
     * Adds a publisher record to the index.
     * @return void
     */
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

    /**
     * Retrieves a publisher record by identifier.
     * @return ?array Publisher record or null.
     */
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

    /**
     * Updates a publisher record.
     * @return bool Whether the record was updated.
     */
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

    /**
     * Updates a publisher heartbeat sequence.
     * @return bool Whether the publisher was updated.
     */
    public function touchPublisher(string $id, int $seq): bool
    {
        $st = $this->db->prepare(
            'UPDATE redp2p_publishers SET seq = ?, last_seen = ?'
            . ' WHERE id = ? AND seq < ?'
        );
        $st->execute([$seq, time(), $id, $seq]);
        return $st->rowCount() === 1;
    }

    /**
     * Creates a pending connection request.
     * @return string Pending request identifier.
     */
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

    /**
     * Lists pending requests for a publisher.
     * @return array Pending requests.
     */
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

    /**
     * Retrieves a pending request by identifier.
     * @return ?array Pending request or null.
     */
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

    /**
     * Records a response for a pending request.
     * @return bool Whether the request was updated.
     */
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

    /**
     * Deletes multiple pending requests.
     * @return void
     */
    public function pendingDeleteMany(array $ids): void
    {
        if ($ids === []) return;
        $ph = implode(',', array_fill(0, count($ids), '?'));
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE id IN (' . $ph . ')'
        );
        $st->execute($ids);
    }

    /**
     * Deletes a pending request.
     * @return void
     */
    public function pendingDelete(string $id): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE id = ?'
        );
        $st->execute([$id]);
    }

    /**
     * Deletes pending requests for a publisher.
     * @return void
     */
    public function pendingDeleteByPublisher(string $publisherId): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE publisher_id = ?'
        );
        $st->execute([$publisherId]);
    }

    /**
     * Checks the pending-request capacity for a publisher.
     * @return ?array Capacity state or null.
     */
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

    /**
     * Applies the connection rate limit.
     * @return bool Whether the connection is allowed.
     */
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

    /**
     * Refills a rate-limit credit balance.
     * @return float Updated credit balance.
     */
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

    /**
     * Begins a pending-request write transaction.
     * @return string Transaction lock token.
     */
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

    /**
     * Ends a pending-request write transaction.
     * @return void
     */
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

    /**
     * Validates and reads an identifier field.
     * @return array Validation result and identifier.
     */
    public function requireId(\stdClass $o, string $field): array
    {
        if (!$this->hasString($o, $field)) return [0, null];
        return $this->isValidId($o->$field) ? [1, $o->$field] : [-1, null];
    }

    /**
     * Validates and reads a hexadecimal field.
     * @return ?string Hexadecimal value or null.
     */
    public function requireHex(\stdClass $o, string $field, int $len): ?string
    {
        if (!$this->hasString($o, $field)) return null;
        return $this->isHexToken($o->$field, $len) ? $o->$field : null;
    }

    /**
     * Validates and reads a lowercase hexadecimal field.
     * @return ?string Hexadecimal value or null.
     */
    public function requireLowerHex(
        \stdClass $o,
        string $field,
        int $len
    ): ?string {
        if (!$this->hasString($o, $field)) return null;
        return preg_match('/^[0-9a-f]{' . $len . '}$/D', $o->$field) === 1
            ? $o->$field : null;
    }

    /**
     * Validates and reads a sequence number.
     * @return ?int Sequence number or null.
     */
    public function requireSequence(\stdClass $o): ?int
    {
        if (!property_exists($o, 'seq') || !is_int($o->seq) ||
            $o->seq < 1 || $o->seq > 9007199254740991) {
            return null;
        }
        return $o->seq;
    }

    /**
     * Reads a string field from a request.
     * @return ?string Field value or null.
     */
    public function getString(\stdClass $o, string $field): ?string
    {
        return $this->hasString($o, $field) ? $o->$field : null;
    }

    /**
     * Decodes a fixed-length hexadecimal value.
     * @return ?string Decoded bytes or null.
     */
    public function hexBytes(string $hex, int $length): ?string
    {
        if (strlen($hex) !== $length * 2 || !ctype_xdigit($hex)) return null;
        $bytes = hex2bin($hex);
        return $bytes !== false && strlen($bytes) === $length ? $bytes : null;
    }

    /**
     * Validates and reads a timestamp field.
     * @return ?int Timestamp or null.
     */
    public function requireTimestamp(\stdClass $request, string $field): ?int
    {
        if (!property_exists($request, $field) ||
            !is_int($request->$field) || $request->$field < 0) {
            return null;
        }
        return $request->$field;
    }

    /**
     * Verifies a publisher challenge response.
     * @return bool Whether the challenge is valid.
     */
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

    /**
     * Verifies proof-of-work for a request.
     * @return bool Whether the proof is valid.
     */
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

    /**
     * Creates an admission proof.
     * @return string Admission proof.
     */
    public function admissionProof(string $password, string $message): string
    {
        return hash_hmac(
            'sha256',
            'REDP2P-ADMISSION-v1' . $message,
            $password,
            true
        );
    }

    /**
     * Creates a proof for a control operation.
     * @return string Control-operation proof.
     */
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

    /**
     * Encodes an integer as unsigned 64-bit big-endian bytes.
     * @return string Encoded bytes.
     */
    public function u64be(int $value): string
    {
        return pack('N2', intdiv($value, 4294967296), $value % 4294967296);
    }

    /**
     * Derives an index secret key for a challenge.
     * @return string Secret key bytes.
     */
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

    /**
     * Builds a successful JSON response.
     * @return array HTTP response.
     */
    public function jsonOk(array $fields = [], bool $okFirst = true): array
    {
        $payload = $okFirst ? array_merge(['ok' => true], $fields)
            : array_merge($fields, ['ok' => true]);
        $body = json_encode($payload, JSON_UNESCAPED_SLASHES);
        if ($body === false) return $this->jsonError(500, 'internal');
        return $this->response(200, 'OK', 'application/json', $body);
    }

    /**
     * Builds an error JSON response.
     * @return array HTTP response.
     */
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

    /**
     * Encodes fields covered by a challenge MAC.
     * @return string MAC input bytes.
     */
    private function challengeMacInput(
        string $nonce,
        int $issuedAt,
        int $expiresAt
    ): string {
        return 'REDP2P-CHALLENGE' . $nonce . $this->u64be($issuedAt)
            . $this->u64be($expiresAt);
    }

    /**
     * Encodes fields covered by proof-of-work.
     * @return string Proof-of-work input bytes.
     */
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

    /**
     * Counts leading zero bits in binary data.
     * @return int Leading zero-bit count.
     */
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

    /**
     * Sets the index admission password.
     * @return void
     */
    private function setPass(string $pass): void
    {
        if ($pass !== '' && !$this->isValidPassToken($pass)) {
            throw new \InvalidArgumentException('pass contains invalid bytes');
        }
        $this->pass = $pass;
    }

    /**
     * Sets the privileged publisher identifiers.
     * @return void
     */
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

    /**
     * Applies index configuration options.
     * @return void
     */
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

    /**
     * Counts non-privileged publishers.
     * @return int Publisher count.
     */
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

    /**
     * Retrieves the admission password for an identifier.
     * @return string Admission password.
     */
    private function getPass(string $id): string
    {
        return $this->vips[$id] ?? $this->pass;
    }

    /**
     * Removes stale publisher records.
     * @return void
     */
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

    /**
     * Removes expired pending requests.
     * @return void
     */
    private function evictPending(): void
    {
        $st = $this->db->prepare(
            'DELETE FROM redp2p_pending WHERE expires_at < ?'
        );
        $st->execute([time()]);
    }

    /**
     * Creates required database tables.
     * @return void
     */
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

    /**
     * Loads or creates the challenge signing key.
     * @return string Challenge signing key.
     */
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

    /**
     * Checks whether an identifier is valid.
     * @return bool Whether the identifier is valid.
     */
    private function isValidId(string $id): bool
    {
        return strlen($id) >= 1 && strlen($id) <= self::ID_MAX
            && preg_match('/^[A-Za-z0-9]+$/D', $id) === 1;
    }

    /**
     * Checks whether a token is hexadecimal with a fixed length.
     * @return bool Whether the token is valid.
     */
    private function isHexToken(string $token, int $len): bool
    {
        return strlen($token) === $len && ctype_xdigit($token);
    }

    /**
     * Checks whether a password token is valid.
     * @return bool Whether the password token is valid.
     */
    private function isValidPassToken(string $pass): bool
    {
        if (strlen($pass) > self::PASS_MAX) return false;
        for ($i = 0, $len = strlen($pass); $i < $len; $i++) {
            $c = ord($pass[$i]);
            if ($c < 0x21 || $c > 0x7e) return false;
        }
        return true;
    }

    /**
     * Checks whether a request field is a string.
     * @return bool Whether the field is a string.
     */
    private function hasString(\stdClass $o, string $field): bool
    {
        return property_exists($o, $field) && is_string($o->$field);
    }

    /**
     * Checks whether HTTP headers contain a name.
     * @return bool Whether the header is present.
     */
    private function hasHeader(array $headers, string $name): bool
    {
        foreach ($headers as $key => $_) {
            if (strcasecmp((string)$key, $name) === 0) return true;
        }
        return false;
    }

    /**
     * Checks JSON text for duplicate top-level keys.
     * @return bool Whether duplicate keys exist.
     */
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

    /**
     * Builds a plain HTTP error response.
     * @return array HTTP response.
     */
    private function httpError(int $status, string $reason): array
    {
        return $this->response($status, $reason, 'text/plain', '');
    }

    /**
     * Builds an HTTP response structure.
     * @return array HTTP response.
     */
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

    /**
     * Resolves a reason phrase for an HTTP status.
     * @return string HTTP reason phrase.
     */
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

/**
 * Summary: REDP2P native punch transport module
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
final class Redp2pNative
{
    private const ADDR_MAX = 47;
    private const CANDIDATES_MAX = 8;

    /**
     * Returns the native transport key.
     * @return Native transport name.
     */
    public function key(): string
    {
        return 'native';
    }

    /**
     * Reports whether a publisher transport is native.
     * @return Whether the transport is accepted.
     */
    public function acceptsPublisherTransport(string $transport): bool
    {
        return $transport === 'tcp' || $transport === 'udp';
    }

    /**
     * Reports whether an operation belongs to this transport.
     * @return Whether the operation is handled.
     */
    public function handles(string $operation): bool
    {
        return $operation === 'punch_req' || $operation === 'punch_poll';
    }

    /**
     * Registers one native publisher.
     * @return Index response payload.
     */
    public function register(Redp2pIndex $index, \stdClass $request, string $id): array
    {
        $nonce = $index->requireHex($request, 'nonce', 64);
        $mac = $index->requireHex($request, 'mac', 64);
        $solution = $index->requireHex($request, 'pow_solution', 16);
        $proof = $index->requireHex($request, 'proof', 64);
        $access = $index->requireLowerHex($request, 'access_proof', 64);
        if (property_exists($request, 'access_proof') && $access === null) {
            return $index->jsonError(400, 'bad_request');
        }
        $pkey = $index->requireHex($request, 'pkey', 64);
        $encryptedSecret = $index->requireHex($request, 'secret', 64);
        $issuedAt = $index->requireTimestamp($request, 'issued_at');
        $expiresAt = $index->requireTimestamp($request, 'expires_at');
        if ($nonce === null || $mac === null || $solution === null || $proof === null ||
            $pkey === null || $encryptedSecret === null || $issuedAt === null ||
            $expiresAt === null) {
            return $index->jsonError(400, 'bad_request');
        }
        [$proto, $udpPort] = $this->requireProtoPort($request);
        if ($proto === null || $udpPort === null) {
            return $index->jsonError(400, 'bad_request');
        }
        [$candOk, $candidates] = $this->parseCandidates($request, 'candidates');
        if (!$candOk) return $index->jsonError(400, 'bad_request');

        $nonceRaw = $index->hexBytes($nonce, 32);
        $macRaw = $index->hexBytes($mac, 32);
        $proofRaw = $index->hexBytes($proof, 32);
        $solutionRaw = $index->hexBytes($solution, 8);
        $publisherPublic = $index->hexBytes($pkey, 32);
        $wireSecret = $index->hexBytes($encryptedSecret, 32);
        if ($nonceRaw === null || $macRaw === null || $proofRaw === null ||
            $solutionRaw === null || $publisherPublic === null || $wireSecret === null) {
            return $index->jsonError(400, 'bad_request');
        }
        if (!$index->verifyChallenge($nonceRaw, $issuedAt, $expiresAt, $macRaw) ||
            !$index->verifyPow($nonceRaw, $issuedAt, $expiresAt, $id, $solutionRaw)) {
            return $index->jsonError(403, 'auth_failed');
        }
        $secret = $this->decryptRegistrationSecret(
            $index,
            $nonceRaw,
            $issuedAt,
            $expiresAt,
            $publisherPublic,
            $wireSecret
        );
        if ($secret === null || !$this->isControlSecret($secret)) {
            return $index->jsonError(403, 'auth_failed');
        }
        $message = $this->registrationMessage(
            $index,
            $nonceRaw,
            $issuedAt,
            $expiresAt,
            $id,
            $secret,
            $proto,
            $udpPort,
            $candidates,
            $solutionRaw
        );
        $expected = hash_hmac('sha256', $message, $secret, true);
        if (!hash_equals($expected, $proofRaw)) {
            return $index->jsonError(403, 'auth_failed');
        }
        return $index->commitPublisherRegistration(
            $id,
            $secret,
            $proto === 1 ? 'tcp' : 'udp',
            [
                'proto' => $proto,
                'udp_port' => $udpPort,
                'candidates' => $candidates,
            ],
            $message,
            $access === null ? null : hex2bin($access)
        );
    }

    /**
     * Refreshes one native publisher record.
     * @return Index response payload.
     */
    public function heartbeat(
        Redp2pIndex $index,
        \stdClass $request,
        string $id,
        array $publisher
    ): array {
        $seq = $index->requireSequence($request);
        $proof = $index->requireHex($request, 'proof', 64);
        if ($seq === null || $proof === null) return $index->jsonError(400, 'bad_request');
        [$proto, $udpPort] = $this->requireProtoPort($request);
        if ($proto === null || $udpPort === null) return $index->jsonError(400, 'bad_request');
        [$ok, $candidates] = $this->parseCandidates($request, 'candidates');
        if (!$ok) return $index->jsonError(400, 'bad_request');
        if ($seq <= (int)$publisher['seq']) return $index->jsonError(403, 'invalid_proof');
        $expected = $this->controlProof(
            (string)$publisher['key'],
            'heartbeat',
            $id,
            $seq,
            $proto,
            $udpPort,
            $candidates
        );
        if (!hash_equals($expected, strtolower($proof))) {
            return $index->jsonError(403, 'invalid_proof');
        }
        $updated = $index->updatePublisher(
            $id,
            $seq,
            $proto === 1 ? 'tcp' : 'udp',
            [
                'proto' => $proto,
                'udp_port' => $udpPort,
                'candidates' => $candidates,
            ]
        );
        return $updated ? $index->jsonOk() : $index->jsonError(403, 'invalid_proof');
    }

    /**
     * Exposes native publisher lookup metadata.
     * @return Native lookup fields.
     */
    public function lookup(Redp2pIndex $index, array $publisher): array
    {
        $data = $publisher['transport_data'];
        return [
            'proto' => (int)($data['proto'] ?? 0),
            'udp_port' => (int)($data['udp_port'] ?? 0),
            'candidates' => is_array($data['candidates'] ?? null)
                ? $data['candidates'] : [],
        ];
    }

    /**
     * Dispatches a native transport operation.
     * @return Index response payload.
     */
    public function handle(
        Redp2pIndex $index,
        string $operation,
        \stdClass $request,
        ?string $peerAddress
    ): array {
        return $operation === 'punch_req'
            ? $this->handlePunchReq($index, $request, $peerAddress)
            : $this->handlePunchPoll($index, $request);
    }

    /**
     * Queues one native punch request.
     * @return Index response payload.
     */
    private function handlePunchReq(
        Redp2pIndex $index,
        \stdClass $request,
        ?string $peerAddress
    ): array {
        [$selfResult, $selfId] = $index->requireId($request, 'self_id');
        [$targetResult, $targetId] = $index->requireId($request, 'target_id');
        if ($selfResult === 0 || $targetResult === 0) {
            return $index->jsonError(400, 'bad_request');
        }
        if ($selfResult < 0 || $targetResult < 0) {
            return $index->jsonError(400, 'invalid_id');
        }
        if ($peerAddress === null || !filter_var($peerAddress, FILTER_VALIDATE_IP)) {
            return $index->jsonError(400, 'bad_request');
        }
        $session = $index->getString($request, 'session');
        if ($session === null || !$this->isSessionToken($session)) {
            return $index->jsonError(400, 'bad_request');
        }
        if (!property_exists($request, 'udp_port')) return $index->jsonError(400, 'bad_request');
        $udpPort = $this->requirePort($request->udp_port);
        if ($udpPort === null) return $index->jsonError(400, 'bad_request');
        [$ok, $candidates] = $this->parseCandidates($request, 'candidates');
        if (!$ok) return $index->jsonError(400, 'bad_request');
        $candidates = $this->mergeObserved($candidates, $peerAddress, $udpPort);
        if ($candidates === null) return $index->jsonError(400, 'bad_request');

        $lock = $index->beginPendingWrite();
        $active = true;
        try {
            $publisher = $index->publisher($targetId, true);
            if ($publisher === null) {
                $index->endPendingWrite($lock, true);
                return $index->jsonError(404, 'not_found');
            }
            if (!$this->acceptsPublisherTransport((string)$publisher['transport'])) {
                $index->endPendingWrite($lock, false);
                return $index->jsonError(409, 'unsupported_transport');
            }
            if (!$index->allowConnectRate($peerAddress, $targetId, $publisher)) {
                $index->endPendingWrite($lock, true);
                return $index->jsonError(429, 'rate_limited');
            }
            $capacityError = $index->pendingCapacityAvailable($targetId);
            if ($capacityError !== null) {
                $index->endPendingWrite($lock, true);
                return $capacityError;
            }
            $index->pendingCreate(
                bin2hex(random_bytes(8)),
                $targetId,
                'native',
                [
                    'self_id' => $selfId,
                    'session' => $session,
                    'candidates' => $candidates,
                ]
            );
            $index->endPendingWrite($lock, true);
            $active = false;
            return $index->jsonOk();
        } catch (\Throwable $e) {
            if ($active) $index->endPendingWrite($lock, false);
            throw $e;
        }
    }

    /**
     * Collects pending native punch requests.
     * @return Index response payload.
     */
    private function handlePunchPoll(Redp2pIndex $index, \stdClass $request): array
    {
        [$result, $id] = $index->requireId($request, 'id');
        if ($result === 0) return $index->jsonError(400, 'bad_request');
        if ($result < 0) return $index->jsonError(400, 'invalid_id');
        $seq = $index->requireSequence($request);
        $proof = $index->requireHex($request, 'proof', 64);
        if ($seq === null || $proof === null) return $index->jsonError(400, 'bad_request');
        $publisher = $index->publisher($id);
        if ($publisher === null) return $index->jsonError(404, 'not_found');
        if (!$this->acceptsPublisherTransport((string)$publisher['transport'])) {
            return $index->jsonError(409, 'unsupported_transport');
        }
        $expected = $this->controlProof(
            (string)$publisher['key'],
            'punch_poll',
            $id,
            $seq
        );
        if ($seq <= (int)$publisher['seq'] ||
            !hash_equals($expected, strtolower($proof)))
        {
            return $index->jsonError(403, 'invalid_proof');
        }
        if (!$index->touchPublisher($id, $seq)) {
            return $index->jsonError(403, 'invalid_proof');
        }
        $rows = $index->pendingForPublisher($id, 'native', false);
        $calls = [];
        $consumed = [];
        foreach ($rows as $row) {
            $payload = $row['request'];
            $consumed[] = (string)$row['id'];
            $calls[] = [
                'self_id' => (string)($payload['self_id'] ?? ''),
                'session' => (string)($payload['session'] ?? ''),
                'candidates' => is_array($payload['candidates'] ?? null)
                    ? $payload['candidates'] : [],
            ];
        }
        $index->pendingDeleteMany($consumed);
        return $index->jsonOk(['calls' => $calls], false);
    }

    /**
     * Extracts a native protocol and UDP port.
     * @return Parsed pair or null fields on failure.
     */
    private function requireProtoPort(\stdClass $request): array
    {
        if (!property_exists($request, 'proto') || !property_exists($request, 'udp_port')) {
            return [null, null];
        }
        $proto = $request->proto;
        if (!is_int($proto) && !is_float($proto)) return [null, null];
        $proto = (float)$proto;
        if ($proto !== 1.0 && $proto !== 2.0) return [null, null];
        $port = $this->requirePort($request->udp_port);
        return $port === null ? [null, null] : [(int)$proto, $port];
    }

    /**
     * Validates one UDP port.
     * @return Port or null when invalid.
     */
    private function requirePort(mixed $value): ?int
    {
        if (!is_int($value) && !is_float($value)) return null;
        $number = (float)$value;
        if (!is_finite($number) || $number < 1.0 || $number > 65535.0 ||
            $number !== floor($number)) {
            return null;
        }
        return (int)$number;
    }

    /**
     * Parses request candidates under native policy.
     * @return Normalized candidates or an empty array.
     */
    private function parseCandidates(\stdClass $request, string $field): array
    {
        if (!property_exists($request, $field)) return [true, []];
        if (!is_array($request->$field)) return [true, []];
        $out = [];
        foreach ($request->$field as $item) {
            if (!($item instanceof \stdClass)) return [false, []];
            $type = property_exists($item, 'type') && is_string($item->type)
                ? $item->type : null;
            $addr = property_exists($item, 'addr') && is_string($item->addr)
                ? $item->addr : null;
            if ($type === null || $addr === null || strlen($addr) > self::ADDR_MAX) {
                return [false, []];
            }
            if (!in_array($type, ['host', 'srflx', 'relay'], true)) return [false, []];
            if (!filter_var($addr, FILTER_VALIDATE_IP) || !property_exists($item, 'port')) {
                return [false, []];
            }
            $port = $this->requirePort($item->port);
            $raw = @inet_pton($addr);
            if ($port === null || $raw === false || !$this->candidateDestAllowed($raw, $port)) {
                return [false, []];
            }
            $out[] = ['type' => $type, 'addr' => inet_ntop($raw), 'port' => $port];
        }
        $out = $this->normalizeCandidates($out);
        return count($out) > self::CANDIDATES_MAX ? [false, []] : [true, $out];
    }

    /**
     * Checks whether one candidate destination is permitted.
     * @return Whether the destination is valid.
     */
    private function candidateDestAllowed(string $raw, int $port): bool
    {
        if ($port < 1) return false;
        if (strlen($raw) === 4) {
            $first = ord($raw[0]);
            return $first !== 0 && $first !== 127 && $first < 224;
        }
        if ($raw[0] === "\xff") return false;
        if (substr($raw, 0, 12) === str_repeat("\x00", 10) . "\xff\xff") {
            return $this->candidateDestAllowed(substr($raw, 12), $port);
        }
        if ($raw === str_repeat("\x00", 16) || $raw === str_repeat("\x00", 15) . "\x01") {
            return false;
        }
        return true;
    }

    /**
     * Normalizes one native candidate list.
     * @return Normalized candidates.
     */
    private function normalizeCandidates(array $candidates): array
    {
        usort($candidates, function (array $a, array $b): int {
            $ar = inet_pton($a['addr']);
            $br = inet_pton($b['addr']);
            $family = strlen($ar) <=> strlen($br);
            if ($family !== 0) return $family;
            $addr = strcmp($ar, $br);
            if ($addr !== 0) return $addr;
            $port = $a['port'] <=> $b['port'];
            return $port !== 0 ? $port : ($a['type'] <=> $b['type']);
        });
        $unique = [];
        foreach ($candidates as $candidate) {
            $key = $candidate['type'] . "\0" . inet_pton($candidate['addr'])
                . "\0" . pack('n', $candidate['port']);
            $unique[$key] = $candidate;
        }
        return array_values($unique);
    }

    /**
     * Merges the index-observed candidate.
     * @return Updated candidates or null on failure.
     */
    private function mergeObserved(array $candidates, string $peerAddress, int $udpPort): ?array
    {
        $raw = @inet_pton($peerAddress);
        if ($raw === false) return $candidates;
        $observed = ['type' => 'observed', 'addr' => inet_ntop($raw), 'port' => $udpPort];
        $matched = false;
        foreach ($candidates as $i => $candidate) {
            if ($candidate['type'] === 'host' && $candidate['port'] === $udpPort &&
                @inet_pton($candidate['addr']) === $raw) {
                $candidates[$i] = $observed;
                $matched = true;
                break;
            }
        }
        if (!$matched) {
            if (count($candidates) >= self::CANDIDATES_MAX) {
                for ($i = count($candidates) - 1; $i >= 0; $i--) {
                    if ($candidates[$i]['type'] === 'host') {
                        unset($candidates[$i]);
                        break;
                    }
                }
                $candidates = array_values($candidates);
                if (count($candidates) >= self::CANDIDATES_MAX) return null;
            }
            $candidates[] = $observed;
        }
        return $this->normalizeCandidates($candidates);
    }

    /**
     * Decrypts a native registration secret.
     * @return Secret or null when invalid.
     */
    private function decryptRegistrationSecret(
        Redp2pIndex $index,
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $publisherPublic,
        string $wireSecret
    ): ?string {
        try {
            $indexSecret = $index->indexSecretKey($nonce, $issuedAt, $expiresAt);
            $indexPublic = sodium_crypto_scalarmult_base($indexSecret);
            $shared = sodium_crypto_scalarmult($indexSecret, $publisherPublic);
            if (strlen($shared) !== 32 || hash_equals($shared, str_repeat("\x00", 32))) {
                return null;
            }
            $key = hash_hmac(
                'sha256',
                'REDP2P-REGISTER-SECRET' . $nonce . $indexPublic . $publisherPublic,
                $shared,
                true
            );
            $secret = sodium_crypto_aead_xchacha20poly1305_ietf_decrypt(
                $wireSecret,
                '',
                substr($nonce, 0, 24),
                $key
            );
            return $secret === false || strlen($secret) !== Redp2pIndex::KEY_SZ
                ? null : $secret;
        } catch (\Throwable $e) {
            return null;
        }
    }

    /**
     * Validates one publisher control secret.
     * @return Whether the secret is canonical.
     */
    private function isControlSecret(string $secret): bool
    {
        return strlen($secret) === Redp2pIndex::KEY_SZ
            && preg_match('/\A[0-9A-Fa-f]{16}\z/D', $secret) === 1;
    }

    /**
     * Builds the native registration transcript.
     * @return Canonical registration bytes.
     */
    private function registrationMessage(
        Redp2pIndex $index,
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $id,
        string $secret,
        int $proto,
        int $port,
        array $candidates,
        string $solution
    ): string {
        $message = 'REDP2P-REGISTER' . $nonce . $index->u64be($issuedAt)
            . $index->u64be($expiresAt) . pack('n', strlen($id)) . $id
            . pack('n', strlen($secret)) . $secret
            . ($proto === 2 ? "\x01" : "\x02")
            . pack('nC', $port, count($candidates));
        foreach ($candidates as $candidate) {
            $address = inet_pton($candidate['addr']);
            $type = match ($candidate['type']) {
                'host' => 1,
                'observed' => 2,
                'relay' => 3,
                default => 4,
            };
            $message .= chr($type) . chr(strlen($address) === 4 ? 4 : 6)
                . $address . pack('n', $candidate['port']);
        }
        return $message . $solution;
    }

    /**
     * Builds one native control proof.
     * @return Lowercase hexadecimal proof.
     */
    private function controlProof(
        string $secret,
        string $op,
        string $id,
        int $sequence,
        int $proto = 0,
        int $udpPort = 0,
        array $candidates = []
    ): string {
        $message = $op . "\n" . $id . "\n" . $sequence;
        if ($op === 'heartbeat') {
            $message .= "\n" . $proto . "\n" . $udpPort . "\n" . count($candidates);
            foreach ($candidates as $candidate) {
                $message .= "\n" . $candidate['type'] . "\n" . $candidate['addr']
                    . "\n" . $candidate['port'];
            }
        }
        return hash_hmac('sha256', $message, $secret);
    }

    /**
     * Validates one native session token.
     * @return Whether the token is canonical.
     */
    private function isSessionToken(string $token): bool
    {
        return strlen($token) >= 1 && strlen($token) <= 63
            && preg_match('/^[A-Za-z0-9._~-]+$/D', $token) === 1;
    }
}

/**
 * Summary: REDP2P WebRTC transport module
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
final class Redp2pWebRtc
{
    private const MAX_SDP = 49152;

    /**
     * Returns the RTC transport key.
     * @return RTC transport name.
     */
    public function key(): string
    {
        return 'rtc';
    }

    /**
     * Reports whether a publisher transport is RTC.
     * @return Whether the transport is accepted.
     */
    public function acceptsPublisherTransport(string $transport): bool
    {
        return $transport === 'rtc';
    }

    /**
     * Reports whether an operation belongs to RTC signaling.
     * @return Whether the operation is handled.
     */
    public function handles(string $operation): bool
    {
        return in_array($operation, ['connect', 'poll', 'answer'], true);
    }

    /**
     * Registers one RTC publisher.
     * @return Index response payload.
     */
    public function register(Redp2pIndex $index, \stdClass $request, string $id): array
    {
        if (!property_exists($request, 'transport') || $request->transport !== 'rtc') {
            return $index->jsonError(409, 'unsupported_transport');
        }
        $nonce = $index->requireHex($request, 'nonce', 64);
        $mac = $index->requireHex($request, 'mac', 64);
        $solution = $index->requireLowerHex($request, 'pow_solution', 16);
        $secret = $index->requireLowerHex($request, 'secret', 16);
        $proof = $index->requireLowerHex($request, 'proof', 64);
        $access = $index->requireLowerHex($request, 'access_proof', 64);
        if (property_exists($request, 'access_proof') && $access === null) {
            return $index->jsonError(400, 'bad_request');
        }
        $issuedAt = $index->requireTimestamp($request, 'issued_at');
        $expiresAt = $index->requireTimestamp($request, 'expires_at');
        if ($nonce === null || $mac === null || $solution === null ||
            $secret === null || $proof === null || $issuedAt === null ||
            $expiresAt === null) {
            return $index->jsonError(400, 'bad_request');
        }
        $nonceRaw = $index->hexBytes($nonce, 32);
        $macRaw = $index->hexBytes($mac, 32);
        $solutionRaw = $index->hexBytes($solution, 8);
        if ($nonceRaw === null || $macRaw === null || $solutionRaw === null) {
            return $index->jsonError(400, 'bad_request');
        }
        if (!$index->verifyChallenge($nonceRaw, $issuedAt, $expiresAt, $macRaw) ||
            !$index->verifyPow($nonceRaw, $issuedAt, $expiresAt, $id, $solutionRaw)) {
            return $index->jsonError(403, 'auth_failed');
        }
        $message = $this->registrationMessage(
            $index,
            $nonceRaw,
            $issuedAt,
            $expiresAt,
            $id,
            $secret,
            $solutionRaw
        );
        $expected = hash_hmac('sha256', $message, $secret);
        if (!hash_equals($expected, $proof)) {
            return $index->jsonError(403, 'auth_failed');
        }
        return $index->commitPublisherRegistration(
            $id,
            $secret,
            'rtc',
            [],
            $message,
            $access === null ? null : hex2bin($access)
        );
    }

    /**
     * Refreshes one RTC publisher record.
     * @return Index response payload.
     */
    public function heartbeat(
        Redp2pIndex $index,
        \stdClass $request,
        string $id,
        array $publisher
    ): array {
        $seq = $index->requireSequence($request);
        $proof = $index->requireHex($request, 'proof', 64);
        if ($seq === null || $proof === null) return $index->jsonError(400, 'bad_request');
        if ($seq <= (int)$publisher['seq']) return $index->jsonError(403, 'invalid_proof');
        $expected = $index->simpleControlProof(
            (string)$publisher['key'],
            'heartbeat',
            $id,
            $seq
        );
        if (!hash_equals($expected, strtolower($proof))) {
            return $index->jsonError(403, 'invalid_proof');
        }
        return $index->touchPublisher($id, $seq)
            ? $index->jsonOk()
            : $index->jsonError(403, 'invalid_proof');
    }

    /**
     * Provides RTC lookup metadata.
     * @return RTC lookup fields.
     */
    public function lookup(Redp2pIndex $index, array $publisher): array
    {
        return [];
    }

    /**
     * Dispatches one RTC signaling operation.
     * @return Index response payload.
     */
    public function handle(
        Redp2pIndex $index,
        string $operation,
        \stdClass $request,
        ?string $peerAddress
    ): array {
        return match ($operation) {
            'connect' => $this->handleConnect($index, $request, $peerAddress),
            'poll' => property_exists($request, 'id')
                ? $this->handlePublisherPoll($index, $request)
                : $this->handleConsumerPoll($index, $request),
            'answer' => $this->handleAnswer($index, $request),
            default => $index->jsonError(400, 'bad_request'),
        };
    }

    /**
     * Queues one RTC offer.
     * @return Index response payload.
     */
    private function handleConnect(
        Redp2pIndex $index,
        \stdClass $request,
        ?string $peerAddress
    ): array {
        [$result, $id] = $index->requireId($request, 'id');
        if ($result === 0) return $index->jsonError(400, 'bad_request');
        if ($result < 0) return $index->jsonError(400, 'invalid_id');
        $offer = property_exists($request, 'offer')
            ? $this->description($request->offer, 'offer') : null;
        if ($offer === null || $peerAddress === null ||
            !filter_var($peerAddress, FILTER_VALIDATE_IP)) {
            return $index->jsonError(400, 'bad_request');
        }

        $lock = $index->beginPendingWrite();
        $active = true;
        try {
            $publisher = $index->publisher($id, true);
            if ($publisher === null) {
                $index->endPendingWrite($lock, true);
                $active = false;
                return $index->jsonError(404, 'not_found');
            }
            if (!$this->acceptsPublisherTransport((string)$publisher['transport'])) {
                $index->endPendingWrite($lock, false);
                $active = false;
                return $index->jsonError(409, 'unsupported_transport');
            }
            if (!$index->allowConnectRate($peerAddress, $id, $publisher)) {
                $index->endPendingWrite($lock, true);
                $active = false;
                return $index->jsonError(429, 'rate_limited');
            }
            $capacityError = $index->pendingCapacityAvailable($id);
            if ($capacityError !== null) {
                $index->endPendingWrite($lock, true);
                $active = false;
                return $capacityError;
            }
            $connection = bin2hex(random_bytes(16));
            $capability = bin2hex(random_bytes(32));
            $index->pendingCreate(
                $connection,
                $id,
                'rtc',
                ['offer' => $offer],
                hash('sha256', $capability)
            );
            $index->endPendingWrite($lock, true);
            $active = false;
            return $index->jsonOk([
                'connection' => $connection,
                'capability' => $capability,
                'expires_at' => time() + Redp2pIndex::PENDING_TTL_S,
            ]);
        } catch (\Throwable $e) {
            if ($active) $index->endPendingWrite($lock, false);
            throw $e;
        }
    }

    /**
     * Returns pending offers to an RTC publisher.
     * @return Index response payload.
     */
    private function handlePublisherPoll(
        Redp2pIndex $index,
        \stdClass $request
    ): array {
        [$result, $id] = $index->requireId($request, 'id');
        if ($result === 0) return $index->jsonError(400, 'bad_request');
        if ($result < 0) return $index->jsonError(400, 'invalid_id');
        $seq = $index->requireSequence($request);
        $proof = $index->requireHex($request, 'proof', 64);
        if ($seq === null || $proof === null) return $index->jsonError(400, 'bad_request');
        $publisher = $index->publisher($id);
        if ($publisher === null) return $index->jsonError(404, 'not_found');
        if (!$this->acceptsPublisherTransport((string)$publisher['transport'])) {
            return $index->jsonError(409, 'unsupported_transport');
        }
        $expected = $index->simpleControlProof(
            (string)$publisher['key'],
            'poll',
            $id,
            $seq
        );
        if ($seq <= (int)$publisher['seq'] ||
            !hash_equals($expected, strtolower($proof)))
        {
            return $index->jsonError(403, 'invalid_proof');
        }
        if (!$index->touchPublisher($id, $seq)) {
            return $index->jsonError(403, 'invalid_proof');
        }
        $rows = $index->pendingForPublisher($id, 'rtc', true);
        $connections = [];
        foreach ($rows as $row) {
            $connections[] = [
                'connection' => (string)$row['id'],
                'offer' => $row['request']['offer'] ?? null,
            ];
        }
        return $index->jsonOk(['connections' => $connections], false);
    }

    /**
     * Stores one RTC publisher answer.
     * @return Index response payload.
     */
    private function handleAnswer(Redp2pIndex $index, \stdClass $request): array
    {
        [$result, $id] = $index->requireId($request, 'id');
        if ($result === 0) return $index->jsonError(400, 'bad_request');
        if ($result < 0) return $index->jsonError(400, 'invalid_id');
        $seq = $index->requireSequence($request);
        $proof = $index->requireHex($request, 'proof', 64);
        $connection = property_exists($request, 'connection')
            ? $index->requireLowerHex($request, 'connection', 32) : null;
        $answer = property_exists($request, 'answer')
            ? $this->description($request->answer, 'answer') : null;
        if ($seq === null || $proof === null || $connection === null || $answer === null) {
            return $index->jsonError(400, 'bad_request');
        }
        $publisher = $index->publisher($id);
        if ($publisher === null) return $index->jsonError(404, 'not_found');
        if (!$this->acceptsPublisherTransport((string)$publisher['transport'])) {
            return $index->jsonError(409, 'unsupported_transport');
        }
        $digest = hash('sha256', $answer['type'] . "\n" . $answer['sdp']);
        $expected = $index->simpleControlProof(
            (string)$publisher['key'],
            'answer',
            $id,
            $seq,
            [$connection, $digest]
        );
        if ($seq <= (int)$publisher['seq'] ||
            !hash_equals($expected, strtolower($proof)))
        {
            return $index->jsonError(403, 'invalid_proof');
        }
        $lock = $index->beginPendingWrite();
        $active = true;
        try {
            if (!$index->pendingRespond($connection, $id, 'rtc', ['answer' => $answer])) {
                $index->endPendingWrite($lock, false);
                $active = false;
                return $index->jsonError(404, 'not_found');
            }
            if (!$index->touchPublisher($id, $seq)) {
                $index->endPendingWrite($lock, false);
                $active = false;
                return $index->jsonError(403, 'invalid_proof');
            }
            $index->endPendingWrite($lock, true);
            $active = false;
            return $index->jsonOk();
        } catch (\Throwable $e) {
            if ($active) $index->endPendingWrite($lock, false);
            throw $e;
        }
    }

    /**
     * Returns an RTC answer to its consumer.
     * @return Index response payload.
     */
    private function handleConsumerPoll(
        Redp2pIndex $index,
        \stdClass $request
    ): array {
        $connection = property_exists($request, 'connection')
            ? $index->requireLowerHex($request, 'connection', 32) : null;
        $capability = property_exists($request, 'capability')
            ? $index->requireLowerHex($request, 'capability', 64) : null;
        if ($connection === null || $capability === null) {
            return $index->jsonError(400, 'bad_request');
        }
        $pending = $index->pending($connection);
        if ($pending === null || $pending['transport'] !== 'rtc') {
            return $index->jsonError(404, 'not_found');
        }
        $capabilityHash = hash('sha256', $capability);
        if (!is_string($pending['consumer_hash']) ||
            !hash_equals($pending['consumer_hash'], $capabilityHash))
        {
            return $index->jsonError(403, 'auth_failed');
        }
        $answer = $pending['response']['answer'] ?? null;
        if ($answer !== null) $index->pendingDelete($connection);
        return $index->jsonOk(['answer' => $answer], false);
    }

    /**
     * Validates one RTC session description.
     * @return Description or null when invalid.
     */
    private function description(mixed $value, string $type): ?array
    {
        if (!($value instanceof \stdClass) || !property_exists($value, 'type') ||
            !property_exists($value, 'sdp') || $value->type !== $type ||
            !is_string($value->sdp) || $value->sdp === '' ||
            strlen($value->sdp) > self::MAX_SDP) {
            return null;
        }
        return ['type' => $type, 'sdp' => $value->sdp];
    }

    /**
     * Builds the RTC registration transcript.
     * @return Canonical registration bytes.
     */
    private function registrationMessage(
        Redp2pIndex $index,
        string $nonce,
        int $issuedAt,
        int $expiresAt,
        string $id,
        string $secret,
        string $solution
    ): string {
        return 'REDP2P-WEB-REGISTER' . $nonce . $index->u64be($issuedAt)
            . $index->u64be($expiresAt) . pack('n', strlen($id)) . $id
            . pack('n', strlen($secret)) . $secret . $solution;
    }
}
