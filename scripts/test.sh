#!/bin/bash
# kclib test tool
# Summary: Tests one or all kclib projects natively, through Wine, and through WASM when supported.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 all|NAME" >&2
}

# Resolves one logical kclib name to its project directory.
# @param proj_dir Projects directory.
# @param name Logical kclib name.
# @return 0 on success, 1 when the project does not exist.
resolve_project() {
    local proj_dir="$1"
    local name="$2"

    if [ -d "$proj_dir/lib$name.c" ]; then
        printf '%s\n' "$proj_dir/lib$name.c"
        return 0
    fi

    return 1
}

# Tests one kclib project on every supported test runtime.
# @param project_dir Project directory.
# @return 0 on success.
test_project() {
    local project_dir="$1"
    local project_name

    project_name=$(basename "$project_dir")
    echo "Testing $project_name..."
    (
        cd "$project_dir"
        make test
        make test wine
        if grep -Eq '^wasm[[:space:]]*:' Makefile; then
            make test wasm
        fi
    )
}

# Dispatches one project test run or all project test runs.
# @param target Logical kclib name or all.
# @return 0 on success.
main() {
    local script_dir root_dir proj_dir target project_dir

    [ "$#" -eq 1 ] || {
        usage
        exit 1
    }

    script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    root_dir=$(dirname "$script_dir")
    proj_dir="$root_dir/proj"
    target="$1"

    [ -d "$proj_dir" ] || {
        echo "error: projects directory not found: $proj_dir" >&2
        exit 1
    }

    if [ "$target" = "all" ]; then
        for project_dir in "$proj_dir"/lib*.c; do
            [ -d "$project_dir" ] || continue
            test_project "$project_dir"
        done
        return 0
    fi

    case "$target" in
        *[!A-Za-z0-9_-]*|'')
            echo "error: invalid project name: $target" >&2
            usage
            exit 1
            ;;
    esac

    project_dir=$(resolve_project "$proj_dir" "$target") || {
        echo "error: project not found: $target" >&2
        exit 1
    }

    test_project "$project_dir"
}

main "$@"
