<?php
/**
 * Summary: REDP2P index protocol server (PHP)
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

/**
 * Class Redp2pIndex
 *
 * Server-side implementation of the REDP2P index protocol over HTTP/JSON.
 *
 * Faithful to the C reference behavior in libredp2p.c: same HTTP status codes,
 * same JSON error and success payloads, same proof-of-work construction, same
 * candidate rules, same seat/VIP semantics, and the same pending punch queue
 * limits. Index state is stored in any PDO database (SQLite locally, MySQL on
 * shared hosting) using only portable SQL.
 *
 * The index coordinates peers and carries no application traffic. Publisher
 * records expire by TTL. Publisher-generated session secrets are encrypted at
 * registration; heartbeat, punch_poll, and deregister use sequenced HMAC
 * proofs.
 *
 * Design constraints:
 * - PHP core, PDO, and ext-sodium only.
 * - Portable SQL: no AUTO_INCREMENT, no driver-specific syntax.
 * - Configurable by constructor options, then REDP2P_* environment variables,
 *   then protocol defaults that match the C implementation.
 * - Explicit validation mirrors the C limits (id 63, candidates 8, body 4096).
 *
 * Usage:
 * Construct with a PDO to the index database, then pass each HTTP request to
 * the handle method.
 */
class Redp2pIndex
{
    public const ID_MAX = 63;
    public const SESSION_MAX = 63;
    public const ADDR_MAX = 47;
    public const KEY_SZ = 16;
    public const PASS_MAX = 255;
    public const CANDIDATES_MAX = 8;
    public const PUNCH_POLL_MAX = 4;
    public const MAX_PENDING_CALLS_GLOBAL = 4096;
    public const MAX_PENDING_CALLS_PER_PUBLISHER = 32;
    public const MAX_CONSUMERS_PER_PUBLISHER = self::MAX_PENDING_CALLS_PER_PUBLISHER;
    public const PENDING_TTL_S = 30;
    public const BODY_MAX = 4096;
    public const DEFAULT_TTL_S = 120;

    public const CANDIDATE_TYPES = ['host', 'observed'];

    private const RATE_SOURCES_MAX = 4096;
    private const RATE_SOURCE_IDLE_MS = 120000;

    private const ERROR_MALFORMED = 'REDP2P_CTRTOK_ERROR:malformed';

    private PDO $db;
    private string $pass = '';
    private array $vips = [];
    private ?int $seats = null;
    private int $pow = 0;
    private int $ttl = self::DEFAULT_TTL_S;
    private int $pendingTtlS = self::PENDING_TTL_S;
    private int $maxConsumers = self::MAX_CONSUMERS_PER_PUBLISHER;
    private string $challengeKey;

    /**
     * Initializes the index with a database connection and configuration.
     *
     * @param PDO $db Open database connection (SQLite or MySQL).
     * @param array<string, mixed> $options Optional index options.
     * @return void
     */
    public function __construct(PDO $db, array $options = [])
    {
        $this->db = $db;
        $this->applyOptions($options);
        $this->ensureSchema();
        $this->challengeKey = $this->loadChallengeKey();
    }

    /**
     * Processes one HTTP request and returns the full HTTP response.
     *
     * @param string $method HTTP method.
     * @param string $body Raw JSON request body.
     * @param array<string, mixed> $headers Request headers.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    public function handle(string $method, string $body, array $headers = [],
        ?string $peerAddress = null): array
    {
        if ($method !== 'POST') {
            return $this->httpError(405, 'Method Not Allowed');
        }
        if ($this->hasHeader($headers, 'Transfer-Encoding')) {
            return $this->httpError(501, 'Not Implemented');
        }
        if (strlen($body) > self::BODY_MAX) {
            return $this->httpError(413, 'Payload Too Large');
        }

        $req = json_decode($body);
        if (!($req instanceof \stdClass)) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($this->hasDuplicateTopKeys($body)) {
            return $this->jsonError(400, 'bad_request');
        }
        if (!$this->hasString($req, 'op')) {
            return $this->jsonError(400, 'bad_request');
        }

        try {
            switch ($req->op) {
                case 'challenge':
                    return $this->handleChallenge($req);
                case 'register':
                    return $this->handleRegister($req);
                case 'heartbeat':
                    return $this->handleHeartbeat($req);
                case 'lookup':
                    return $this->handleLookup($req);
                case 'list':
                    return $this->handleList();
                case 'deregister':
                    return $this->handleDeregister($req);
                case 'punch_req':
                    return $this->handlePunchReq($req, $peerAddress);
                case 'punch_poll':
                    return $this->handlePunchPoll($req);
                default:
                    return $this->jsonError(400, 'bad_request');
            }
        } catch (\PDOException $e) {
            return $this->jsonError(500, 'internal');
        }
    }

    /**
     * Serves one request from the web server globals and emits the response.
     *
     * Standalone front-controller helper: reads the request method, raw body,
     * and headers from the current request and prints the index response.
     *
     * @param array{
     *     dsn: string,
     *     user?: string,
     *     pass?: string
     * } $db Database connection settings.
     * @param array<string, mixed> $options Optional index options.
     * @return void
     */
    public static function serve(array $db, array $options = []): void
    {
        $pdo = new PDO($db['dsn'], $db['user'] ?? null, $db['pass'] ?? null);
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $index = new self($pdo, $options);

        $headers = function_exists('getallheaders') ? getallheaders() : [];
        $res = $index->handle(
            $_SERVER['REQUEST_METHOD'],
            file_get_contents('php://input'),
            $headers,
            $_SERVER['REMOTE_ADDR'] ?? null
        );

        header(sprintf('HTTP/1.1 %d %s', $res['status'], $res['reason']), true, $res['status']);
        foreach ($res['headers'] as $name => $value) {
            header("$name: $value");
        }
        echo $res['body'];
    }

    /**
     * Removes expired publisher records and pending punch calls.
     *
     * Called externally on a schedule by the deployment operator, for example
     * from a cron job. Request handling also removes expired records lazily;
     * this method is the standalone cleanup entry point.
     *
     * @return void
     */
    public function prune(): void
    {
        $this->evictStale();
        $this->evictPending();
    }

    /**
     * Sets the shared registration password.
     *
     * @param string $pass Shared password, or an empty string for none.
     * @return void
     */
    public function setPass(string $pass): void
    {
        if ($pass !== '' && !$this->isValidPassToken($pass)) {
            throw new \InvalidArgumentException('REDP2P_PASS contains invalid bytes');
        }
        $this->pass = $pass;
    }

    /**
     * Sets reserved VIP seats as identifier/password pairs.
     *
     * @param array<int|string, mixed>|string|null $vips VIP map, list of
     *   id/pass pairs, or the C-style whitespace-separated "<id> <pass> ..."
     *   string.
     * @return void
     */
    public function setVips($vips): void
    {
        $map = [];
        if (is_string($vips)) {
            $tokens = preg_split('/\s+/', trim($vips), -1, PREG_SPLIT_NO_EMPTY);
            $tokens = $tokens === false ? [] : $tokens;
            if (count($tokens) % 2 !== 0) {
                throw new \InvalidArgumentException('REDP2P_VIP has odd token count');
            }
            for ($i = 0; $i < count($tokens); $i += 2) {
                if (isset($map[$tokens[$i]])) {
                    throw new \InvalidArgumentException("REDP2P_VIP redefines reserved id '{$tokens[$i]}'");
                }
                $map[$tokens[$i]] = $tokens[$i + 1];
            }
        } elseif (is_array($vips)) {
            foreach ($vips as $key => $value) {
                if (is_int($key)) {
                    if (!is_array($value) || !isset($value['id'], $value['pass'])) {
                        throw new \InvalidArgumentException('REDP2P_VIP pair must provide id and pass');
                    }
                    $map[(string)$value['id']] = (string)$value['pass'];
                } else {
                    $map[(string)$key] = (string)$value;
                }
            }
        } elseif ($vips !== null) {
            throw new \InvalidArgumentException('REDP2P_VIP must be a string or an array');
        }
        foreach ($map as $id => $pass) {
            if (!$this->isValidId($id)) {
                throw new \InvalidArgumentException("REDP2P_VIP invalid id '$id'");
            }
            if (!$this->isValidPassToken($pass)) {
                throw new \InvalidArgumentException("REDP2P_VIP invalid password for id '$id'");
            }
        }
        $this->vips = $map;
    }

