<?php
/**
 * test.php - REDP2P browser integration tests.
 * Summary: Starts a local PHP server and runs automated browser WebRTC tests.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);

const REDP2P_TEST_HOST = '127.0.0.1';
const REDP2P_TEST_PASS = '1234';
const REDP2P_TEST_TIMEOUT_S = 240;

/**
 * Returns the platform null device.
 * @return string Null device path.
 */
function testNullDevice(): string
{
    return PHP_OS_FAMILY === 'Windows' ? 'NUL' : '/dev/null';
}

/**
 * Creates one private temporary directory.
 * @return string Temporary directory path.
 */
function testTempDir(): string
{
    $path = rtrim(sys_get_temp_dir(), DIRECTORY_SEPARATOR)
        . DIRECTORY_SEPARATOR . 'redp2p-web-' . bin2hex(random_bytes(8));
    if (!mkdir($path, 0700, true) && !is_dir($path)) {
        throw new RuntimeException('Unable to create temporary directory');
    }
    return $path;
}

/**
 * Reserves and returns one currently free TCP port.
 * @return int Free port number.
 */
function testFreePort(): int
{
    $errno = 0;
    $errstr = '';
    $socket = @stream_socket_server(
        'tcp://' . REDP2P_TEST_HOST . ':0',
        $errno,
        $errstr
    );
    if ($socket === false) {
        throw new RuntimeException('Unable to find a free TCP port: ' . $errstr);
    }
    $name = stream_socket_get_name($socket, false);
    fclose($socket);
    if (!is_string($name) || !preg_match('/:(\d+)$/', $name, $match)) {
        throw new RuntimeException('Unable to read the temporary TCP port');
    }
    return (int)$match[1];
}

/**
 * Waits until the temporary PHP server accepts connections.
 * @param resource $process PHP server process.
 * @param int $port Server port.
 * @param string $logPath Server log path.
 * @return void
 */
function testWaitServer($process, int $port, string $logPath): void
{
    for ($attempt = 0; $attempt < 100; $attempt++) {
        $status = proc_get_status($process);
        if (!($status['running'] ?? false)) {
            $log = @file_get_contents($logPath);
            throw new RuntimeException(
                'PHP test server stopped unexpectedly'
                . ($log === false || $log === '' ? '' : "\n" . trim($log))
            );
        }
        $errno = 0;
        $errstr = '';
        $socket = @fsockopen(
            REDP2P_TEST_HOST,
            $port,
            $errno,
            $errstr,
            0.1
        );
        if ($socket !== false) {
            fclose($socket);
            return;
        }
        usleep(50000);
    }
    throw new RuntimeException('Timed out waiting for the PHP test server');
}

/**
 * Opens the test URL with the platform default browser.
 * @return bool Whether a browser launcher started successfully.
 */
function testOpenBrowser(string $url): bool
{
    $null = testNullDevice();
    $descriptors = [
        0 => ['file', $null, 'r'],
        1 => ['file', $null, 'a'],
        2 => ['file', $null, 'a'],
    ];

    if (PHP_OS_FAMILY === 'Windows') {
        $command = ['cmd', '/c', 'start', '', $url];
        $process = @proc_open($command, $descriptors, $pipes);
        if (!is_resource($process)) return false;
        proc_close($process);
        return true;
    }

    $launcher = PHP_OS_FAMILY === 'Darwin' ? 'open' : 'xdg-open';
    $shell = sprintf(
        '%s %s >/dev/null 2>&1 &',
        $launcher,
        escapeshellarg($url)
    );
    $process = @proc_open(['/bin/sh', '-c', $shell], $descriptors, $pipes);
    if (!is_resource($process)) return false;
    return proc_close($process) === 0;
}

/**
 * Stops the temporary PHP server process.
 * @param resource $process PHP server process.
 * @return void
 */
function testStopServer($process): void
{
    if (!is_resource($process)) return;
    $status = proc_get_status($process);
    if ($status['running'] ?? false) {
        @proc_terminate($process);
        for ($attempt = 0; $attempt < 20; $attempt++) {
            usleep(50000);
            $status = proc_get_status($process);
            if (!($status['running'] ?? false)) break;
        }
        if ($status['running'] ?? false) {
            @proc_terminate($process, 9);
        }
    }
    @proc_close($process);
}

