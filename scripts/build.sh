#!/bin/bash
# kclib build tool
# Summary: Builds one or all kclib projects with make all.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

usage() {
    echo "Usage: $0 all|NAME.c" >&2
}

build_project() {
    local project_dir="$1"
    local project_name

    project_name=$(basename "$project_dir")
    echo "Building $project_name..."
    (
        cd "$project_dir"
        make all
    )
}

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
        echo "error: projects directory not found: proj/" >&2
        exit 1
    }

    if [ "$target" = "all" ]; then
        for project_dir in "$proj_dir"/*.c; do
            [ -d "$project_dir" ] || continue
            build_project "$project_dir"
        done
        return 0
    fi

    case "$target" in
        *.c) ;;
        *)
            echo "error: project name must use the NAME.c form" >&2
            usage
            exit 1
            ;;
    esac

    project_dir="$proj_dir/$target"
    [ -d "$project_dir" ] || {
        echo "error: project not found: $target" >&2
        exit 1
    }

    build_project "$project_dir"
}

main "$@"