    /**
     * Sets the configured publisher seat capacity.
     *
     * @param int $seats Total publisher capacity. Zero rejects all publishers
     *   when explicitly configured; leaving seats unset means no limit.
     * @return void
     */
    public function setSeats(int $seats): void
    {
        if ($seats < 0) {
            throw new \InvalidArgumentException('REDP2P_SEATS must be zero or positive');
        }
        $this->seats = $seats;
    }

    /**
     * Sets the per-publisher consumer safety window.
     *
     * This is a bound against pathological or malicious punch_req accumulation,
     * not a normal traffic-shaping limit. Zero restores the default of 32.
     *
     * @param int $max Consumers waiting for one publisher; 0 restores default.
     * @return void
     */
    public function setMaxConsumersPerPublisher(int $max): void
    {
        if ($max < 0) {
            throw new \InvalidArgumentException(
                'REDP2P_MAX_CONSUMERS_PER_PUBLISHER must be zero or positive');
        }
        $this->maxConsumers = $max === 0
            ? self::MAX_PENDING_CALLS_PER_PUBLISHER : $max;
    }

    /**
     * Sets the proof-of-work difficulty in leading zero bits.
     *
     * @param int $bits Difficulty target, 0..32.
     * @return void
     */
    public function setPow(int $bits): void
    {
        if ($bits < 0 || $bits > 32) {
            throw new \InvalidArgumentException('REDP2P_POW must be between 0 and 32');
        }
        $this->pow = $bits;
    }

    /**
     * Sets the pending punch call TTL in seconds.
     *
     * @param int $ttl TTL seconds, 1..86400.
     * @return void
     */
    public function setPendingTtl(int $ttl): void
    {
        if ($ttl < 1 || $ttl > 86400) {
            throw new \InvalidArgumentException('REDP2P_PENDING_CALL_TTL_S must be between 1 and 86400');
        }
        $this->pendingTtlS = $ttl;
    }

    /**
     * Sets the publisher eviction TTL in seconds.
     *
     * @param int $ttl TTL seconds, 1..86400.
     * @return void
     */
    public function setTtl(int $ttl): void
    {
        if ($ttl < 1 || $ttl > 86400) {
            throw new \InvalidArgumentException('REDP2P_ETIMEOUT_SEC must be between 1 and 86400');
        }
        $this->ttl = $ttl;
    }

    /**
     * Applies constructor options with environment fallback.
     *
     * @param array $options Option overrides for pass, vip, seats, pow, ttl
     *   and pending_ttl_s.
     *   Falls back to REDP2P_* environment variables.
     * @return void
     */
    private function applyOptions(array $options): void
    {
        $pass = $options['pass'] ?? $this->env('REDP2P_PASS') ?? '';
        $vip = $options['vip'] ?? $this->env('REDP2P_VIP');
        $seats = $options['seats'] ?? $this->env('REDP2P_SEATS');
        $pow = $options['pow'] ?? $this->env('REDP2P_POW') ?? 0;
        $ttl = $options['ttl'] ?? $this->env('REDP2P_ETIMEOUT_SEC') ?? self::DEFAULT_TTL_S;
        $pendingTtl = $options['pending_ttl_s']
            ?? $this->env('REDP2P_PENDING_CALL_TTL_S')
            ?? self::PENDING_TTL_S;
        $maxConsumers = $options['max_consumers_per_publisher']
            ?? $this->env('REDP2P_MAX_CONSUMERS_PER_PUBLISHER');

        $this->setPass((string)$pass);
        if ($vip !== null) {
            $this->setVips($vip);
        }
        if ($seats !== null) {
            $this->setSeats((int)$seats);
        }
        if ($maxConsumers !== null) {
            $this->setMaxConsumersPerPublisher((int)$maxConsumers);
        }
        $this->setPow((int)$pow);
        $this->setTtl((int)$ttl);
        $this->setPendingTtl((int)$pendingTtl);
    }

    /**
     * Reads an environment variable.
     *
     * @param string $name Variable name.
     * @return string|null Value, or null when unset.
     */
    private function env(string $name): ?string
    {
        $value = getenv($name);
        return $value === false ? null : $value;
    }

