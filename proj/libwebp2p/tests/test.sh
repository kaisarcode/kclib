#!/bin/bash
# REDP2P browser integration test
# Summary: Runs the local PHP index and browser-to-browser transport test.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

HOST="${TEST_HOST:-127.0.0.1}"
PORT="${TEST_PORT:-8088}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMP_DIR=""
SERVER_PID=""

# Removes generated test resources.
# @return 0 on success.
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    if [ -n "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}

# Stops the test when a required dependency is unavailable.
# @return 0 on success.
check_dependencies() {
    command -v php >/dev/null 2>&1 || {
        echo "PHP is required." >&2
        exit 1
    }

    php -r 'exit(extension_loaded("pdo_sqlite") ? 0 : 1);' || {
        echo "PHP PDO SQLite is required." >&2
        exit 1
    }

    php -r 'exit(extension_loaded("sodium") ? 0 : 1);' || {
        echo "PHP Sodium is required." >&2
        exit 1
    }
}

# Runs the PHP index contract tests.
# @return 0 on success.
run_index_test() {
    php "$SCRIPT_DIR/test.php"
}

# Creates the temporary front controller used by PHP's development server.
# @return 0 on success.
create_router() {
    cat > "$TEMP_DIR/router.php" <<'PHP'
<?php
declare(strict_types=1);

$root = getenv('REDP2P_TEST_ROOT');
$dbPath = getenv('REDP2P_TEST_DB');

if (!is_string($root) || $root === '' ||
    !is_string($dbPath) || $dbPath === '') {
    http_response_code(500);
    echo 'test configuration missing';
    return true;
}

$path = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH);
if ($path !== '/index') {
    return false;
}

require $root . '/imp/redp2p-idx.php';

\KaisarCode\Redp2pIndex::serve([
    'dsn' => 'sqlite:' . $dbPath,
]);

return true;
PHP
}

# Waits until the local PHP server accepts connections.
# @return 0 when the server is ready.
wait_server() {
    local attempt

    attempt=0
    while [ "$attempt" -lt 50 ]; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            cat "$TEMP_DIR/php.log" >&2
            return 1
        fi

        # shellcheck disable=SC2016
        if TEST_HOST_INTERNAL="$HOST" TEST_PORT_INTERNAL="$PORT" php -r '$socket = @fsockopen(getenv("TEST_HOST_INTERNAL"), (int)getenv("TEST_PORT_INTERNAL"), $errno, $errstr, 0.1); if (!$socket) { exit(1); } fclose($socket);' 2>/dev/null; then
            return 0
        fi

        attempt=$((attempt + 1))
        sleep 0.1
    done

    echo "Timed out waiting for PHP server." >&2
    return 1
}

# Runs the local REDP2P browser integration environment.
# @return 0 on success.
main() {
    check_dependencies

    TEMP_DIR="$(mktemp -d)"
    trap cleanup EXIT INT TERM

    run_index_test
    create_router

    env \
        -u REDP2P_SEATS \
        -u REDP2P_PASS \
        -u REDP2P_VIP \
        REDP2P_TEST_ROOT="$ROOT_DIR" \
        REDP2P_TEST_DB="$TEMP_DIR/redp2p.sqlite" \
        REDP2P_POW=0 \
        REDP2P_MAX_CONSUMERS_PER_PUBLISHER=32 \
        php -S "$HOST:$PORT" -t "$ROOT_DIR" "$TEMP_DIR/router.php" \
        >"$TEMP_DIR/php.log" 2>&1 &
    SERVER_PID=$!

    wait_server

    echo "REDP2P browser test server running."
    echo
    echo "Open:"
    echo "http://$HOST:$PORT/tests/test.html"
    echo
    echo "Press Ctrl+C to stop."

    wait "$SERVER_PID"
}

main "$@"
