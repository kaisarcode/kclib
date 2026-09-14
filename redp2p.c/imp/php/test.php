<?php
/**
 * Summary: REDP2P PHP index control and registration tests.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);

require __DIR__ . '/Redp2pIndex.php';

use KaisarCode\Redp2pIndex;

/**
 * Sends one JSON request to the PHP index.
 *
 * @return array{status: int, reason: string, headers: array<string, string>, body: string}
 */
function request(Redp2pIndex $index, array $request,
    string $source = '127.0.0.1'): array
{
    return $index->handle('POST', json_encode($request,
        JSON_PRESERVE_ZERO_FRACTION | JSON_THROW_ON_ERROR), [], $source);
}

/**
 * Creates a zero-work registration request.
 *
 * @return array<string, mixed>
 */
function registration(Redp2pIndex $index, string $id, int $port,
    string $password = '', ?string $controlSecret = null): array
{
    $challenge = body(request($index, ['op' => 'challenge', 'id' => $id]));
    $nonce = hex2bin($challenge['nonce']);
    $solution = str_repeat("\x00", 8);
    $idBytes = pack('n', strlen($id)) . $id;
    while (leadingZeroBits(hash('sha256', 'REDP2P-POW' . $nonce .
        u64be($challenge['issued_at']) . u64be($challenge['expires_at']) .
        $idBytes . $solution, true)) < $challenge['bits']) {
        for ($i = 7; $i >= 0; $i--) {
            $next = (ord($solution[$i]) + 1) & 0xff;
            $solution[$i] = chr($next);
            if ($next !== 0) {
                break;
            }
        }
        if ($i < 0) {
            throw new RuntimeException('pow solution exhausted');
        }
    }
    $secret = $controlSecret ?? str_pad(dechex($port), 16, '0', STR_PAD_LEFT);
    $publisherSecret = random_bytes(32);
    $publisherPublic = sodium_crypto_scalarmult_base($publisherSecret);
    $indexPublic = hex2bin($challenge['pkey']);
    $shared = sodium_crypto_scalarmult($publisherSecret, $indexPublic);
    if (hash_equals($shared, str_repeat("\x00", 32))) {
        throw new RuntimeException('invalid registration shared secret');
    }
    $encryptionKey = hash_hmac('sha256', 'REDP2P-REGISTER-SECRET' . $nonce .
        $indexPublic . $publisherPublic, $shared, true);
    $encryptedSecret = sodium_crypto_aead_xchacha20poly1305_ietf_encrypt(
        $secret, '', substr($nonce, 0, 24), $encryptionKey);
    $candidates = [['type' => 'host', 'addr' => '192.0.2.1', 'port' => $port]];
    $message = 'REDP2P-REGISTER' . $nonce . u64be($challenge['issued_at']) .
        u64be($challenge['expires_at']) . $idBytes . pack('n', strlen($secret)) .
        $secret . "\x02" . pack('nC', $port, 1) . "\x01\x04" .
        inet_pton('192.0.2.1') . pack('n', $port) . $solution;
    $request = [
        'op' => 'register',
        'id' => $id,
        'nonce' => $challenge['nonce'],
        'issued_at' => $challenge['issued_at'],
        'expires_at' => $challenge['expires_at'],
        'mac' => $challenge['mac'],
        'pow_solution' => bin2hex($solution),
        'proof' => hash_hmac('sha256', $message, $secret),
        'pkey' => bin2hex($publisherPublic),
        'secret' => bin2hex($encryptedSecret),
        'proto' => 1,
        'udp_port' => $port,
        'candidates' => $candidates,
    ];
    if ($password !== '') {
        $request['access_proof'] = hash_hmac('sha256',
            'REDP2P-ADMISSION-v1' . $message, $password);
    }
    return $request;
}

/**
 * Encodes one safe timestamp as uint64 big-endian bytes.
 *
 * @param int $value Timestamp.
 * @return string Canonical bytes.
 */
function u64be(int $value): string
{
    return pack('N2', intdiv($value, 4294967296), $value % 4294967296);
}

/**
 * Counts leading zero bits in a digest.
 *
 * @param string $digest Raw digest.
 * @return int Leading zero count.
 */
function leadingZeroBits(string $digest): int
{
    $count = 0;
    foreach (str_split($digest) as $byte) {
        $value = ord($byte);
        if ($value === 0) {
            $count += 8;
            continue;
        }
        while (($value & 0x80) === 0) {
            $count++;
            $value <<= 1;
        }
        break;
    }
    return $count;
}

/**
 * Decodes one JSON response body.
 *
 * @return array<string, mixed>
 */
function body(array $response): array
{
    return json_decode($response['body'], true, 512, JSON_THROW_ON_ERROR);
}

/**
 * Stops the test on a failed assertion.
 *
 * @return void
 */
function check(bool $condition, string $message): void
{
    if (!$condition) {
        fwrite(STDERR, "FAIL: $message\n");
        exit(1);
    }
}

/**
 * Builds one canonical heartbeat request for a fixed publisher secret.
 *
 * @param string $id Publisher id.
 * @param string $secret Publisher session secret.
 * @param int $seq Control sequence.
 * @param int $port Publisher UDP port.
 * @return array<string, mixed> Heartbeat request.
 */
function heartbeatRequest(string $id, string $secret, int $seq, int $port): array
{
    $candidates = [['type' => 'host', 'addr' => '192.0.2.1', 'port' => $port]];
    $message = "heartbeat\n$id\n$seq\n1\n$port\n1\nhost\n192.0.2.1\n$port";
    return [
        'op' => 'heartbeat',
        'id' => $id,
        'seq' => $seq,
        'proof' => hash_hmac('sha256', $message, $secret),
        'proto' => 1,
        'udp_port' => $port,
        'candidates' => $candidates,
    ];
}

/**
 * Issues two requests concurrently through separate SQLite connections.
 *
 * @param string $path SQLite database path.
 * @param array<string, mixed> $first First request.
 * @param array<string, mixed> $second Second request.
 * @param array<string, mixed> $options Index options for both workers.
 * @return array{0: array<string, mixed>, 1: array<string, mixed>} Responses.
 */
function concurrentRequests(string $path, array $first, array $second,
    array $options = []): array
{
    $spec = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    $firstProcess = proc_open([PHP_BINARY, __FILE__, 'worker-request', $path,
        base64_encode(json_encode($first, JSON_THROW_ON_ERROR)),
        base64_encode(json_encode($options, JSON_THROW_ON_ERROR))], $spec,
        $firstPipes);
    $secondProcess = proc_open([PHP_BINARY, __FILE__, 'worker-request', $path,
        base64_encode(json_encode($second, JSON_THROW_ON_ERROR)),
        base64_encode(json_encode($options, JSON_THROW_ON_ERROR))], $spec,
        $secondPipes);
    check(is_resource($firstProcess) && is_resource($secondProcess), 'concurrent request workers start');
    fwrite($firstPipes[0], "go\n");
    fwrite($secondPipes[0], "go\n");
    fclose($firstPipes[0]);
    fclose($secondPipes[0]);
    $firstResponse = json_decode(stream_get_contents($firstPipes[1]), true,
        512, JSON_THROW_ON_ERROR);
    $secondResponse = json_decode(stream_get_contents($secondPipes[1]), true,
        512, JSON_THROW_ON_ERROR);
    fclose($firstPipes[1]);
    fclose($secondPipes[1]);
    fclose($firstPipes[2]);
    fclose($secondPipes[2]);
    check(proc_close($firstProcess) === 0 && proc_close($secondProcess) === 0,
        'concurrent request workers finish');
    return [$firstResponse, $secondResponse];
}

