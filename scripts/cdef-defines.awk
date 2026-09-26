# kclib cdef constant emitter
# Summary: Emits object-like constants as one LuaJIT enum declaration.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

{
    comma = (NR == 1 ? "" : ",")
    printf "%s\n    %s = %s", comma, $1, $2
}
END {
    print "\n};"
}
