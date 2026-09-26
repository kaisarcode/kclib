#!/bin/sh
# kclib cdef tool
# Summary: Converts one public kclib header into LuaJIT ffi.cdef declarations.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 path/to/libNAME.h PLATFORM" >&2
}

[ "$#" -eq 2 ] || {
    usage
    exit 1
}

HEADER=$1
PLATFORM=$2

[ -f "$HEADER" ] || {
    echo "error: header not found: $HEADER" >&2
    exit 1
}

TMPDIR_ROOT=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPDIR_ROOT/kclib-cdef.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

BODY="$WORK/body.h"
DEFINES="$WORK/defines.txt"
OUT="$WORK/out.cdef"

: > "$BODY"
: > "$DEFINES"

awk -v defs="$DEFINES" -v platform="$PLATFORM" \
    -f "$SCRIPT_DIR/cdef-header.awk" "$HEADER" > "$BODY"

if [ -s "$DEFINES" ]; then
    echo "enum {" > "$OUT"
    awk -F '\t' -f "$SCRIPT_DIR/cdef-defines.awk" "$DEFINES" >> "$OUT"
    echo >> "$OUT"
else
    : > "$OUT"
fi

if grep -Eq '(^|[^A-Za-z0-9_])sig_atomic_t([^A-Za-z0-9_]|$)' "$BODY"; then
    {
        echo "typedef int sig_atomic_t;"
        echo
    } >> "$OUT"
fi

awk -f "$SCRIPT_DIR/cdef-time.awk" "$BODY" >> "$OUT"

OMITTED=$(sed -n \
    's@^/\* KC_CDEF_OMITTED_TIME_T:\([A-Za-z_][A-Za-z0-9_]*\) \*/$@\1@p' \
    "$OUT")

for NAME in $OMITTED; do
    if grep -v "KC_CDEF_OMITTED_TIME_T:$NAME" "$OUT" |
        grep -Eq "(^|[^A-Za-z0-9_])$NAME([^A-Za-z0-9_]|$)"; then
        echo "error: target-dependent type $NAME is used by the public FFI surface" >&2
        exit 1
    fi
done

awk -f "$SCRIPT_DIR/cdef-comments.awk" "$OUT"