if (($argv[1] ?? '') === 'worker') {
    $db = new PDO('sqlite:' . $argv[2]);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_TIMEOUT, 2);
    $index = new Redp2pIndex($db, ['pass' => '']);
    fgets(STDIN);
    echo json_encode(request($index, registration($index, 'concurrent', (int)$argv[3])), JSON_THROW_ON_ERROR);
    exit(0);
}

if (($argv[1] ?? '') === 'worker-request') {
    $db = new PDO('sqlite:' . $argv[2]);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_TIMEOUT, 2);
    $options = json_decode(base64_decode($argv[4], true), true, 512,
        JSON_THROW_ON_ERROR);
    $index = new Redp2pIndex($db, $options);
    $request = json_decode(base64_decode($argv[3], true), true, 512,
        JSON_THROW_ON_ERROR);
    fgets(STDIN);
    echo json_encode($index->handle('POST', json_encode($request,
        JSON_THROW_ON_ERROR), [], '127.0.0.1'), JSON_THROW_ON_ERROR);
    exit(0);
}

$db = new PDO('sqlite::memory:');
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$index = new Redp2pIndex($db, ['pass' => '']);

$vectorNonce = '';
for ($i = 0; $i < 32; $i++) {
    $vectorNonce .= chr($i);
}
$vectorIssued = 1700000000;
$vectorExpires = 1700000060;
$vectorId = 'vector';
$vectorSecret = '0123456789abcdef';
$vectorSolution = hex2bin('0102030405060708');
$vectorCandidates = "\x01\x04" . inet_pton('127.0.0.1') . pack('n', 41009) .
    "\x02\x06" . inet_pton('2001:db8::1') . pack('n', 4444);
$vectorPow = 'REDP2P-POW' . $vectorNonce . u64be($vectorIssued) .
    u64be($vectorExpires) . pack('n', strlen($vectorId)) . $vectorId .
    $vectorSolution;
$vectorMessage = 'REDP2P-REGISTER' . $vectorNonce . u64be($vectorIssued) .
    u64be($vectorExpires) . pack('n', strlen($vectorId)) . $vectorId .
    pack('n', strlen($vectorSecret)) . $vectorSecret . "\x02" .
    pack('nC', 41009, 2) . $vectorCandidates . $vectorSolution;
check(hash_hmac('sha256', 'REDP2P-CHALLENGE' . $vectorNonce .
    u64be($vectorIssued) . u64be($vectorExpires), $vectorNonce) ===
    'b4ae0428385fc80d7daeefc69d0ce98aafd9c2c36c65b1e9b998ac80c989a0f3',
    'canonical challenge MAC vector');
check(hash('sha256', $vectorPow) ===
    '357b34780abc35e2529d74352d41660474cbe71502979cba2d9063a9b27bd2a3',
    'canonical registration PoW vector');
check(hash_hmac('sha256', $vectorMessage, $vectorSecret) ===
    '702ade4feeb6297ec53e7eebc262bf8a61feb20731fdcf7f6001df7d616de906',
    'canonical registration proof vector');
check(hash_hmac('sha256', 'REDP2P-ADMISSION-v1' . $vectorMessage,
    'admission-pass') ===
    '252d6e3219e49f38e73fb587e66400a051f2c6c5ec593f8dfab583a5c7225b1d',
    'canonical admission proof vector');
check(hash_hmac('sha256', 'REDP2P-ADMISSION-v1' . $vectorMessage,
    'Admission-pass') ===
    'caa4863ffe53fe7aa240e40147e65481f423aeaed141ef1389c8ea58ce009b58',
    'case-sensitive admission proof vector');
check(hash_hmac('sha256', 'REDP2P-ADMISSION-v1' . $vectorMessage,
    'admission-pass') !== hash_hmac('sha256', 'REDP2P-ADMISSION-v1' . $vectorMessage,
    'Admission-pass'), 'password casing changes admission proof');
$vectorChallengeKey = $vectorNonce;
$vectorPublisherSecret = '';
for ($i = 1; $i <= 32; $i++) {
    $vectorPublisherSecret .= chr($i);
}
$vectorIndexSecret = hash_hmac('sha256', 'REDP2P-INDEX-KEY' . $vectorNonce .
    u64be($vectorIssued) . u64be($vectorExpires), $vectorChallengeKey, true);
$vectorIndexPublic = sodium_crypto_scalarmult_base($vectorIndexSecret);
$vectorPublisherPublic = sodium_crypto_scalarmult_base($vectorPublisherSecret);
$vectorShared = sodium_crypto_scalarmult($vectorPublisherSecret,
    $vectorIndexPublic);
$vectorEncryptionKey = hash_hmac('sha256', 'REDP2P-REGISTER-SECRET' .
    $vectorNonce . $vectorIndexPublic . $vectorPublisherPublic, $vectorShared,
    true);
$vectorWireSecret = sodium_crypto_aead_xchacha20poly1305_ietf_encrypt(
    $vectorSecret, '', substr($vectorNonce, 0, 24), $vectorEncryptionKey);
check(bin2hex($vectorIndexSecret) ===
    'cbcb985720a1c789c78b2086e7d28418b398e7524daac8a4d24feb437cdb1635',
    'index secret key vector');
check(bin2hex($vectorIndexPublic) ===
    'cb2a1d87390c90d74588ab1b0959af4232404cd6f8f74e731028ab63dc263b66',
    'index public key vector');
check(bin2hex($vectorPublisherPublic) ===
    '07a37cbc142093c8b755dc1b10e86cb426374ad16aa853ed0bdfc0b2b86d1c7c',
    'publisher public key vector');
check(bin2hex($vectorShared) ===
    '1a294ae3b7cdb9307edcd551841d879b3529c6d09eab920c72d350a11be33e38',
    'registration shared secret vector');
check(bin2hex($vectorEncryptionKey) ===
    '9d0f990cb51ee320f14245b526b35cf80848d58b6cc6ff5d02fec6a4b7df5975',
    'registration encryption key vector');
check(bin2hex($vectorWireSecret) ===
    'edccec6e4b9f610612682014714771710cb5970b35db8e45e5d3a184eda862b5',
    'encrypted registration secret vector');

