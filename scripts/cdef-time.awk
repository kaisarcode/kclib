# kclib cdef time_t filter
# Summary: Omits unused typedef structs that depend on target-specific time_t.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

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
