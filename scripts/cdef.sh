#!/bin/sh
# kclib cdef tool
# Summary: Converts one public kclib header into LuaJIT ffi.cdef declarations.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -eu

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
HEADER_AWK="$WORK/header.awk"
DEFINES_AWK="$WORK/defines.awk"
TIME_AWK="$WORK/time.awk"
COMMENTS_AWK="$WORK/comments.awk"

: > "$BODY"
: > "$DEFINES"

cat > "$HEADER_AWK" <<'AWK'
function trim(s) {
    sub(/^[[:space:]]+/, "", s)
    sub(/[[:space:]]+$/, "", s)
    return s
}

/^[[:space:]]*#[[:space:]]*include[[:space:]]/ { next }
/^[[:space:]]*#[[:space:]]*pragma[[:space:]]/  { next }

/^[[:space:]]*#[[:space:]]*ifndef[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*$/ { next }

/^[[:space:]]*#[[:space:]]*ifdef[[:space:]]+__cplusplus[[:space:]]*$/ { cpp = 1; next }
cpp && /^[[:space:]]*extern[[:space:]]+"C"[[:space:]]*\{[[:space:]]*$/ { next }
cpp && /^[[:space:]]*\}[[:space:]]*$/ { next }
cpp && /^[[:space:]]*#[[:space:]]*endif([[:space:]].*)?$/ { cpp = 0; next }

/^[[:space:]]*#[[:space:]]*ifdef[[:space:]]+_WIN32[[:space:]]*$/ {
    platform_if = 1
    platform_emit = (platform == "windows")
    next
}
platform_if && /^[[:space:]]*#[[:space:]]*else([[:space:]].*)?$/ {
    platform_emit = !platform_emit
    next
}
platform_if && /^[[:space:]]*#[[:space:]]*endif([[:space:]].*)?$/ {
    platform_if = 0
    platform_emit = 1
    next
}
platform_if && !platform_emit { next }

/^[[:space:]]*#[[:space:]]*define[[:space:]]+/ {
    line = $0
    sub(/^[[:space:]]*#[[:space:]]*define[[:space:]]+/, "", line)

    if (match(line, /^[A-Za-z_][A-Za-z0-9_]*\(/)) {
        print "error: unsupported function-like macro: " $0 > "/dev/stderr"
        exit 2
    }

    if (!match(line, /^[A-Za-z_][A-Za-z0-9_]*/)) {
        print "error: unsupported #define: " $0 > "/dev/stderr"
        exit 2
    }

    name = substr(line, RSTART, RLENGTH)
    value = trim(substr(line, RLENGTH + 1))

    if (value == "") next
    if (value ~ /^"([^"\\]|\\.)*"$/) next

    if (value ~ /^[A-Za-z0-9_[:space:]()+\-*/%<>&|~^xXuUlL]+$/ &&
        value !~ /[;{},?:=!]/) {
        print name "\t" value >> defs
        next
    }

    print "error: unsupported object-like macro: " $0 > "/dev/stderr"
    exit 2
}

/^[[:space:]]*#[[:space:]]*endif([[:space:]].*)?$/ { next }
/^[[:space:]]*#[[:space:]]*if/ {
    print "error: unsupported conditional directive: " $0 > "/dev/stderr"
    exit 2
}
/^[[:space:]]*#/ {
    print "error: unsupported preprocessor directive: " $0 > "/dev/stderr"
    exit 2
}

{ print }
AWK

cat > "$DEFINES_AWK" <<'AWK'
{
    comma = (NR == 1 ? "" : ",")
    printf "%s\n    %s = %s", comma, $1, $2
}
END {
    print "\n};"
}
AWK

cat > "$TIME_AWK" <<'AWK'
BEGIN {
    in_struct = 0
    struct_text = ""
    has_time_t = 0
}

function emit_struct(text, has_time,    n, lines, last, name, i) {
    if (!has_time) {
        printf "%s", text
        return
    }

    n = split(text, lines, "\n")
    last = ""
    for (i = n; i >= 1; i--) {
        if (lines[i] !~ /^[[:space:]]*$/) {
            last = lines[i]
            break
        }
    }
    name = last
    sub(/^.*}[[:space:]]*/, "", name)
    sub(/[[:space:]]*;[[:space:]]*$/, "", name)
    if (name !~ /^[A-Za-z_][A-Za-z0-9_]*$/) {
        print "error: cannot identify typedef containing time_t" > "/dev/stderr"
        exit 3
    }

    printf "/* KC_CDEF_OMITTED_TIME_T:%s */\n", name
}

{
    if (!in_struct && $0 ~ /^[[:space:]]*typedef[[:space:]]+struct[^{]*\{/) {
        in_struct = 1
        struct_text = $0 "\n"
        has_time_t = ($0 ~ /(^|[^A-Za-z0-9_])time_t([^A-Za-z0-9_]|$)/)
        if ($0 ~ /}[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;/) {
            emit_struct(struct_text, has_time_t)
            in_struct = 0
        }
        next
    }

    if (in_struct) {
        struct_text = struct_text $0 "\n"
        if ($0 ~ /(^|[^A-Za-z0-9_])time_t([^A-Za-z0-9_]|$)/) has_time_t = 1
        if ($0 ~ /}[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;/) {
            emit_struct(struct_text, has_time_t)
            in_struct = 0
        }
        next
    }

    print
}

END {
    if (in_struct) {
        print "error: unterminated typedef struct" > "/dev/stderr"
        exit 3
    }
}
AWK

cat > "$COMMENTS_AWK" <<'AWK'
BEGIN {
    in_comment = 0
}

{
    if ($0 ~ /KC_CDEF_OMITTED_TIME_T:/) next
    line = $0
    out = ""

    while (length(line) > 0) {
        if (in_comment) {
            if (match(line, /\*\//)) {
                line = substr(line, RSTART + RLENGTH)
                in_comment = 0
            } else {
                line = ""
            }
        } else if (match(line, /\/\*/)) {
            out = out substr(line, 1, RSTART - 1)
            line = substr(line, RSTART + RLENGTH)
            in_comment = 1
        } else {
            out = out line
            line = ""
        }
    }

    if (out !~ /^[[:space:]]*$/) print out
}
AWK

awk -v defs="$DEFINES" -v platform="$PLATFORM" \
    -f "$HEADER_AWK" "$HEADER" > "$BODY"

if [ -s "$DEFINES" ]; then
    echo "enum {" > "$OUT"
    awk -F '\t' -f "$DEFINES_AWK" "$DEFINES" >> "$OUT"
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

awk -f "$TIME_AWK" "$BODY" >> "$OUT"

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

awk -f "$COMMENTS_AWK" "$OUT"