$passwordValidation = new Redp2pIndex(new PDO('sqlite::memory:'),
    ['pass' => "a!\"\\[]{}()<>|&*^$#?~`\xc3\xb1"]);
$passwordValidation->setPass('');
foreach (["bad pass", "bad\tpass", "bad\rpass", "bad\npass", "bad\x7fpass"] as $invalidPass) {
    try {
        $passwordValidation->setPass($invalidPass);
        check(false, 'control password is rejected');
    } catch (InvalidArgumentException) {
        check(true, 'control password is rejected');
    }
}
try {
    $passwordValidation->setVips('vip bad pass');
    check(false, 'VIP whitespace password is rejected');
} catch (InvalidArgumentException) {
    check(true, 'VIP whitespace password is rejected');
}

$first = body(request($index, registration($index, 'normal', 41001)));
check($first['ok'] === true, 'free normal registration');
$lookup = body(request($index, ['op' => 'lookup', 'id' => 'normal']));
$takeover = request($index, registration($index, 'normal', 41002));
$takeoverBody = body($takeover);
check($takeover['status'] === 409 && $takeoverBody === ['ok' => false, 'error' => 'already_registered'], 'normal takeover rejection');
$after = body(request($index, ['op' => 'lookup', 'id' => 'normal']));
check($after === $lookup, 'normal takeover leaves record unchanged');
check(!isset($takeoverBody['secret']), 'normal takeover does not disclose secret');

foreach ([
    'nonhex' => '0123456789abcde!',
    'embedded NUL' => "0123456\x0089abcdef",
    'malformed' => '0123456789abcde ',
] as $label => $invalidSecret) {
    $invalidResponse = request($index, registration($index,
        'invalidsecret' . strlen($label), 41012, '', $invalidSecret));
    check($invalidResponse['status'] === 403 && body($invalidResponse)['error'] ===
        'auth_failed', 'authenticated encrypted ' . $label . ' secret fails auth');
}

foreach ([
    ['secret', str_repeat('0', 64)],
    ['proto', 2],
    ['candidate_address', '192.0.2.2'],
    ['pow_solution', str_repeat('f', 16)],
] as $tamperIndex => [$field, $tamperValue]) {
    $mutationId = 'authtamper' . $tamperIndex;
    $mutated = registration($index, $mutationId, 41100);
    if ($field === 'secret' || $field === 'proto' || $field === 'pow_solution') {
        $mutated[$field] = $tamperValue;
    } else {
        $mutated['candidates'][0]['addr'] = $tamperValue;
    }
    $rejected = request($index, $mutated);
    check($rejected['status'] === 403 && body($rejected)['error'] === 'auth_failed',
        'authenticated registration rejects tampered ' . $field);
    check(request($index, ['op' => 'lookup', 'id' => $mutationId])['status'] === 404,
        'tampered ' . $field . ' creates no publisher');
}
foreach ([
    ['authexpired', 'is expired'],
    ['authlife', 'has a non-60-second lifetime'],
    ['authfuture', 'has a future issued_at beyond tolerance'],
] as $validityIndex => [$validityId, $validityLabel]) {
    $invalid = registration($index, $validityId, 41102);
    if ($validityIndex === 0) {
        $invalid['expires_at'] = $invalid['issued_at'] - 1;
    } elseif ($validityIndex === 1) {
        $invalid['expires_at'] = $invalid['issued_at'] + 61;
    } else {
        $invalid['issued_at'] = time() + 10;
        $invalid['expires_at'] = $invalid['issued_at'] + 60;
    }
    check(request($index, $invalid)['status'] === 403,
        'challenge that ' . $validityLabel . ' is rejected');
    check(request($index, ['op' => 'lookup', 'id' => $validityId])['status'] === 404,
        $validityLabel . ' challenge creates no publisher');
}

$orderDb = new PDO('sqlite::memory:');
$orderDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$order = new Redp2pIndex($orderDb, ['pass' => '', 'pow' => 8]);
$orderChallenge = body(request($order, ['op' => 'challenge', 'id' => 'orderpub']));
$orderNonce = hex2bin($orderChallenge['nonce']);
$orderIdBytes = pack('n', strlen('orderpub')) . 'orderpub';
$failing = str_repeat("\x00", 8);
while (leadingZeroBits(hash('sha256', 'REDP2P-POW' . $orderNonce .
    u64be($orderChallenge['issued_at']) . u64be($orderChallenge['expires_at']) .
    $orderIdBytes . $failing, true)) >= $orderChallenge['bits']) {
    for ($i = 7; $i >= 0; $i--) {
        $next = (ord($failing[$i]) + 1) & 0xff;
        $failing[$i] = chr($next);
        if ($next !== 0) {
            break;
        }
    }
    if ($i < 0) {
        throw new RuntimeException('failing pow solution exhausted');
    }
}
$orderRequest = registration($order, 'orderpub', 41000);
$orderRequest['pow_solution'] = bin2hex($failing);
$orderRequest['proof'] = str_repeat('0', 64);
$orderRequest['candidates'][0]['addr'] = 'not-an-address';
$orderRejected = request($order, $orderRequest);
check($orderRejected['status'] === 400 && body($orderRejected)['error'] === 'bad_request',
    'register rejects malformed candidates before PoW auth');
check(request($order, ['op' => 'lookup', 'id' => 'orderpub'])['status'] === 404,
    'malformed-candidate register creates no publisher');

$normalSecret = str_pad(dechex(41001), 16, '0', STR_PAD_LEFT);
$heartbeatMessage = "heartbeat\nnormal\n1\n1\n41001\n1\nhost\n192.0.2.1\n41001";
check(hash_hmac('sha256', "heartbeat\nproofpub\n1\n1\n41009\n0", '0123456789abcdef') ===
    'f4e616982629b13857d62c938dfd22a2ed831a4f4d1c735507cc0ffe374a1f1d',
    'canonical heartbeat HMAC vector');
$heartbeat = request($index, [
    'op' => 'heartbeat', 'id' => 'normal', 'seq' => 1,
    'proof' => hash_hmac('sha256', $heartbeatMessage, $normalSecret),
    'proto' => 1, 'udp_port' => 41001,
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 41001]],
]);
check(body($heartbeat)['ok'] === true, 'authenticated heartbeat');
$replay = request($index, [
    'op' => 'heartbeat', 'id' => 'normal', 'seq' => 1,
    'proof' => hash_hmac('sha256', $heartbeatMessage, $normalSecret),
    'proto' => 1, 'udp_port' => 41001,
]);
check($replay['status'] === 403 && body($replay)['error'] === 'invalid_proof', 'heartbeat replay rejection');
$integralHeartbeat = request($index, [
    'op' => 'heartbeat', 'id' => 'normal', 'seq' => 2,
    'proof' => hash_hmac('sha256', "heartbeat\nnormal\n2\n1\n123\n0", $normalSecret),
    'proto' => 1, 'udp_port' => 123.0,
]);
check($integralHeartbeat['status'] === 200 && body($integralHeartbeat)['ok'] === true,
    'integral decimal udp port is accepted');