/**
 * Removes files created for one test run.
 * @return void
 */
function testRemoveTempDir(string $path): void
{
    if (!is_dir($path)) return;
    foreach (scandir($path) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..') continue;
        $item = $path . DIRECTORY_SEPARATOR . $entry;
        if (is_file($item) || is_link($item)) @unlink($item);
    }
    @rmdir($path);
}

/**
 * Runs the complete browser integration harness from the CLI.
 * @return int Process exit code.
 */
function testRunCli(): int
{
    if (!function_exists('proc_open')) {
        fwrite(STDERR, "proc_open is required to run browser tests.\n");
        return 1;
    }

    $root = realpath(__DIR__ . '/..');
    if ($root === false) {
        fwrite(STDERR, "Unable to locate libredp2p.c root.\n");
        return 1;
    }

    $tempDir = '';
    $server = null;
    try {
        $tempDir = testTempDir();
        $dbPath = $tempDir . DIRECTORY_SEPARATOR . 'redp2p.sqlite';
        $resultPath = $tempDir . DIRECTORY_SEPARATOR . 'result.json';
        $logPath = $tempDir . DIRECTORY_SEPARATOR . 'php.log';
        $token = bin2hex(random_bytes(16));
        $port = testFreePort();

        putenv('REDP2P_TEST_DB=' . $dbPath);
        putenv('REDP2P_TEST_RESULT=' . $resultPath);
        putenv('REDP2P_TEST_TOKEN=' . $token);

        $descriptors = [
            0 => ['file', testNullDevice(), 'r'],
            1 => ['file', $logPath, 'a'],
            2 => ['file', $logPath, 'a'],
        ];
        $server = @proc_open(
            [PHP_BINARY, '-S', REDP2P_TEST_HOST . ':' . $port, '-t', $root],
            $descriptors,
            $pipes,
            $root
        );
        if (!is_resource($server)) {
            throw new RuntimeException('Unable to start the PHP test server');
        }
        testWaitServer($server, $port, $logPath);

        $url = sprintf(
            'http://%s:%d/tests/test.php',
            REDP2P_TEST_HOST,
            $port
        );
        echo "REDP2P browser tests\n";
        echo "Server: $url\n";
        if (testOpenBrowser($url)) {
            echo "Browser opened. Waiting for tests...\n\n";
        } else {
            echo "Could not open the default browser automatically.\n";
            echo "Open this URL manually: $url\n\n";
        }

        $deadline = microtime(true) + REDP2P_TEST_TIMEOUT_S;
        $result = null;
        while (microtime(true) < $deadline) {
            if (is_file($resultPath)) {
                $raw = @file_get_contents($resultPath);
                $decoded = $raw === false ? null : json_decode($raw, true);
                if (is_array($decoded) && isset(
                    $decoded['passed'],
                    $decoded['failed'],
                    $decoded['results']
                )) {
                    $result = $decoded;
                    break;
                }
            }
            $status = proc_get_status($server);
            if (!($status['running'] ?? false)) {
                $log = @file_get_contents($logPath);
                throw new RuntimeException(
                    'PHP test server stopped unexpectedly'
                    . ($log === false || $log === '' ? '' : "\n" . trim($log))
                );
            }
            usleep(100000);
        }
        if ($result === null) {
            throw new RuntimeException('Timed out waiting for browser test results');
        }

        usleep(250000);
        foreach ($result['results'] as $item) {
            if (!is_array($item)) continue;
            $ok = ($item['ok'] ?? false) === true;
            $name = (string)($item['name'] ?? 'unnamed test');
            $detail = (string)($item['detail'] ?? '');
            printf('[%s] %s%s%s', $ok ? 'PASS' : 'FAIL', $name,
                $detail === '' ? '' : ': ', $detail);
            echo "\n";
        }
        $passed = (int)$result['passed'];
        $failed = (int)$result['failed'];
        echo "\n";
        printf(
            '[%s] %d/%d browser tests passed\n',
            $failed === 0 ? 'SUCCESS' : 'FAIL',
            $passed,
            $passed + $failed
        );
        return $failed === 0 ? 0 : 1;
    } catch (Throwable $error) {
        fwrite(STDERR, '[FAIL] ' . $error->getMessage() . "\n");
        return 1;
    } finally {
        if (is_resource($server)) testStopServer($server);
        putenv('REDP2P_TEST_DB');
        putenv('REDP2P_TEST_RESULT');
        putenv('REDP2P_TEST_TOKEN');
        if ($tempDir !== '') testRemoveTempDir($tempDir);
    }
}