    /**
     * Handles the challenge operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleChallenge(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $nonce = random_bytes(32);
        $issuedAt = time();
        $expiresAt = $issuedAt + 60;
        $mac = hash_hmac('sha256', $this->challengeMacInput($nonce, $issuedAt,
            $expiresAt), $this->challengeKey, true);
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
     * Handles the register operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleRegister(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $nonce = $this->requireHex($req, 'nonce', 64);
        $mac = $this->requireHex($req, 'mac', 64);
        $solution = $this->requireHex($req, 'pow_solution', 16);
        $proof = $this->requireHex($req, 'proof', 64);
        $access = $this->requireLowerHex($req, 'access_proof', 64);
        if (property_exists($req, 'access_proof') && $access === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $pkey = $this->requireHex($req, 'pkey', 64);
        $encryptedSecret = $this->requireHex($req, 'secret', 64);
        $issuedAt = $this->requireTimestamp($req, 'issued_at');
        $expiresAt = $this->requireTimestamp($req, 'expires_at');
        if ($nonce === null || $mac === null || $solution === null || $proof === null ||
            $pkey === null || $encryptedSecret === null || $issuedAt === null ||
            $expiresAt === null) {
            return $this->jsonError(400, 'bad_request');
        }
        [$proto, $udpPort] = $this->requireProtoPort($req);
        if ($proto === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $nonceRaw = $this->hexBytes($nonce, 32);
        $macRaw = $this->hexBytes($mac, 32);
        $proofRaw = $this->hexBytes($proof, 32);
        $solutionRaw = $this->hexBytes($solution, 8);
        if ($nonceRaw === null || $macRaw === null || $proofRaw === null ||
            $solutionRaw === null) {
            return $this->jsonError(400, 'bad_request');
        }
        if (!$this->verifyChallenge($nonceRaw, $issuedAt, $expiresAt, $macRaw)) {
            return $this->jsonError(403, 'auth_failed');
        }
        [$candOk, $candidates] = $this->parseCandidates($req, 'candidates');
        if (!$candOk) {
            return $this->jsonError(400, 'bad_request');
        }
        if (!$this->verifyPow($nonceRaw, $issuedAt, $expiresAt, $id,
            $solutionRaw)) {
            return $this->jsonError(403, 'auth_failed');
        }
        $publisherPublic = $this->hexBytes($pkey, 32);
        $wireSecret = $this->hexBytes($encryptedSecret, 32);
        if ($publisherPublic === null || $wireSecret === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $secret = $this->decryptRegistrationSecret($nonceRaw, $issuedAt,
            $expiresAt, $publisherPublic, $wireSecret);
        if ($secret === null || !$this->isControlSecret($secret)) {
            return $this->jsonError(403, 'auth_failed');
        }
        $message = $this->registrationMessage($nonceRaw,
            $issuedAt, $expiresAt, $id, $secret, $proto, $udpPort, $candidates,
            $solutionRaw);
        $expected = hash_hmac('sha256', $message, $secret, true);
        if (!hash_equals($expected, $proofRaw)) {
            return $this->jsonError(403, 'auth_failed');
        }

        $password = $this->getPass($id);
        if ($password !== '' && ($access === null || !hash_equals(
            $this->admissionProof($password, $message), hex2bin($access)))) {
            return $this->jsonError(403, 'auth_failed');
        }
        unset($password, $message, $expected);
        $this->evictStale();
        $added = $this->addPublisher($id, $secret, $proto, $udpPort, $candidates);
        if ($added === 'exists') {
            return $this->jsonError(409, 'already_registered');
        }
        if ($added === 'full') {
            return $this->jsonError(503, 'table_full');
        }
        if ($added !== 'ok') {
            return $this->jsonError(500, 'internal');
        }
        return $this->jsonOk();
    }

    /**
     * Handles the heartbeat operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleHeartbeat(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $seq = $this->requireSequence($req);
        $proof = $this->requireHex($req, 'proof', 64);
        if ($seq === null || $proof === null) {
            return $this->jsonError(400, 'bad_request');
        }
        [$proto, $udpPort] = $this->requireProtoPort($req);
        if ($proto === null) {
            return $this->jsonError(400, 'bad_request');
        }
        [$candOk, $candidates] = $this->parseCandidates($req, 'candidates');
        if (!$candOk) {
            return $this->jsonError(400, 'bad_request');
        }

        $this->evictStale();
        $stored = $this->findPublisherSecret($id);
        if ($stored === null) {
            return $this->jsonError(404, 'not_found');
        }
        if ($seq <= (int)$stored['seq'] || !hash_equals(
            $this->controlProof((string)$stored['key'], 'heartbeat', $id,
                $seq, $proto, $udpPort, $candidates), $proof)) {
            return $this->jsonError(403, 'invalid_proof');
        }
        if (!$this->updatePublisherIfSequence($id, $proto, $udpPort,
            $candidates, $seq)) {
            return $this->jsonError(403, 'invalid_proof');
        }
        return $this->jsonOk();
    }

    /**
     * Handles the lookup operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleLookup(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $row = $this->findPublisher($id);
        if ($row === null) {
            return $this->jsonError(404, 'not_found');
        }
        if ((int)$row['last_seen'] < time() - $this->ttl) {
            return $this->jsonError(404, 'not_found');
        }
        return $this->jsonOk([
            'id' => $id,
            'proto' => (int)$row['proto'],
            'udp_port' => (int)$row['udp_port'],
            'candidates' => $this->decodeCandidates((string)$row['candidates']),
            'last_seen' => (int)$row['last_seen'],
        ]);
    }

    /**
     * Handles the list operation.
     *
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleList(): array
    {
        $cutoff = time() - $this->ttl;
        $st = $this->db->prepare('SELECT id FROM redp2p_publishers WHERE last_seen >= ?');
        $st->execute([$cutoff]);
        $ids = $st->fetchAll(PDO::FETCH_COLUMN);
        return $this->jsonOk(['ids' => array_map('strval', $ids)], false);
    }

    /**
     * Handles the deregister operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handleDeregister(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $seq = $this->requireSequence($req);
        $proof = $this->requireHex($req, 'proof', 64);
        if ($seq === null || $proof === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $this->evictStale();
        $stored = $this->findPublisherSecret($id);
        if ($stored === null || $seq <= (int)$stored['seq'] || !hash_equals(
            $this->controlProof((string)$stored['key'], 'deregister', $id,
                $seq), $proof)) {
            return $this->jsonError(403, 'invalid_proof');
        }
        $st = $this->db->prepare(
            'DELETE FROM redp2p_publishers WHERE id = ? AND seq < ?'
        );
        $st->execute([$id, $seq]);
        if ($st->rowCount() !== 1) {
            return $this->jsonError(403, 'invalid_proof');
        }
        return $this->jsonOk();
    }

    /**
     * Merges the server-derived observed candidate into a punch request.
     *
     * The observed endpoint is the trusted transport source address of the
     * request joined with the declared udp_port; it is authoritative over a
     * host candidate that names the same endpoint and otherwise replaces the
     * lowest-priority host candidate in a full list.
     *
     * @param array<int,array{type:string,addr:string,port:int}> $candidates
     * @param string|null $peerAddress Trusted peer transport address.
     * @param int $udpPort Punched source port.
     * @return array|null Updated candidate list, or null when storage fails.
     */
    private function mergeObserved(
        array $candidates, ?string $peerAddress, int $udpPort): ?array
    {
        if ($peerAddress === null || $peerAddress === '' ||
            !filter_var($peerAddress, FILTER_VALIDATE_IP)) {
            return $candidates;
        }
        $raw = @inet_pton($peerAddress);
        if ($raw === false || (strlen($raw) !== 4 && strlen($raw) !== 16)) {
            return $candidates;
        }
        $addr = inet_ntop($raw);
        $observed = ['type' => 'observed', 'addr' => $addr, 'port' => $udpPort];
        $matched = false;
        foreach ($candidates as $i => $c) {
            if ($c['type'] === 'host' && $c['port'] === $udpPort) {
                $cRaw = @inet_pton($c['addr']);
                if ($cRaw !== false && $cRaw === $raw) {
                    $candidates[$i] = $observed;
                    $matched = true;
                    break;
                }
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
                if (count($candidates) >= self::CANDIDATES_MAX) {
                    return null;
                }
            }
            $candidates[] = $observed;
        }
        return $this->normalizeCandidates($candidates);
    }

    /**
     * Handles the punch_req operation.
     *
     * @param \stdClass $req Parsed request object.
     * @param string|null $peerAddress Trusted transport source address.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handlePunchReq(\stdClass $req, ?string $peerAddress = null): array
    {
        [$selfResult, $selfId] = $this->requireId($req, 'self_id');
        [$targetResult, $targetId] = $this->requireId($req, 'target_id');
        if ($selfResult === 0 || $targetResult === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($selfResult < 0 || $targetResult < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $lock = $this->beginPendingWrite();
        $lockActive = true;
        try {
            $targetRow = $this->findPublisher($targetId, true);
            if ($targetRow === null ||
                (int)$targetRow['last_seen'] < time() - $this->ttl) {
                if ($targetRow !== null) {
                    $remove = $this->db->prepare(
                        'DELETE FROM redp2p_publishers WHERE id = ?'
                    );
                    $remove->execute([$targetId]);
                }
                $this->endPendingWrite($lock, true);
                $lockActive = false;
                return $this->jsonError(404, 'not_found');
            }
            $session = $this->getString($req, 'session');
            if ($session === null || !$this->isSessionToken($session)) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }
            if (!property_exists($req, 'udp_port')) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }
            $udpPort = $this->requirePort($req->udp_port);
            if ($udpPort === null) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }
            if ($req->op === 'punch_req' && ($peerAddress === null ||
                    !filter_var($peerAddress, FILTER_VALIDATE_IP))) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }
            [$candOk, $candidates] = $this->parseCandidates($req, 'candidates');
            if (!$candOk) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }
            $candidates = $this->mergeObserved($candidates, $peerAddress, $udpPort);
            if ($candidates === null) {
                $this->endPendingWrite($lock, false);
                $lockActive = false;
                return $this->jsonError(400, 'bad_request');
            }

            if (!$this->allowPunchRate($peerAddress, $targetId, $targetRow)) {
                $this->endPendingWrite($lock, true);
                $lockActive = false;
                return $this->jsonError(429, 'rate_limited');
            }
            $this->evictPending();
            $target = $this->db->prepare(
                'SELECT COUNT(*) FROM redp2p_pending_calls WHERE target_id = ?'
            );
            $target->execute([$targetId]);
            if ((int)$target->fetchColumn() >= $this->maxConsumers) {
                $this->endPendingWrite($lock, true);
                $lockActive = false;
                return $this->jsonError(429, 'pending_limit_publisher');
            }
            $global = $this->db->query(
                'SELECT COUNT(*) FROM redp2p_pending_calls'
            );
            if ((int)$global->fetchColumn() >= self::MAX_PENDING_CALLS_GLOBAL) {
                $this->endPendingWrite($lock, true);
                $lockActive = false;
                return $this->jsonError(429, 'pending_limit_global');
            }
            $ins = $this->db->prepare(
                'INSERT INTO redp2p_pending_calls (id, self_id, target_id, session, candidates, ts)'
                . ' VALUES (?, ?, ?, ?, ?, ?)'
            );
            $ins->execute([
                bin2hex(random_bytes(8)),
                $selfId,
                $targetId,
                $session,
                json_encode($candidates),
                time(),
            ]);
            $this->endPendingWrite($lock, true);
            $lockActive = false;
            return $this->jsonOk();
        } catch (\Throwable $e) {
            if ($lockActive) $this->endPendingWrite($lock, false);
            throw $e;
        }
    }

    /**
     * Refills bounded thousandth-token credit using persistent milliseconds.
     * @param int $credit Stored credit.
     * @param int $updated Last accounting time.
     * @param int $now Current accounting time.
     * @param int $capacity Maximum credit.
     * @param int $rate Credit per millisecond.
     * @return int Refilled credit.
     */
    private function refillCredit(int $credit, int $updated, int $now,
        int $capacity, int $rate): int
    {
        $credit = max(0, min($capacity, $credit));
        $elapsed = min(4000, max(0, $now - $updated));
        return min($capacity, $credit + $elapsed * $rate);
    }