$minimumPort = request($index, registration($index, 'portone', 1));
check($minimumPort['status'] === 200 && body($minimumPort)['ok'] === true,
    'minimum port is accepted');
$maximumPort = request($index, registration($index, 'portmax', 65535));
check($maximumPort['status'] === 200 && body($maximumPort)['ok'] === true,
    'maximum port is accepted');
foreach ([0, 65536, -1, 123.9] as $invalidIndex => $invalidPort) {
    $invalidRequest = registration($index, 'portinvalid' . $invalidIndex, 123);
    $invalidRequest['udp_port'] = $invalidPort;
    $invalidResponse = request($index, $invalidRequest);
    check($invalidResponse['status'] === 400 && body($invalidResponse)['error'] === 'bad_request',
        'invalid udp port is rejected');
}
$candidateRequest = [
    'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'candidate', 'target_id' => 'normal',
    'session' => 'sessport',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 123.0]],
];
$integralCandidate = request($index, $candidateRequest);
check($integralCandidate['status'] === 200 && body($integralCandidate)['ok'] === true,
    'integral decimal candidate port is accepted');
$candidateRequest['candidates'][0]['port'] = 123.9;
$fractionalCandidate = request($index, $candidateRequest);
check($fractionalCandidate['status'] === 400 && body($fractionalCandidate)['error'] === 'bad_request',
    'fractional candidate port is rejected');
$rawSecret = request($index, ['op' => 'heartbeat', 'id' => 'normal', 'key' => $normalSecret]);
check($rawSecret['status'] === 400 && body($rawSecret)['error'] === 'bad_request', 'raw secret is rejected');
$removed = body(request($index, [
    'op' => 'deregister', 'id' => 'normal', 'seq' => 3,
    'proof' => hash_hmac('sha256', "deregister\nnormal\n3", $normalSecret),
]));
check($removed['ok'] === true, 'normal deregistration');
$replacement = body(request($index, registration($index, 'normal', 41003)));
check($replacement['ok'] === true, 'normal registration after deregistration');

$stale = body(request($index, registration($index, 'expired', 41004)));
check($stale['ok'] === true, 'expired registration setup');
$db->exec("UPDATE redp2p_publishers SET last_seen = 0 WHERE id = 'expired'");
$afterExpiry = body(request($index, registration($index, 'expired', 41005)));
check($afterExpiry['ok'] === true, 'normal registration after expiry');

$vipDb = new PDO('sqlite::memory:');
$vipDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$vip = new Redp2pIndex($vipDb, ['pass' => '', 'vip' => 'vip vip-pass']);
$vipFirst = body(request($vip, registration($vip, 'vip', 41006, 'vip-pass')));
check($vipFirst['ok'] === true, 'VIP registration uses its admission password');

$protectedDb = new PDO('sqlite::memory:');
$protectedDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$protected = new Redp2pIndex($protectedDb, [
    'pass' => 'global-pass', 'vip' => 'vip protected-vip-pass', 'seats' => 2,
]);
$protectedChallenge = body(request($protected, ['op' => 'challenge', 'id' => 'global']));
check(isset($protectedChallenge['pkey']) && strlen($protectedChallenge['pkey']) === 64 &&
    !isset($protectedChallenge['auth_scheme']),
    'challenge contains protected-registration key without auth scheme');
$missingAdmission = request($protected, registration($protected, 'global', 41020));
check($missingAdmission['status'] === 403 && body($missingAdmission)['error'] === 'auth_failed',
    'global password requires an admission proof');
$wrongAdmission = request($protected,
    registration($protected, 'global', 41021, 'wrong-pass'));
check($wrongAdmission['status'] === 403 && body($wrongAdmission)['error'] === 'auth_failed',
    'incorrect global admission proof is rejected');
check(request($protected, ['op' => 'lookup', 'id' => 'global'])['status'] === 404,
    'failed global admission creates no publisher');
$missingVipAdmission = request($protected, registration($protected, 'vip', 41022));
check($missingVipAdmission['status'] === 403 && body($missingVipAdmission)['error'] === 'auth_failed',
    'VIP password requires an admission proof');
$globalForVip = request($protected,
    registration($protected, 'vip', 41023, 'global-pass'));
check($globalForVip['status'] === 403 && body($globalForVip)['error'] === 'auth_failed',
    'global password cannot register a VIP id');
$vipForGlobal = request($protected,
    registration($protected, 'other', 41024, 'protected-vip-pass'));
check($vipForGlobal['status'] === 403 && body($vipForGlobal)['error'] === 'auth_failed',
    'VIP password cannot register a non-VIP id');
$malformedAdmission = registration($protected, 'malformed', 41025, 'global-pass');
$malformedAdmission['access_proof'] = 'not-a-proof';
check(request($protected, $malformedAdmission)['status'] === 400,
    'malformed admission proof is bad_request');
$formatRequest = registration($protected, 'formatproof', 41028, 'global-pass');
foreach ([strtoupper($formatRequest['access_proof']),
    substr($formatRequest['access_proof'], 0, 63) . 'A',
    substr($formatRequest['access_proof'], 0, 63),
    $formatRequest['access_proof'] . '0', str_repeat('g', 64), ''] as $format) {
    $formatRequest['access_proof'] = $format;
    check(request($protected, $formatRequest)['status'] === 400,
        'present non-lowercase admission proof is bad_request');
}
foreach ([123, null, true] as $format) {
    $formatRequest['access_proof'] = $format;
    check(request($protected, $formatRequest)['status'] === 400,
        'non-string admission proof is bad_request');
}
$duplicateJson = json_encode(registration($protected, 'duplicateproof', 41031,
    'global-pass'), JSON_THROW_ON_ERROR);
$duplicateJson = substr($duplicateJson, 0, -1) .
    ',"access_proof":"' . str_repeat('0', 64) . '"}';
check($protected->handle('POST', $duplicateJson)['status'] === 400,
    'duplicate admission proof is bad_request');
$correctGlobal = registration($protected, 'global', 41026, 'global-pass');
check(!str_contains(json_encode($correctGlobal, JSON_THROW_ON_ERROR), 'global-pass'),
    'registration request does not contain the global password');
check(request($protected, $correctGlobal)['status'] === 200,
    'correct global admission proof is accepted');
check(request($protected, registration($protected, 'vip', 41027,
    'protected-vip-pass'))['status'] === 200,
    'correct VIP admission proof is accepted');
$caseGlobal = request($protected, registration($protected, 'caseglobal', 41029,
    'Global-Pass'));
check($caseGlobal['status'] === 403 && body($caseGlobal)['error'] === 'auth_failed',
    'global password casing is exact');
$caseVip = request($protected, registration($protected, 'casevip', 41030,
    'Protected-vip-pass'));
check($caseVip['status'] === 403 && body($caseVip)['error'] === 'auth_failed',
    'VIP password casing is exact');

