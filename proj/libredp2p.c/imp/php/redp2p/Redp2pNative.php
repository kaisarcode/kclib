<?php
/**
 * Summary: REDP2P native punch transport module
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);
namespace KaisarCode;

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
