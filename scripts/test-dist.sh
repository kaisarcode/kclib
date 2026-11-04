#!/usr/bin/env bash
# test-dist.sh - Distribution layout regression tests.
# Summary: Verifies shared targets, incremental updates and stale file cleanup.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

fail() {
    echo "[FAIL] $1" >&2
    exit 1
}

check() {
    [ "$1" = "$2" ] || fail "$3: expected [$2], got [$1]"
}

make_library() {
    local name="$1" root="$WORK_DIR/proj/lib$name.c"
    mkdir -p "$root/src" "$root/bin/linux/x86_64"
    : > "$root/Makefile"
    printf '{"name":"%s","description":"Synthetic distribution test"}\n' "$name" > "$root/manifest.json"
    printf '/* %s header */\n' "$name" > "$root/src/lib$name.h"
    printf 'Shared LGPLv3 fixture\n' > "$root/LICENSE"
    printf 'Shared GPLv3 fixture\n' > "$root/COPYING"
    printf '%s native artifact\n' "$name" > "$root/bin/linux/x86_64/lib$name.so"
    (cd "$root/bin/linux/x86_64" && sha256sum "lib$name.so" > SHA256SUMS)
}

mkdir -p "$WORK_DIR/scripts"
cp "$SCRIPT_DIR/dist.sh" "$WORK_DIR/scripts/dist.sh"
make_library alpha
make_library beta

mkdir -p "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32"
printf 'synthetic wasm bytes' > "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32/libalpha.wasm"
printf 'export default 1;\n' > "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32/libalpha.js"
printf 'export default 2;\n' > "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32/libalpha.mjs"
printf '{"type":"module"}\n' > "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32/package.json"
(cd "$WORK_DIR/proj/libalpha.c/bin/wasm/wasm32" &&
    sha256sum libalpha.wasm libalpha.js libalpha.mjs package.json > SHA256SUMS)

bash "$WORK_DIR/scripts/dist.sh" all > "$WORK_DIR/result.log"

D="$WORK_DIR/dist"
for artifact in libalpha.so libalpha.h libbeta.so libbeta.h LICENSE COPYING SHA256SUMS; do
    [ -f "$D/linux-x86_64/$artifact" ] || fail "Missing native artifact: $artifact"
done
for artifact in libalpha.wasm libalpha.js libalpha.mjs libalpha.h package.json LICENSE COPYING SHA256SUMS; do
    [ -f "$D/wasm-wasm32/$artifact" ] || fail "Missing WASM artifact: $artifact"
done
[ -f "$D/libalpha.json" ] || fail "Missing alpha project manifest"
[ -f "$D/libbeta.json" ] || fail "Missing beta project manifest"
[ ! -e "$D/linux-x86_64/README.md" ] || fail "README published unexpectedly"
[ ! -e "$D/libalpha.c" ] || fail "Legacy project directory created"
[ ! -e "$D/libbeta.c" ] || fail "Legacy project directory created"
(cd "$D/linux-x86_64" && sha256sum -c SHA256SUMS >/dev/null)
(cd "$D/wasm-wasm32" && sha256sum -c SHA256SUMS >/dev/null)
check "$(find "$D/linux-x86_64" -name LICENSE | wc -l)" 1 "Shared LICENSE count"
check "$(find "$D/linux-x86_64" -name COPYING | wc -l)" 1 "Shared COPYING count"
echo "[1/5] [PASS] target layout: flat artifacts and shared metadata"

cp "$D/linux-x86_64/libbeta.so" "$WORK_DIR/beta-before"
printf 'alpha updated\n' > "$WORK_DIR/proj/libalpha.c/bin/linux/x86_64/libalpha.so"
(cd "$WORK_DIR/proj/libalpha.c/bin/linux/x86_64" &&
    sha256sum libalpha.so > SHA256SUMS)
bash "$WORK_DIR/scripts/dist.sh" alpha > "$WORK_DIR/result.log"
cmp -s "$D/linux-x86_64/libbeta.so" "$WORK_DIR/beta-before" ||
    fail "Partial dist changed another library"
grep -q 'alpha updated' "$D/linux-x86_64/libalpha.so" ||
    fail "Partial dist did not update alpha"
(cd "$D/linux-x86_64" && sha256sum -c SHA256SUMS >/dev/null)
echo "[2/5] [PASS] incremental update: preserves other libraries"

rm -rf "$WORK_DIR/proj/libalpha.c/bin/wasm"
bash "$WORK_DIR/scripts/dist.sh" alpha > "$WORK_DIR/result.log"
[ ! -d "$D/wasm-wasm32" ] || fail "Removed WASM target was not pruned"
[ -f "$D/linux-x86_64/libbeta.so" ] || fail "Pruning removed beta"
echo "[3/5] [PASS] stale target removal: preserves remaining libraries"

printf 'Different license\n' > "$WORK_DIR/proj/libbeta.c/LICENSE"
if bash "$WORK_DIR/scripts/dist.sh" beta > "$WORK_DIR/result.log" 2>&1; then
    fail "Conflicting shared license was accepted"
fi
[ -f "$D/linux-x86_64/libalpha.so" ] || fail "License conflict removed alpha"
grep -q '^Shared LGPLv3 fixture$' "$D/linux-x86_64/LICENSE" ||
    fail "License conflict overwrote common LICENSE"
echo "[4/5] [PASS] conflict prevention: shared LICENSE is immutable"

printf 'Shared LGPLv3 fixture\n' > "$WORK_DIR/proj/libbeta.c/LICENSE"
rm -rf "$WORK_DIR/proj/libbeta.c"
bash "$WORK_DIR/scripts/dist.sh" all > "$WORK_DIR/result.log"
[ ! -f "$D/linux-x86_64/libbeta.so" ] || fail "Orphan library artifact remains"
[ ! -f "$D/libbeta.json" ] || fail "Orphan library manifest remains"
[ -f "$D/linux-x86_64/libalpha.so" ] || fail "Orphan pruning removed alpha"
(cd "$D/linux-x86_64" && sha256sum -c SHA256SUMS >/dev/null)
echo "[5/5] [PASS] orphan removal: other projects remain intact"
echo "5 passed, 0 failed"