$path = tempnam(sys_get_temp_dir(), 'redp2p-');
check($path !== false, 'concurrent database path');
$raceDb = new PDO('sqlite:' . $path);
$raceDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$raceDb->setAttribute(PDO::ATTR_TIMEOUT, 2);
new Redp2pIndex($raceDb, ['pass' => '']);
$firstProcess = proc_open([PHP_BINARY, __FILE__, 'worker', $path, '41008'], [
    0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w'],
], $firstPipes);
$secondProcess = proc_open([PHP_BINARY, __FILE__, 'worker', $path, '41009'], [
    0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w'],
], $secondPipes);
check(is_resource($firstProcess) && is_resource($secondProcess), 'concurrent workers start');
fwrite($firstPipes[0], "go\n");
fwrite($secondPipes[0], "go\n");
fclose($firstPipes[0]);
fclose($secondPipes[0]);
$firstResult = json_decode(stream_get_contents($firstPipes[1]), true, 512, JSON_THROW_ON_ERROR);
$secondResult = json_decode(stream_get_contents($secondPipes[1]), true, 512, JSON_THROW_ON_ERROR);
fclose($firstPipes[1]);
fclose($secondPipes[1]);
fclose($firstPipes[2]);
fclose($secondPipes[2]);
check(proc_close($firstProcess) === 0 && proc_close($secondProcess) === 0, 'concurrent workers finish');
$successful = $firstResult['status'] === 200 ? $firstResult : $secondResult;
$failed = $firstResult['status'] === 409 ? $firstResult : $secondResult;
$winnerPort = $firstResult['status'] === 200 ? 41008 : 41009;
check($successful['status'] === 200 && $failed['status'] === 409, 'concurrent registration has one winner');
check(body($failed) === ['ok' => false, 'error' => 'already_registered'], 'concurrent loser has conflict response');
check(!isset(body($failed)['secret']), 'concurrent loser does not disclose secret');
$row = $raceDb->query("SELECT key, proto, udp_port, candidates FROM redp2p_publishers WHERE id = 'concurrent'")->fetch(PDO::FETCH_ASSOC);
check($row !== false && (int)$raceDb->query("SELECT COUNT(*) FROM redp2p_publishers WHERE id = 'concurrent'")->fetchColumn() === 1, 'concurrent registration has one row');
check((string)$row['key'] === str_pad(dechex($winnerPort), 16, '0', STR_PAD_LEFT), 'concurrent winner owns stored secret');
check((int)$row['proto'] === 1 && (int)$row['udp_port'] === $winnerPort, 'concurrent loser does not overwrite winner port');
check((string)$row['candidates'] === json_encode([
    ['type' => 'host', 'addr' => '192.0.2.1', 'port' => $winnerPort],
]), 'concurrent loser does not overwrite winner candidates');
check(body(request(new Redp2pIndex($raceDb, ['pass' => '']), ['op' => 'lookup', 'id' => 'concurrent']))['udp_port'] === $winnerPort, 'concurrent lookup keeps winner metadata');
unlink($path);

$heartbeatPath = tempnam(sys_get_temp_dir(), 'redp2p-hb-');
check($heartbeatPath !== false, 'heartbeat concurrency database path');
$heartbeatDb = new PDO('sqlite:' . $heartbeatPath);
$heartbeatDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$heartbeatIndex = new Redp2pIndex($heartbeatDb, ['pass' => '']);
$heartbeatSecret = str_pad(dechex(42000), 16, '0', STR_PAD_LEFT);
check(body(request($heartbeatIndex, registration($heartbeatIndex, 'heartbeatrace', 42000)))['ok'] === true,
    'heartbeat concurrency registration');
[$firstHeartbeat, $secondHeartbeat] = concurrentRequests($heartbeatPath,
    heartbeatRequest('heartbeatrace', $heartbeatSecret, 1, 42001),
    heartbeatRequest('heartbeatrace', $heartbeatSecret, 1, 42002));
$heartbeatWinner = $firstHeartbeat['status'] === 200 ? $firstHeartbeat : $secondHeartbeat;
$heartbeatLoser = $firstHeartbeat['status'] === 403 ? $firstHeartbeat : $secondHeartbeat;
$winnerPort = $firstHeartbeat['status'] === 200 ? 42001 : 42002;
check($heartbeatWinner['status'] === 200 && $heartbeatLoser['status'] === 403,
    'concurrent heartbeat has one winner');
check(body($heartbeatLoser)['error'] === 'invalid_proof',
    'concurrent heartbeat loser is rejected');
$heartbeatRow = $heartbeatDb->query("SELECT seq, udp_port, candidates FROM redp2p_publishers WHERE id = 'heartbeatrace'")->fetch(PDO::FETCH_ASSOC);
check($heartbeatRow !== false && (int)$heartbeatRow['seq'] === 1,
    'concurrent heartbeat consumes one sequence');
check((int)$heartbeatRow['udp_port'] === $winnerPort,
    'concurrent heartbeat loser does not overwrite endpoint');
$nextHeartbeat = heartbeatRequest('heartbeatrace', $heartbeatSecret, 2, 42003);
check(body(request($heartbeatIndex, $nextHeartbeat))['ok'] === true,
    'higher heartbeat sequence succeeds');
check(request($heartbeatIndex, $nextHeartbeat)['status'] === 403,
    'consumed higher heartbeat sequence is rejected');
unlink($heartbeatPath);

$deregisterPath = tempnam(sys_get_temp_dir(), 'redp2p-del-');
check($deregisterPath !== false, 'deregister concurrency database path');
$deregisterDb = new PDO('sqlite:' . $deregisterPath);
$deregisterDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$deregisterIndex = new Redp2pIndex($deregisterDb, ['pass' => '']);
$deregisterSecret = str_pad(dechex(42010), 16, '0', STR_PAD_LEFT);
check(body(request($deregisterIndex, registration($deregisterIndex, 'deregisterrace', 42010)))['ok'] === true,
    'deregister concurrency registration');
$deregisterRequest = [
    'op' => 'deregister',
    'id' => 'deregisterrace',
    'seq' => 1,
    'proof' => hash_hmac('sha256', "deregister\nderegisterrace\n1", $deregisterSecret),
];
[$firstDeregister, $secondDeregister] = concurrentRequests($deregisterPath,
    $deregisterRequest, $deregisterRequest);
$deregisterWinner = $firstDeregister['status'] === 200 ? $firstDeregister : $secondDeregister;
$deregisterLoser = $firstDeregister['status'] === 403 ? $firstDeregister : $secondDeregister;
check($deregisterWinner['status'] === 200 && $deregisterLoser['status'] === 403,
    'concurrent deregister has one winner');
check($deregisterDb->query("SELECT COUNT(*) FROM redp2p_publishers WHERE id = 'deregisterrace'")->fetchColumn() === 0,
    'concurrent deregister removes one session');
unlink($deregisterPath);