/**
 * Stores the final result posted by the browser harness.
 * @return void
 */
function testServeResult(): void
{
    $expected = getenv('REDP2P_TEST_TOKEN');
    $provided = $_GET['token'] ?? '';
    if (!is_string($provided) ||
        (is_string($expected) && $expected !== '' &&
        !hash_equals($expected, $provided))) {
        http_response_code(403);
        header('Content-Type: application/json');
        echo '{"ok":false}';
        return;
    }

    $raw = file_get_contents('php://input');
    $result = is_string($raw) ? json_decode($raw, true) : null;
    if (!is_array($result) || !is_int($result['passed'] ?? null) ||
        !is_int($result['failed'] ?? null) ||
        !is_array($result['results'] ?? null)) {
        http_response_code(400);
        header('Content-Type: application/json');
        echo '{"ok":false}';
        return;
    }

    $path = getenv('REDP2P_TEST_RESULT');
    if (is_string($path) && $path !== '') {
        $encoded = json_encode($result, JSON_UNESCAPED_SLASHES);
        if ($encoded === false || file_put_contents($path, $encoded, LOCK_EX) === false) {
            http_response_code(500);
            header('Content-Type: application/json');
            echo '{"ok":false}';
            return;
        }
    }
    header('Content-Type: application/json');
    echo '{"ok":true}';
}

/**
 * Serves one REDP2P index protocol request for the browser tests.
 * @return void
 */
function testServeIndex(): void
{
    require_once __DIR__ . '/../imp/redp2p-idx.php';

    $dbPath = getenv('REDP2P_TEST_DB');
    if (!is_string($dbPath) || $dbPath === '') {
        http_response_code(500);
        header('Content-Type: application/json');
        echo '{"ok":false,"error":"test_configuration"}';
        return;
    }
    $server = new \KaisarCode\Redp2pIndex([
        'dsn' => 'sqlite:' . $dbPath,
        'pass' => REDP2P_TEST_PASS,
    ]);
    $server->serve();
}

if (PHP_SAPI === 'cli') {
    exit(testRunCli());
}

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    if (isset($_GET['result'])) {
        testServeResult();
    } else {
        testServeIndex();
    }
    exit;
}

$resultToken = getenv('REDP2P_TEST_TOKEN');
if (!is_string($resultToken)) $resultToken = '';
?>
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>REDP2P Browser Tests</title>
    <style>
        :root {
            color-scheme: dark;
        }

        body {
            background: #0b0d0e;
            color: #d7d7d7;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
                "Liberation Mono", "Courier New", monospace;
            margin: 0;
            padding: 28px;
        }

        #terminal {
            max-width: 1080px;
            margin: 0 auto;
            background: #111416;
            border: 1px solid #30363a;
            border-radius: 8px;
            box-shadow: 0 12px 40px rgba(0, 0, 0, 0.35);
            min-height: 560px;
            overflow: hidden;
        }

        #bar {
            background: #1b1f22;
            border-bottom: 1px solid #30363a;
            padding: 10px 14px;
            color: #9da5aa;
        }

        #screen {
            padding: 18px;
            white-space: pre-wrap;
            overflow-wrap: anywhere;
        }

        .prompt,
        .info {
            color: #9da5aa;
        }

        .pass {
            color: #7ee787;
        }

        .fail {
            color: #ff7b72;
        }

        .summary {
            margin-top: 16px;
            font-weight: bold;
        }
    </style>
</head>
<body>
<div id="terminal">
    <div id="bar">REDP2P browser integration</div>
    <div id="screen"><div class="prompt">$ php test.php</div></div>
