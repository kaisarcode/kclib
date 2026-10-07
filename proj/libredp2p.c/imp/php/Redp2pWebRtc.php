<?php
/**
 * Summary: REDP2P WebRTC transport module
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);
namespace KaisarCode;

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