$pollDb = new PDO('sqlite:' . tempnam(sys_get_temp_dir(), 'redp2p-poll-'));
$pollDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$pollIndex = new Redp2pIndex($pollDb, [
    'pass' => '',
    'max_consumers_per_publisher' => 0,
]);
$pollSecret = str_pad(dechex(42020), 16, '0', STR_PAD_LEFT);
check(body(request($pollIndex, registration($pollIndex, 'pollpub', 42020)))['ok'] === true,
    'punch poll publisher registration');
$punchRequest = [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'caller',
    'target_id' => 'pollpub',
    'session' => 'sess3',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 9]],
];
check(body(request($pollIndex, $punchRequest))['ok'] === true,
    'queue punch request');
$missingProof = request($pollIndex, ['op' => 'punch_poll', 'id' => 'pollpub']);
check($missingProof['status'] === 400 && body($missingProof)['error'] === 'bad_request',
    'punch poll missing proof rejected');
$pollOne = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 1,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n1", $pollSecret),
]);
check($pollOne['status'] === 200 && body($pollOne)['calls'][0]['self_id'] === 'caller',
    'valid punch poll retrieves pending call');
$replay = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 1,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n1", $pollSecret),
]);
check($replay['status'] === 403 && body($replay)['error'] === 'invalid_proof',
    'punch poll replay of same sequence rejected');
$punchTwo = [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'caller2',
    'target_id' => 'pollpub',
    'session' => 'sess4',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 10]],
];
check(body(request($pollIndex, $punchTwo))['ok'] === true,
    'queue second punch request');
$wrongProof = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 2,
    'proof' => str_repeat('0', 64),
]);
check($wrongProof['status'] === 403 && body($wrongProof)['error'] === 'invalid_proof',
    'punch poll wrong proof rejected');
$pollTwo = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 2,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n2", $pollSecret),
]);
check($pollTwo['status'] === 200 && body($pollTwo)['calls'][0]['self_id'] === 'caller2',
    'wrong proof did not consume pending call');
$rawSecret = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 3,
    'secret' => $pollSecret,
]);
check($rawSecret['status'] === 400 && body($rawSecret)['error'] === 'bad_request',
    'raw session secret not used as proof');
$pollThree = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 3,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n3", $pollSecret),
]);
check($pollThree['status'] === 200 && body($pollThree)['calls'] === [],
    'consume punch poll');

$queueA = [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'callerA',
    'target_id' => 'pollpub',
    'session' => 'sess5',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 11]],
];
check(body(request($pollIndex, $queueA))['ok'] === true,
    'queue concurrent punch request A');
$queueB = [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'callerB',
    'target_id' => 'pollpub',
    'session' => 'sess6',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 12]],
];
check(body(request($pollIndex, $queueB))['ok'] === true,
    'queue concurrent punch request B');
$wrongFour = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 4,
    'proof' => str_repeat('0', 64),
]);
check($wrongFour['status'] === 403 && body($wrongFour)['error'] === 'invalid_proof',
    'wrong proof consumes none of queued calls');
$pollFour = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 4,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n4", $pollSecret),
]);
check($pollFour['status'] === 200
    && count(body($pollFour)['calls']) === 2
    && body($pollFour)['calls'][0]['self_id'] === 'callerA'
    && body($pollFour)['calls'][1]['self_id'] === 'callerB',
    'valid poll returns multiple pending calls');
$pollFive = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 5,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n5", $pollSecret),
]);
check($pollFive['status'] === 200 && body($pollFive)['calls'] === [],
    'later poll retrieves no remaining calls');

$boundary = [];
for ($i = 0; $i < Redp2pIndex::PUNCH_POLL_MAX + 1; $i++) {
    $boundary[$i] = [
        'op' => 'punch_req', 'udp_port' => 41080,
        'self_id' => 'brem' . $i,
        'target_id' => 'pollpub',
        'session' => 'bsess' . $i,
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 100 + $i]],
    ];
    check(body(request($pollIndex, $boundary[$i]))['ok'] === true,
        'queue boundary punch request ' . $i);
}
$pollSix = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 6,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n6", $pollSecret),
]);
check($pollSix['status'] === 200
    && count(body($pollSix)['calls']) === Redp2pIndex::PUNCH_POLL_MAX,
    'punch poll returns at most PUNCH_POLL_MAX calls');
$pollSeven = request($pollIndex, [
    'op' => 'punch_poll', 'id' => 'pollpub', 'seq' => 7,
    'proof' => hash_hmac('sha256', "punch_poll\npollpub\n7", $pollSecret),
]);
check($pollSeven['status'] === 200
    && count(body($pollSeven)['calls']) === 1
    && body($pollSeven)['calls'][0]['self_id'] === 'brem4',
    'remaining boundary call returns in a later poll');

$rejectNonexistent = request($pollIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'callerX',
    'target_id' => 'ghosttarget',
    'session' => 'sess8',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 50]],
]);
check($rejectNonexistent['status'] === 404
    && body($rejectNonexistent)['error'] === 'not_found',
    'punch_req nonexistent target rejected');

check(body(request($pollIndex, registration($pollIndex, 'expiredpub', 42021)))['ok'] === true,
    'expired target publisher registration');
$pollDb->exec("UPDATE redp2p_publishers SET last_seen = 0 WHERE id = 'expiredpub'");
$rejectExpired = request($pollIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'callerY',
    'target_id' => 'expiredpub',
    'session' => 'sess9',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 51]],
]);
check($rejectExpired['status'] === 404
    && body($rejectExpired)['error'] === 'not_found',
    'punch_req expired target rejected');

$rejectedFill = null;
for ($i = 0; $i < 8; $i++) {
    $rejectedFill = request($pollIndex, [
        'op' => 'punch_req', 'udp_port' => 41080,
        'self_id' => 'fill',
        'target_id' => 'ghosttarget',
        'session' => 'bsess9',
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 52]],
    ]);
}
check($rejectedFill !== null
    && $rejectedFill['status'] === 404
    && body($rejectedFill)['error'] === 'not_found',
    'rejected targets enqueue nothing');

$acceptedAfterRejects = request($pollIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'callerZ',
    'target_id' => 'pollpub',
    'session' => 'sess10',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 53]],
]);
check($acceptedAfterRejects['status'] === 200
    && body($acceptedAfterRejects)['ok'] === true,
    'active target accepted after rejected fills');

$ttlPath = tempnam(sys_get_temp_dir(), 'redp2p-pending-ttl-');
check($ttlPath !== false, 'short pending TTL database path');
$ttlDb = new PDO('sqlite:' . $ttlPath);
$ttlDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
putenv('REDP2P_PENDING_CALL_TTL_S=1');
$shortTtlIndex = new Redp2pIndex($ttlDb, ['pass' => '']);
putenv('REDP2P_PENDING_CALL_TTL_S');
$ttlSecret = str_pad(dechex(42040), 16, '0', STR_PAD_LEFT);
check(body(request($shortTtlIndex, registration($shortTtlIndex, 'ttlpub', 42040)))['ok'] === true,
    'short pending TTL publisher registration');