</div>

<script type="module">
import RedP2P from "../imp/redp2p.js";

(async () => {
    const screen = document.getElementById("screen");
    const index = new URL(location.pathname, location.href).href;
    const pass = "1234";
    const resultToken = <?= json_encode($resultToken, JSON_UNESCAPED_SLASHES) ?>;
    const random = crypto.getRandomValues(new Uint32Array(2));
    const runId = `${Date.now()}${random[0]}${random[1]}`;
    const publisherIds = ["a", "b", "c"].map(suffix =>
        `web${runId}${suffix}`.slice(0, 63));
    const publishers = new Map();
    const consumers = [];
    const results = [];
    let passed = 0;
    let failed = 0;

    /** Appends one terminal line. */
    function write(text, className = "info") {
        const line = document.createElement("div");
        line.className = className;
        line.textContent = text;
        screen.appendChild(line);
        window.scrollTo(0, document.body.scrollHeight);
    }

    /** Records one test result. */
    function report(ok, name, detail = "") {
        results.push({ok, name, detail});
        if (ok) {
            passed += 1;
        } else {
            failed += 1;
        }
        write(
            `[${ok ? "PASS" : "FAIL"}] ${name}`
                + (detail ? `: ${detail}` : ""),
            ok ? "pass" : "fail"
        );
    }

    /** Executes one test without aborting the remaining suite. */
    async function test(name, callback) {
        try {
            await callback();
            report(true, name);
            return true;
        } catch (error) {
            const detail = error && error.message ? error.message : String(error);
            report(false, name, detail);
            return false;
        }
    }

    /** Throws when a test invariant is false. */
    function assert(condition, message) {
        if (!condition) throw new Error(message);
    }

    /** Converts received binary data to a byte view. */
    function bytes(value) {
        if (value instanceof ArrayBuffer) return new Uint8Array(value);
        if (ArrayBuffer.isView(value)) {
            return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        }
        throw new Error("expected binary payload");
    }

    /** Compares text or binary payloads exactly. */
    function samePayload(expected, actual) {
        if (typeof expected === "string") {
            return typeof actual === "string" && expected === actual;
        }
        const left = bytes(expected);
        const right = bytes(actual);
        if (left.byteLength !== right.byteLength) return false;
        for (let i = 0; i < left.byteLength; i += 1) {
            if (left[i] !== right[i]) return false;
        }
        return true;
    }

    /** Creates deterministic binary test data. */
    function binaryPayload(size, seed = 17) {
        const data = new Uint8Array(size);
        for (let i = 0; i < data.length; i += 1) {
            data[i] = (i * 31 + seed) & 0xff;
        }
        return data;
    }

    /** Reads a JSON response from the local test index. */
    async function indexRequest(body) {
        const response = await fetch(index, {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({version: 0, ...body}),
            cache: "no-store",
            credentials: "omit"
        });
        let value = null;
        try {
            value = await response.json();
        } catch (error) {
            throw new Error(`index returned HTTP ${response.status}`);
        }
        return {response, value};
    }

    /** Removes one disconnected client from its publisher labels. */
    function removePublisherClient(state, client) {
        state.clients.delete(client);
        for (const [label, current] of state.clientsByLabel.entries()) {
            if (current === client) state.clientsByLabel.delete(label);
        }
    }

    /** Starts one echo publisher used by the integration suite. */
    async function createPublisher(id) {
        const state = {
            id,
            pub: null,
            clients: new Set(),
            clientsByLabel: new Map(),
            errors: []
        };
        state.pub = await RedP2P.pub({
            index,
            id,
            pass,
            pollInterval: 100,
            heartbeatInterval: 10000,
            connect(client) {
                state.clients.add(client);
            },
            receive(event) {
                if (typeof event.data === "string" &&
                    event.data.startsWith("__redp2p_bind__:")) {
                    const label = event.data.slice("__redp2p_bind__:".length);
                    state.clientsByLabel.set(label, event.client);
                }
                event.client.respond(event.data);
            },
            disconnect(client) {
                removePublisherClient(state, client);
            },
            error(error, client) {
                state.errors.push(error);
                if (client) removePublisherClient(state, client);
            }
        });
        publishers.set(id, state);
        return state;
    }

    /** Rejects all pending receives for one consumer. */
    function rejectConsumerWaiters(state, error) {
        while (state.waiters.length > 0) {
            const waiter = state.waiters.shift();
            clearTimeout(waiter.timer);
            waiter.reject(error);
        }
    }

    /** Waits for the next application message on one consumer. */
    function nextConsumerMessage(state, timeoutMs = 15000) {
        if (state.buffered.length > 0) {
            return Promise.resolve(state.buffered.shift());
        }
        return new Promise((resolve, reject) => {
            const waiter = {resolve, reject, timer: null};
            waiter.timer = setTimeout(() => {
                const index = state.waiters.indexOf(waiter);
                if (index >= 0) state.waiters.splice(index, 1);
                reject(new Error(`${state.label} receive timed out`));
            }, timeoutMs);
            state.waiters.push(waiter);
        });
    }

    /** Sends one message and waits for the publisher echo. */
    async function roundtrip(state, payload, timeoutMs = 15000) {
        const reply = nextConsumerMessage(state, timeoutMs);
        try {
            state.con.send(payload);
        } catch (error) {
            const waiter = state.waiters.shift();
            if (waiter) {
                clearTimeout(waiter.timer);
                waiter.reject(error);
            }
            throw error;
        }
        return reply;
    }

    /** Connects one labeled consumer and binds it to its publisher client. */
    async function createConsumer(publisherId, label) {
        const state = {
            publisherId,
            label,
            con: null,
            waiters: [],
            buffered: [],
            errors: [],
            closed: false
        };
        state.con = await RedP2P.con({
            index,
            id: publisherId,
            pollInterval: 100,
            timeout: 20000,
            receive(data) {
                const waiter = state.waiters.shift();
                if (waiter) {
                    clearTimeout(waiter.timer);
                    waiter.resolve(data);
                } else {
                    state.buffered.push(data);
                }
            },
            disconnect() {
                state.closed = true;
                rejectConsumerWaiters(
                    state,
                    new Error(`${state.label} disconnected`)
                );
            },
            error(error) {
                state.errors.push(error);
            }
        });
        consumers.push(state);
        const bind = `__redp2p_bind__:${label}`;
        const echoed = await roundtrip(state, bind);
        assert(echoed === bind, `${label} bind echo mismatch`);
        const publisher = publishers.get(publisherId);
        assert(publisher && publisher.clientsByLabel.has(label),
            `${label} publisher client was not bound`);
        return state;
    }

    /** Closes one consumer capability once. */
    function closeConsumer(state) {
        if (!state || state.closed) return;
        state.closed = true;
        state.con.close();
        rejectConsumerWaiters(state, new Error(`${state.label} closed`));
    }

    /** Returns currently open consumers. */
    function activeConsumers() {
        return consumers.filter(state => !state.closed);
    }

    /** Delivers one payload directly from publisher to consumer. */
    async function publisherToConsumer(state, payload) {
        const publisher = publishers.get(state.publisherId);
        assert(publisher, `${state.label} publisher unavailable`);
        const client = publisher.clientsByLabel.get(state.label);
        assert(client, `${state.label} publisher client unavailable`);
        const received = nextConsumerMessage(state, 15000);
        client.respond(payload);
        return received;
    }

    /** Closes every resource still owned by the test page. */
    async function cleanup() {
        for (const state of activeConsumers()) {
            try {
                closeConsumer(state);
            } catch (error) {
            }
        }
        const closing = [];
        for (const state of publishers.values()) {
            if (!state.pub) continue;
            closing.push(state.pub.close().catch(() => undefined));
            state.pub = null;
        }
        await Promise.all(closing);
    }

    /** Posts the final suite result back to the CLI harness. */
    async function submitResult() {
        const endpoint = new URL(location.pathname, location.href);
        endpoint.searchParams.set("result", "1");
        endpoint.searchParams.set("token", resultToken);
        try {
            await fetch(endpoint, {
                method: "POST",
                headers: {"Content-Type": "application/json"},
                body: JSON.stringify({passed, failed, results}),
                cache: "no-store"
            });
        } catch (error) {
            write(`[FAIL] unable to report result: ${error.message || error}`, "fail");
        }
    }

    write(`index: ${index}`);
    write("running automated WebRTC publisher/consumer permutations...");
    write("");

    try {
        await test("browser capabilities", async () => {
            assert(typeof RTCPeerConnection === "function",
                "RTCPeerConnection unavailable");
            assert(globalThis.crypto && crypto.subtle,
                "Web Crypto unavailable");
            assert(typeof fetch === "function", "Fetch unavailable");
            assert(globalThis.RedP2P === RedP2P &&
                typeof RedP2P.pub === "function" &&
                typeof RedP2P.con === "function", "REDP2P runtime unavailable");
        });

        await test("index endpoint", async () => {
            const {response, value} = await indexRequest({op: "list"});
            assert(response.ok, `HTTP ${response.status}`);
            assert(value && value.ok === true, value && value.error
                ? value.error : "invalid index response");
            assert(Array.isArray(value.ids), "index list missing ids");
        });

        await test("index password rejects invalid publisher", async () => {
            let badPublisher = null;
            let rejected = false;
            try {
                badPublisher = await RedP2P.pub({
                    index,
                    id: `bad${runId}`.slice(0, 63),
                    pass: "wrong",
                    pollInterval: 100
                });
            } catch (error) {
                rejected = error &&
                    (error.code === "auth_failed" || error.status === 403);
            } finally {
                if (badPublisher) await badPublisher.close();
            }
            assert(rejected, "invalid index password was accepted");
        });

        await test("three publishers register concurrently", async () => {
            const settled = await Promise.allSettled(
                publisherIds.map(id => createPublisher(id))
            );
            const errors = settled.filter(item => item.status === "rejected");
            assert(errors.length === 0,
                `${errors.length} publisher registration(s) failed`);
            assert(publishers.size === 3,
                `expected 3 publishers, got ${publishers.size}`);
        });

        await test("index lists all publishers", async () => {
            const {response, value} = await indexRequest({op: "list"});
            assert(response.ok && value && value.ok === true,
                value && value.error ? value.error : `HTTP ${response.status}`);
            for (const id of publisherIds) {
                assert(value.ids.includes(id), `publisher ${id} missing from list`);
            }
        });

        await test("all publisher lookups report rtc", async () => {
            for (const id of publisherIds) {
                const {response, value} = await indexRequest({op: "lookup", id});
                assert(response.ok && value && value.ok === true,
                    `lookup failed for ${id}`);
                assert(value.id === id, `lookup id mismatch for ${id}`);
                assert(value.transport === "rtc",
                    `publisher ${id} transport is not rtc`);
            }
        });

        await test("one consumer per publisher connects concurrently", async () => {
            const settled = await Promise.allSettled([
                createConsumer(publisherIds[0], "a1"),
                createConsumer(publisherIds[1], "b1"),
                createConsumer(publisherIds[2], "c1")
            ]);
            const errors = settled.filter(item => item.status === "rejected");
            assert(errors.length === 0,
                `${errors.length} initial consumer connection(s) failed`);
        });

        await test("multiple consumers share one publisher", async () => {
            const settled = await Promise.allSettled([
                createConsumer(publisherIds[0], "a2"),
                createConsumer(publisherIds[0], "a3")
            ]);
            const errors = settled.filter(item => item.status === "rejected");
            assert(errors.length === 0,
                `${errors.length} shared-publisher connection(s) failed`);
            const publisher = publishers.get(publisherIds[0]);
            assert(publisher && publisher.clientsByLabel.size >= 3,
                "publisher A did not keep three consumers");
        });

        await test("second publisher handles multiple consumers", async () => {
            await createConsumer(publisherIds[1], "b2");
            const publisher = publishers.get(publisherIds[1]);
            assert(publisher && publisher.clientsByLabel.size >= 2,
                "publisher B did not keep two consumers");
        });

        await test("all consumer -> publisher text roundtrips", async () => {
            const active = activeConsumers();
            assert(active.length === 6,
                `expected 6 consumers, got ${active.length}`);
            await Promise.all(active.map(async (state, index) => {
                const payload = `hello-${state.label}-${index}`;
                const received = await roundtrip(state, payload);
                assert(samePayload(payload, received),
                    `${state.label} text roundtrip mismatch`);
            }));
        });

        await test("all publisher -> consumer deliveries", async () => {
            const active = activeConsumers();
            await Promise.all(active.map(async (state, index) => {
                const payload = `reverse-${state.label}-${index}`;
                const received = await publisherToConsumer(state, payload);
                assert(samePayload(payload, received),
                    `${state.label} reverse delivery mismatch`);
            }));
        });

        for (const size of [1, 1024, 65536, 262144]) {
            await test(`binary roundtrip ${size} bytes`, async () => {
                const state = activeConsumers()[0];
                assert(state, "no consumer available for binary test");
                const payload = binaryPayload(size, size & 0xff);
                const received = await roundtrip(state, payload, 20000);
                assert(samePayload(payload, received),
                    `${size}-byte payload mismatch`);
            });
        }

        await test("mixed binary sizes across concurrent consumers", async () => {
            const active = activeConsumers();
            const sizes = [7, 257, 4096, 16384, 65536, 131072];
            assert(active.length === sizes.length,
                `expected ${sizes.length} active consumers`);
            await Promise.all(active.map(async (state, index) => {
                const payload = binaryPayload(sizes[index], index + 1);
                const received = await roundtrip(state, payload, 20000);
                assert(samePayload(payload, received),
                    `${state.label} mixed payload mismatch`);
            }));
        });

        await test("six persistent streams transfer concurrently", async () => {
            const active = activeConsumers();
            assert(active.length === 6,
                `expected 6 active consumers, got ${active.length}`);
            await Promise.all(active.map(async state => {
                for (let round = 0; round < 10; round += 1) {
                    const payload = `${state.label}-round-${round}`;
                    const received = await roundtrip(state, payload);
                    assert(samePayload(payload, received),
                        `${state.label} round ${round} mismatch`);
                }
            }));
        });

        await test("consumer reconnects to existing publisher", async () => {
            const original = activeConsumers().find(state => state.label === "a1");
            assert(original, "consumer a1 unavailable");
            closeConsumer(original);
            await new Promise(resolve => setTimeout(resolve, 100));
            const replacement = await createConsumer(publisherIds[0], "a1r");
            const payload = "reconnected";
            const received = await roundtrip(replacement, payload);
            assert(samePayload(payload, received),
                "reconnected consumer roundtrip mismatch");
        });

        await test("all consumers close cleanly", async () => {
            const active = activeConsumers();
            assert(active.length === 6,
                `expected 6 active consumers before close, got ${active.length}`);
            for (const state of active) closeConsumer(state);
            await new Promise(resolve => setTimeout(resolve, 100));
            assert(activeConsumers().length === 0,
                "consumer remained open after close");
        });

        await test("all publishers deregister concurrently", async () => {
            const closing = [];
            for (const state of publishers.values()) {
                if (!state.pub) continue;
                closing.push(state.pub.close());
            }
            await Promise.all(closing);
            for (const state of publishers.values()) state.pub = null;
        });

        await test("deregistered publishers disappear from index", async () => {
            for (const id of publisherIds) {
                const {response, value} = await indexRequest({op: "lookup", id});
                assert(response.status === 404,
                    `publisher ${id} returned HTTP ${response.status}`);
                assert(value && value.error === "not_found",
                    `publisher ${id} remained registered`);
            }
        });
    } finally {
        await cleanup();
    }

    write("");
    const total = passed + failed;
    const summary = `${passed}/${total} passed`;
    write(
        `[${failed === 0 ? "SUCCESS" : "FAIL"}] ${summary}`,
        failed === 0 ? "pass summary" : "fail summary"
    );
    document.title = failed === 0
        ? "PASS - REDP2P Browser Tests"
        : "FAIL - REDP2P Browser Tests";
    await submitResult();
})();
</script>
</body>
</html>