    /**
     * Checks and debits both buckets under the pending-write transaction.
     * @param string $peerAddress Trusted HTTP source IP.
     * @param string $targetId Active publisher id.
     * @param array<string, mixed> $target Locked publisher record.
     * @return bool Whether both buckets permit this request.
     */
    private function allowPunchRate(string $peerAddress, string $targetId,
        array $target): bool
    {
        $raw = inet_pton($peerAddress);
        if (strlen($raw) === 16 && substr($raw, 0, 12) ===
            str_repeat("\x00", 10) . "\xff\xff") $raw = substr($raw, 12);
        $address = bin2hex($raw);
        $now = (int)floor(microtime(true) * 1000);
        $expired = $this->db->prepare(
            'DELETE FROM redp2p_rate_sources WHERE updated_ms <= ?'
        );
        $expired->execute([$now - self::RATE_SOURCE_IDLE_MS]);
        $get = $this->db->prepare(
            'SELECT credit, updated_ms FROM redp2p_rate_sources WHERE address = ?'
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
        $targetNow = max($now, (int)$target['punch_ms']);
        $sourceCredit = $this->refillCredit((int)$source['credit'],
            (int)$source['updated_ms'], $sourceNow, 20000, 5);
        $targetCredit = $this->refillCredit((int)$target['punch_credit'],
            (int)$target['punch_ms'], $targetNow, 40000, 10);
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
        if ($sourceCredit >= 1000 || $allowed) {
            $update = $this->db->prepare(
                'UPDATE redp2p_publishers SET punch_credit = ?, punch_ms = ?'
                . ' WHERE id = ?'
            );
            $update->execute([$targetCredit, $targetNow, $targetId]);
        }
        return $allowed;
    }

    /**
     * Starts a serialized pending-call write operation.
     *
     * @return string Lock release mode.
     */
    private function beginPendingWrite(): string
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
                    'SELECT value FROM redp2p_meta WHERE name = ? FOR UPDATE'
                );
                $lock->execute(['pending_write_lock']);
                if ($lock->fetchColumn() === false)
                    throw new \PDOException('pending write lock is missing');
                return 'mysql';
            } catch (\Throwable $e) {
                if ($this->db->inTransaction()) $this->db->rollBack();
                throw $e;
            }
        }
        throw new \PDOException('unsupported PDO driver');
    }

    /**
     * Completes a serialized pending-call write operation.
     *
     * @param string $lock Lock release mode.
     * @param bool $commit Whether the pending insert is accepted.
     * @return void
     */
    private function endPendingWrite(string $lock, bool $commit): void
    {
        if ($lock === 'sqlite') {
            $this->db->exec($commit ? 'COMMIT' : 'ROLLBACK');
            return;
        }
        if ($lock === 'mysql') {
            if ($commit) {
                $this->db->commit();
                return;
            }
            $this->db->rollBack();
            return;
        }
        throw new \PDOException('unsupported pending write lock');
    }

    /**
     * Handles the punch_poll operation.
     *
     * @param \stdClass $req Parsed request object.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function handlePunchPoll(\stdClass $req): array
    {
        [$result, $id] = $this->requireId($req, 'id');
        if ($result === 0) {
            return $this->jsonError(400, 'bad_request');
        }
        if ($result < 0) {
            return $this->jsonError(400, 'invalid_id');
        }
        $seq = $this->requireSequence($req);
        $proof = $this->requireHex($req, 'proof', 64);
        if ($seq === null || $proof === null) {
            return $this->jsonError(400, 'bad_request');
        }
        $this->evictStale();
        $stored = $this->findPublisherSecret($id);
        if ($stored === null) {
            return $this->jsonError(404, 'not_found');
        }
        if ($seq <= (int)$stored['seq'] || !hash_equals(
            $this->controlProof((string)$stored['key'], 'punch_poll', $id,
                $seq), $proof)) {
            return $this->jsonError(403, 'invalid_proof');
        }
        $up = $this->db->prepare(
            'UPDATE redp2p_publishers SET seq = ?, last_seen = ?'
            . ' WHERE id = ? AND seq < ?'
        );
        $up->execute([$seq, time(), $id, $seq]);
        if ($up->rowCount() !== 1) {
            return $this->jsonError(403, 'invalid_proof');
        }
        $this->evictPending();
        $st = $this->db->prepare(
            'SELECT id, self_id, session, candidates FROM redp2p_pending_calls WHERE target_id = ? LIMIT ' . self::PUNCH_POLL_MAX
        );
        $st->execute([$id]);
        $rows = $st->fetchAll(PDO::FETCH_ASSOC);
        $calls = [];
        $consumed = [];
        foreach ($rows as $row) {
            $consumed[] = (string)$row['id'];
            $calls[] = [
                'self_id' => (string)$row['self_id'],
                'session' => (string)$row['session'],
                'candidates' => $this->decodeCandidates((string)$row['candidates']),
            ];
        }
        $body = json_encode(['calls' => $calls, 'ok' => true]);
        if ($body === false) {
            return $this->jsonError(500, 'internal');
        }
        if ($consumed !== []) {
            $placeholders = implode(',', array_fill(0, count($consumed), '?'));
            $del = $this->db->prepare(
                'DELETE FROM redp2p_pending_calls WHERE id IN (' . $placeholders . ')'
            );
            $del->execute($consumed);
        }
        return $this->response(200, 'OK', 'application/json', $body);
    }

    /**
     * Adds a publisher seat when capacity allows.
     *
     * @param string $id Publisher id.
     * @param string $secret Publisher session secret.
     * @param int $proto Protocol version (1 or 2).
     * @param int $udpPort Publisher UDP port.
     * @param array<int, array<string, mixed>> $candidates
     *   Candidate endpoints.
     * @return string 'ok', 'exists', or 'full'.
     */
    private function addPublisher(string $id, string $secret, int $proto,
        int $udpPort, array $candidates): string
    {
        $now = time();
        $st = $this->db->prepare('SELECT 1 FROM redp2p_publishers WHERE id = ?');
        $st->execute([$id]);
        if ($st->fetch()) {
            return 'exists';
        }

        if ($this->seats !== null) {
            $st = $this->db->query('SELECT COUNT(*) FROM redp2p_publishers');
            if ((int)$st->fetchColumn() >= $this->seats) {
                return 'full';
            }
            if (!isset($this->vips[$id])) {
                $nonVip = $this->countNonVip();
                $cap = $this->seats - count($this->vips);
                if ($cap < 0) {
                    $cap = 0;
                }
                if ($nonVip >= $cap) {
                    return 'full';
                }
            }
        }

        $ins = $this->db->prepare(
            'INSERT INTO redp2p_publishers (id, key, seq, proto, udp_port, candidates, last_seen)'
            . ' VALUES (?, ?, 0, ?, ?, ?, ?)'
        );
        try {
            $ins->execute([$id, $secret, $proto, $udpPort,
                json_encode($candidates), $now]);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0]) ? (string)$e->errorInfo[0] :
                (string)$e->getCode();
            if ($state === '23000') {
                return 'exists';
            }
            throw $e;
        }
        return 'ok';
    }

    /**
     * Atomically replaces endpoint state when a control sequence is still new.
     *
     * @param string $id Publisher id.
     * @param int $proto Protocol version.
     * @param int $udpPort Publisher UDP port.
     * @param array<int, array<string, mixed>> $candidates Candidate endpoints.
     * @param int $sequence Accepted publisher control sequence.
     * @return bool True when exactly one record accepted the sequence.
     */
    private function updatePublisherIfSequence(string $id, int $proto,
        int $udpPort, array $candidates, int $sequence): bool
    {
        $up = $this->db->prepare(
            'UPDATE redp2p_publishers SET proto = ?, udp_port = ?, candidates = ?, seq = ?, last_seen = ?'
            . ' WHERE id = ? AND seq < ?'
        );
        $up->execute([
            $proto,
            $udpPort,
            json_encode($candidates),
            $sequence,
            time(),
            $id,
            $sequence,
        ]);
        return $up->rowCount() === 1;
    }

    /**
     * Counts publishers not reserved as VIP.
     *
     * @return int Publisher count.
     */
    private function countNonVip(): int
    {
        if ($this->vips === []) {
            $st = $this->db->query('SELECT COUNT(*) FROM redp2p_publishers');
            return (int)$st->fetchColumn();
        }
        $ids = array_keys($this->vips);
        $ph = implode(',', array_fill(0, count($ids), '?'));
        $st = $this->db->prepare("SELECT COUNT(*) FROM redp2p_publishers WHERE id NOT IN ($ph)");
        $st->execute($ids);
        return (int)$st->fetchColumn();
    }

    /**
     * Finds a publisher session secret and its accepted sequence.
     *
     * @param string $id Publisher id.
     * @return array<string, mixed>|null Session state, or null when unknown.
     */
    private function findPublisherSecret(string $id): ?array
    {
        $st = $this->db->prepare('SELECT key, seq FROM redp2p_publishers WHERE id = ?');
        $st->execute([$id]);
        $value = $st->fetch(PDO::FETCH_ASSOC);
        return $value === false ? null : $value;
    }

    /**
     * Finds the live record for a publisher.
     *
     * @param string $id Publisher id.
     * @param bool $locked Lock the row inside a serialized write transaction.
     * @return array<string, mixed>|null Record, or null when unknown.
     */
    private function findPublisher(string $id, bool $locked = false): ?array
    {
        $st = $this->db->prepare(
            'SELECT proto, udp_port, candidates, last_seen, punch_credit, punch_ms'
            . ' FROM redp2p_publishers WHERE id = ?'
            . ($locked && $this->db->getAttribute(PDO::ATTR_DRIVER_NAME) ===
                'mysql' ? ' FOR UPDATE' : '')
        );
        $st->execute([$id]);
        $row = $st->fetch(PDO::FETCH_ASSOC);
        return $row === false ? null : $row;
    }

    /**
     * Deletes publisher records past the TTL.
     *
     * @return void
     */
    private function evictStale(): void
    {
        $st = $this->db->prepare('DELETE FROM redp2p_publishers WHERE last_seen < ?');
        $st->execute([time() - $this->ttl]);
    }

    /**
     * Deletes pending punch calls past the TTL.
     *
     * @return void
     */
    private function evictPending(): void
    {
        $st = $this->db->prepare('DELETE FROM redp2p_pending_calls WHERE ts < ?');
        $st->execute([time() - $this->pendingTtlS]);
    }

    /**
     * Creates the index tables when missing.
     *
     * @return void
     */
    private function ensureSchema(): void
    {
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_publishers ('
            . ' id VARCHAR(63) NOT NULL PRIMARY KEY'
            . ', key CHAR(16) NOT NULL'
            . ', seq INTEGER NOT NULL DEFAULT 0'
            . ', proto INTEGER NOT NULL'
            . ', udp_port INTEGER NOT NULL'
            . ', candidates TEXT NULL'
            . ', last_seen INTEGER NOT NULL'
            . ')'
        );
        try {
            $this->db->exec('ALTER TABLE redp2p_publishers ADD COLUMN seq INTEGER NOT NULL DEFAULT 0');
        } catch (\PDOException $e) {
        }
        foreach (['punch_credit INTEGER NOT NULL DEFAULT 40000',
            'punch_ms BIGINT NOT NULL DEFAULT 0'] as $column) {
            try {
                $this->db->exec('ALTER TABLE redp2p_publishers ADD COLUMN ' .
                    $column);
            } catch (\PDOException $e) {
                if (stripos($e->getMessage(), 'duplicate column') === false)
                    throw $e;
            }
        }
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_rate_sources ('
            . ' address VARCHAR(32) NOT NULL PRIMARY KEY'
            . ', credit INTEGER NOT NULL'
            . ', updated_ms BIGINT NOT NULL'
            . ')'
        );
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_pending_calls ('
            . ' id VARCHAR(32) NOT NULL PRIMARY KEY'
            . ', self_id VARCHAR(63) NOT NULL'
            . ', target_id VARCHAR(63) NOT NULL'
            . ', session VARCHAR(63) NOT NULL'
            . ', candidates TEXT NULL'
            . ', ts INTEGER NOT NULL'
            . ')'
        );
        $this->db->exec(
            'CREATE TABLE IF NOT EXISTS redp2p_meta ('
            . ' name VARCHAR(32) NOT NULL PRIMARY KEY'
            . ', value TEXT NULL'
            . ')'
        );
        try {
            $lock = $this->db->prepare(
                'INSERT INTO redp2p_meta (name, value) VALUES (?, ?)'
            );
            $lock->execute(['pending_write_lock', '']);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0]) ? (string)$e->errorInfo[0] :
                (string)$e->getCode();
            if (strncmp($state, '23', 2) !== 0) {
                throw $e;
            }
        }
        try {
            $this->db->exec(
                'CREATE INDEX IF NOT EXISTS redp2p_pending_calls_target ON redp2p_pending_calls (target_id)'
            );
        } catch (\PDOException $e) {
        }
    }

    /**
     * Loads or establishes the persistent registration challenge key.
     *
     * @return string Raw 32-byte challenge key.
     */
    private function loadChallengeKey(): string
    {
        $candidate = bin2hex(random_bytes(32));
        try {
            $insert = $this->db->prepare(
                'INSERT INTO redp2p_meta (name, value) VALUES (?, ?)'
            );
            $insert->execute(['registration_challenge_key', $candidate]);
        } catch (\PDOException $e) {
            $state = isset($e->errorInfo[0]) ? (string)$e->errorInfo[0] :
                (string)$e->getCode();
            if (strncmp($state, '23', 2) !== 0) {
                throw $e;
            }
        }
        $select = $this->db->prepare(
            'SELECT value FROM redp2p_meta WHERE name = ?'
        );
        $select->execute(['registration_challenge_key']);
        $value = $select->fetchColumn();
        if (!is_string($value) || !preg_match('/^[0-9a-f]{64}$/D', $value)) {
            throw new \PDOException('registration challenge key is malformed');
        }
        $key = hex2bin($value);
        if ($key === false || strlen($key) !== 32) {
            throw new \PDOException('registration challenge key is malformed');
        }
        return $key;
    }

    /**
     * Requires a valid id field on the request.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name holding the id.
     * @return array{int, string|null} [1, id], [0, null] when missing,
     *   [-1, null] when invalid.
     */
    private function requireId(\stdClass $o, string $field): array
    {
        if (!$this->hasString($o, $field)) {
            return [0, null];
        }
        $id = $o->$field;
        if (!$this->isValidId($id)) {
            return [-1, null];
        }
        return [1, $id];
    }

    /**
     * Requires a hex token of the exact length.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name holding the token.
     * @param int $len Expected token length.
     * @return string|null Token, or null when absent or invalid.
     */
    private function requireHex(\stdClass $o, string $field, int $len): ?string
    {
        if (!$this->hasString($o, $field)) {
            return null;
        }
        $value = $o->$field;
        return $this->isHexToken($value, $len) ? $value : null;
    }

    /**
     * Requires a lowercase hexadecimal token of the exact length.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name holding the token.
     * @param int $len Expected token length.
     * @return string|null Token, or null when absent or invalid.
     */
    private function requireLowerHex(\stdClass $o, string $field, int $len): ?string
    {
        if (!$this->hasString($o, $field)) {
            return null;
        }
        $value = $o->$field;
        return preg_match('/^[0-9a-f]{' . $len . '}$/D', $value) === 1
            ? $value : null;
    }

    /**
     * Returns one positive control sequence within JSON's exact integer range.
     *
     * @param \stdClass $o Parsed request object.
     * @return int|null Sequence, or null when invalid.
     */
    private function requireSequence(\stdClass $o): ?int
    {
        if (!property_exists($o, 'seq') || !is_int($o->seq) || $o->seq < 1 ||
            $o->seq > 9007199254740991) {
            return null;
        }
        return $o->seq;
    }

    /**
     * Produces the canonical HMAC proof for one publisher control request.
     *
     * @param string $secret Publisher session secret.
     * @param string $op Control operation name.
     * @param string $id Publisher identifier.
     * @param int $sequence Strictly increasing control sequence.
     * @param int $proto Heartbeat protocol, or zero for other controls.
     * @param int $udpPort Heartbeat port, or zero for other controls.
     * @param array<int, array<string, mixed>> $candidates Heartbeat candidates.
     *   Ignored for deregistration and punch_poll.
     * @return string Lowercase hexadecimal proof.
     */
    private function controlProof(string $secret, string $op, string $id,
        int $sequence, int $proto = 0, int $udpPort = 0,
        array $candidates = []): string
    {
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
     * Requires the proto and udp_port fields.
     *
     * @param \stdClass $o Parsed request object.
     * @return array{int|null, int|null} [proto, udp_port], or [null, null] when
     *   absent or invalid.
     */
    private function requireProtoPort(\stdClass $o): array
    {
        if (!property_exists($o, 'proto') || !property_exists($o, 'udp_port')) {
            return [null, null];
        }
        $proto = $o->proto;
        if (!is_int($proto) && !is_float($proto)) {
            return [null, null];
        }
        $proto = (float)$proto;
        if ($proto !== 1.0 && $proto !== 2.0) {
            return [null, null];
        }
        $udpPort = $this->requirePort($o->udp_port);
        if ($udpPort === null) {
            return [null, null];
        }
        return [(int)$proto, $udpPort];
    }

    /**
     * Validates one parsed JSON numeric value as a UDP port.
     *
     * @param mixed $value Parsed JSON value.
     * @return int|null Port in range, or null when invalid.
     */
    private function requirePort(mixed $value): ?int
    {
        if (!is_int($value) && !is_float($value)) {
            return null;
        }
        $number = (float)$value;
        if (!is_finite($number) || $number < 1.0 || $number > 65535.0) {
            return null;
        }
        if ($number !== floor($number)) {
            return null;
        }
        return (int)$number;
    }

    /**
     * Parses and validates the candidate list for a server request.
     *
     * Requests may only submit host candidates naming reachable unicast
     * endpoints; the list is canonicalized, de-duplicated, and sorted using
     * the same ordering the client already applies, so authenticated proofs
     * computed over the normalized list match between both sides.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name holding the candidate list.
     * @return array{
     *     bool,
     *     array<int, array{type: string, addr: string, port: int}>
     * } [ok, candidates], [true, []] when absent.
     */
    private function parseCandidates(\stdClass $o, string $field): array
    {
        if (!property_exists($o, $field)) {
            return [true, []];
        }
        $value = $o->$field;
        if (!is_array($value)) {
            return [true, []];
        }
        $out = [];
        foreach ($value as $item) {
            if (!($item instanceof \stdClass)) {
                return [false, []];
            }
            $type = $this->getString($item, 'type');
            $addr = $this->getString($item, 'addr');
            if ($type === null || $addr === null) {
                return [false, []];
            }
            if (strlen($addr) > self::ADDR_MAX) {
                return [false, []];
            }
            if ($type !== 'host') {
                return [false, []];
            }
            if (!filter_var($addr, FILTER_VALIDATE_IP)) {
                return [false, []];
            }
            if (!property_exists($item, 'port')) {
                return [false, []];
            }
            $port = $this->requirePort($item->port);
            if ($port === null) {
                return [false, []];
            }
            $raw = @inet_pton($addr);
            if ($raw === false || (strlen($raw) !== 4 && strlen($raw) !== 16)) {
                return [false, []];
            }
            if (!$this->candidateDestAllowed($raw, $port)) {
                return [false, []];
            }
            $out[] = ['type' => $type, 'addr' => inet_ntop($raw), 'port' => $port];
        }
        $out = $this->normalizeCandidates($out);
        if (count($out) > self::CANDIDATES_MAX) {
            return [false, []];
        }
        return [true, $out];
    }

    /**
     * Applies the index destination policy to one raw candidate address.
     *
     * Host candidates may only name reachable unicast endpoints. Loopback,
     * unspecified, multicast, broadcast, and reserved destinations are
     * refused. IPv6 IPv4-mapped addresses are resolved through the IPv4
     * policy so that loopback and multicast cannot be smuggled past the v6
     * checks.
     *
     * @param string $raw Raw network-order address bytes.
     * @param int $port Candidate UDP port.
     * @return bool True when the endpoint is acceptable.
     */
    private function candidateDestAllowed(string $raw, int $port): bool
    {
        if ($port < 1) return false;
        if (strlen($raw) === 4) {
            $c = $raw[0];
            if ($c === "\x00") return false;
            if ($c === "\x7f") return false;
            if (ord($c) >= 224) return false;
            return true;
        }
        if ($raw[0] === "\xff") return false;
        $i = 0;
        while ($i < 10 && $raw[$i] === "\x00") $i++;
        if ($i === 10 && $raw[10] === "\xff" && $raw[11] === "\xff") {
            return $this->candidateDestAllowed(substr($raw, 12), $port);
        }
        for ($i = 0; $i < 16 && $raw[$i] === "\x00"; $i++) {}
        if ($i === 16) return false;
        if ($i === 15 && $raw[15] === "\x01") return false;
        return true;
    }

    /**
     * Normalizes candidate priority, removes duplicates, and sorts the list.
     *
     * @param array<int, array{type: string, addr: string, port: int}> $out
     * @return array<int, array{type: string, addr: string, port: int}>
     */
    private function normalizeCandidates(array $out): array
    {
        usort($out, function (array $a, array $b): int {
            $aRaw = inet_pton($a['addr']);
            $bRaw = inet_pton($b['addr']);
            $family = strlen($aRaw) <=> strlen($bRaw);
            if ($family !== 0) {
                return $family;
            }
            $address = strcmp($aRaw, $bRaw);
            if ($address !== 0) {
                return $address;
            }
            $port = $a['port'] <=> $b['port'];
            return $port !== 0 ? $port : ($a['type'] <=> $b['type']);
        });
        $unique = [];
        foreach ($out as $candidate) {
            $key = $candidate['type'] . "\0" . inet_pton($candidate['addr']) .
                "\0" . pack('n', $candidate['port']);
            $unique[$key] = $candidate;
        }
        return array_values($unique);
    }

    /**
     * Verifies a proof-of-work solution against the difficulty target.
     *
     * @param string $nonce Raw challenge nonce.
     * @param int $issuedAt Challenge issue timestamp.
     * @param int $expiresAt Challenge expiry timestamp.
     * @param string $id Publisher id.
     * @param string $solution Raw uint64 client solution.
     * @return bool True when the proof matches and passes the difficulty.
     */
    private function verifyPow(string $nonce, int $issuedAt, int $expiresAt,
        string $id, string $solution): bool
    {
        $digest = hash('sha256', $this->powInput($nonce, $issuedAt, $expiresAt,
            $id, $solution), true);
        return $this->leadingZeroBits($digest) >= $this->pow;
    }

    /**
     * Decodes an exact-length hexadecimal token.
     *
     * @param string $hex Hexadecimal token.
     * @param int $length Required decoded byte length.
     * @return string|null Raw bytes, or null when malformed.
     */
    private function hexBytes(string $hex, int $length): ?string
    {
        if (strlen($hex) !== $length * 2 || !ctype_xdigit($hex)) {
            return null;
        }
        $bytes = hex2bin($hex);
        return $bytes !== false && strlen($bytes) === $length ? $bytes : null;
    }

    /**
     * Reads one exact nonnegative timestamp from JSON.
     *
     * @param \stdClass $request Parsed request.
     * @param string $field Timestamp field name.
     * @return int|null Timestamp, or null when malformed.
     */
    private function requireTimestamp(\stdClass $request, string $field): ?int
    {
        if (!property_exists($request, $field) || !is_int($request->$field) ||
            $request->$field < 0) {
            return null;
        }
        return $request->$field;
    }

    /**
     * Encodes a safe Unix timestamp as uint64 big-endian bytes.
     *
     * @param int $value Nonnegative timestamp.
     * @return string Eight canonical bytes.
     */
    private function u64be(int $value): string
    {
        return pack('N2', intdiv($value, 4294967296), $value % 4294967296);
    }

    /**
     * Builds the authenticated challenge MAC input.
     *
     * @param string $nonce Raw 32-byte nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @return string Canonical challenge bytes.
     */
    private function challengeMacInput(string $nonce, int $issuedAt,
        int $expiresAt): string
    {
        return 'REDP2P-CHALLENGE' . $nonce . $this->u64be($issuedAt) .
            $this->u64be($expiresAt);
    }

    /**
     * Derives the stateless index X25519 secret key for one challenge.
     *
     * @param string $nonce Raw 32-byte challenge nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @return string Derived 32-byte X25519 secret key.
     */
    private function indexSecretKey(string $nonce, int $issuedAt,
        int $expiresAt): string
    {
        return hash_hmac('sha256', 'REDP2P-INDEX-KEY' . $nonce .
            $this->u64be($issuedAt) . $this->u64be($expiresAt),
            $this->challengeKey, true);
    }

    /**
     * Decrypts a protected publisher registration secret.
     *
     * @param string $nonce Raw 32-byte challenge nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @param string $publisherPublic Publisher 32-byte X25519 public key.
     * @param string $wireSecret 16-byte ciphertext followed by 16-byte tag.
     * @return string|null Original 16-byte control secret, or null on failure.
     */
    private function decryptRegistrationSecret(string $nonce, int $issuedAt,
        int $expiresAt, string $publisherPublic, string $wireSecret): ?string
    {
        try {
            $indexSecret = $this->indexSecretKey($nonce, $issuedAt, $expiresAt);
            $indexPublic = sodium_crypto_scalarmult_base($indexSecret);
            $shared = sodium_crypto_scalarmult($indexSecret, $publisherPublic);
            if (strlen($shared) !== 32 || hash_equals($shared,
                str_repeat("\x00", 32))) {
                return null;
            }
            $key = hash_hmac('sha256', 'REDP2P-REGISTER-SECRET' . $nonce .
                $indexPublic . $publisherPublic, $shared, true);
            $secret = sodium_crypto_aead_xchacha20poly1305_ietf_decrypt(
                $wireSecret, '', substr($nonce, 0, 24), $key);
            return $secret === false || strlen($secret) !== self::KEY_SZ ? null :
                $secret;
        } catch (\Throwable $e) {
            return null;
        }
    }

    /**
     * Validates the recovered fixed-width publisher control secret.
     *
     * @param string $secret Recovered 16-byte control secret.
     * @return bool True when every byte is ASCII hexadecimal.
     */
    private function isControlSecret(string $secret): bool
    {
        return strlen($secret) === self::KEY_SZ &&
            preg_match('/\A[0-9A-Fa-f]{16}\z/D', $secret) === 1;
    }

    /**
     * Validates one authenticated and unexpired challenge.
     *
     * @param string $nonce Raw 32-byte nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @param string $mac Raw 32-byte MAC.
     * @return bool True when valid.
     */
    private function verifyChallenge(string $nonce, int $issuedAt, int $expiresAt,
        string $mac): bool
    {
        $now = time();
        if (strlen($nonce) !== 32 || strlen($mac) !== 32 || $expiresAt <= $issuedAt ||
            $expiresAt - $issuedAt !== 60 || $issuedAt > $now + 5 ||
            $now > $expiresAt) {
            return false;
        }
        $expected = hash_hmac('sha256', $this->challengeMacInput($nonce,
            $issuedAt, $expiresAt), $this->challengeKey, true);
        return hash_equals($expected, $mac);
    }

    /**
     * Returns the canonical registration protocol byte.
     *
     * @param int $proto Public protocol value.
     * @return string One canonical byte.
     */
    private function canonicalProto(int $proto): string
    {
        return $proto === 2 ? "\x01" : "\x02";
    }

    /**
     * Builds the immutable registration PoW input.
     *
     * @param string $nonce Raw nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @param string $id Publisher id.
     * @param string $solution Raw uint64 solution.
     * @return string Canonical PoW bytes.
     */
    private function powInput(string $nonce, int $issuedAt, int $expiresAt,
        string $id, string $solution): string
    {
        return 'REDP2P-POW' . $nonce . $this->u64be($issuedAt) .
            $this->u64be($expiresAt) . pack('n', strlen($id)) . $id . $solution;
    }

    /**
     * Builds the canonical registration proof input.
     *
     * @param string $nonce Raw nonce.
     * @param int $issuedAt Issue timestamp.
     * @param int $expiresAt Expiry timestamp.
     * @param string $id Publisher id.
     * @param string $secret Publisher secret text.
     * @param int $proto Public protocol value.
     * @param int $port Publisher UDP port.
     * @param array $candidates Normalized candidates.
     * @param string $solution Raw uint64 solution.
     * @return string Canonical registration bytes.
     */
    private function registrationMessage(string $nonce, int $issuedAt,
        int $expiresAt, string $id, string $secret, int $proto, int $port,
        array $candidates, string $solution): string
    {
        $message = 'REDP2P-REGISTER' . $nonce . $this->u64be($issuedAt) .
            $this->u64be($expiresAt) . pack('n', strlen($id)) . $id .
            pack('n', strlen($secret)) . $secret . $this->canonicalProto($proto) .
            pack('nC', $port, count($candidates));
        foreach ($candidates as $candidate) {
            $address = inet_pton($candidate['addr']);
            $message .= chr($candidate['type'] === 'host' ? 1 : 2) .
                chr(strlen($address) === 4 ? 4 : 6) . $address .
                pack('n', $candidate['port']);
        }
        return $message . $solution;
    }

    /**
     * Counts leading zero bits of a binary string.
     *
     * @param string $bin Raw binary digest.
     * @return int Leading zero bit count.
     */
    private function leadingZeroBits(string $bin): int
    {
        $total = 0;
        $len = strlen($bin);
        for ($i = 0; $i < $len; $i++) {
            if ($bin[$i] === "\x00") {
                $total += 8;
                continue;
            }
            $byte = ord($bin[$i]);
            while (($byte & 0x80) === 0) {
                $total++;
                $byte <<= 1;
            }
            break;
        }
        return $total;
    }

    /**
     * Computes the binary password-bound registration admission proof.
     *
     * @param string $password Nonempty index admission password.
     * @param string $message Canonical registration bytes.
     * @return string Binary SHA-256 HMAC.
     */
    private function admissionProof(string $password, string $message): string
    {
        return hash_hmac('sha256', 'REDP2P-ADMISSION-v1' . $message,
            $password, true);
    }

    /**
     * Returns the password for a publisher id.
     *
     * @param string $id Publisher id.
     * @return string VIP password when reserved, else the shared password.
     */
    private function getPass(string $id): string
    {
        return $this->vips[$id] ?? $this->pass;
    }

    /**
     * Decodes a stored candidate JSON list.
     *
     * @param string $text Stored JSON.
     * @return array<int, array{type: string, addr: string, port: int}>
     *   Candidates, or [] when invalid.
     */
    private function decodeCandidates(string $text): array
    {
        $decoded = json_decode($text, true);
        return is_array($decoded) ? array_values($decoded) : [];
    }

    /**
     * Checks a publisher id token.
     *
     * @param string $id Id token.
     * @return bool True when alphanumeric and at most ID_MAX.
     */
    private function isValidId(string $id): bool
    {
        return $id !== '' && strlen($id) <= self::ID_MAX && ctype_alnum($id);
    }

    /**
     * Checks a session token.
     *
     * @param string $token Session token.
     * @return bool True when alphanumeric and at most SESSION_MAX.
     */
    private function isSessionToken(string $token): bool
    {
        return $token !== '' && strlen($token) <= self::SESSION_MAX && ctype_alnum($token);
    }

    /**
     * Checks a hex token of the exact length.
     *
     * @param string $token Hex token.
     * @param int $len Expected length.
     * @return bool True when the token has exactly $len hex digits.
     */
    private function isHexToken(string $token, int $len): bool
    {
        return strlen($token) === $len && ctype_xdigit($token);
    }

    /**
     * Checks a shared password token.
     *
     * @param string $pass Password token.
     * @return bool True when non-empty, short, and free of control bytes.
     */
    private function isValidPassToken(string $pass): bool
    {
        if ($pass === '' || strlen($pass) > self::PASS_MAX) {
            return false;
        }
        return preg_match('/[\x00-\x20\x7f]/D', $pass) !== 1;
    }

    /**
     * Checks that a field exists and holds a string.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name.
     * @return bool True when the field holds a string.
     */
    private function hasString(\stdClass $o, string $field): bool
    {
        return property_exists($o, $field) && is_string($o->$field);
    }

    /**
     * Returns a string field, or null when absent.
     *
     * @param \stdClass $o Parsed request object.
     * @param string $field Field name.
     * @return string|null Field value, or null when absent.
     */
    private function getString(\stdClass $o, string $field): ?string
    {
        return $this->hasString($o, $field) ? $o->$field : null;
    }

    /**
     * Checks headers for a name, case-insensitively.
     *
     * @param array<string, string> $headers Header map.
     * @param string $name Header name.
     * @return bool True when present.
     */
    private function hasHeader(array $headers, string $name): bool
    {
        $name = strtolower($name);
        foreach ($headers as $key => $value) {
            if (strtolower((string)$key) === $name) {
                return true;
            }
        }
        return false;
    }

    /**
     * Detects duplicate keys at the top level of a JSON body.
     *
     * @param string $json Raw JSON body.
     * @return bool True when a duplicate top-level key exists.
     */
    private function hasDuplicateTopKeys(string $json): bool
    {
        $keys = [];
        $len = strlen($json);
        $depth = 0;
        $i = 0;
        while ($i < $len) {
            $c = $json[$i];
            if ($c === '"') {
                $start = $i + 1;
                $j = $i + 1;
                while ($j < $len) {
                    if ($json[$j] === '\\') {
                        $j += 2;
                        continue;
                    }
                    if ($json[$j] === '"') {
                        break;
                    }
                    $j++;
                }
                $k = $j + 1;
                while ($k < $len && ($json[$k] === ' ' || $json[$k] === "\t")) {
                    $k++;
                }
                if ($depth === 1 && $k < $len && $json[$k] === ':') {
                    $key = json_decode('"' . substr($json, $start, $j - $start) . '"');
                    if (!is_string($key)) {
                        $key = substr($json, $start, $j - $start);
                    }
                    if (isset($keys[$key])) {
                        return true;
                    }
                    $keys[$key] = 1;
                }
                $i = $j + 1;
                continue;
            }
            if ($c === '{' || $c === '[') {
                $depth++;
            } elseif ($c === '}' || $c === ']') {
                $depth--;
            }
            $i++;
        }
        return false;
    }

    /**
     * Builds a JSON success response.
     *
     * @param array<string, mixed> $fields Extra payload fields.
     * @param bool $okFirst Place 'ok' before the fields.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function jsonOk(array $fields = [], bool $okFirst = true): array
    {
        $data = $okFirst
            ? array_merge(['ok' => true], $fields)
            : array_merge($fields, ['ok' => true]);
        return $this->response(200, 'OK', 'application/json', json_encode($data));
    }

    /**
     * Builds a JSON error response.
     *
     * @param int $status HTTP status code.
     * @param string $code Machine-readable error code.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function jsonError(int $status, string $code): array
    {
        $body = '{"ok":false,"error":"' . $code . '"}';
        return $this->response($status, $this->statusReason($status), 'application/json', $body);
    }

    /**
     * Builds a plain-text error response.
     *
     * @param int $status HTTP status code.
     * @param string $reason HTTP reason phrase.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function httpError(int $status, string $reason): array
    {
        return $this->response($status, $reason, 'text/plain', self::ERROR_MALFORMED);
    }

    /**
     * Builds a full HTTP response array.
     *
     * @param int $status HTTP status code.
     * @param string $reason HTTP reason phrase.
     * @param string $contentType Response Content-Type.
     * @param string $body Response body.
     * @return array{
     *     status: int,
     *     reason: string,
     *     headers: array<string, string>,
     *     body: string
     * }
     */
    private function response(int $status, string $reason, string $contentType, string $body): array
    {
        return [
            'status' => $status,
            'reason' => $reason,
            'headers' => [
                'Content-Type' => $contentType,
                'Content-Length' => (string)strlen($body),
                'Connection' => 'close',
            ],
            'body' => $body,
        ];
    }

    /**
     * Maps an HTTP status code to its reason phrase.
     *
     * @param int $status HTTP status code.
     * @return string Reason phrase, or 'Error' when unknown.
     */
    private function statusReason(int $status): string
    {
        return match ($status) {
            200 => 'OK',
            400 => 'Bad Request',
            403 => 'Forbidden',
            404 => 'Not Found',
            405 => 'Method Not Allowed',
            413 => 'Payload Too Large',
            429 => 'Too Many Requests',
            500 => 'Internal Server Error',
            501 => 'Not Implemented',
            503 => 'Service Unavailable',
            default => 'Error',
        };
    }
}