check(body(request($shortTtlIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'ttlcaller',
    'target_id' => 'ttlpub',
    'session' => 'ttlsess',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 7]],
]))['ok'] === true, 'short pending TTL punch request queued');
check($ttlDb->query('SELECT COUNT(*) FROM redp2p_pending_calls')->fetchColumn() === 1,
    'short pending TTL punch request stored');
$ttlDb->exec('UPDATE redp2p_pending_calls SET ts = ts - 61');
$shortPoll = request($shortTtlIndex, [
    'op' => 'punch_poll', 'id' => 'ttlpub', 'seq' => 1,
    'proof' => hash_hmac('sha256', "punch_poll\nttlpub\n1", $ttlSecret),
]);
check($shortPoll['status'] === 200 && body($shortPoll)['calls'] === [],
    'pending call expired at shortened TTL is not returned');

$publisherLimitPath = tempnam(sys_get_temp_dir(), 'redp2p-pending-publisher-');
check($publisherLimitPath !== false, 'publisher pending-limit database path');
$publisherLimitDb = new PDO('sqlite:' . $publisherLimitPath);
$publisherLimitDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$publisherLimitDb->setAttribute(PDO::ATTR_TIMEOUT, 2);
$publisherLimitOptions = ['pass' => '', 'max_consumers_per_publisher' => 32];
$publisherLimitIndex = new Redp2pIndex($publisherLimitDb,
    $publisherLimitOptions);
check(body(request($publisherLimitIndex, registration($publisherLimitIndex, 'pendingpub', 42050)))['ok'] === true,
    'publisher pending-limit registration');
check(body(request($publisherLimitIndex, registration($publisherLimitIndex, 'quotaOther', 42051)))['ok'] === true,
    'pending-limit isolation publisher registration');
for ($i = 0; $i < 31; $i++) {
    check(body(request($publisherLimitIndex, [
        'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'pcaller' . $i,
        'target_id' => 'pendingpub', 'session' => 'psess' . $i,
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 84]],
    ], $i < 16 ? '127.0.0.10' : '127.0.0.11'))['ok'] === true, 'prefill publisher pending-limit call ' . ($i + 1));
}
[$firstPublisherLimit, $secondPublisherLimit] = concurrentRequests(
    $publisherLimitPath,
    [
        'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'pfirst',
        'target_id' => 'pendingpub', 'session' => 'pfirstsess',
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 85]],
    ],
    [
        'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'psecond',
        'target_id' => 'pendingpub', 'session' => 'psecondsess',
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 86]],
    ],
    $publisherLimitOptions
);
$publisherLimitSuccess = $firstPublisherLimit['status'] === 200
    ? $firstPublisherLimit : $secondPublisherLimit;
$publisherLimitRejected = $firstPublisherLimit['status'] === 429
    ? $firstPublisherLimit : $secondPublisherLimit;
check($publisherLimitSuccess['status'] === 200
    && $publisherLimitRejected['status'] === 429
    && body($publisherLimitRejected)['error'] === 'pending_limit_publisher',
    'concurrent per-publisher limit has one winner');
check((int)$publisherLimitDb->query(
    "SELECT COUNT(*) FROM redp2p_pending_calls WHERE target_id = 'pendingpub'"
)->fetchColumn() === 32, 'per-publisher pending count stays at the hard bound');
$otherAccepted = request($publisherLimitIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'qother',
    'target_id' => 'quotaOther',
    'session' => 'qothersess',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 70]],
]);
check($otherAccepted['status'] === 200 && body($otherAccepted)['ok'] === true,
    'other publisher unaffected by a full consumer window');
$publisherLimitDb->exec(
    "UPDATE redp2p_pending_calls SET ts = 0 WHERE target_id = 'pendingpub'");
$afterExpiry = request($publisherLimitIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'qafter',
    'target_id' => 'pendingpub',
    'session' => 'qsessafter',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 71]],
]);
check($afterExpiry['status'] === 200 && body($afterExpiry)['ok'] === true,
    'expired pending calls release per-publisher capacity');
unlink($publisherLimitPath);

$globalLimitPath = tempnam(sys_get_temp_dir(), 'redp2p-pending-global-');
check($globalLimitPath !== false, 'global pending-limit database path');
$globalLimitDb = new PDO('sqlite:' . $globalLimitPath);
$globalLimitDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$globalLimitDb->setAttribute(PDO::ATTR_TIMEOUT, 2);
$globalLimitOptions = [
    'pass' => '',
    'max_consumers_per_publisher' => Redp2pIndex::MAX_PENDING_CALLS_GLOBAL,
];
$globalLimitIndex = new Redp2pIndex($globalLimitDb, $globalLimitOptions);
check(body(request($globalLimitIndex, registration($globalLimitIndex, 'globalpubone', 42060)))['ok'] === true,
    'global pending-limit first publisher registration');
check(body(request($globalLimitIndex, registration($globalLimitIndex, 'globalpubtwo', 42061)))['ok'] === true,
    'global pending-limit second publisher registration');
check(request($globalLimitIndex, [
    'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'seed',
    'target_id' => 'globalpubone', 'session' => 'seed',
])['status'] === 200, 'seed a pending call through the public handler');
$seed = $globalLimitDb->query(
    'SELECT * FROM redp2p_pending_calls'
)->fetch(PDO::FETCH_ASSOC);
$prefill = $globalLimitDb->prepare(
    'INSERT INTO redp2p_pending_calls'
    . ' (id, self_id, target_id, session, candidates, ts) VALUES (?, ?, ?, ?, ?, ?)'
);
for ($i = 1; $i < 4095; $i++) {
    $prefill->execute(['seed' . $i, $seed['self_id'], $seed['target_id'],
        $seed['session'], $seed['candidates'], $seed['ts']]);
}
[$firstGlobalLimit, $secondGlobalLimit] = concurrentRequests(
    $globalLimitPath,
    [
        'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'gpfirst',
        'target_id' => 'globalpubone', 'session' => 'gpfirstsess',
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 88]],
    ],
    [
        'op' => 'punch_req', 'udp_port' => 41080, 'self_id' => 'gpsecond',
        'target_id' => 'globalpubtwo', 'session' => 'gpsecondsess',
        'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 89]],
    ],
    $globalLimitOptions
);
$globalLimitSuccess = $firstGlobalLimit['status'] === 200
    ? $firstGlobalLimit : $secondGlobalLimit;
$globalLimitRejected = $firstGlobalLimit['status'] === 429
    ? $firstGlobalLimit : $secondGlobalLimit;
check($globalLimitSuccess['status'] === 200
    && $globalLimitRejected['status'] === 429
    && body($globalLimitRejected)['error'] === 'pending_limit_global',
    'concurrent global limit has one winner');
check((int)$globalLimitDb->query(
    'SELECT COUNT(*) FROM redp2p_pending_calls'
)->fetchColumn() === 4096, 'global pending count stays at the hard bound');
check(request($globalLimitIndex, ['op' => 'list'])['status'] === 200,
    'index responds while the global pending limit is full');
