# kclib cdef header filter
# Summary: Filters public C headers into LuaJIT-compatible declarations.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

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
