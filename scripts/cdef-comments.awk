# kclib cdef comment stripper
# Summary: Removes C block comments while preserving declarations.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

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