$globalFull = request($globalLimitIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'globalfull',
    'target_id' => 'globalpubtwo',
    'session' => 'globalfullsess',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 82]],
]);
check($globalFull['status'] === 429
    && $globalFull['reason'] === 'Too Many Requests'
    && body($globalFull)['error'] === 'pending_limit_global',
    'global limit rejects a publisher below its own limit');
$globalLimitDb->exec('UPDATE redp2p_pending_calls SET ts = 0');
$globalAfterExpiry = request($globalLimitIndex, [
    'op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'afterexpiry',
    'target_id' => 'globalpubtwo',
    'session' => 'afterexpirysess',
    'candidates' => [['type' => 'host', 'addr' => '192.0.2.1', 'port' => 83]],
]);
check($globalAfterExpiry['status'] === 200 && body($globalAfterExpiry)['ok'] === true,
    'expired pending calls release global capacity');
unlink($globalLimitPath);

$ratePath = tempnam(sys_get_temp_dir(), 'redp2p-rate-');
check($ratePath !== false, 'rate database path');
$rateDb = new PDO('sqlite:' . $ratePath);
$rateDb->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$rateOptions = ['pass' => '', 'max_consumers_per_publisher' => 64];
$rateIndex = new Redp2pIndex($rateDb, $rateOptions);
$rateOne = registration($rateIndex, 'rateone', 42100);
foreach ([$rateOne, registration($rateIndex, 'ratetwo', 42101),
    registration($rateIndex, 'ratethree', 42102)] as $reg) {
    check(request($rateIndex, $reg)['status'] === 200, 'register rate target');
}
$ratePunch = ['op' => 'punch_req', 'udp_port' => 41080,
    'self_id' => 'ratecaller', 'target_id' => 'rateone', 'session' => 'ratesess'];
for ($i = 0; $i < 20; $i++) {
    check(request($rateIndex, $ratePunch, '127.0.0.20')['status'] === 200,
        'source burst reaches capacity');
}
$otherPunch = $ratePunch;
$otherPunch['target_id'] = 'ratetwo';
$rateIndex = new Redp2pIndex($rateDb, $rateOptions);
$limited = $rateIndex->handle('POST', json_encode($otherPunch),
    ['X-Forwarded-For' => '192.0.2.200'], '::ffff:127.0.0.20');
check($limited['status'] === 429 && body($limited)['error'] === 'rate_limited',
    'source state persists and mapped IP or forwarded header cannot bypass it');
for ($i = 0; $i < 20; $i++) {
    check(request($rateIndex, $ratePunch, '127.0.0.21')['status'] === 200,
        'second source reaches target capacity');
}
$limited = request($rateIndex, $ratePunch, '127.0.0.22');
check($limited['status'] === 429 && body($limited)['error'] === 'rate_limited',
    'target rate spans independent sources');
for ($i = 0; $i < 20; $i++) {
    check(request($rateIndex, $otherPunch, '127.0.0.22')['status'] === 200,
        'target rejection consumes no source credit');
}
for ($i = 0; $i < 20; $i++) {
    check(request($rateIndex, $otherPunch, '127.0.0.23')['status'] === 200,
        'source rejection consumes no target credit');
}
$rateDb->exec('UPDATE redp2p_pending_calls SET ts = 0');
$limited = request($rateIndex, $otherPunch, '127.0.0.24');
check($limited['status'] === 429 && body($limited)['error'] === 'rate_limited',
    'isolated target capacity enforced');
check((int)$rateDb->query('SELECT COUNT(*) FROM redp2p_pending_calls')
    ->fetchColumn() === 80, 'rate rejection neither enqueues nor prunes calls');
$restore = $rateDb->prepare('UPDATE redp2p_pending_calls SET ts = ?');
$restore->execute([time()]);
usleep(250000);
check(request($rateIndex, $ratePunch, '127.0.0.20')['status'] === 200,
    'source and target refill permit another request');
check((int)$rateDb->query('SELECT COUNT(*) FROM redp2p_pending_calls')
    ->fetchColumn() === 81, 'only accepted requests occupy pending capacity');
$sequence = 1;
$deregister = ['op' => 'deregister', 'id' => 'rateone', 'seq' => $sequence,
    'proof' => hash_hmac('sha256', "deregister\nrateone\n1",
        str_pad(dechex(42100), 16, '0', STR_PAD_LEFT))];
check(request($rateIndex, $deregister)['status'] === 200,
    'deregister removes target rate state');
check(request($rateIndex, registration($rateIndex, 'rateone', 42103))['status']
    === 200, 'same publisher id can acquire a fresh target bucket');
$rateDb->exec("DELETE FROM redp2p_pending_calls WHERE target_id = 'rateone'");
for ($i = 0; $i < 40; $i++) {
    check(request($rateIndex, $ratePunch, $i < 20 ? '127.0.0.25' :
        '127.0.0.26')['status'] === 200, 'new target starts at full capacity');
}
$expiredAddress = bin2hex(inet_pton('127.0.0.20'));
$expire = $rateDb->prepare(
    'UPDATE redp2p_rate_sources SET updated_ms = 0 WHERE address = ?'
);
$expire->execute([$expiredAddress]);
$ratePunch['target_id'] = 'ratethree';
check(request($rateIndex, $ratePunch, '2001:db8::1')['status'] === 200,
    'new source reclaims expired source entries');
$findExpired = $rateDb->prepare(
    'SELECT COUNT(*) FROM redp2p_rate_sources WHERE address = ?'
);
$findExpired->execute([$expiredAddress]);
check((int)$findExpired->fetchColumn() === 0, 'inactive source was removed');
$findExpired->closeCursor();
for ($i = 0; $i < 19; $i++) {
    check(request($rateIndex, $ratePunch, '2001:0db8:0:0:0:0:0:1')['status'] ===
        200, 'IPv6 text variants share one bucket');
}
check(request($rateIndex, $ratePunch, '2001:db8::1')['status'] === 429,
    'normalized IPv6 source capacity enforced');
$ratePunch['target_id'] = 'rateconcurrent';
check(request($rateIndex, registration($rateIndex, 'rateconcurrent', 42104))
    ['status'] === 200, 'register concurrent rate target');
for ($i = 0; $i < 19; $i++) {
    check(request($rateIndex, $ratePunch)['status'] === 200,
        'prepare one remaining source token');
}
[$rateFirst, $rateSecond] = concurrentRequests($ratePath, $ratePunch,
    $ratePunch, $rateOptions);
$statuses = [$rateFirst['status'], $rateSecond['status']];
sort($statuses);
check($statuses === [200, 429], 'concurrent requests cannot double-spend credit');
$rateFailure = $rateFirst['status'] === 429 ? $rateFirst : $rateSecond;
check(body($rateFailure)['error'] === 'rate_limited',
    'concurrent loser is rate limited');
unlink($ratePath);

echo "PHP index tests passed\n";
