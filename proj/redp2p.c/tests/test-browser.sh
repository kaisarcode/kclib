#!/bin/bash
# REDP2P browser integration test server.
# Summary: Runs the local PHP index and serves the browser RTC test page.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

set -e

HOST="${TEST_HOST:-127.0.0.1}"
PORT="${TEST_PORT:-8088}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMP_DIR=""
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [ -n "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}

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

wait_server() {
    local i
    for i in $(seq 1 100); do
        if php -r '$u=$argv[1]; $c=@file_get_contents($u); exit($c === false ? 1 : 0);' \
            "http://$HOST:$PORT/tests/test.html" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.05
    done
    echo "Browser test server did not start." >&2
    if [ -f "$TEMP_DIR/php.log" ]; then
        cat "$TEMP_DIR/php.log" >&2
    fi
    return 1
}

create_router() {
    cat > "$TEMP_DIR/router.php" <<'PHP'
<?php
declare(strict_types=1);

$root = getenv('REDP2P_TEST_ROOT');
$dbPath = getenv('REDP2P_TEST_DB');
if (!is_string($root) || $root === '' || !is_string($dbPath) || $dbPath === '') {
    http_response_code(500);
    echo "test configuration missing";
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

main() {
    check_dependencies

    TEMP_DIR="$(mktemp -d)"
    trap cleanup EXIT INT TERM
    create_router

    REDP2P_TEST_ROOT="$ROOT_DIR" \
    REDP2P_TEST_DB="$TEMP_DIR/redp2p.sqlite" \
    REDP2P_POW=0 \
    REDP2P_SEATS="" \
    REDP2P_PASS="" \
    REDP2P_VIP="" \
    REDP2P_MAX_CONSUMERS_PER_PUBLISHER=32 \
    php -S "$HOST:$PORT" -t "$ROOT_DIR" "$TEMP_DIR/router.php" \
        >"$TEMP_DIR/php.log" 2>&1 &
    SERVER_PID=$!

    wait_server

    echo "REDP2P browser test server running."
    echo
    echo "Open:"
    echo "http://$HOST:$PORT/tests/test.html?index=http://$HOST:$PORT/index"
    echo
    echo "Press Ctrl+C to stop."

    wait "$SERVER_PID"
}

main "$@"
